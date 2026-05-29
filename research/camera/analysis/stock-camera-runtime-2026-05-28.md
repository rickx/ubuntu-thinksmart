# Stock Camera Runtime Notes

Date: 2026-05-28

Source dump:

- `research/camera/inventories/dumpsys-media-camera-2026-05-28.txt`
- `research/camera/inventories/dumpsys-media-camera-after-launch-2026-05-28.txt`
- `research/camera/inventories/camera-sysfs-after-launch-2026-05-28.txt`
- `research/camera/inventories/logcat-camera-filtered-2026-05-28.txt`

## Confirmed Runtime Facts

- Camera service reports exactly one camera device (`Number of camera devices: 1`)
- Exposed camera is `legacy/0`, facing `Front`, orientation `270`
- No flash unit is present (`Has a flash unit: false`)
- Focus mode is fixed (`focus-mode: fixed`)
- Dynamic parameters include:
  - `focal-length: 4.04`
  - `horizontal-view-angle: 64.8`
  - `vertical-view-angle: 51.2`
  - `raw-size: 2608x1960`
- API2 metadata includes:
  - `android.sensor.info.pixelArraySize = [2624 1976]`
  - `android.sensor.info.activeArraySize = [8 8 2608 1960]`
  - `android.sensor.info.colorFilterArrangement = GRBG`
  - `android.lens.facing = FRONT`

## Post-Launch Snapshot

After launching Snapcam (`org.codeaurora.snapcam`) and capturing immediate runtime state:

- Camera service shows active client on camera ID 0 (`State: 2`, PID 5890)
- Kernel live camera nodes remain:
  - `1b0c000.qcom,cci:qcom,camera@0`
  - `1b0c000.qcom,cci:qcom,camera@2`
- `camera@0` maps to `video1`
- `camera@2` has no `video4linux` child at probe time
- `actuator@0` and `actuator@1` still had no readable bound driver link at probe time

## Logcat Camera Enumeration Signals

From filtered logcat (`QCamera` / `mm-qcamera-daem`) capture:

- `get_num_of_cameras` reports `dev_info[id=0,name='video1']`
- HAL logs `num_cameras=1`
- Sorting logs show `Camera id: 0 facing: 1` (front-facing in Qualcomm convention)

These logs reinforce that the active Android stack is centered on a single front camera
pipeline associated with `video1`.

## Interpretation

Runtime camera service behavior is consistent with a front-camera-only exposed profile.
The optical values match the front module values from `camera_config.xml` and support
ongoing S5KC505A-focused bring-up as the most relevant Ubuntu target path.
