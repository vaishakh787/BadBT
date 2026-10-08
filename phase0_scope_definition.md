# Phase 0 — Scope Definition
## Bluetooth HID Injection ("BadBT") Detection & Classification — Windows Prototype

---

## 1. Problem Statement

Bluetooth HID injection attacks (e.g., malicious BLE keyboards, exemplified by tools like Flipper Zero's BadBT mode) can inject keystrokes into a host machine that are indistinguishable from real human typing at the OS input layer, unless explicitly detected. Prior work addresses either:

- **Bluetooth network/protocol-layer attacks** (spoofing, MITM, DoS) — e.g., BLEGuard, BlueSWAT, ML-BIDS — but does not address input-authenticity classification.
- **USB HID injection detection** (QUACK, HID Detector, Saila) — behavioral/timing-based human-vs-machine classification — but scoped entirely to USB, not Bluetooth.

**Gap:** No existing work applies behavioral input-classification techniques to Bluetooth HID injection specifically. This project targets that gap.

---

## 2. Classification Layer

**Decision: OS input-layer classification only.**

- We classify based on keystroke event data as exposed by the operating system (timing + device identity metadata).
- We do **not** capture or analyze raw Bluetooth radio/protocol traffic (connection intervals, RSSI, HID descriptor bytes at the air interface) in this phase.
- Rationale: radio-layer capture requires dedicated sniffer hardware (Ubertooth One, nRF52840 + Sniffer firmware) not presently available; OS-layer classification is also the actual identified literature gap, so no novelty is lost by deferring radio-layer work.
- Radio-layer features are noted as **future work**, not a Phase 1 dependency.

---

## 3. Target Platform

**Decision: Windows only, for this stage.**

- Capture mechanism: **Windows Raw Input API** (`RegisterRawInputDevices` + `WM_INPUT`), not `SetWindowsHookEx(WH_KEYBOARD_LL)`.
- Reason: Raw Input reports a per-event device handle; `GetRawInputDeviceInfo` (`RIDI_DEVICENAME`) returns a device path that identifies Bluetooth-origin devices (`BTHENUM\...` / `BTHLEDevice\...`), giving free, reliable ground-truth device labeling. Low-level keyboard hooks do not reliably expose per-device identity.
- macOS (IOHIDManager) and Linux (evdev/libinput) are architecturally similar and noted as **portable future work**, not built in this phase.

---

## 4. Attack Definition — What Counts as "BadBT"

**In scope:**
- A device that connects over Bluetooth (Classic or LE) and identifies to the OS as a HID keyboard.
- Whose keystrokes are script/firmware-driven injection, not live human input.

**Explicitly out of scope (parking lot):**
- Bluetooth mice, audio devices, or non-HID Bluetooth attacks (data exfiltration, MITM, DoS) — different problem, different literature.
- Multi-class attribution of *which* attack tool was used — binary classification only, for now.

---

## 5. Classes & Labels

- **Binary classification: `human` vs `injected`.**
- Multi-class (attack-tool attribution) deliberately deferred — insufficient device/data diversity at this stage, and binary matches the comparable literature (QUACK, HID Detector) for direct benchmarking.

---

## 6. Dataset Source Devices (confirmed)

**BadBT source: Flipper Zero (BadBT/BadUSB mode, DuckyScript)**

Planned payload variants (all labeled `injected`, but tagged by sub-type for later stratified analysis):
| Variant | Description | Purpose |
|---|---|---|
| Max-speed | Default DuckyScript, no `DELAY` | Baseline / easy case |
| Fixed-delay | Constant `DELAY` (e.g., 50–100ms) between keys | Naive evasion attempt |
| Randomized-delay | `DELAY` sampled per-keystroke from a range | Realistic evasion attempt — the actual hard case |

**Stated limitation:** all injected data originates from a single hardware source (one Flipper Zero). The model may partially learn Flipper's specific BLE HID stack behavior rather than a fully general "injected" signature. A second, architecturally different BadBT source (e.g., a DIY ESP32 BLE HID emulator) is flagged as future work to validate generalization.

**Human keyboard source: 2–3 physical Bluetooth keyboards**
- Vendor diversity preferred over quantity (different vendors batch/rate HID reports differently — reduces risk of the model keying on vendor-specific artifacts instead of the human-vs-machine distinction).
- Multiple human subjects, varied typing speed, mix of free-text and fixed-text typing tasks.

---

## 7. Data Handling / Ethics Note

- Capture **timing and device metadata only** — do not log actual typed content (matches the privacy stance taken in QUACK).
- If this project is formalized into a paper/thesis, plan for informed consent from human subjects even at small scale.

---

## 8. Success Criteria for This Project Stage

1. Binary classifier operating on a sliding window of N keystroke events (timing + device-path features).
2. Evaluated with **subject-independent** train/test splits (split by person, not by session) to avoid the model learning individual typing signatures instead of the human-vs-injected distinction.
3. Reported **separately per DuckyScript variant** (max-speed / fixed-delay / randomized-delay) — not one blended accuracy number.
4. An explicit, measured **latency/confidence tradeoff**: minimum keystrokes needed before the model reaches a stated confidence threshold. Treated as a real result, not omitted.
5. A background Windows process that surfaces a **warning notification only** (no auto-disconnect) when a window is flagged.

---

## 9. Explicitly Deferred (Future Work List)

- Radio/protocol-layer feature capture (requires sniffer hardware).
- Auto-disconnect / active response.
- macOS / Linux implementations.
- Multi-class attack-tool attribution.
- Second BadBT hardware source for generalization testing.
- Promiscuous over-the-air capture.

---

## 10. Open Items Before Phase 1

- [ ] Finalize exact human subject count and session structure (how many typing sessions per person, task design).
- [ ] Confirm which 2–3 BT keyboard vendors are available.
- [ ] Define exact data schema for logged events (fields, file format) — next step.
