# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

BadBT is a **defensive** Windows security tool: it detects Bluetooth HID keystroke-injection
attacks (e.g. Flipper Zero / BadUSB-style automated typing) and, on detection, blocks further
keyboard input to contain the attack. It is a C++17 Win32 application backed by a Python/scikit-learn
ML pipeline whose trained model is exported to ONNX and run in-process via ONNX Runtime.

**This is Windows-only.** The capture, hook, and tray layers depend on Win32 APIs
(`GetRawInputData`, `SetWindowsHookExW`/`WH_KEYBOARD_LL`, tray/message loop). It will not build or run
on macOS/Linux — only the feature-extraction and ONNX logic are portable. Do not attempt to compile
the C++ targets on this (darwin) machine; reason about them from source.

## Build (Windows)

```powershell
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release                      # all targets
cmake --build build --target badbt-tray --config Release  # just the production shield
```

Toolchain: MSVC, CMake 3.20+, C++17. Static CRT (`/MT`) is forced on MSVC; the tray/detector link
statically (`-static`). ONNX Runtime is **vendored** under `third_party/onnxruntime/`; its DLLs
(and `model/rf_model.onnx`) are copied next to the built executables by post-build commands — don't
expect them to be found via any system install.

Produced binaries:
- `build/tray/badbt-tray.exe` — production background shield
- `build/detector/badbt-detector.exe` — CLI diagnostic detector
- `build/capture/badbt-capture.exe` — Raw Input → JSONL logger (dataset collection)

## Tests

Tests are plain executables (no CTest registration) — run them directly from the repo root so their
hardcoded relative paths (e.g. `data/raw/adhi_01.jsonl`) resolve:

```powershell
& "build/tray/test_tray_state.exe"          # hook lifecycle, block-until-Reset, blocklist, toggle bypass
& "build/tray/test_blocklist.exe"           # Blocklist add/dedup/clear + save/load round-trip (portable)
& "build/tray/test_bt_address.exe"          # Raw Input path -> instance ID -> Bluetooth MAC parsing (portable)
& "build/detector/test_config_loader.exe"   # service.toml parsing + key precedence
& "build/detector/test_onnx_inference.exe"  # ONNX inference + JSONL replay regression
& "build/detector/test_feature_parity.exe"  # see "Feature parity" below
```

`test_blocklist` and `test_feature_parity` are pure C++ (no Win32/ONNX) and also build with
`clang++ -std=c++17` on macOS/Linux for quick local checks.

## ML pipeline (Python)

```powershell
python -m venv venv; venv\Scripts\activate
pip install -r requirements.txt            # pinned; also: jupyter
```

Notebooks in `ml/` run in order (launch `jupyter notebook` **from inside `ml/`** — they use relative
paths like `../data/raw`): `01_build_features` → `02_train_model Rf` → `03_predict Rf`. Notebook 5
trains the comparison models; 6 compares them. Feature-building logic also exists as a standalone
script: `python data/build_features.py --raw-dir data/raw --out-dir data/processed --window 25`.

Raw captures live in `data/raw/*.jsonl` (one shared copy; notebooks do not keep their own). Human
sessions are named by subject (`adhi_*`, `vik*`, etc.); injection samples are `inj_NN_<pattern>_*`.
`data/build_features.py` turns these into `data/processed/features.csv`.

## Architecture (the parts that span files)

- **`badbt-detector-core`** (static lib, `detector/`) is the shared brain used by both the CLI
  detector and the tray shield. It owns `FeatureExtractor`, `ModelRunner` (ONNX Runtime), and the
  `Detector` orchestration. Both executables link it; behavior changes belong here, not duplicated.
- **Sliding-window detection**: `FeatureExtractor` buffers keystrokes and emits a **12-feature**
  vector per window (dwell/flight timing stats, autorepeat & backspace ratios, throughput, device
  count). `ModelRunner` scores it; detection fires at **P(injected) ≥ 0.95** (high threshold =
  near-zero false positives on fast human typing). Runtime config lives in `config/service.toml`
  (`model_path`, `window_size`, `confidence_threshold`, `cooldown_secs`, `device_filter`,
  optional `blocklist_path`). It is loaded at startup by both `badbt-tray` and `badbt-detector`
  (`detector/src/config_loader.cpp`, `parse_service_config` + `find_config_path`); precedence is
  **defaults → service.toml → CLI flags**.
- **Window size = 25** is the current source of truth: it's the default in both `data/build_features.py`
  and `detector/include/feature_extractor.h`. Note `config/service.toml` and `ml/HOW_TO_RUN.md` contain
  conflicting/stale mentions (25 vs 20) — trust the code default and keep Python and C++ in lockstep.
- **Feature parity is a hard invariant.** The Python extractor (`data/build_features.py`) and the C++
  extractor (`detector/src/feature_extractor.cpp`) must compute byte-for-byte equivalent features, or
  the ONNX model scores garbage at runtime. `test_feature_parity` replays a raw log through C++ and
  asserts equality against `features.csv` rows. **Any change to one extractor must be mirrored in the
  other, and parity re-verified.**
- **Tray shield threading** (`tray/src/tray_app.cpp`): a **UI thread** owns the message pump, the
  `WH_KEYBOARD_LL` hook (install/uninstall only here), tray icon, and notifications; a **background
  thread** does Raw Input capture. The hook deliberately lets **modifier KEYUP** events pass through
  even while blocking, to avoid stuck-modifier state — preserve that when touching the hook.
- **Device blocklist + block-until-Reset** (`tray/include/blocklist.h`, `tray/src/blocklist.cpp`):
  on detection the offending `primary_device_path` is added to a persistent blocklist
  (`%APPDATA%\BadBT\blocklist.txt`, overridable via `blocklist_path`) and keyboard blocking stays
  engaged until the user clicks **Reset** (tray menu "Reset Blocklist" / window "Reset" button →
  `TrayApp::reset_blocklist`). There is **no** 30s auto-cooldown unblock anymore. Enforcement is
  still global — `WH_KEYBOARD_LL` has no device identity — so the lock-free `is_blocking_active_`
  atomic is the authoritative gate and the blocklist is the durable record of why input is blocked;
  the hook never touches the `Blocklist` (which is mutex-guarded for the UI/capture threads). The
  blocklist is reloaded on startup, so a device stays blocked across restarts until Reset. The
  detector's own `Cooldown` state still exists internally but no longer drives the UI (status reads
  `Monitoring` / `Blocked` / `Disabled`).
- **Disconnect-on-detect** (`tray/src/bt_disconnect.cpp`, `tray/src/bt_address.cpp`; `TrayConfig::disconnect_on_detect`,
  default on): on detection the blocklisted device is disconnected and unpaired through the Windows Bluetooth API
  (Raw Input path → devnode → walk parents for the MAC → per-radio `BluetoothSetServiceState(HID, DISABLE)` +
  `BluetoothRemoveDevice`; falls back to disabling the devnode, which needs admin). It runs on a worker thread and
  reports back via `WM_DISCONNECT_DONE`. Input is blocked only while it is in flight: **success lifts the block
  automatically**, **failure keeps it until Reset** (old behavior). A blocklisted device that reconnects and types is
  disconnected again by the capture thread (rate-limited) and never reaches the detector. With
  `disconnect_on_detect=false` the legacy block-until-Reset behavior applies, including blocking at startup when the
  blocklist is non-empty. `parse_bt_address` / `raw_input_path_to_instance_id` are pure C++ and covered by
  `test_bt_address` (portable, builds with clang++ on macOS). The Windows Bluetooth calls themselves are untested
  off-device — verify on real hardware.
- **`service/`** is scaffolding only; it is commented out in the top-level `CMakeLists.txt` and not built.

## Model artifacts — stale-data caution

`model/*.joblib` / `model/rf_model.onnx` and `outputs/*` were regenerated after a data-labeling fix.
`ml/model_stale_quarantine/` and `ml/outputs_stale_quarantine/` hold the **pre-fix** artifacts kept for
reference — do not ship or compare against them. If you change features, the window, or the raw data,
you must re-run notebooks 1 and 2 (and 5 for the other models), then re-export ONNX
(`ml/export_onnx.py`, validate with `ml/validate_onnx.py`) and re-run `test_feature_parity`.

## Packaging (optional, Windows)

```powershell
& "C:\Users\<user>\AppData\Local\Programs\Inno Setup 6\iscc.exe" /F"BadBT-1.0.2-Setup" "installer/badbt_setup.iss"
```

## Graph tools

Per the repo's root project instructions, prefer the `code-review-graph` MCP tools to narrow scope
before reading source (impact radius, callers/callees, review context). Always verify behavior against
the actual source — the source wins when it disagrees with the graph.
