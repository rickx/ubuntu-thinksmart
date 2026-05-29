# Stock Audio Runtime Notes

Date: 2026-05-28

## Sources Captured

- `research/audio/inventories/dumpsys-media-audio_policy-2026-05-28.txt`
- `research/audio/inventories/dumpsys-media-audio_flinger-2026-05-28.txt`
- `research/audio/inventories/vendor-audio_policy_configuration.xml`
- `research/audio/inventories/vendor-audio_platform_info.xml`
- `research/audio/inventories/vendor-audio_platform_info_extcodec.xml`
- `research/audio/inventories/vendor-mixer_paths.xml`

## Confirmed Facts

### 1. Speaker path is policy-locked to 16-bit PCM on stock Android

From `vendor-audio_policy_configuration.xml` and runtime `dumpsys media.audio_policy`:

- `primary output` profile: `AUDIO_FORMAT_PCM_16_BIT` @ 48 kHz, stereo
- `deep_buffer` profile: `AUDIO_FORMAT_PCM_16_BIT` @ 48 kHz, stereo
- `Speaker` device profile: `AUDIO_FORMAT_PCM_16_BIT` @ 48 kHz, stereo
- Speaker routes use sources: `primary output`, `deep_buffer`, `compressed_offload`, BT SCO, telephony
- Speaker route does **not** include `direct_pcm` source

Implication: stock Android normal speaker playback path is intentionally 16-bit.

### 2. 24-bit exists only on direct PCM profile, not speaker-routed profile

From runtime `dumpsys media.audio_policy` HW module dump (`primary` module):

- output `direct_pcm` supports:
  - `AUDIO_FORMAT_PCM_16_BIT`
  - `AUDIO_FORMAT_PCM_8_24_BIT`
  - `AUDIO_FORMAT_PCM_24_BIT_PACKED`
- `direct_pcm` supported devices listed do not include `Speaker` (wired headset/headphones/line/BT/HDMI/proxy shown)

Implication: the policy advertises wider PCM formats only for direct profile/device combinations, not the default speaker route.

### 3. Runtime output threads to speaker are 16-bit in AudioFlinger

From `dumpsys media.audio_flinger`:

- `AudioOut_D` (primary/fast to speaker): `HAL format AUDIO_FORMAT_PCM_16_BIT`
- `AudioOut_15` (deep buffer to speaker): `HAL format AUDIO_FORMAT_PCM_16_BIT`

Implication: effective hardware stream format to speaker is 16-bit in stock runtime.

### 4. Platform info advertises 24-bit speaker backend preference

From `vendor-audio_platform_info.xml` and `vendor-audio_platform_info_extcodec.xml`:

- `bit_width_configs`: `SND_DEVICE_OUT_SPEAKER` -> `bit_width="24"`

This does not override the policy routing facts above; it appears to be a backend capability hint/calibration parameter, not proof of active 24-bit speaker playback in stock Android.

### 5. Ubuntu TAS5782M codec DAI format declaration is S32-only in source

From `sources/driver/tas5782m.c`:

- codec DAI playback `.formats = SNDRV_PCM_FMTBIT_S32_LE`

This is an implementation detail of the codec endpoint declaration. In the full Qualcomm DPCM graph, effective user-visible format constraints are determined by front-end/back-end interactions and machine routing.

## Interpretation for Ubuntu Port

1. Current Ubuntu decision to force S16LE for speaker is consistent with stock Android speaker policy/runtime behavior.
2. Chasing S32 speaker playback should be treated as a low-probability path on current stack.
3. If pursuing improved 24-bit behavior, effort should focus on Q6 AFE/front-end format negotiation and gain scaling alignment, not TAS5782M register programming alone.

## Practical Improvement Targets

- Keep S16LE default on speaker path for reliability.
- Add explicit diagnostics to capture FE/BE negotiated formats during playback (`hw_params`, ASoC debug if available).
- If enabling optional S24 experiments, gate behind a separate profile switch and document expected low-volume failure mode.
- Avoid enabling S32 on default user path until ALSA/Q6 path confirms accepted non-silent playback.
