# BadBT — Project Takeover Audit

**Date:** 2026-10-14
**Scope:** Read-only investigation of the existing codebase (no application code/config changed during the audit).
**Auditor environment:** macOS. The C++ production targets are Windows-only (Win32 Raw Input + `WH_KEYBOARD_LL`); they were **not** built/run here. The ML model and the portable feature-extraction code **were** executed locally (see "Verification method").

---

## 1. System overview

A **defensive** Windows tool that detects Bluetooth HID keystroke-injection (Flipper Zero / BadUSB-style
automated typing) and, on detection, blocks the keyboard to contain the attack.

| Layer | Binary | Role |
|---|---|---|
| `capture/` | `badbt-capture.exe` | Raw Input (`WM_INPUT`) keystroke logger → JSONL (dataset collection) |
| `detector/` | `badbt-detector-core` (static lib) + `badbt-detector.exe` | Feature extraction + ONNX inference; CLI diagnostic |
| `tray/` | `badbt-tray.exe` | Production shield: UI thread owns `WH_KEYBOARD_LL` hook + cooldown timer; background thread runs capture → core detector |
| `ml/` + `data/` | — | Python/scikit-learn pipeline; RF model exported to `model/rf_model.onnx` |
| `service/` | — | Empty stub (commented-out `CMakeLists.txt`); superseded by `tray/` |

**Scope expansion vs. original spec:** `phase0_scope_definition.md` specified *"warning notification only, no
auto-disconnect."* The shipped product adds **active keyboard blocking** — a deliberate expansion, recorded
here for traceability, not a defect.

## 2. Verification method

- Loaded `model/rf_model.onnx` in ONNX Runtime (throwaway venv — the repo's local Python env has a NumPy/sklearn
  ABI break) and scored all 12,896 rows of `data/processed/features.csv`.
- Compiled and ran the portable `FeatureExtractor` + `test_feature_parity` with `clang++ -std=c++17` on macOS
  (these sources have no Win32/ONNX dependency).
- Static reading for everything Windows-specific (hook, tray threading, capture loop).

## 3. Verified findings (executed)

- **Model is wired correctly:** input `float_input [None,12]`, outputs `label` + `probabilities`; the 12 feature
  columns match `model_meta.json` and the C++ canonical order. Sentinel `-999` is consistent between Python and C++.
- **Operating threshold 0.95 — false positives:** 0 / 4,860 human windows flagged (window-FPR 0.00%).
- **Detection by injection family:** ≈100% for 17 of 18 families. **Exception: `inj_18_evasion_payload` — only
  38.6% of windows detected (evades 61%).** However, **0 / 114 injected sessions are fully missed** (every attack
  session crosses 0.95 at least once).
- Note: the 99.8% aggregate accuracy includes training data and is optimistic. `model_meta` test AUC 0.99997 /
  session-accuracy 1.0 is consistent with the documented risk that the model partly learns the single Flipper
  source's fingerprint.

## 4. Source-verified findings (not executed)

- Concurrency matches the README's claims: shared flags are `std::atomic` (acquire/release), hook + cooldown
  `SetTimer` live on the UI thread, capture runs on its own thread. No race spotted.
- `model_runner` requests `probabilities` by name and fails safe (returns −1.0 on error → never trips the 0.95 gate).
- No TODO/FIXME/mock/stub/placeholder markers anywhere in the source.

## 5. Defects

1. **`config/service.toml` is never read.** No TOML parser is vendored and no code references the file; every value
   (`window_size=25`, `threshold=0.95`, `cooldown=30`) is a hardcoded C++ default in `detector/include/detector.h`
   and `tray/include/tray_app.h`, changeable only via CLI flags. Editing the toml has no effect. Contradicts the README.
2. **Train/serve skew in feature extraction.** `data/build_features.py::clean_session` drops (a) non-dominant
   `device_path` events per session and (b) exact-duplicate events, *before* pairing. The C++ `FeatureExtractor`
   does neither — it only dedups **consecutive** identical events and never filters minority devices. On a BT
   keyboard exposing two HID interfaces this can corrupt flight-time math and inflate `n_devices_in_window`,
   risking live false positives not reflected in `features.csv` or the (single-file) parity test.
3. **Build toolchain is self-contradictory.** Root `CMakeLists.txt` sets MSVC-only `CMAKE_MSVC_RUNTIME_LIBRARY` and
   `/W4`, but detector/tray targets pass GCC-only `-static` and `model_runner.cpp` carries a MinGW-w64 `_stdcall`
   workaround. README says MSVC. The shipped `dist/` binaries were likely built with MinGW-w64, not MSVC.
4. **Evasion weakness is operationally undocumented** (see §3, `inj_18`).

## 6. Prioritized backlog

**P0 — Blockers:** None. Core path (model + inference + threshold) verified working.

**P1 — Required/incomplete + correctness**
- **P1-a** Port `clean_session` semantics (minority-device filter + exact-dedup) to C++, or consciously document the
  divergence. Guarded by P2-a.
- **P1-b** Make `config/service.toml` authoritative, or delete it and fix docs.
- **P1-c** Reconcile the build toolchain (one documented, reproducible path).

**P2 — Reliability / integration / security / tests**
- **P2-a** Broaden the feature-parity test beyond one clean file (multi-session, multi-device, evasion family).
  **Smallest high-value task — do first; it confirms/guards P1-a.** *(in progress)*
- **P2-b** Add a test runner / CTest registration + minimal CI (tests are currently manual `.exe`s).
- **P2-c** Document the evasion ceiling and track a complementary detection cue.
- **P2-d** Stop committing build artifacts (~30 MB: `dist/*.exe`, vendored `onnxruntime.dll`, duplicate `.joblib`
  sets in `model/` and `ml/model_stale_quarantine/`).
- **P2-e** Verify/repin `requirements.txt`; confirm notebooks reproduce the deployed ONNX.

**P3 — Polish**
- README/`HOW_TO_RUN.md` doc-rot: claims window size "20 to match service.toml"; actual everywhere (code and toml) is **25**.
- Remove or implement the `service/` stub; prune `scratch/` dev files.

## 7. Known data facts (for test design)

- Only **`vsh3.jsonl`** has >1 device path in the raw data: 2,268 events on the dominant device + **1** stray
  keydown with a **null** `device_path` (event index 316) that never pairs. Its first window is unaffected.
- **Zero** raw sessions contain exact-duplicate events → the duplicate case requires a documented **synthetic** fixture.
- Evasion fixtures exist: `data/raw/inj_18_evasion_payload_*.jsonl`.
