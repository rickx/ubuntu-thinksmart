# Venus Hardware Decode Findings

Date: 2026-06-05

## Summary

Hardware video decode via the Qualcomm Venus V4L2 M2M driver works on this device. The single constraint is **B-frames**: any stream encoded with bidirectional prediction stalls at frame=0 regardless of codec or profile. Content without B-frames works reliably at well above real-time.

## Confirmed Results

| Content | Codec | Resolution | Speed | Notes |
|---------|-------|------------|-------|-------|
| Real camera RTSP | H.264 Baseline | 1080p 20fps | **1.26×** real-time | `rtsp://192.168.0.78/av0_0` |
| Synthetic, no B-frames | H.264 High | 1080p | 4.85× | `-bf 0` |
| Synthetic, no B-frames | H.264 Baseline | 1080p | 4.64× | — |
| Synthetic, no B-frames | HEVC | 1080p | 5.74× | `-tune zerolatency` |
| Synthetic, no B-frames | HEVC | 720p | 10.3× | — |
| **Any codec** | H.264 High | any | ✗ fail | frame=0, timeout — B-frames present |
| **Any codec** | HEVC | any | ✗ fail | same — B-frames present |

ffmpeg decoders used: `h264_v4l2m2m`, `hevc_v4l2m2m` on `/dev/video7`.

## B-Frame Constraint

**B-frames (bidirectional predictive frames)** are an encoder choice that references both past and future frames for compression. They improve file size by 15–30% but require the decoder to buffer future frames before decoding current ones.

The Venus V4L2 M2M path on this kernel/firmware version cannot handle B-frame DPB management for either H.264 or HEVC. When B-frames are present, `V4L2_EVENT_SOURCE_CHANGE` does not complete and ffmpeg waits indefinitely at frame=0.

**This is not a profile-level limitation.** H.264 High profile without B-frames (`-bf 0`) works perfectly. Only the presence of B-frames matters.

**Android is not affected** because it uses the Qualcomm OMX/Codec2 path (`OMX.qcom.video.decoder.*`) which handles B-frames via a different firmware interface.

### Practical impact

- **IP camera streams**: not affected. Cameras encode without B-frames for low latency. Both H.264 and HEVC camera streams work.
- **Stored/downloaded content** (movies, YouTube): typically uses B-frames. Will fail unless re-encoded with `-bf 0` or `-tune zerolatency`.

## Session Management Requirements

Venus requires cleanup between test runs to avoid stale-fd and DMA fragmentation failures:

1. Kill any stale ffmpeg holding `/dev/videoX`: `kill -9 <pid>` (D-state processes may need Venus driver unbind to release)
2. Set `power/control=on` for the Venus platform device to prevent PM autosuspend between sessions
3. After many sessions, reload modules to reset DMA allocator: `rmmod venus_dec venus_enc venus_core && modprobe venus_core venus_dec venus_enc`
4. Always use `timeout -k N` (not plain `timeout N`) — Venus-unresponsive ffmpeg ignores SIGTERM

Scripts: `back2ubuntu_18-05/reset-venus.sh`, `back2ubuntu_18-05/h264-bframe-test.sh`

## Test Logs

- `inventories/h264-eval-20260605-112232.log` — multi-codec eval with RTSP camera stream
- `inventories/hevc-variant-test-20260605.log` — HEVC B-frame vs no-B-frame comparison
- `inventories/hevc-1080p-nobframes-20260605.log` — HEVC 1080p no-bframes result
- `inventories/h264-bframe-test-20260605-143420.log` — H.264 B-frame isolation: High+bf / High-nobf / Baseline
