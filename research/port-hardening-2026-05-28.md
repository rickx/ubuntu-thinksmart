# Port Hardening Roadmap

Date: 2026-05-28

This roadmap consolidates current research into concrete, testable next steps to make the Ubuntu port more robust across camera, sensors, and audio.

## Current Baseline

- Camera: one front camera exposed on stock Android (`video1`, camera ID 0, front-facing)
- Sensors: VCNL4200 ALS/PRX and BMA25x accelerometer family active on stock runtime
- Audio: TAS5782M speaker playback working on Ubuntu with S16LE forced; stock Android speaker path is also 16-bit

## Priority 0 — Reliability and Reproducibility

1. Freeze reproducible test scripts for each subsystem
- Camera script: open/close camera, capture `dumpsys media.camera`, save node mappings
- Sensor script: capture `dumpsys sensorservice`, verify VCNL4200 and BMA runtime entries
- Audio script: capture PipeWire/ALSA format + level checks, then dump `hw_params`

2. Add machine-readable pass/fail snapshots
- Save command outputs into dated files under `research/*/inventories/`
- Keep one summary markdown per date under `analysis/`

## Camera Hardening Tasks

1. Confirm active front sensor selection path at runtime
- Correlate `camera_config.xml` candidates with live probe behavior
- Capture additional `QCamera` logcat windows around camera-open events

2. Reduce ambiguity between S5K4E8 and S5KC505A fallback entries
- Keep S5KC505A as primary Ubuntu target (already strongest evidence)
- Add explicit runtime evidence block whenever sensor-id-related logs become available

3. Strengthen driver bring-up checkpoints
- Probe success on canonical ID regs
- One stable preview mode
- Deterministic stream start/stop without HAL-style crashes

## Sensor Hardening Tasks

1. Lock in VCNL4200 behavior parity
- Verify threshold behavior and wake/non-wake channels against stock traces
- Compare ALS response curves under fixed lux conditions

2. Resolve BMA253 vs BMA255 naming discrepancy cleanly
- Treat as BMA25x family unless board-level evidence proves variant split
- Document any calibration or orientation differences seen in runtime

3. Keep non-sensor I2C noise out of backlog
- `3-0014` and `3-0038` are touchscreen fallback DT nodes; no longer sensor candidates

## Audio Hardening Tasks

1. Keep S16LE as production default
- Matches stock Android speaker policy/runtime
- Avoid regressing user-perceived loudness/clarity

2. Add a controlled S24 experimental lane (opt-in only)
- Separate profile/rule set for experimentation
- Collect gain/level measurements and subjective loudness delta

3. Investigate codec DAI format declaration mismatch risk
- Current TAS codec DAI advertises `SNDRV_PCM_FMTBIT_S32_LE`
- Validate if broadening to include `S16_LE` / `S24_LE` changes negotiation behavior on Ubuntu
- Only keep changes that preserve current working playback stability

4. Trace FE/BE format negotiation end-to-end
- Confirm where S24 becomes near-silent (Q6 AFE scaling vs FE/BE packing mismatch)
- Prioritize instrumentation over speculative register changes

## Suggested Execution Order

1. Instrumentation and repeatable capture scripts
2. Camera probe-path hardening
3. Sensor threshold parity checks
4. Audio FE/BE format-trace experiments
5. Optional S24 experimental profile with strict rollback

## Definition of Done for a Stronger Port

- Camera starts/stops reliably and captures preview without manual intervention
- Sensor data is stable and behaviorally consistent with stock expectations
- Audio playback is stable and full-volume on default profile (S16LE)
- Any experimental higher-bit-depth mode is clearly marked non-default and measurable
