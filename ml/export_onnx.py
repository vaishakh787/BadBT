"""
BadBT Detection — ONNX Model Exporter
Converts canonical model/rf_model.joblib to model/rf_model.onnx with
exact 12-feature float32 input tensor [None, 12] and probability tensor [None, 2].
"""

import os
import json
import joblib
from skl2onnx import convert_sklearn
from skl2onnx.common.data_types import FloatTensorType

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

def export():
    model_dir = "model"
    joblib_path = os.path.join(model_dir, "rf_model.joblib")
    meta_path = os.path.join(model_dir, "model_meta.json")
    onnx_path = os.path.join(model_dir, "rf_model.onnx")

    assert os.path.exists(joblib_path), f"Missing {joblib_path}"
    assert os.path.exists(meta_path), f"Missing {meta_path}"

    with open(meta_path, "r", encoding="utf-8") as f:
        meta = json.load(f)

    meta_features = meta.get("feature_columns", [])
    assert meta_features == CANONICAL_FEATURES, (
        f"Feature mismatch!\nMeta: {meta_features}\nCanonical: {CANONICAL_FEATURES}"
    )

    clf = joblib.load(joblib_path)
    print(f"Loaded {joblib_path} (n_estimators={clf.n_estimators}, n_features_in_={clf.n_features_in_})")

    initial_type = [("float_input", FloatTensorType([None, len(CANONICAL_FEATURES)]))]

    # Disable zipmap so output is a direct tensor [None, 2] instead of map
    options = {"zipmap": False}

    print("Converting model to ONNX...")
    onx = convert_sklearn(
        clf,
        name="BadBT_RandomForest",
        initial_types=initial_type,
        options=options,
        target_opset=17,
    )

    with open(onnx_path, "wb") as f:
        f.write(onx.SerializeToString())

    print(f"Saved ONNX model -> {onnx_path} ({os.path.getsize(onnx_path):,} bytes)")

if __name__ == "__main__":
    export()
