# BadBT Detection — Implementation Plan

**Target:** Windows prototype, 2-4 weeks  
**Stack:** C++ (capture/service) + Python (ML) + ONNX (inference)  
**Portable:** Yes — minimal deps, statically linked

---

## 1. Project Structure

```
bluetooththing/
├── CMakeLists.txt                 # Root CMake project
├── capture/                       # Raw Input capture CLI (C++)
│   ├── CMakeLists.txt
│   └── src/
│       ├── main.cpp
│       ├── raw_input.cpp/.h       # RegisterRawInputDevices + WM_INPUT loop
│       ├── device_info.cpp/.h     # GetRawInputDeviceInfo (RIDI_DEVICENAME)
│       ├── schema.h               # Structs + nlohmann/json serialization
│       └── cli.cpp/.h             # Argument parsing
├── service/                       # Windows background service (C++)
│   ├── CMakeLists.txt
│   └── src/
│       ├── main.cpp               # Service entry (SERVICE_TABLE_ENTRY)
│       ├── capture_loop.cpp/.h    # Reuses capture raw_input logic
│       ├── inference.cpp/.h       # ONNX Runtime C++ API session
│       ├── notifier.cpp/.h        # WinRT Toast notifications
│       └── config.cpp/.h          # Config file parsing
├── third_party/                   # Header-only deps (vendored)
│   ├── nlohmann/json.hpp
│   └── tomlplusplus/toml.hpp
├── ml/                            # Python ML pipeline
│   ├── pyproject.toml
│   ├── data/
│   │   ├── raw/                   # JSONL from capture tool
│   │   └── processed/            # Parquet features + labels
│   ├── src/
│   │   ├── features.py           # Feature extraction
│   │   ├── dataset.py            # Subject-independent splits (GroupKFold)
│   │   ├── train.py              # RF, XGB, LR + calibration
│   │   ├── evaluate.py           # Per-variant metrics, latency curves
│   │   └── export_onnx.py        # skl2onnx export
│   └── notebooks/
├── models/                        # Exported ONNX models (gitignored)
│   └── best_model.onnx
├── config/
│   └── service.toml
├── scripts/
│   ├── collect_human.sh
│   ├── collect_injected.sh
│   └── build_release.ps1
└── README.md
```

---

## 2. Data Schema (JSONL → Parquet)

**Raw event:**
```json
{
  "timestamp_ns": 1722000000123456789,
  "device_path": "\\\\?\\HID#VID_046D&PID_B33A...#{884b96c3-...}",
  "vkey": 65,
  "scan_code": 4,
  "flags": 0,
  "label": "human",
  "subject_id": "s03",
  "session_id": "s03_free_01",
  "payload_variant": null
}
```

**Injected:** `label="injected"`, `payload_variant` ∈ {`max_speed`, `fixed_50ms`, `fixed_100ms`, `random_20_150ms`}

**Processed features (Parquet):** One row per sliding window (N=10, stride=1)
- Timing: `iki_mean`, `iki_std`, `iki_p10`, `iki_p90`, `hold_mean`, `hold_std`, `burstiness`
- Sequential: `digraph_freq_entropy`, `top_5_digraph_share`
- Device: `device_path_hash`, `vendor_id`, `est_report_rate_hz`
- Label: `is_injected`, `subject_id`, `session_id`, `payload_variant`

---

## 3. Capture Tool (`capture/`)

**CLI:**
```bash
capture --output data/raw/s03_free_01.jsonl --label human --subject s03 --session s03_free_01 --duration 300
capture --output data/raw/injected_max_speed_01.jsonl --label injected --payload-variant max_speed --duration 60
```

**Core logic:**
- `RegisterRawInputDevices` for `HID_USAGE_PAGE_GENERIC` + `HID_USAGE_GENERIC_KEYBOARD`
- Message loop: `GetMessage` → `WM_INPUT` → `GetRawInputData` → `RAWKEYBOARD`
- `GetRawInputDeviceInfo(handle, RIDI_DEVICENAME)` → device path string
- Filter: only log events where device path contains `BTHENUM` or `BTHLEDevice`
- Write JSONL lines (buffered, flush every 100 events or 1s)

**Deps:** Win32 API (`user32.lib`), nlohmann/json (header-only), CLI11 or manual `argc/argv` parsing

---

## 4. Feature Engineering (`ml/src/features.py`)

**Window params:** `window_size ∈ {5, 10, 20, 50}`, `stride = 1`

| Category | Features |
|----------|----------|
| IKI stats | mean, std, min, max, p10, p50, p90, cv |
| Hold stats | mean, std, p10, p90 |
| Flight time | mean, std (press_i+1 - release_i) |
| Burstiness | `σ²/μ` of IKI, fraction of IKI < 50ms |
| Digraphs | entropy of top-20 digraph timings, top-5 share |
| Device | hash(device_path), vendor_id |

**Implementation:** Polars; group by `session_id`; rolling window with `shift` + `rolling_*`

---

## 5. ML Training & Evaluation (`ml/src/`)

**Splits:** `GroupKFold(n_splits=5, groups=subject_id)` — strictly subject-independent

**Models:**
1. `RandomForestClassifier(n_estimators=500, max_depth=10, class_weight='balanced')`
2. `XGBClassifier(n_estimators=300, max_depth=6, scale_pos_weight=neg/pos)`
3. `LogisticRegression(C=1.0, class_weight='balanced')` + `CalibratedClassifierCV(method='isotonic')`

**Per-variant evaluation:**
```python
for variant in ['max_speed', 'fixed_50ms', 'fixed_100ms', 'random_20_150ms']:
    y_true, y_pred = eval_on_variant(test_df, variant)
    print(f"{variant}: acc={acc:.3f} f1={f1:.3f} roc_auc={auc:.3f}")
```

**Latency/confidence curves:** For each `window_size`, sweep threshold → plot (keystrokes_needed vs. TPR@FPR=1%)

**Export:** `skl2onnx.convert_sklearn(best_model, initial_types=[('float_input', FloatTensorType([None, n_features]))])`

---

## 6. Windows Service (`service/`)

**Architecture:**
```
Service (SERVICE_TABLE_ENTRY + RegisterServiceCtrlHandler)
  └── Capture Loop (reuses capture raw_input logic)
       └── Event Buffer (std::deque with max-size cap)
            └── Feature Extractor (C++ port of Python logic)
                 └── ONNX Runtime Session (Ort::Session C++ API)
                      └── Decision: p(injected) > threshold
                           └── Toast Notification (WinRT ToastNotificationManager)
                                └── Cooldown: suppress for 30s after alert
```

**Config (`config/service.toml`):**
```toml
model_path = "models/best_model.onnx"
window_size = 20
confidence_threshold = 0.95
cooldown_secs = 30
device_filter = ["BTHENUM", "BTHLEDevice"]
log_level = "info"
```

**Deps:** Win32 API (`advapi32.lib` for service), ONNX Runtime C++ API, WinRT C++/WinRT (Toast), toml++ (header-only)

**Install:** `cmake --build . --config Release` → `sc.exe create BadBTDetector binPath= "C:\path\service.exe"`

---

## 7. Data Collection Scripts

**Human (`scripts/collect_human.sh`):**
- 10 subjects × 3 keyboards × 4 tasks (free_text + 3 fixed_text) = 120 sessions

**Injected (`scripts/collect_injected.sh`):**
- 4 variants × 10 runs each = 40 sessions

---

## 8. Week-by-Week Execution

| Week | Tasks | Deliverables |
|------|-------|--------------|
| **1** | CMake project setup, capture CLI (`raw_input.cpp`, device filtering, JSONL output), collect all data | Working capture `.exe`, raw dataset |
| **2** | Python env, features, GroupKFold splits, train 3 models, per-variant eval, latency curves, export ONNX | Trained models, evaluation report, `best_model.onnx` |
| **3** | C++ feature extraction port, ONNX Runtime C++ integration, service skeleton, Toast notifier, config | Working service (console mode), inference verified |
| **4** | Service install/test, edge cases, cooldown logic, README, release script | Installable `.exe`, config template, docs |

---

## 9. Key Technical Decisions

| Decision | Rationale |
|----------|-----------|
| C++ with direct Win32 calls | APIs are C-native; zero wrapper overhead, no FFI friction |
| Header-only deps (nlohmann/json, toml++) | No build system headaches — just `#include` and go |
| ONNX Runtime C++ API | First-class native API, no bindings layer |
| CMake build system | Standard C++ tooling, good MSVC/Visual Studio integration |
| MSVC compiler with `/MT` | Best Win32/WinRT integration, static CRT linking |
| JSONL → Parquet | Human-readable raw, fast analytics |
| Subject-independent `GroupKFold` | Prevents memorizing typing styles |
| Per-variant evaluation | Randomized-delay is the real threat |
| Toast only, no auto-disconnect | False positive = annoyance, not data loss |

---

## 10. Risks & Mitigations

| Risk | Likelihood | Mitigation |
|------|------------|------------|
| Flipper timing fingerprint dominates | High | Explicit limitation; future: ESP32 source |
| ONNX export fails for XGBoost | Medium | Test export early (Week 2); RF exports reliably |
| Service install requires admin | High | Document `sc.exe` + signed binary |
| Multiple BT keyboards concurrent | Medium | Device-path hash separates in features |
| Feature extraction drift (Python vs C++) | Medium | Validate numerical parity on same input data |

---

## 11. Dependencies

**C++ (capture + service):**

| Dependency | Purpose | Integration |
|------------|---------|-------------|
| Win32 API | Raw Input, device info, console ctrl handler, service API | Link `user32.lib`, `advapi32.lib` — system libs |
| ONNX Runtime 1.16+ | ML inference | NuGet package or download pre-built `.dll` + headers |
| nlohmann/json | JSONL serialization | Header-only, vendored in `third_party/` |
| toml++ | Config file parsing | Header-only, vendored in `third_party/` |
| WinRT (C++/WinRT) | Toast notifications | Ships with Windows SDK 10.0.17763+ |

**Python:**
```toml
dependencies = [
  "polars>=1.0", "scikit-learn>=1.3", "xgboost>=2.0",
  "skl2onnx>=1.15", "onnxruntime>=1.16", "numpy>=1.24",
  "pyyaml", "tqdm",
]
```

---

## 12. Immediate Next Steps

1. Set up CMake workspace with `capture` and `service` subdirectories
2. Implement `raw_input.cpp` — minimal working capture loop with `RegisterRawInputDevices` + `WM_INPUT`
3. Test on one BT keyboard — verify device path contains `BTHENUM`
4. Define JSON schema structs with nlohmann/json serialization
5. Collect pilot data (1 subject, 1 keyboard, 1 injected variant) to validate pipeline