import sys
sys.path.insert(0, '.')
import pandas as pd
import numpy as np
from ml.validate_onnx import load_raw_file, clean_session, pair_dwell_flight, windowize, CANONICAL_FEATURES
import onnxruntime as rt

raw_inj = load_raw_file('data/raw/inj_01_zerodelay_01.jsonl')
paired_inj = pair_dwell_flight(clean_session(raw_inj, 'test'))

raw_hum = load_raw_file('data/raw/adhi_01.jsonl')
paired_hum = pair_dwell_flight(clean_session(raw_hum, 'test'))

# Inj1: 50 keystrokes (indices 0..49)
inj1 = paired_inj.iloc[:50].copy()

# Human types 15 keystrokes 10 seconds later (indices 50..64)
hum = paired_hum.iloc[:15].copy()
hum_offset = (inj1['timestamp_ns'].iloc[-1] + int(10 * 1e9)) - hum['timestamp_ns'].iloc[0]
hum['timestamp_ns'] += hum_offset

# Attack 2 arrives 35 seconds after attack 1 (indices 65..114)
inj2 = paired_inj.iloc[:50].copy()
inj2_offset = (inj1['timestamp_ns'].iloc[-1] + int(35 * 1e9)) - inj2['timestamp_ns'].iloc[0]
inj2['timestamp_ns'] += inj2_offset

combined = pd.concat([inj1, hum, inj2]).reset_index(drop=True)
sess = rt.InferenceSession('model/rf_model.onnx', providers=['CPUExecutionProvider'])
w = windowize(combined, 25, 1, {})
X = w[CANONICAL_FEATURES].fillna(-999.0).values.astype(np.float32)
probs = sess.run(None, {'float_input': X})[1][:, 1]

print(f"Total windows: {len(probs)}")
for i, p in enumerate(probs):
    t_start = (combined['timestamp_ns'].iloc[i] - combined['timestamp_ns'].iloc[0]) / 1e9
    t_end = (combined['timestamp_ns'].iloc[i+24] - combined['timestamp_ns'].iloc[0]) / 1e9
    eps = w['events_per_sec'].iloc[i]
    dwell = w['mean_dwell_ms'].iloc[i]
    # Print when inj2 begins entering the window (keys ending >= 65)
    if (i + 24 >= 64) and (i + 24 <= 95):
        print(f"Win {i:2d} (keys {i:2d}..{i+24:2d}, t={t_start:4.1f}s..{t_end:4.1f}s): P(inj)={p:.4f}, eps={eps:.2f}, dwell={dwell:.1f}")
