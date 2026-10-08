import sys
sys.path.insert(0, '.')
import pandas as pd
import numpy as np
from ml.validate_onnx import load_raw_file, clean_session, pair_dwell_flight, windowize, CANONICAL_FEATURES
import onnxruntime as rt

raw = load_raw_file('data/raw/inj_01_zerodelay_01.jsonl')
cleaned = clean_session(raw, 'test')
paired = pair_dwell_flight(cleaned)

inj1 = paired.iloc[:50].copy()
inj2 = paired.iloc[:50].copy()
time_offset = (inj1['timestamp_ns'].iloc[-1] + int(35 * 1e9)) - inj2['timestamp_ns'].iloc[0]
inj2['timestamp_ns'] += time_offset

combined = pd.concat([inj1, inj2]).reset_index(drop=True)
sess = rt.InferenceSession('model/rf_model.onnx', providers=['CPUExecutionProvider'])
w = windowize(combined, 25, 1, {})
X = w[CANONICAL_FEATURES].fillna(-999.0).values.astype(np.float32)
probs = sess.run(None, {'float_input': X})[1][:, 1]

for i, p in enumerate(probs):
    t_start = (combined['timestamp_ns'].iloc[i] - combined['timestamp_ns'].iloc[0]) / 1e9
    t_end = (combined['timestamp_ns'].iloc[i+24] - combined['timestamp_ns'].iloc[0]) / 1e9
    print(f"Win {i:2d} (keys {i:2d}..{i+24:2d}, t={t_start:4.1f}s..{t_end:4.1f}s): P(inj)={p:.4f}, eps={w['events_per_sec'].iloc[i]:.2f}")
