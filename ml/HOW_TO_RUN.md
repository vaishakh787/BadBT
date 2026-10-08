# How to Run This on Your PC (Windows)

## 1. Install Python
If you don't already have it: download Python 3.11+ from https://www.python.org/downloads/
During install, check **"Add python.exe to PATH"**.

## 2. Unzip and open a terminal in the project folder
Unzip `badbt_notebooks.zip` somewhere convenient, e.g. `C:\Users\you\badbt_notebooks`.
Open PowerShell (or cmd) and `cd` into it:
```powershell
cd C:\Users\you\badbt_notebooks
```

## 3. Create a virtual environment (recommended, keeps this isolated from other Python stuff)
```powershell
python -m venv venv
venv\Scripts\activate
```
You should see `(venv)` appear at the start of your prompt.

## 4. Install the required packages
```powershell
pip install jupyter pandas numpy scipy scikit-learn matplotlib joblib
```

## Project layout — one data folder, not two
Raw and processed data live in a single place: `<repo root>/data/raw` and
`<repo root>/data/processed`. The notebooks live in `ml/` (one level down) and reference
that folder as `../data/raw` / `../data/processed` — there is no separate copy of the data
inside `ml/` anymore, so editing/regenerating data only ever needs to happen in one place.

**⚠️ The pretrained models currently in `model/*.joblib` were trained before this fix**
(old window size of 25, and 16 human capture files that were mislabeled under one shared
`session_id`/`subject_id`). Those raw files have now been corrected (see repo notes / your
own change log) and the window size changed to 20 to match `config/service.toml`. **You
need to re-run notebooks 1, 2, and 5 to regenerate `features.csv` and every `model/*.joblib`
file before trusting any prediction** — the old saved models and the old `outputs/*` plots
and reports reflect the pre-fix data and are now stale.

## 5. Launch Jupyter
```powershell
jupyter notebook
```
This opens a browser tab at `http://localhost:8888`. Open the notebooks from inside `ml/`,
in order:

1. **`01_build_features.ipynb`** — turns raw `.jsonl` logs from `../data/raw` into
   `../data/processed/features.csv`. **Re-run this now** — the underlying raw data changed.
2. **`02_train_model.ipynb`** — trains the Random Forest on `features.csv` and saves it to
   `model/rf_model.joblib`. **Re-run this now** too, for the same reason.
3. **`03_predict.ipynb`** — classifies a new raw log. This is the one you'll actually use
   day-to-day. Change the `RAW_LOG_PATH` variable in the second cell to point at whatever
   session you want to check, then run all cells (`Cell → Run All` in the menu, or
   `Shift+Enter` through each one).
4. **`04_generate_evasion_payload.ipynb`** — generates a Flipper Zero DuckyScript
   (`HOLD`/`DELAY`/`RELEASE` per key) with dwell and flight timing sampled from your real
   human capture data, then simulates the resulting keystroke stream and runs it through
   the trained model so you know whether it evades detection before touching hardware.
   Change `PAYLOAD_TEXT` to whatever you want typed. Output is `evasion_payload.txt` —
   copy that to `SD Card/badusb/` on the Flipper. Only run generated payloads on systems
   you own or are authorized to test.

To run every cell in a notebook top to bottom: click the first cell, then press **Shift+Enter**
repeatedly, or use the menu **Cell → Run All**.

## What's already included
- `../data/raw/` — your original capture logs (single copy, shared by every notebook)
- `../data/processed/features.csv` — needs rebuilding (see warning above)
- `model/rf_model.joblib` + `model/model_meta.json` — needs retraining (see warning above)
- `outputs/` — evaluation plots/report (will be re-created once you re-run notebook 2)

## Classifying a log from your C++ capture tool
Point `RAW_LOG_PATH` in `03_predict.ipynb` at the `.jsonl` file your `badbt-capture.exe`
tool wrote, run the notebook, and read the verdict + per-window probability plot at the
bottom. There's also a cell further down that scans a whole folder of `.jsonl` files at once
if you want to batch-check a session archive — just change `RAW_DIR_TO_SCAN`.

## If something breaks
- **`ModuleNotFoundError`** → you forgot to activate the venv (`venv\Scripts\activate`) or
  install a package — re-run the `pip install` line from step 4.
- **`FileNotFoundError` on a jsonl/csv/model file** → check the path in the config cell at
  the top of the notebook matches where the file actually is relative to where you launched
  `jupyter notebook` from.
- Jupyter should always be launched from inside the `ml/` folder so the relative paths
  (`../data/raw`, `model/`, etc.) resolve correctly.
