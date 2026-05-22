/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tas5782m_dbg.h — runtime-switchable per-phase debugging for the TAS5782M
 * ASoC driver.
 *
 * WHY THIS EXISTS
 * ---------------
 * The TAS5782M has a narrow ordering/timing race between the host enabling
 * BCLK on the I2S pins and the driver sending MODE_PLAY.  Without a way to
 * observe exact call-chain timestamps we cannot confirm which happens first.
 * This header provides:
 *
 *   1. Phase masks — each functional phase gets its own bit flag so verbosity
 *      can be toggled independently at runtime.
 *   2. A dev_info-based trace macro that is compiled out completely unless
 *      TAS5782M_DEBUG is defined, adding zero overhead to production builds.
 *   3. A debugfs node that lets you change the active trace mask during a live
 *      session without reloading the module.
 *   4. A register-write wrapper that logs every I2C write in the REG phase,
 *      making it easy to replay/verify the exact byte sequence the driver uses.
 *   5. A `struct tas5782m_dbg_state` that accumulates per-stream counters for
 *      post-mortem analysis.
 *
 * HOW TO BUILD
 * ------------
 * Debug build:   make EXTRA_CFLAGS="-DTAS5782M_DEBUG" -C /lib/modules/...
 * Normal build:  no flag needed; all trace calls compile to nothing.
 *
 * HOW TO USE AT RUNTIME
 * ---------------------
 * After insmod:
 *   mount -t debugfs none /sys/kernel/debug   # if not already mounted
 *   ls /sys/kernel/debug/tas5782m-1-0049/
 *   # Files:  trace_mask   status
 *
 *   # Enable only DAPM + PLAY phase tracing:
 *   echo 0x0C > /sys/kernel/debug/tas5782m-1-0049/trace_mask
 *
 *   # Enable everything:
 *   echo 0xFF > /sys/kernel/debug/tas5782m-1-0049/trace_mask
 *
 *   # Check per-stream counters after a play attempt:
 *   cat /sys/kernel/debug/tas5782m-1-0049/status
 */

#ifndef _TAS5782M_DBG_H
#define _TAS5782M_DBG_H

#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/debugfs.h>
#include <linux/atomic.h>

struct tas5782m_priv;

/* ------------------------------------------------------------------ */
/* Phase bit masks (8 phases, fits in a u8 / u32 trace_mask)          */
/* ------------------------------------------------------------------ */

#define TAS5782M_PHASE_PROBE    BIT(0)  /* module probe / i2c detect  */
#define TAS5782M_PHASE_INIT     BIT(1)  /* chip init / preboot writes */
#define TAS5782M_PHASE_DAPM     BIT(2)  /* DAPM event callbacks       */
#define TAS5782M_PHASE_PLAY     BIT(3)  /* MODE_PLAY / stream start   */
#define TAS5782M_PHASE_MUTE     BIT(4)  /* mute / unmute transitions  */
#define TAS5782M_PHASE_VOL      BIT(5)  /* volume kcontrol updates    */
#define TAS5782M_PHASE_REG      BIT(6)  /* every raw register write   */
#define TAS5782M_PHASE_FAULT    BIT(7)  /* fault register reads       */

/* Friendly names for debugfs display */
#define TAS5782M_PHASE_NAMES \
	{ TAS5782M_PHASE_PROBE, "PROBE"  }, \
	{ TAS5782M_PHASE_INIT,  "INIT"   }, \
	{ TAS5782M_PHASE_DAPM,  "DAPM"   }, \
	{ TAS5782M_PHASE_PLAY,  "PLAY"   }, \
	{ TAS5782M_PHASE_MUTE,  "MUTE"   }, \
	{ TAS5782M_PHASE_VOL,   "VOL"    }, \
	{ TAS5782M_PHASE_REG,   "REG"    }, \
	{ TAS5782M_PHASE_FAULT, "FAULT"  }

/* ------------------------------------------------------------------ */
/* Core trace macro                                                     */
/* ------------------------------------------------------------------ */

#ifdef TAS5782M_DEBUG

/**
 * tas5782m_trace - emit a dev_info trace if phase is in the mask.
 * @_priv:  pointer to struct tas5782m_priv
 * @_phase: one of TAS5782M_PHASE_* constants
 * @_fmt:   printf format string
 *
 * Expands to dev_info() with a "[tas5782m PHASE]" prefix so trace lines are
 * grep-able even when mixed with other kernel log output.
 *
 * Note: _priv->dbg.trace_mask is read without a lock.  It is a u32 written
 * only from debugfs (user context) so a torn read can occur but the worst
 * outcome is a missed or extra trace line — acceptable.
 */
#define tas5782m_trace(_priv, _phase, _fmt, ...) do {                  \
	if ((_priv)->dbg.trace_mask & (_phase))                        \
		dev_info(&(_priv)->client->dev,                        \
			 "[tas5782m %s] " _fmt,                        \
			 tas5782m_phase_name(_phase),                  \
			 ##__VA_ARGS__);                               \
} while (0)

/**
 * tas5782m_phase_name - return human-readable phase label.
 * @phase: a single TAS5782M_PHASE_* bit value.
 */
static inline const char *tas5782m_phase_name(u32 phase)
{
	switch (phase) {
	case TAS5782M_PHASE_PROBE:  return "PROBE";
	case TAS5782M_PHASE_INIT:   return "INIT";
	case TAS5782M_PHASE_DAPM:   return "DAPM";
	case TAS5782M_PHASE_PLAY:   return "PLAY";
	case TAS5782M_PHASE_MUTE:   return "MUTE";
	case TAS5782M_PHASE_VOL:    return "VOL";
	case TAS5782M_PHASE_REG:    return "REG";
	case TAS5782M_PHASE_FAULT:  return "FAULT";
	default:                    return "????";
	}
}

/* ------------------------------------------------------------------ */
/* Traced register write                                               */
/* ------------------------------------------------------------------ */

/**
 * tas5782m_write_traced - write one register and log it if REG is enabled.
 * @priv:   pointer to struct tas5782m_priv
 * @reg:    register address (within the currently-selected Book/Page)
 * @val:    byte value
 *
 * Returns the return value of regmap_write() so callers can propagate errors.
 *
 * WHY A WRAPPER: All TAS5782M register writes must go through set_book_page()
 * first (the chip uses a multiplexed address space).  Adding the trace at the
 * write layer lets us capture the final <reg, val> pair without scattering
 * pr_debug calls everywhere.
 */
static inline int tas5782m_write_traced(struct tas5782m_priv *priv,
					unsigned int reg, unsigned int val)
{
	int ret = regmap_write(priv->regmap, reg, val);

	tas5782m_trace(priv, TAS5782M_PHASE_REG,
		       "regmap_write(0x%02x, 0x%02x) => %d\n", reg, val, ret);
	return ret;
}

/* ------------------------------------------------------------------ */
/* debugfs helpers                                                     */
/* ------------------------------------------------------------------ */

/**
 * tas5782m_dbg_init - create debugfs entries for this instance.
 * @priv: pointer to struct tas5782m_priv
 *
 * Creates /sys/kernel/debug/tas5782m-<busid>/ with two files:
 *   trace_mask — hex u32 rw; controls which phases emit log lines
 *   status     — ro text dump of all counters
 *
 * If debugfs is not mounted or the call fails, the driver continues normally;
 * tracing still works via the initial trace_mask value set in probe().
 */
int tas5782m_dbg_init(struct tas5782m_priv *priv);

/**
 * tas5782m_dbg_remove - remove debugfs entries.
 * @priv: pointer to struct tas5782m_priv
 *
 * Safe to call even if tas5782m_dbg_init() failed or was never called.
 */
void tas5782m_dbg_remove(struct tas5782m_priv *priv);

#else  /* !TAS5782M_DEBUG */

/* ------------------------------------------------------------------ */
/* No-op stubs for production build                                   */
/* ------------------------------------------------------------------ */

#define tas5782m_trace(_priv, _phase, _fmt, ...)  do {} while (0)

/* In the non-debug build tas5782m_write_traced is a macro so it does not
 * need a complete struct tas5782m_priv definition — it just calls regmap_write
 * directly using the regmap field from whatever priv pointer is passed. */
#define tas5782m_write_traced(_priv, _reg, _val) \
        regmap_write((_priv)->regmap, (_reg), (_val))

static inline int  tas5782m_dbg_init(void *priv)   { (void)priv; return 0; }
static inline void tas5782m_dbg_remove(void *priv) { (void)priv; }

#endif /* TAS5782M_DEBUG */

#endif /* _TAS5782M_DBG_H */
