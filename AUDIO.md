# Working Audio Stack — Lenovo ThinkSmart View (CD-18781Y)

**Device:** Lenovo ThinkSmart View CD-18781Y (APQ8053 / MSM8953)  
**OS:** Ubuntu 22.04 (custom aarch64 image, kernel 6.19.5-msm8953)  
**Status:** Fully working as of 2026-05-19 🔊  
**Confirmed:** Audible speaker output via direct ALSA (`hw:0,0`) and PipeWire (`pw-play`, KDE speaker test)

### Known benign idle behavior
- **REG_03 oscillates `0x60` ↔ `0xe0`** during silence/keepalive. `0x60` = chip PLL detects 48kHz BCLK; `0xe0` = chip PLL loses lock momentarily on a zero-data I2S stream. Normal for BCLK present but no audio data. During active playback REG_03=`0x00`.
- **LPASS QUAD MI2S IBIT clock shows 19.2 MHz** in kernel clockfs debugfs — this is the XO parent rate. The actual BCLK is programmed by the ADSP internally and is not visible via the kernel clock framework. Expected, not a bug.

---

## Architecture Overview

```
KDE / libcanberra / pw-play
         │
    PipeWire daemon (user session)
         │  ACP (Advanced Control Profile)
         │  UCM2 HiFi profile ──→ ALSA controls (QUAT_MI2S_RX routing)
         │
    ALSA (kernel layer)
         │  PCM device: hw:cd18781y,0  (MultiMedia1 front-end)
         │
    Qualcomm Q6 DPCM machine driver
         │  msm8953-qdsp6-sndcard  (in-kernel, no source needed)
         │  Front-ends: MultiMedia1 (hw:0,0), MM3, VoiceMMode1
         │  Back-end: QUATERNARY_MI2S_RX → GPIO 135–138
         │
    ASoC codec driver: snd-soc-tas5782m-dbg
         │  I2C bus 1, address 0x49  (TLMM: SDA=GPIO2, SCL=GPIO3)
         │  PDN: GPIO44 (active-low in DT → physical LOW = chip active)
         │  PVDD: fixed 20V regulator
         │
    TAS5782M hardware amplifier
         └── Speaker (built-in)
```

---

## Component Reference

### 1. Device Tree (DTB)

**File on device:** `/boot/apq8053-lenovo-cd-18781y.dtb`  
**SHA:** `776ed340` (stock Ubuntu DTB, loaded at 2026-05-18 after EDL reflash)

Relevant DT nodes:

```dts
/* TAS5782M I2C node */
tas5782m@49 {
    compatible = "ti,tas5782m";
    reg = <0x49>;
    pdn-gpios = <&tlmm 44 GPIO_ACTIVE_LOW>;   /* ← SETTLED, DO NOT CHANGE */
    pvdd-supply = <&speaker_amp_pvdd>;         /* 20V fixed regulator */
    sound-name-prefix = "Speaker Amp";
    ti,dsp-config-name = "lenovo_cd-18781y";
};

/* Sound card — Qualcomm Q6 machine driver */
sound_card: sound@... {
    compatible = "qcom,msm8953-qdsp6-sndcard";
    aux-devices = <&speaker_amp>;              /* TAS5782M */
    quaternary-mi2s-dai-link {
        /* SCLK=GPIO135, WS=GPIO136, DATA0=GPIO137, DATA1=GPIO138 */
    };
};
```

**PDN polarity — SETTLED:** `GPIO_ACTIVE_LOW` is correct for TAS5782M (PDN pin is active-HIGH on chip; ACTIVE_LOW in DT makes `gpiod_set_value(pdn,1)` produce physical LOW = chip active). See CLAUDE.md for full analysis.

---

### 2. TAS5782M ASoC Codec Driver

**Module on device:** `/lib/modules/6.19.5-msm8953/extra/snd-soc-tas5782m-dbg.ko`  
**SHA (current):** `0c4e99a9` (with mute fix, 2026-05-19)  
**Build:** `CC=clang-18` (KCFI mandatory — GCC builds fail silently at `do_one_initcall`)  
**Auto-load:** `/etc/modules-load.d/audio-cd18781y.conf` → `snd_soc_tas5782m_dbg`

**Source files ([sources/driver/](sources/driver/)):**

| File | Purpose |
|------|---------|
| [sources/driver/tas5782m.c](sources/driver/tas5782m.c) | Core driver: probe, DAI ops, DAPM, work queue |
| [sources/driver/tas5782m.h](sources/driver/tas5782m.h) | Register map, volume table declaration |
| [sources/driver/tas5782m_priv.h](sources/driver/tas5782m_priv.h) | `struct tas5782m_priv` |
| [sources/driver/tas5782m_tables.c](sources/driver/tas5782m_tables.c) | Volume table, preboot sequence, regmap config |
| [sources/driver/tas5782m_dbg.h](sources/driver/tas5782m_dbg.h) | Debug trace macros / debugfs stubs |
| [sources/driver/tas5782m_dbg.c](sources/driver/tas5782m_dbg.c) | Debug implementation (compiled with `-DTAS5782M_DEBUG`) |
| [sources/driver/felixka-reference/](sources/driver/felixka-reference/) | FelixKa's original PMOS driver (reference) |

**FelixKa reference driver** ([sources/driver/felixka-reference/tas5805m-felixka.c](sources/driver/felixka-reference/tas5805m-felixka.c)) — the original working PMOS driver that confirmed TAS5782M register map, preboot sequence, and the zero-initialized `is_muted` pattern.

#### Register Map (Book 0, Page 0)

| Register | Address | Function | Key values |
|----------|---------|---------|------------|
| `REG_PAGE` | 0x00 | Page select within current book | 0x00 = page 0 |
| `REG_RST` | 0x01 | Reset modules/registers | 0x11 = full reset |
| `CTRL2` | 0x02 | Power / mode | 0x00=PLAY, 0x01=POWERDOWN, 0x10=HI-Z |
| `CTRL3` | 0x03 | Mute control | 0x00=unmuted, 0x11=both muted |
| `SIG_CH_CTRL` | 0x2A | DAC channel routing | 0x00=DAC mute |
| `REG_BOOK` | 0x7F | Book select | 0x00=main, 0x8C=DSP coeff |

**⚠️ TAS5782M vs TAS5805M:** reg 0x03 on TAS5805M = DEVICE_CTRL_2 (write 0x03 for PLAY). On TAS5782M reg 0x03 = mute register — writing 0x03 there mutes both channels. The upstream `tas5805m.c` driver is incorrect for TAS5782M.

#### Volume Registers (DSP Coefficient Memory)

Volume lives in Book `0x8C`, Page `0x2A`:
- Left channel: offset `0x24` (4 bytes, big-endian fixed-point)
- Right channel: offset `0x28` (4 bytes, big-endian fixed-point)

Range: 86 steps, 0.5 dB each (step 0 ≈ −90 dB, step 85 = 0 dB / unity).  
The ALSA control "Speaker Amp Master Playback Volume" maps to these.

#### Playback Flow (critical sequence)

```
probe()
  ├── devm_kzalloc → is_muted = false, is_powered = false  (kzalloc zeros)
  ├── is_muted = true          ← NOTE: explicitly set true at probe
  ├── Firmware cached: tas5728m_dsp_lenovo_cd-18781y.bin
  ├── PVDD enabled (100ms settling)
  └── GPIO44 asserted (chip exits power-down)

DAPM routing opens PCM → ASoC DPCM starts stream
  └── .trigger(START) fires
        └── schedule_work(&priv->work)

do_work() [kworker context]
  ├── wait 5ms  (BCLK from Q6 AFE needs to stabilize)
  ├── send_cfg(tas5782m_dsp_init)   ← preboot: reset + mute + 0x25=0x18 + power-on
  ├── wait 5–15ms  (DSP internal boot)
  ├── send_cfg(dsp_cfg_data)        ← firmware blob from .bin file
  ├── is_muted = false              ← KEY: must happen before refresh()
  ├── is_powered = true
  └── tas5782m_refresh()
        ├── set_volume()  [Book 0x8C/Page 0x2A → Book 0/Page 0]
        └── write CTRL3 = 0x00 (TAS5782M_NORMAL_VOLUME)  ← chip unmuted

Audio plays ✓
```

#### Mute Bug (fixed 2026-05-19)

**Symptom:** `speaker-test -D hw:0,0` worked (audible); `pw-play` / KDE speaker test → silence.

**Root cause:** `priv->is_muted = true` at probe. In the direct-ALSA path, `mute_stream(mute=0)` is called before playback → sets `is_muted=false` → `refresh()` writes CTRL3=0x00. In the PipeWire/DPCM path, `mute_stream(0)` is not called (PipeWire's ACP layer does not invoke the ALSA digital mute ioctl path before stream start). `do_work` called `refresh()` with `is_muted=true` → wrote CTRL3=0x11 → chip muted.

**Fix:** In `do_work`, set `priv->is_muted = false` before `priv->is_powered = true` and `refresh()`. This mirrors FelixKa's driver, where `devm_kzalloc` zero-initializes `is_muted` and no explicit `= true` is ever set.

**Verification after fix (dmesg, t=106s in boot):**
```
[106.244992] tas5782m 1-0049: [tas5782m MUTE] mute_stream mute=0
[106.297682] tas5782m 1-0049: [tas5782m PLAY] trigger(START) — scheduling init work
[106.297755] tas5782m 1-0049: [tas5782m INIT] stream-start: waiting 5ms for BCLK...
[107.074865] tas5782m 1-0049: [tas5782m PLAY] stream-start: chip in PLAY mode, refresh applied
```
reg 0x03 = `0x00` after playback ✓

#### Preboot Sequence (tas5782m_dsp_init)

```c
TAS5782M_REG_PAGE, 0x00,          // Book 0 / Page 0
TAS5782M_REG_BOOK, 0x00,
TAS5782M_CTRL2,    HIZ|POWERDOWN, // safe state before reset
TAS5782M_REG_RST,  0x11,          // full reset
// ... re-select page/book after reset ...
TAS5782M_CTRL3,    MUTE,          // mute before firmware load
TAS5782M_SIG_CH_CTRL, DAC_MUTE,  // mute DAC path
0x25, 0x18,                       // IGNORE_SCLK | IGNORE_SCLK_HALT (prevents BCLK fault)
0x0d, 0x10,                       // PLL ref = SCLK
TAS5782M_CTRL2,    PLAY,          // power on
```

The `0x25 = 0x18` write is **critical** — without it the chip stays in Sleep mode due to BCLK/PLL fault (GLOBAL2=0x03). FelixKa identified this; copied verbatim.

#### Build Command

```bash
# In WSL or Linux — MUST use CC=clang-18 (KCFI alignment with kernel)
export KSRC=/path/to/kernel-build-v619
export WSLSRC=/path/to/tas5782m-src

make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- CC=clang-18 \
     KBUILD_MODPOST_WARN=1 \
  -C "$KSRC" \
  M="$WSLSRC" \
     modules
```

See [sources/scripts/build-wsl.sh](sources/scripts/build-wsl.sh).

---

### 3. I2C Bus — QUP pinctrl fix

**Bus:** `78b5000.i2c` (BLSP1 QUP1) → GPIO2 (SDA), GPIO3 (SCL)  
**Device:** TAS5782M at address `0x49` (I2C bus 1 = logical `1-0049`)

**Root cause of initial I2C failure:**  
After boot, PM runtime autosuspend resets GPIO2/3 mux from `blsp_i2c1` (func2) back to GPIO mode (func0). The pinctrl framework shows "default" state in software but the hardware register is wrong. All I2C transactions → `-EIO`.

**Fix:** Unbind + rebind `i2c_qup` driver → triggers `pinctrl_bind_pins()` → restores FUNC_SEL=2 for GPIO2/3. Then `power/control = on` to prevent re-regression via PM runtime.

**Persistent service:** `/etc/systemd/system/qup-i2c-pinctrl-fix.service`  
→ runs `/usr/local/sbin/qup-i2c-pinctrl-fix.sh`  
→ `After=sysinit.target Before=sound.target alsa-restore.service`  
→ `WantedBy=multi-user.target` (enabled)

**Source:** [sources/scripts/persist-qup-fix.sh](sources/scripts/persist-qup-fix.sh)

To recover manually (without reboot):
```bash
sudo systemctl start qup-i2c-pinctrl-fix.service
```

Verify I2C works:
```bash
i2ctransfer -y -f 1 w1@0x49 0x00 r1   # should return: 0x00
```

---

### 4. DSP Firmware

**File:** `/lib/firmware/tas5728m_dsp_lenovo_cd-18781y.bin` (3044 bytes, real TI PPC3 config)  
**Symlink:** `/lib/firmware/tas5728m_dsp_default.bin` → `tas5728m_dsp_lenovo_cd-18781y.bin`

The driver probes with `ti,dsp-config-name = "lenovo_cd-18781y"` from DT, constructing filename `tas5728m_dsp_lenovo_cd-18781y.bin`. The `default` symlink is a fallback in case the DT property isn't read.

The firmware is a raw sequence of `{reg, val}` pairs (PPC3 export format), loaded after the preboot sequence settles. It configures DSP coefficients, EQ, and channel routing in Book 0x8C.

---

### 5. ALSA Card and PCM Devices

**Card name (ALSA long name):** `cd-18781y` (with hyphen — used for UCM lookup)  
**Card ID (short name):** `cd18781y` (no hyphen — used for `amixer -c`, `aplay -D`)

PCM devices:
| PCM | hw address | Use |
|-----|-----------|-----|
| MultiMedia1 | `hw:cd18781y,0` | Speaker playback ← primary |
| MultiMedia3 | `hw:cd18781y,2` | Not used for speaker |
| VoiceMMode1 | `hw:cd18781y,4` | Voice call path |

**ALSA routing mixer control** (must be ON for audio to reach the DAC):
```bash
amixer -c 0 cset name='QUAT_MI2S_RX Audio Mixer MultiMedia1' 1
```
This is set by the UCM HiFi EnableSequence (automatically when PipeWire activates the HiFi profile) or manually.

**Volume control:**
```bash
amixer -c 0 cset name='Speaker Amp Master Playback Volume' 60,60
```
Persisted via `/var/lib/alsa/asound.state` (loaded by `alsa-restore.service` at boot).

**Note:** No ALSA PipeWire plugin (`libasound_module_pcm_pipewire.so`) is installed. The ALSA `default` device goes directly to `hw:cd18781y,0`, bypassing PipeWire. Use `pw-play` for PipeWire-native playback.

---

### 6. UCM2 (ALSA Use Case Manager)

UCM2 provides named device profiles ("HiFi verb") that abstract the ALSA mixer controls. PipeWire's ACP uses UCM to enumerate sinks/sources and configure routing.

**UCM lookup:** ALSA resolves by card long name `cd-18781y` (with hyphen).  
**Config root:** `/usr/share/alsa/ucm2/`

**Files deployed:**

| Path | Content |
|------|---------|
| `conf.d/cd18781y/cd18781y.conf` | Entry point for ALSA card ID (no-hyphen alias) |
| `conf.d/cd-18781y/cd-18781y.conf` | Entry point for ALSA long name (with hyphen) |
| `Lenovo/cd-18781y/HiFi.conf` | HiFi verb definition — Speaker + Microphone devices |
| `Qualcomm/cd-18781y/cd-18781y.conf` | Qualcomm machine-driver path alias |

**HiFi verb (`Lenovo/cd-18781y/HiFi.conf`):**
```ucm
SectionVerb {
    EnableSequence [
        cset "name='QUAT_MI2S_RX Audio Mixer MultiMedia1' 1"   ← routes Q6 → TAS
        cset "name='MultiMedia2 Mixer TERT_MI2S_TX' 1"         ← mic capture
    ]
    ...
}
SectionDevice."Speaker" {
    Value { PlaybackPCM "hw:${CardId},0" }    ← MultiMedia1
}
SectionDevice."Microphone" {
    EnableSequence [
        cset "name='DEC1 MUX' DMIC1"
        cset "name='DEC2 MUX' DMIC2"
        ...
    ]
    Value { CapturePCM "hw:${CardId},1" }
}
```

**BootSequence** sets microphone digital volumes:
```ucm
BootSequence [
    cset "name='TX1 Digital Volume' 115"
    cset "name='TX2 Digital Volume' 115"
]
```

**Deploy script:** [sources/deploy-ucm-pipewire.sh](sources/deploy-ucm-pipewire.sh)

**Verify:**
```bash
alsaucm -c "cd-18781y" listucm    # ← must use long name with hyphen
```

---

### 7. WirePlumber

WirePlumber is the PipeWire session manager. It enumerates ALSA devices, applies UCM profiles, and manages default sink selection.

#### Custom rule file

**Path:** `/etc/wireplumber/main.lua.d/99-cd18781y-alsa.lua`

**Why `99-` (not `51-`):** WirePlumber applies its own built-in policies after loading numbered files. A `51-` file gets its format settings overridden by WirePlumber's internal defaults; loading as `99-` ensures the rules are the last applied and actually take effect. Confirmed empirically: deploying the same content as `51-cd18781y-alsa.lua` resulted in PipeWire negotiating `S24_LE` at runtime; renaming to `99-` locked it back to `S16_LE`.

**Canonical content** ([sources/deploy-ucm-pipewire.sh](sources/deploy-ucm-pipewire.sh) deploys this):

```lua
alsa_monitor.rules = alsa_monitor.rules or {}

-- Prefer ACP routing and disable mmap probes that fail on this card.
table.insert(alsa_monitor.rules, {
  matches = {
    { { "device.name", "equals", "alsa_card.platform-c051000.sound-card" } },
  },
  apply_properties = {
    ["api.alsa.use-acp"]      = true,
    ["api.alsa.use-ucm"]      = true,
    ["api.acp.auto-profile"]  = true,
    ["api.acp.auto-port"]     = true,
    ["api.alsa.disable-mmap"] = true,   -- mmap unsupported on Qualcomm DPCM path
  },
})

-- Keep the speaker node always active and force S16 format.
table.insert(alsa_monitor.rules, {
  matches = {
    { { "node.name", "equals", "alsa_output.platform-c051000.sound-card.HiFi__hw_cd18781y_0__sink" } },
    { { "node.name", "equals", "alsa_output.platform-c051000.sound-card.playback.0.0" } },
  },
  apply_properties = {
    ["node.pause-on-idle"]             = false,
    ["session.suspend-timeout-seconds"] = 0,
    ["audio.format"]                   = "S16LE",
    ["audio.rate"]                     = 48000,
    ["audio.channels"]                 = 2,
  },
})
```

`api.alsa.disable-mmap = true` is required — the Qualcomm DPCM driver does not support mmap-based transfers from userspace and returns an error during WirePlumber's hw probe.

#### Sample format: why S16 must be forced

| Format | ALSA hw:0,0 | PipeWire volume | Notes |
|--------|-------------|-----------------|-------|
| S16_LE | ✓ opens | **Full volume** | ← use this |
| S24_LE | ✓ opens | Much quieter (almost inaudible) | Scaling/alignment mismatch in QDSP6 AFE |
| S32_LE | ✗ EINVAL | — | Rejected by ALSA hw path |

The TAS5782M chip itself supports 16/20/24/32-bit serial audio (per datasheet §9.3.3.5, Table 7). The constraint is in the Linux audio stack: the Qualcomm QDSP6/ASoC machine code and ASM front-end path on this device only handle S16 and S24. S32 is rejected at ALSA level. S24 is accepted but the QDSP6 AFE applies incorrect volume scaling for this output path, resulting in near-silence. S16 is the correct operating point.

Confirmed via `wpctl inspect @DEFAULT_AUDIO_SINK@` which shows `alsa.resolution_bits = "16"` and `audio.format = "S16LE"` once the rule is active, and `/proc/asound/card0/pcm0p/sub0/hw_params` reporting `format: S16_LE` during playback.

#### Persistent profile and default sink

WirePlumber stores selected profile and default node in:

| File | Content |
|------|---------|
| `~/.local/state/wireplumber/default-profile` | `alsa_card.platform-c051000.sound-card=HiFi` |
| `~/.local/state/wireplumber/default-nodes` | `default.configured.audio.sink=alsa_output.platform-c051000.sound-card.HiFi__hw_cd18781y_0__sink` |

Without these files, WirePlumber may auto-select "pro-audio" profile on boot, which does not activate the UCM speaker routing.

**Verify profile is active:**
```bash
wpctl status      # should show "Built-in Audio Speaker" as default sink (*)
```

**If profile resets to pro-audio:**
```bash
wpctl set-profile <device-id> 1    # 1 = HiFi index
# Then persist:
sed -i 's/=.*/=HiFi/' ~/.local/state/wireplumber/default-profile
```

#### How PipeWire routes audio to the speaker

1. Application (pw-play, KDE) → PipeWire daemon
2. PipeWire ACP driver sees card `alsa_card.platform-c051000.sound-card`
3. WirePlumber rule loads UCM HiFi profile → runs `EnableSequence` (sets QUAT_MI2S_RX mixer)
4. Sink node: `alsa_output.platform-c051000.sound-card.HiFi__hw_cd18781y_0__sink`
5. PipeWire opens `hw:cd18781y,0` (MultiMedia1)
6. ALSA → Q6 DPCM front-end → Q6 AFE → QUATERNARY_MI2S_RX → GPIO 135–138
7. I2S BCLK appears on GPIO135 → TAS5782M PLL locks
8. `trigger(START)` → `do_work` → preboot + firmware + unmute → speaker active

---

### 8. ALSA State Persistence

**File:** `/var/lib/alsa/asound.state`  
**Service:** `alsa-restore.service` (loads state at boot, before sound.target)

Volume saved: `Speaker Amp Master Playback Volume` = 60, 60

Save current state:
```bash
sudo alsactl store
```

---

## Boot Sequence Summary

```
Boot
  │
  ├── qup-i2c-pinctrl-fix.service  (Before: sound.target)
  │     └── unbind/rebind 78b5000.i2c → GPIO2/3 = blsp_i2c1
  │         power/control = on → no future autosuspend regression
  │
  ├── alsa-restore.service
  │     └── /var/lib/alsa/asound.state → Speaker Amp volume = 60,60
  │
  ├── modules-load (audio-cd18781y.conf)
  │     └── insmod snd_soc_tas5782m_dbg
  │           ├── firmware cached: tas5728m_dsp_lenovo_cd-18781y.bin
  │           ├── PVDD enabled (100ms wait)
  │           └── GPIO44 asserted → TAS5782M exits power-down
  │               I2C responds: regmap 0x00 = 0x00 ✓
  │
  ├── PipeWire + WirePlumber (user session)
  │     ├── WirePlumber loads 99-cd18781y-alsa.lua rule
  │     ├── ACP enumerates card with UCM HiFi profile
  │     ├── default-profile → HiFi profile activated
  │     │     └── UCM EnableSequence: QUAT_MI2S_RX = 1
  │     └── default sink: HiFi__hw_cd18781y_0__sink
  │
  └── Audio ready
        ├── pw-play / KDE test → PipeWire → hw:0,0 → Q6 → MI2S → TAS5782M
        └── speaker-test -D hw:0,0 → direct ALSA → same Q6 path
```

---

## Quick Diagnostics

```bash
# I2C health
i2ctransfer -y -f 1 w1@0x49 0x00 r1   # expect: 0x00
i2ctransfer -y -f 1 w1@0x49 0x02 r1   # expect: 0x00 during play, 0x10 when idle

# Module loaded
lsmod | grep tas    # snd_soc_tas5782m_dbg    24576  1

# Routing on
amixer -c 0 cget name='QUAT_MI2S_RX Audio Mixer MultiMedia1'  # values=on

# PipeWire profile
wpctl status | grep Speaker   # * Built-in Audio Speaker [vol: ...]

# Volume
amixer -c 0 cget name='Speaker Amp Master Playback Volume'    # values=60,60

# Direct ALSA test (bypasses PipeWire)
amixer -c 0 cset name='QUAT_MI2S_RX Audio Mixer MultiMedia1' 1
speaker-test -D hw:0,0 -c 2 -t sine -f 1000 -l 1

# PipeWire test
pw-play /usr/share/sounds/Oxygen-Sys-Trash-Emptied.ogg

# TAS register state during play
i2ctransfer -y -f 1 w1@0x49 0x03 r1   # 0x00 = unmuted, 0x11 = muted

# dmesg audio events
dmesg | grep -i 'tas\|trigger\|PLAY\|mute' | tail -10
```

---

## Known Issues / Notes

1. **No ALSA PipeWire plugin:** `libasound_module_pcm_pipewire.so` not installed. `speaker-test -D default` and `speaker-test -D pulse` do NOT go through PipeWire — they hit `hw:cd18781y,0` directly. `pw-play` is the correct tool for PipeWire-native tests.

2. **reg 0x03 reads ~0x60 after stream stop:** After teardown (DAPM PRE_PMD → HIZ), a raw `i2ctransfer` read of reg 0x03 may return unexpected values (e.g., 0x60). This is a Book/Page context issue — the chip may be in Book 0x8C (DSP memory) at the moment of reading. Not a functional problem; the chip enters HI-Z mode correctly.

3. **KCFI:** Module MUST be built with `CC=clang-18`. GCC produces a different CFI type hash → kernel refuses to call the module's `init_module` → probe never runs → no audio. The kernel was compiled with `clang-18.1.3` and has KCFI enabled.

4. **GPU faults:** Repeated `qcom-iommu-ctx ... Unhandled context fault` in dmesg during UI use. Independent of audio stack; related to VP5/Adreno workload.

5. **UCM card name mismatch:** `alsaucm -c cd18781y` (no hyphen) fails with ENOENT. `alsaucm -c "cd-18781y"` (long name with hyphen) succeeds. PipeWire uses `api.alsa.card.name` which is the long name, so UCM does load correctly.

---

## Source Files

All relevant sources are in [FINAL/sources/](sources/):

| File | Description |
|------|-------------|
| [tas5782m.c](sources/driver/tas5782m.c) | Main driver (probe, DAI, DAPM, workqueue) |
| [tas5782m.h](sources/driver/tas5782m.h) | Registers, volume table declaration |
| [tas5782m_priv.h](sources/driver/tas5782m_priv.h) | Private state struct |
| [tas5782m_tables.c](sources/driver/tas5782m_tables.c) | Volume table, preboot seq, regmap config |
| [tas5782m_dbg.h](sources/driver/tas5782m_dbg.h) | Debug trace / debugfs API |
| [tas5782m_dbg.c](sources/driver/tas5782m_dbg.c) | Debug implementation |
| [felixka-reference/tas5805m-felixka.c](sources/driver/felixka-reference/tas5805m-felixka.c) | FelixKa's PMOS reference driver |
| [felixka-reference/tas5805m-felixka.h](sources/driver/felixka-reference/tas5805m-felixka.h) | FelixKa's register map + init sequence |
| [deploy-ucm-pipewire.sh](sources/scripts/deploy-ucm-pipewire.sh) | Deploys UCM2 + WirePlumber config to device |
| [persist-qup-fix.sh](sources/scripts/persist-qup-fix.sh) | Deploys QUP I2C pinctrl fix service |
| [build-wsl.sh](sources/scripts/build-wsl.sh) | WSL module build script |
