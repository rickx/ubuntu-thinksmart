/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TAS5782M_H
#define _TAS5782M_H

#include <linux/regmap.h>
#include <linux/debugfs.h>
#include <linux/atomic.h>
#include <sound/soc.h>

// ---------------------------------------------------------------------------
// Register map — Book 0, Page 0 (default)
// ---------------------------------------------------------------------------

#define TAS5782M_REG_PAGE       0x00  // Page select (within current book)
#define TAS5782M_REG_RST        0x01  // Reset control
#define TAS5782M_CTRL2          0x02  // Device control 2 — power/mode
#define TAS5782M_CTRL3          0x03  // Device control 3 — mute
#define TAS5782M_SIG_CH_CTRL    0x2A  // TAS5782M channel routing (0x2A, not TAS5805M's 0x28)
#define TAS5782M_AUTOMUTE_CTRL  0x59  // Auto-mute enable: bit4=R, bit0=L; default 0x11 = both enabled
#define TAS5782M_SAP_CTRL_1     0x33  // Serial audio port control (BCLK ratio)
/* NOTE: TAS5782M fault/status register address is not fully settled in public
 * docs. 0x68 works on some traces, but FelixKa notes TAS5782M fault registers
 * are not clearly documented. Keep this configurable point visible. */
#define TAS5782M_FAULT          0x68
#define TAS5782M_REG_BOOK       0x7F  // Book select

// CTRL2 (0x02) — power/mode bits (FelixKa/Android-aligned)
#define TAS5782M_MODE_PLAY        0x00  // Power-on/play
#define TAS5782M_MODE_POWERDOWN   0x01
#define TAS5782M_MODE_HIZ         0x10  // Standby/HI-Z
#define TAS5782M_MODE_HIZ_PD      (TAS5782M_MODE_HIZ | TAS5782M_MODE_POWERDOWN)

// CTRL3 (0x03) — mute
#define TAS5782M_MUTE           0x11  // Mute both channels (L|R)
#define TAS5782M_NORMAL_VOLUME  0x00

// SIG_CH_CTRL (0x28) — channel assignment
#define TAS5782M_CH_LEFT_LEFT     0x10
#define TAS5782M_CH_RIGHT_RIGHT   0x01
#define TAS5782M_CH_DAC_MUTE      0x00
#define TAS5782M_CH_DAC_NORMAL    (TAS5782M_CH_LEFT_LEFT | TAS5782M_CH_RIGHT_RIGHT)  /* 0x11, normal L→L R→R */

// SAP_CTRL_1 (0x33) — BCLK ratio
#define TAS5782M_SAP_BCLK_64     0x04  // 64 * LRCLK (32-bit stereo)

// FAULT (0x68) flags
#define TAS5782M_FAULT_CLOCK_ERROR  0x08
#define TAS5782M_FAULT_CLKE         0x04
#define TAS5782M_FAULT_OC           0x02
#define TAS5782M_FAULT_OT           0x01

// ---------------------------------------------------------------------------
// Volume table location — Book 0x8C, Page 0x18
// (address depends on DSP firmware; adjust if using a different PPC3 config)
// ---------------------------------------------------------------------------

#ifdef TAS5782M_DEBUG
struct tas5782m_dbg_state {
	u32                trace_mask;
	struct dentry     *debugfs_root;
	atomic_t           n_probe;
	atomic_t           n_play_attempted;
	atomic_t           n_play_success;
	atomic_t           n_fault_seen;
	atomic_t           n_bclk_miss;
	atomic_t           n_firmware_load;
	u8                 last_fault_reg;
};
#else
struct tas5782m_dbg_state {
	u8                 unused;
};
#endif

#define TAS5782M_VOL_BOOK       0x8C
#define TAS5782M_VOL_PAGE       0x2A
#define TAS5782M_VOL_CH_LEFT    0x24
#define TAS5782M_VOL_CH_RIGHT   0x28

#define TAS5782M_VOLUME_ENTRIES 86
#define TAS5782M_VOLUME_MIN     0
#define TAS5782M_VOLUME_MAX     (TAS5782M_VOLUME_ENTRIES - 1)

// ---------------------------------------------------------------------------
// Volume table (86 entries, 0.5dB steps) — from FelixKa, Apache-2.0
// ---------------------------------------------------------------------------

extern const uint32_t tas5782m_volume[TAS5782M_VOLUME_ENTRIES];

// ---------------------------------------------------------------------------
// Preboot / init sequence
// ---------------------------------------------------------------------------

extern const uint8_t tas5782m_dsp_init[];
extern const size_t  tas5782m_dsp_init_len;

// ---------------------------------------------------------------------------
// ASoC descriptors (defined in tas5782m.c or tas5782m_codec.c)
// ---------------------------------------------------------------------------

extern const struct regmap_config         tas5782m_regmap_config;
extern const struct snd_soc_component_driver tas5782m_component;
extern struct snd_soc_dai_driver          tas5782m_dai;

#endif /* _TAS5782M_H */
