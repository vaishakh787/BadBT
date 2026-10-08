"""
BadBT Detection — ONNX Model Validation Script
Compares outputs of model/rf_model.joblib and model/rf_model.onnx across:
1. The canonical feature dataset (data/processed/features.csv)
2. Known human session: data/raw/adhi_01.jsonl
3. Known injected session: data/raw/inj_01_zerodelay_01.jsonl
"""

import os
import json
import joblib
import numpy as np
import pandas as pd
import onnxruntime as rt
import skl2onnx
import onnx

CANONICAL_FEATURES = [
    "mean_dwell_ms",
    "std_dwell_ms",
    "cv_dwell",
    "n_dwell_outliers_excluded",
    "mean_flight_ms",
    "std_flight_ms",
    "cv_flight",
    "n_pauses_excluded",
    "autorepeat_ratio",
    "backspace_ratio",
    "events_per_sec",
    "n_devices_in_window",
]

FLAG_BREAK_BIT = 0x01
FLAG_E0_BIT = 0x02
VK_BACK = 0x08
VK_DELETE = 0x2E
PAUSE_THRESHOLD_MS = 2000
DWELL_CAP_MS = 2000


def load_raw_file(path: str) -> pd.DataFrame:
    rows = []
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            rows.append(json.loads(line))
    if not rows:
        raise ValueError(f"{path}: no events found")
    df = pd.DataFrame(rows)
    df["source_file"] = os.path.basename(path)
    for col, default in [
        ("seq", np.nan),
        ("qpc_ticks", np.nan),
        ("device_handle", ""),
        ("message", np.nan),
        ("is_autorepeat", np.nan),
        ("payload_variant", None),
        ("session_id", os.path.basename(path)),
        ("subject_id", "unknown"),
        ("label", "unknown"),
    ]:
        if col not in df.columns:
            df[col] = default
    return df


def clean_session(session: pd.DataFrame, log_prefix: str) -> pd.DataFrame:
    before = len(session)
    dedup_cols = [c for c in ["timestamp_ns", "vkey", "scan_code", "flags"] if c in session.columns]
    session = session.drop_duplicates(subset=dedup_cols).reset_index(drop=True)
    n_dupes = before - len(session)

    n_minority_dropped = 0
    if "device_path" in session.columns and session["device_path"].nunique() > 1:
        counts = session["device_path"].value_counts()
        dominant = counts.idxmax()
        minority_mask = session["device_path"] != dominant
        n_minority_dropped = int(minority_mask.sum())
        session = session[~minority_mask].reset_index(drop=True)

    return session


def pair_dwell_flight(session: pd.DataFrame) -> pd.DataFrame:
    session = session.sort_values("timestamp_ns").reset_index(drop=True)
    open_keys = {}
    last_keyup_ts = None
    records = []

    for idx, row in session.iterrows():
        is_keyup = bool(int(row["flags"]) & FLAG_BREAK_BIT)
        is_extended = bool(int(row["flags"]) & FLAG_E0_BIT)
        key_id = (row["scan_code"], is_extended)

        if not is_keyup:
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
    return pd.DataFrame.from_records(records)


def cv(x: pd.Series) -> float:
    m = x.mean()
    if not np.isfinite(m) or m == 0:
        return np.nan
    return x.std() / m


def windowize(paired: pd.DataFrame, window: int, step: int, meta: dict) -> pd.DataFrame:
    n = len(paired)
    if n < window:
        return pd.DataFrame()

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
        flight = flight_all[~pause_mask]

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


def validate():
    joblib_path = "model/rf_model.joblib"
    onnx_path = "model/rf_model.onnx"
    meta_path = "model/model_meta.json"
    dataset_path = "data/processed/features.csv"

    print("=" * 60)
    print("      BADBT ONNX EXPORT & PREDICTION VALIDATION")
    print("=" * 60)

    # 1. Inspect Files & Versions
    assert os.path.exists(joblib_path), f"Missing {joblib_path}"
    assert os.path.exists(onnx_path), f"Missing {onnx_path}"
    assert os.path.exists(meta_path), f"Missing {meta_path}"
    assert os.path.exists(dataset_path), f"Missing {dataset_path}"

    with open(meta_path, "r", encoding="utf-8") as f:
        meta = json.load(f)

    assert meta["feature_columns"] == CANONICAL_FEATURES, "Feature list in metadata does not match canonical order!"

    print(f"skl2onnx version:   {skl2onnx.__version__}")
    print(f"onnx version:       {onnx.__version__}")
    print(f"onnxruntime version:{rt.__version__}")
    print(f"ONNX Model Path:    {onnx_path}")
    print(f"Joblib Model Path:  {joblib_path}")

    # 2. Inspect ONNX Model Structure
    sess = rt.InferenceSession(onnx_path, providers=["CPUExecutionProvider"])
    inputs = sess.get_inputs()
    outputs = sess.get_outputs()

    print("\n--- ONNX Model Signature ---")
    for inp in inputs:
        print(f"  Input:  name='{inp.name}', type={inp.type}, shape={inp.shape}")
    for out in outputs:
        print(f"  Output: name='{out.name}', type={out.type}, shape={out.shape}")

    assert len(inputs) == 1, f"Expected 1 input, got {len(inputs)}"
    assert inputs[0].shape[1] == 12, f"Expected 12 input features, got {inputs[0].shape}"
    assert len(outputs) == 2, f"Expected 2 outputs (label, probabilities), got {len(outputs)}"
    assert outputs[1].name == "probabilities", f"Expected 'probabilities' as second output, got {outputs[1].name}"

    # 3. Load Scikit-Learn Model
    clf = joblib.load(joblib_path)
    sentinel = meta["sentinel_fill_value"]

    # 4. Validate Across Canonical Dataset
    print("\n--- Validating on Canonical Dataset (12,896 windows) ---")
    df = pd.read_csv(dataset_path)
    X = df[CANONICAL_FEATURES].fillna(sentinel)
    X_float32 = X.values.astype(np.float32)

    # Joblib inference
    joblib_probas = clf.predict_proba(X)[:, 1]
    joblib_preds = (joblib_probas >= 0.5).astype(int)

    # ONNX inference
    onnx_res = sess.run(None, {inputs[0].name: X_float32})
    onnx_preds = onnx_res[0]
    onnx_probas = onnx_res[1][:, 1]

    # Compare
    abs_diff = np.abs(joblib_probas - onnx_probas)
    max_prob_diff = float(np.max(abs_diff))
    mean_prob_diff = float(np.mean(abs_diff))
    pred_agreement = float((joblib_preds == onnx_preds).mean())

    print(f"  Samples tested:             {len(X):,}")
    print(f"  Max probability difference: {max_prob_diff:.8f}")
    print(f"  Mean probability difference:{mean_prob_diff:.8f}")
    print(f"  Class prediction agreement: {pred_agreement:.2%}")

    assert max_prob_diff < 1e-4, f"Probability difference {max_prob_diff} exceeds tolerance 1e-4!"
    assert pred_agreement == 1.0, f"Class prediction mismatch! Agreement: {pred_agreement}"

    # 5. Validate on Known Raw Sessions
    print("\n--- Validating on Known Raw Capture Sessions ---")

    def evaluate_session(file_path, expected_verdict):
        raw_df = load_raw_file(file_path)
        cleaned = clean_session(raw_df, os.path.basename(file_path))
        paired = pair_dwell_flight(cleaned)
        session_meta = {"source_file": os.path.basename(file_path), "session_id": cleaned["session_id"].iloc[0]}
        windows = windowize(paired, meta["window_size"], meta["window_step"], session_meta)
        assert not windows.empty, f"No windows for {file_path}"

        X_win_df = windows[CANONICAL_FEATURES].fillna(sentinel)
        X_win_np = X_win_df.values.astype(np.float32)

        j_p = clf.predict_proba(X_win_df)[:, 1]
        j_pred = (j_p >= 0.5).astype(int)
        j_verdict = "INJECTED" if j_pred.mean() >= 0.5 else "human"

        o_res = sess.run(None, {inputs[0].name: X_win_np})
        o_p = o_res[1][:, 1]
        o_pred = (o_p >= 0.5).astype(int)
        o_verdict = "INJECTED" if o_pred.mean() >= 0.5 else "human"

        diff = float(np.max(np.abs(j_p - o_p)))

        print(f"  Session: {os.path.basename(file_path)}")
        print(f"    Windows evaluated: {len(windows)}")
        print(f"    Joblib: verdict={j_verdict:8s} (mean P(inj)={j_p.mean():.4f})")
        print(f"    ONNX:   verdict={o_verdict:8s} (mean P(inj)={o_p.mean():.4f})")
        print(f"    Max window prob diff: {diff:.8f}")

        assert j_verdict == expected_verdict, f"Joblib verdict {j_verdict} != expected {expected_verdict}"
        assert o_verdict == expected_verdict, f"ONNX verdict {o_verdict} != expected {expected_verdict}"
        assert j_verdict == o_verdict, "Verdict mismatch between Joblib and ONNX!"
        assert diff < 1e-4, f"Difference {diff} exceeds tolerance 1e-4"
        return o_verdict, float(o_p.mean()), diff

    human_verdict, human_p, human_diff = evaluate_session("data/raw/adhi_01.jsonl", "human")
    inj_verdict, inj_p, inj_diff = evaluate_session("data/raw/inj_01_zerodelay_01.jsonl", "INJECTED")

    print("\n" + "=" * 60)
    print("                 VALIDATION SUMMARY")
    print("=" * 60)
    print(f"ONNX Model Path:            {onnx_path}")
    print(f"ONNX Input Shape:           {inputs[0].shape} ({inputs[0].name}, {inputs[0].type})")
    print(f"ONNX Output Structure:      label: {outputs[0].shape}, probabilities: {outputs[1].shape}")
    print(f"Max Probability Difference: {max_prob_diff:.8f}")
    print(f"Human Sample Result:        {human_verdict} (mean P(inj)={human_p:.4f})")
    print(f"Injected Sample Result:     {inj_verdict} (mean P(inj)={inj_p:.4f})")
    print("Validation Result:          PASS")
    print("=" * 60)

if __name__ == "__main__":
    validate()
