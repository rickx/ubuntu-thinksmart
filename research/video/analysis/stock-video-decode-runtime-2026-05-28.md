# Stock Video Decode Runtime Notes

Date: 2026-05-28

## Sources Captured

- `research/video/inventories/getprop-media-codec-2026-05-28.txt`
- `research/video/inventories/media-codec-config-paths-2026-05-28.txt`
- `research/video/inventories/dev-video-media-nodes-2026-05-28.txt`
- `research/video/inventories/sys-class-video4linux-device-links-2026-05-28.txt`
- `research/video/inventories/vendor-media_codecs.xml`
- `research/video/inventories/vendor-media_codecs_performance.xml`
- `research/video/inventories/dumpsys-media-player-2026-05-28.txt`
- `research/video/inventories/dumpsys-media-extractor-2026-05-28.txt`
- `research/video/inventories/dumpsys-media-metrics-2026-05-28.txt`
- `research/video/inventories/cmd-media-codec-list-2026-05-28.txt`
- `research/video/inventories/sample-5s.mp4`
- `research/video/inventories/am-start-movieactivity-2026-05-28.txt`
- `research/video/inventories/dumpsys-media-metrics-after-explicit-playback-2026-05-28.txt`
- `research/video/inventories/dumpsys-media-player-after-explicit-playback-2026-05-28.txt`
- `research/video/inventories/dumpsys-media-extractor-after-explicit-playback-2026-05-28.txt`
- `research/video/inventories/logcat-after-explicit-playback-full-2026-05-28.txt`
- `research/video/inventories/logcat-after-explicit-playback-filtered-2026-05-28.txt`
- `research/video/inventories/sample-5s-hevc.mp4`
- `research/video/inventories/sample-5s-vp9.webm`
- `research/video/inventories/am-start-movieactivity-hevc-2026-05-28.txt`
- `research/video/inventories/am-start-movieactivity-vp9-2026-05-28.txt`
- `research/video/inventories/dumpsys-media-metrics-after-hevc-playback-2026-05-28.txt`
- `research/video/inventories/dumpsys-media-metrics-after-vp9-playback-2026-05-28.txt`
- `research/video/inventories/logcat-after-hevc-playback-filtered-2026-05-28.txt`
- `research/video/inventories/logcat-after-vp9-playback-filtered-2026-05-28.txt`
- `research/video/inventories/pm-list-packages-2026-05-28.txt`
- `research/video/inventories/am-start-widevine-url-2026-05-28.txt`
- `research/video/inventories/dumpsys-media-metrics-before-secure-probe-2026-05-28.txt`
- `research/video/inventories/dumpsys-media-metrics-after-widevine-url-probe-2026-05-28.txt`
- `research/video/inventories/logcat-after-widevine-url-probe-full-2026-05-28.txt`
- `research/video/inventories/logcat-after-widevine-url-probe-filtered-2026-05-28.txt`
- `research/video/inventories/dumpsys-media-drm-2026-05-28.txt`
- `research/video/inventories/dumpsys-drm-drmManager-2026-05-28.txt`

## Confirmed Facts

### 1. Stock Android exposes Qualcomm VIDC video nodes

From `/dev` inventory and sysfs device links:

- `/dev/video32` and `/dev/video33` are present
- both resolve to `/sys/devices/soc/1d00000.qcom,vidc`
- camera nodes are separate (`/dev/video0`, `/dev/video1` and many `/dev/v4l-subdev*` map to `qcom,msm-cam`/CCI paths)

Implication: the kernel-side VPU decode/encode interface exists independently from camera bring-up and should be targetable via V4L2 mem2mem (`qcom,vidc`) on Ubuntu.

### 2. Media service stack for codec/extractor/metrics is active

From getprop and dumpsys:

- `init.svc.mediacodec=running`
- `init.svc.mediaextractor=running`
- `init.svc.mediametrics=running`

This confirms stock userspace keeps the codec pipeline online.

### 3. Vendor codec declarations include broad Qualcomm hardware decoders

From `vendor-media_codecs.xml`, stock declares hardware decode components including:

- `OMX.qcom.video.decoder.avc` (+ secure variant)
- `OMX.qcom.video.decoder.hevc` (+ secure variant)
- `OMX.qcom.video.decoder.vp8`
- `OMX.qcom.video.decoder.vp9`
- `OMX.qcom.video.decoder.mpeg2`
- `OMX.qcom.video.decoder.mpeg4`
- `OMX.qcom.video.decoder.h263`
- `OMX.qcom.video.decoder.wmv` / `vc1` / `divx*`

Selected declared limits include:

- AVC/VP8/VP9/HEVC non-secure decode up to `3840x2160`
- secure AVC/HEVC decode limited to `1920x1088`
- adaptive-playback feature declared on these Qualcomm decoders

### 4. Performance XML contains measured decoder frame-rate tables

From `vendor-media_codecs_performance.xml`:

- `OMX.qcom.video.decoder.avc`: up to `1920x1088` entry at `88 fps`
- `OMX.qcom.video.decoder.hevc`: up to `3840x2160` entry at `44 fps`
- `OMX.qcom.video.decoder.vp8`: up to `1920x1080` entry at `199 fps`
- `OMX.qcom.video.decoder.vp9`: up to `3840x2160` entry at `48 fps`

These are vendor-declared/measured Android values and are useful as an upper-bound reference when validating Ubuntu decode throughput.

### 5. Initial runtime metrics snapshot did not include active video decode sessions

From `dumpsys-media-metrics-2026-05-28.txt` and extractor history:

- finalized codec records in this capture are audio/vorbis decode sessions
- extractor records are also audio-centric (`application/ogg`)
- no confirmed active video decode session was observed in this snapshot

Implication: this initial inventory established capability and node mapping, but required a dedicated on-device playback run to capture live decoder selection.

### 6. `cmd media.codec list` is not available on this build

`cmd-media-codec-list-2026-05-28.txt` reports:

- `cmd: Can't find service: media.codec`

This limits direct service-list enumeration through `cmd`, so XML declarations plus dumpsys/logcat remain the primary evidence path.

### 7. Explicit on-device playback confirms Qualcomm hardware AVC decode path in use

Playback trigger:

- `am start -W -n com.android.gallery3d/.app.MovieActivity -a android.intent.action.VIEW -d file:///sdcard/Download/sample-5s.mp4 -t video/mp4`

From `dumpsys-media-metrics-after-explicit-playback-2026-05-28.txt`:

- `android.media.mediacodec.mime=video/avc`
- `android.media.mediacodec.codec=OMX.qcom.video.decoder.avc`
- `android.media.mediacodec.width=1920`
- `android.media.mediacodec.height=1080`
- NuPlayer session reports `frames=172` and `dropped=3`
- Extractor record shows `android.media.mediaextractor.mime=video/mp4` with `ntrk=2`

Audio side during same playback used `OMX.google.aac.decoder`.

This is direct runtime evidence that stock playback is using Qualcomm hardware video decode (VIDC-backed OMX path), not a software AVC decoder, for this test clip.

### 8. Explicit HEVC playback confirms Qualcomm hardware HEVC decoder path

Playback trigger:

- `am start -W -n com.android.gallery3d/.app.MovieActivity -a android.intent.action.VIEW -d file:///sdcard/Download/sample-5s-hevc.mp4 -t video/mp4`

From `dumpsys-media-metrics-after-hevc-playback-2026-05-28.txt`:

- `android.media.mediacodec.mime=video/hevc`
- `android.media.mediacodec.codec=OMX.qcom.video.decoder.hevc`
- `android.media.mediacodec.width=1920`
- `android.media.mediacodec.height=1080`
- NuPlayer session reports `frames=151` and `dropped=1`

Supporting logcat (`logcat-after-hevc-playback-filtered-2026-05-28.txt`) includes:

- `makeComponentInstance(OMX.qcom.video.decoder.hevc)`
- `MediaCodec: [OMX.qcom.video.decoder.hevc]`

### 9. Explicit VP9 playback confirms Qualcomm hardware VP9 decoder path

Playback trigger:

- `am start -W -n com.android.gallery3d/.app.MovieActivity -a android.intent.action.VIEW -d file:///sdcard/Download/sample-5s-vp9.webm -t video/webm`

From `dumpsys-media-metrics-after-vp9-playback-2026-05-28.txt`:

- `android.media.mediacodec.mime=video/x-vnd.on2.vp9`
- `android.media.mediacodec.codec=OMX.qcom.video.decoder.vp9`
- `android.media.mediacodec.width=1920`
- `android.media.mediacodec.height=1080`
- NuPlayer session reports `frames=151` and `dropped=1`

Supporting logcat (`logcat-after-vp9-playback-filtered-2026-05-28.txt`) includes:

- `makeComponentInstance(OMX.qcom.video.decoder.vp9)`
- `MediaCodec: [OMX.qcom.video.decoder.vp9]`

### 10. Secure-capable decoder declarations exist, but secure runtime path was not observed in this environment

From `vendor-media_codecs.xml`:

- secure decoder components are declared: `OMX.qcom.video.decoder.avc.secure`, `OMX.qcom.video.decoder.hevc.secure`, `OMX.qcom.video.decoder.mpeg2.secure`, `OMX.qcom.video.decoder.vc1.secure`
- each secure decoder block advertises `Feature name="secure-playback" required="true"`

From runtime probing:

- package inventory shows no installed streaming clients (only local players plus `org.chromium.webview_shell`)
- Android VIEW intent for a known Widevine DASH URL resolves to `org.chromium.webview_shell/.WebViewBrowserActivity`
- URL launch succeeds at activity level, but post-probe `dumpsys media.metrics` contains no `OMX.qcom.video.decoder.*.secure` entries and all codec records remain `android.media.mediacodec.secure=0`
- filtered/full post-probe logcat similarly provides no `MediaDrm`/Widevine handshake evidence and no `.secure` component instantiation

Result: secure decode is declared in stock codec config, but this session did not produce runtime proof of secure playback activation due to practical content/app constraints.

### 11. `dumpsys media.drm` and `dumpsys drm.drmManager` return empty output on this build/user context

- `dumpsys-media-drm-2026-05-28.txt` is empty
- `dumpsys-drm-drmManager-2026-05-28.txt` is empty

This means MediaDrm plugin/session-level details are not exposed through these dumpsys endpoints in the current non-root capture path.

## Driver-Oriented Interpretation For Ubuntu

1. Prioritize kernel bring-up around the `qcom,vidc` node (`1d00000.qcom,vidc`) and verify exported mem2mem video devices align with stock's two-node pattern (`video32`, `video33`).
2. Treat stock XML limits as validation targets, not guaranteed Linux-native performance; use them to design stress matrix tiers (720p, 1080p, 4K).
3. Separate camera and decode debugging tracks: camera currently sits on msm-cam/CCI paths, while decode is on VIDC.
4. Non-secure AVC/HEVC/VP9 runtime paths are confirmed (`OMX.qcom.video.decoder.avc`, `.hevc`, `.vp9`) while secure path runtime activation remains unproven in this environment.

## Immediate Next Capture Set (Recommended)

- If app installation/account sign-in becomes possible, run one known DRM stream (Widevine test asset in a DRM-capable player) and capture:
  - `dumpsys media.metrics` before/after playback
  - focused logcat filter for `MediaDrm|WVCdm|OMX|MediaCodec|NuPlayer|secure`
  - explicit confirmation of `OMX.qcom.video.decoder.*.secure` and `android.media.mediacodec.secure=1`
- For Ubuntu driver work, proceed on confirmed non-secure decode path first; treat secure playback as optional parity work until a reproducible stock secure trigger is available.