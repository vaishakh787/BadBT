"""
BadBT Detection — Feature Engineering Pipeline
================================================
Raw keydown/keyup events -> dwell/flight pairing -> sliding-window
aggregate features -> EDA plots -> statistical separation test.

Usage:
    python build_features.py --raw-dir data/raw --out-dir data/processed --window 25

Schema tolerance:
    Works with both the old capture schema (timestamp_ns only) and the
    updated schema (seq, qpc_ticks, device_handle, message, is_autorepeat).
    Missing optional columns degrade gracefully rather than crashing.
"""

import argparse
import glob
import json
import os

import numpy as np
import pandas as pd
from scipy import stats
import matplotlib.pyplot as plt

# RI_KEY_BREAK bit — set on any keyup event (plain or extended/E0)
FLAG_BREAK_BIT = 0x01
# RI_KEY_E0 bit — marks "extended" keys (many keys reuse scan codes between
# the base and E0-prefixed variants, e.g. main Enter vs numpad Enter)
FLAG_E0_BIT = 0x02

VK_BACK = 0x08
VK_DELETE = 0x2E

# Flight-time gaps longer than this are treated as "thinking pauses" (or,
# in injected sessions, the DELAY before typing starts) rather than part
# of typing rhythm — standard practice in keystroke dynamics literature.
# They're excluded from mean/std/CV but counted separately per window.
PAUSE_THRESHOLD_MS = 2000

# Dwell times longer than this are treated as a deliberate long hold
# (or a stuck/unmatched key) rather than typing rhythm — same logic as
# the flight-time pause cap above. Excluded from mean/std/CV, counted
# separately per window.
DWELL_CAP_MS = 2000


# ─────────────────────────────────────────────────────────────────────
# Step 0.5 — Clean
# ─────────────────────────────────────────────────────────────────────

def clean_session(session: pd.DataFrame, log_prefix: str) -> pd.DataFrame:
    """Remove artifacts found in real captured data before pairing:

    1. Exact duplicate events (same timestamp/vkey/scan_code/flags) —
       can occur if a device double-reports a HID input report.
    2. Minority device_path(s) within a session — some Bluetooth
       keyboards expose two HID interfaces for the same physical
       device (e.g. a boot-protocol interface alongside a
       vendor-defined one), so a session can end up with a small
       fraction of events reported on a second device handle. We keep
       only the dominant device_path per session and drop the rest,
       since mixing two interfaces' timestamps corrupts flight-time
       math between them.
    """
    before = len(session)

    dedup_cols = [c for c in ["timestamp_ns", "vkey", "scan_code", "flags"]
                  if c in session.columns]
    session = session.drop_duplicates(subset=dedup_cols).reset_index(drop=True)
    n_dupes = before - len(session)

    n_minority_dropped = 0
    if "device_path" in session.columns and session["device_path"].nunique() > 1:
        counts = session["device_path"].value_counts()
        dominant = counts.idxmax()
        minority_mask = session["device_path"] != dominant
        n_minority_dropped = int(minority_mask.sum())
        session = session[~minority_mask].reset_index(drop=True)

    if n_dupes or n_minority_dropped:
        print(f"  [clean] {log_prefix}: dropped {n_dupes} duplicate events, "
              f"{n_minority_dropped} minority-device events")

    return session


# ─────────────────────────────────────────────────────────────────────
# Step 0 — Load
# ─────────────────────────────────────────────────────────────────────

def load_sessions(raw_dir: str) -> pd.DataFrame:
    """Load every .jsonl in raw_dir into one long DataFrame, tagging each
    row with its source filename so we never accidentally mix sessions."""
    files = sorted(glob.glob(os.path.join(raw_dir, "*.jsonl")))
    if not files:
        raise SystemExit(f"No .jsonl files found in {raw_dir}")

    frames = []
    for f in files:
        rows = []
        with open(f, encoding="utf-8") as fh:
            for line in fh:
                line = line.strip()
                if not line:
                    continue
                rows.append(json.loads(line))
        if not rows:
            continue
        df = pd.DataFrame(rows)
        df["source_file"] = os.path.basename(f)
        frames.append(df)

    full = pd.concat(frames, ignore_index=True, sort=False)

    # Backfill optional columns so downstream code can assume they exist
    for col, default in [
        ("seq", np.nan),
        ("qpc_ticks", np.nan),
        ("device_handle", ""),
        ("message", np.nan),
        ("is_autorepeat", np.nan),
        ("payload_variant", None),
    ]:
        if col not in full.columns:
            full[col] = default

    return full


# ─────────────────────────────────────────────────────────────────────
# Step 1 — Pair events into dwell / flight times
# ─────────────────────────────────────────────────────────────────────

def pair_dwell_flight(session: pd.DataFrame) -> pd.DataFrame:
    """Given one session's events (already sorted by time), return one row
    per completed keydown with dwell_ms and flight_ms attached.

    dwell  = keyup_ts  - keydown_ts    (same key: scan_code + E0 bit)
    flight = keydown_ts - previous_keyup_ts   (gap since *any* prior key
             was released — this is the standard flight-time definition
             used in keystroke dynamics literature)
    """
    session = session.sort_values("timestamp_ns").reset_index(drop=True)

    open_keys = {}       # (scan_code, is_extended) -> keydown row index
    last_keyup_ts = None
    records = []

    for idx, row in session.iterrows():
        is_keyup = bool(int(row["flags"]) & FLAG_BREAK_BIT)
        is_extended = bool(int(row["flags"]) & FLAG_E0_BIT)
        key_id = (row["scan_code"], is_extended)

        if not is_keyup:
            # Keydown: open it, and compute flight from last keyup if any
            flight_ms = (
                (row["timestamp_ns"] - last_keyup_ts) / 1e6
                if last_keyup_ts is not None
                else np.nan
            )
            open_keys[key_id] = {
                "keydown_ts": row["timestamp_ns"],
                "vkey": row["vkey"],
                "scan_code": row["scan_code"],
                "flight_ms": flight_ms,
                "is_autorepeat": row.get("is_autorepeat", np.nan),
                "device_path": row.get("device_path", None),
                "seq": row.get("seq", idx),
            }
        else:
            # Keyup: close the matching open keydown if we have one
            opened = open_keys.pop(key_id, None)
            last_keyup_ts = row["timestamp_ns"]
            if opened is not None:
                dwell_ms = (row["timestamp_ns"] - opened["keydown_ts"]) / 1e6
                records.append({
                    "seq": opened["seq"],
                    "timestamp_ns": opened["keydown_ts"],
                    "vkey": opened["vkey"],
                    "scan_code": opened["scan_code"],
                    "dwell_ms": dwell_ms,
                    "flight_ms": opened["flight_ms"],
                    "is_autorepeat": opened["is_autorepeat"],
                    "device_path": opened["device_path"],
                })
            # Unmatched keyups (no prior open keydown, e.g. mid-session
            # capture start) are silently dropped — they can't yield a
            # dwell time and shouldn't be treated as errors.

    return pd.DataFrame.from_records(records)


# ─────────────────────────────────────────────────────────────────────
# Step 2 — Sliding window aggregation
# ─────────────────────────────────────────────────────────────────────

def cv(x: pd.Series) -> float:
    """Coefficient of variation: std / mean. NaN-safe, guards div-by-zero."""
    m = x.mean()
    if not np.isfinite(m) or m == 0:
        return np.nan
    return x.std() / m


def windowize(paired: pd.DataFrame, window: int, step: int,
              meta: dict) -> pd.DataFrame:
    """Slide a window of `window` keystrokes (step `step` apart) over one
    session's paired events and compute the aggregate feature row for
    each window."""
    n = len(paired)
    if n < window:
        return pd.DataFrame()  # session too short for even one window

    rows = []
    for start in range(0, n - window + 1, step):
        w = paired.iloc[start:start + window]

        dwell_all = w["dwell_ms"].dropna()
        dwell_outlier_mask = dwell_all > DWELL_CAP_MS
        n_dwell_outliers = int(dwell_outlier_mask.sum())
        dwell = dwell_all[~dwell_outlier_mask]

        flight_all = w["flight_ms"].dropna()
        pause_mask = flight_all > PAUSE_THRESHOLD_MS
        n_pauses = int(pause_mask.sum())
        flight = flight_all[~pause_mask]  # typing-rhythm flights only

        span_s = (w["timestamp_ns"].iloc[-1] - w["timestamp_ns"].iloc[0]) / 1e9
        events_per_sec = (len(w) / span_s) if span_s > 0 else np.nan

        backspace_ratio = w["vkey"].isin([VK_BACK, VK_DELETE]).mean()

        if w["is_autorepeat"].notna().any():
            autorepeat_ratio = w["is_autorepeat"].astype(float).mean()
        else:
            autorepeat_ratio = np.nan

        n_devices = w["device_path"].nunique(dropna=True)

        rows.append({
            **meta,
            "window_start_seq": w["seq"].iloc[0],
            "mean_dwell_ms": dwell.mean(),
            "std_dwell_ms": dwell.std(),
            "cv_dwell": cv(dwell),
            "n_dwell_outliers_excluded": n_dwell_outliers,
            "mean_flight_ms": flight.mean(),
            "std_flight_ms": flight.std(),
            "cv_flight": cv(flight),
            "n_pauses_excluded": n_pauses,
            "autorepeat_ratio": autorepeat_ratio,
            "backspace_ratio": backspace_ratio,
            "events_per_sec": events_per_sec,
            "n_devices_in_window": n_devices,
        })

    return pd.DataFrame(rows)


# ─────────────────────────────────────────────────────────────────────
# Step 3 — EDA: plots + statistical separation test
# ─────────────────────────────────────────────────────────────────────

EDA_FEATURES = ["cv_flight", "std_dwell_ms", "events_per_sec",
                 "cv_dwell", "mean_flight_ms", "backspace_ratio"]


def run_eda(features: pd.DataFrame, out_dir: str) -> pd.DataFrame:
    os.makedirs(out_dir, exist_ok=True)
    results = []

    human = features[features["label"] == "human"]
    injected = features[features["label"] == "injected"]

    fig, axes = plt.subplots(2, 3, figsize=(15, 8))
    axes = axes.ravel()

    for i, feat in enumerate(EDA_FEATURES):
        h = human[feat].dropna()
        inj = injected[feat].dropna()

        ax = axes[i]
        if len(h) > 0:
            ax.hist(h, bins=25, alpha=0.55, label=f"human (n={len(h)})", color="#2e7d32")
        if len(inj) > 0:
            ax.hist(inj, bins=25, alpha=0.55, label=f"injected (n={len(inj)})", color="#c62828")
        ax.set_title(feat)
        ax.legend(fontsize=8)

        if len(h) > 1 and len(inj) > 1:
            stat, p = stats.mannwhitneyu(h, inj, alternative="two-sided")
        else:
            stat, p = np.nan, np.nan

        results.append({
            "feature": feat,
            "human_n": len(h), "human_mean": h.mean() if len(h) else np.nan,
            "human_median": h.median() if len(h) else np.nan,
            "injected_n": len(inj), "injected_mean": inj.mean() if len(inj) else np.nan,
            "injected_median": inj.median() if len(inj) else np.nan,
            "mannwhitney_p": p,
            "separates_well (p<0.05)": (p < 0.05) if pd.notna(p) else None,
        })

    plt.tight_layout()
    plot_path = os.path.join(out_dir, "eda_distributions.png")
    plt.savefig(plot_path, dpi=130)
    plt.close()

    results_df = pd.DataFrame(results)
    print(f"\nSaved EDA plot -> {plot_path}\n")
    print(results_df.to_string(index=False))
    return results_df


# ─────────────────────────────────────────────────────────────────────
# Orchestration
# ─────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw-dir", default="data/raw")
    ap.add_argument("--out-dir", default="data/processed")
    ap.add_argument("--window", type=int, default=25,
                     help="keystrokes per sliding window (canonical configuration)")
    ap.add_argument("--step", type=int, default=5,
                     help="stride between windows (smaller = more overlap)")
    args = ap.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    print(f"Loading raw sessions from {args.raw_dir} ...")
    raw = load_sessions(args.raw_dir)
    print(f"Loaded {len(raw)} events across {raw['source_file'].nunique()} files")

    all_windows = []
    for source_file, session in raw.groupby("source_file"):
        session = clean_session(session, source_file)
        paired = pair_dwell_flight(session)
        if paired.empty:
            print(f"  [skip] {source_file}: no complete keydown/keyup pairs")
            continue

        meta = {
            "source_file": source_file,
            "label": session["label"].iloc[0],
            "subject_id": session["subject_id"].iloc[0],
            "session_id": session["session_id"].iloc[0],
            "payload_variant": session["payload_variant"].iloc[0],
        }
        windows = windowize(paired, args.window, args.step, meta)
        if windows.empty:
            print(f"  [skip] {source_file}: fewer than {args.window} paired keystrokes")
            continue

        all_windows.append(windows)
        print(f"  [ok]   {source_file}: {len(paired)} paired keystrokes -> {len(windows)} windows")

    if not all_windows:
        raise SystemExit("No usable windows produced — check --window size vs session lengths")

    features = pd.concat(all_windows, ignore_index=True)
    features_path = os.path.join(args.out_dir, "features.csv")
    features.to_csv(features_path, index=False)
    print(f"\nWrote {len(features)} feature rows -> {features_path}")

    # Also write one processed CSV per session, so each raw capture file
    # has a matching per-session feature table (same schema/columns as
    # the combined file, just filtered to that session's windows).
    per_session_dir = os.path.join(args.out_dir, "per_session")
    os.makedirs(per_session_dir, exist_ok=True)
    for session_id, g in features.groupby("session_id"):
        g.to_csv(os.path.join(per_session_dir, f"{session_id}.csv"), index=False)
    print(f"Wrote {features['session_id'].nunique()} per-session CSVs -> {per_session_dir}/")

    print(f"\nLabel counts:\n{features['label'].value_counts().to_string()}\n")

    if features["label"].nunique() >= 2:
        eda_results = run_eda(features, args.out_dir)
        eda_path = os.path.join(args.out_dir, "eda_separation_report.csv")
        eda_results.to_csv(eda_path, index=False)
        print(f"\nSaved separation report -> {eda_path}")
    else:
        print("Only one label present — add sessions from the other class "
              "before running EDA/statistical separation checks.")


if __name__ == "__main__":
    main()
