// SPDX-License-Identifier: GPL-2.0-only
/*
 * snd-soc-tas5782m.c — ASoC CODEC driver for Texas Instruments TAS5782M
 *
 * Target hardware: Lenovo CD-18781Y (ThinkSmart View), APQ8053/MSM8953,
 * TAS5782M at I2C bus 1 address 0x49 (DT: "1-0049").
 *
 * Based on:
 *   - Earlier TI smart-amp ASoC driver patterns by Andy Liu <andy-liu@ti.com>,
 *     Daniel Beer <daniel.beer@igorinstitute.com>,
 *     Felix Kaechele <felix@kaechele.ca>
 *   - TAS5782M reference driver posted by TI on E2E forum
 *   - Lenovo CD-18781Y OSS package (Android kernel DTS)
 *
 * Critical ordering note (root cause of prior -EIO regression)
 * ============================================================
 * The TAS5782M's I2C interface does not respond reliably until BCLK is
 * present on the SCLK pin. Writes issued before BCLK starts are NACK'd
 * (regmap_write returns -EIO). FelixKa's working driver documents this
 * and gates all chip programming behind a 5–10 ms delay scheduled from
 * the codec DAI .trigger(START) callback — by which time the CPU DAI has
 * already called q6afe_port_start() and BCLK is on GPIO 135.
 *
 * An earlier version of this driver used SND_SOC_DAPM_POST_PMU to do the
 * preboot/firmware/volume sequence. In Qualcomm Q6 ASoC, the codec
 * widget's POST_PMU fires during the DAPM bias-level transition that
 * precedes CPU-side trigger(START), so BCLK is not yet running and the
 * first I2C write fails deterministically. That placement was wrong.
 *
 * This driver therefore:
 *   1. Holds the chip in HI-Z after probe (no I2C beyond regulator+PDN).
 *   2. On .trigger(START), schedules priv->work.
 *   3. The work waits 5–10 ms for BCLK to stabilize, sends the preboot
 *      sequence, waits 5–15 ms for the DSP to boot, loads firmware, then
 *      pushes initial volume/unmute.
 *   4. On DAPM PRE_PMD, cancels any pending work and puts the chip into
 *      standby (HIZ, reg 0x02=0x10), sets is_powered=false.
 *   5. mute_stream and volume kcontrol updates call refresh() directly
 *      under priv->lock — they run in process context.
 *
 * Other design points
 * -------------------
 *  - PDN polarity: DT declares GPIO_ACTIVE_LOW (datasheet semantics);
 *    gpiod_set_value(pdn, 1) yields a physical LOW = amplifier active.
 *  - REGCACHE_NONE: Book/Page multiplex makes register caching incorrect.
 *  - Dual build: -DTAS5782M_DEBUG enables dev_info trace + debugfs counters.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/initval.h>

#include "tas5782m_priv.h"

/* Include debug layer AFTER priv struct so non-debug stubs see the full type */
#include "tas5782m_dbg.h"

/* ========================================================================= */
/* Book / Page multiplexer helper                                             */
/* ========================================================================= */

static int tas5782m_set_book_page(struct tas5782m_priv *priv,
				  u8 book, u8 page)
{
	int ret;

	ret = tas5782m_write_traced(priv, TAS5782M_REG_PAGE, 0x00);
	if (ret)
		return ret;

	ret = tas5782m_write_traced(priv, TAS5782M_REG_BOOK, book);
	if (ret)
		return ret;

	ret = tas5782m_write_traced(priv, TAS5782M_REG_PAGE, page);
	if (ret)
		return ret;

	tas5782m_trace(priv, TAS5782M_PHASE_REG,
		       "set book=0x%02x page=0x%02x\n", book, page);
	return 0;
}

/* ========================================================================= */
/* Volume                                                                     */
/* ========================================================================= */

/*
 * Volume lives in DSP coefficient memory at Book 0x8C, Page 0x2A,
 * registers 0x24 (left) and 0x28 (right). Each value is a 32-bit
 * fixed-point coefficient written big-endian.
 *
 * Caller must hold priv->lock and the chip must be powered (out of
 * HI-Z) — the DSP ignores coefficient writes when not in PLAY mode.
 * Returns to Book 0/Page 0 on exit so subsequent main-control writes
 * land in the right place.
 */
static void tas5782m_set_volume(struct tas5782m_priv *priv)
{
	struct regmap *rm = priv->regmap;
	u8  v[4];
	u32 x;
	int i;

	tas5782m_trace(priv, TAS5782M_PHASE_VOL,
		       "set volume L=%d R=%d\n", priv->vol[0], priv->vol[1]);

	if (tas5782m_set_book_page(priv, TAS5782M_VOL_BOOK, TAS5782M_VOL_PAGE))
		return;

	x = tas5782m_volume[priv->vol[0]];
	for (i = 0; i < 4; i++) { v[3 - i] = (u8)(x & 0xFF); x >>= 8; }
	regmap_bulk_write(rm, TAS5782M_VOL_CH_LEFT, v, 4);

	x = tas5782m_volume[priv->vol[1]];
	for (i = 0; i < 4; i++) { v[3 - i] = (u8)(x & 0xFF); x >>= 8; }
	regmap_bulk_write(rm, TAS5782M_VOL_CH_RIGHT, v, 4);

	tas5782m_set_book_page(priv, 0x00, 0x00);
}

/* ========================================================================= */
/* Refresh — apply current vol/mute state to the chip                        */
/* ========================================================================= */

/*
 * Caller must hold priv->lock and priv->is_powered must be true.
 * Volume is pushed first so an unmute does not pop to a stale level.
 */
static void tas5782m_refresh(struct tas5782m_priv *priv)
{
	tas5782m_set_volume(priv);
	tas5782m_write_traced(priv, TAS5782M_CTRL3,
			      priv->is_muted ? TAS5782M_MUTE
					     : TAS5782M_NORMAL_VOLUME);
}

/* ========================================================================= */
/* Firmware handling                                                          */
/* ========================================================================= */

static int tas5782m_cache_firmware(struct tas5782m_priv *priv)
{
	const struct firmware *fw;
	char fw_name[128];
	int ret;

	snprintf(fw_name, sizeof(fw_name),
		 "tas5728m_dsp_%s.bin", priv->dsp_config_name);

	ret = request_firmware(&fw, fw_name, &priv->client->dev);
	if (ret) {
		dev_err(&priv->client->dev,
			"firmware '%s' not found (%d)\n", fw_name, ret);
		return ret;
	}

	if (fw->size < 2 || (fw->size & 1)) {
		dev_err(&priv->client->dev, "firmware '%s' is invalid\n", fw_name);
		release_firmware(fw);
		return -EINVAL;
	}

	priv->dsp_cfg_data = devm_kmemdup(&priv->client->dev, fw->data,
					  fw->size, GFP_KERNEL);
	if (!priv->dsp_cfg_data) {
		release_firmware(fw);
		return -ENOMEM;
	}
	priv->dsp_cfg_len = fw->size;

	dev_info(&priv->client->dev,
		 "cached DSP firmware '%s' (%zu bytes)\n",
		 fw_name, fw->size);

	release_firmware(fw);
	return 0;
}

/* ========================================================================= */
/* Stream-start work — preboot + firmware + initial refresh                  */
/* ========================================================================= */

static int tas5782m_send_cfg(struct tas5782m_priv *priv,
			     const u8 *data, size_t len)
{
	size_t i;
	int ret;
	int first_err = 0;

	for (i = 0; i + 1 < len; i += 2) {
		ret = regmap_write(priv->regmap, data[i], data[i + 1]);
		if (ret)
			if (!first_err)
				first_err = ret;
	}

	if (first_err) {
		tas5782m_trace(priv, TAS5782M_PHASE_INIT,
			       "send_cfg completed with write errors (first=%d)\n",
			       first_err);
	}

	return 0;
}

/*
 * tas5782m_do_work() — runs all chip programming that requires a live BCLK.
 *
 * Scheduled from .trigger(START) on the codec DAI, which fires after the
 * CPU DAI's trigger has called q6afe_port_start() and brought BCLK up on
 * the I2S pins. We still wait an extra 5–10 ms because BCLK takes a few
 * frames to settle and the chip latches a clock-error flag if the first
 * I2C write straddles that boundary.
 */
static void tas5782m_do_work(struct work_struct *work)
{
	struct tas5782m_priv *priv =
		container_of(work, struct tas5782m_priv, work);
	int ret;

	mutex_lock(&priv->lock);

	/* If chip is already in PLAY mode (e.g. DAPM pmdown_time kept it live),
	 * skip the full 545ms preboot+firmware sequence and just refresh state. */
	if (priv->is_powered) {
		priv->is_muted = false;
		tas5782m_refresh(priv);
		tas5782m_trace(priv, TAS5782M_PHASE_PLAY,
			       "stream-start: chip already powered, refresh only\n");
		mutex_unlock(&priv->lock);
		return;
	}

	tas5782m_trace(priv, TAS5782M_PHASE_INIT,
		       "stream-start: waiting 5ms for BCLK to stabilize\n");
	usleep_range(5000, 10000);

#ifdef TAS5782M_DEBUG
	atomic_inc(&priv->dbg.n_play_attempted);
#endif

	ret = tas5782m_send_cfg(priv, tas5782m_dsp_init, tas5782m_dsp_init_len);
	if (ret)
		dev_err(&priv->client->dev,
			"stream-start: preboot send_cfg reported: %d\n", ret);

	/* DSP needs ~5 ms to boot internally before accepting firmware blob */
	usleep_range(5000, 15000);

	tas5782m_send_cfg(priv, priv->dsp_cfg_data, priv->dsp_cfg_len);

#ifdef TAS5782M_DEBUG
	atomic_inc(&priv->dbg.n_firmware_load);
#endif

	/* The firmware's first write (reg 0x02=0x80) triggers an internal
	 * register reload that restores page-0 defaults, including reg 0x59=0x11
	 * (auto-mute enabled for both channels). Disable it here, after firmware
	 * completes, so silence zeros from WP keep-alive don't trigger auto-mute
	 * before real audio arrives. Firmware ends in book 0/page 0 so no
	 * book/page switch is needed. */
	tas5782m_write_traced(priv, TAS5782M_AUTOMUTE_CTRL, 0x00);

	/* Always unmute on stream start — mute_stream handles re-muting.
	 * The preboot sequence set CTRL3=MUTE; this ungate runs after firmware
	 * init. In the PipeWire/DPCM path, mute_stream(0) may not be called. */
	priv->is_muted = false;
	priv->is_powered = true;
	tas5782m_refresh(priv);

#ifdef TAS5782M_DEBUG
	atomic_inc(&priv->dbg.n_play_success);
#endif

	tas5782m_trace(priv, TAS5782M_PHASE_PLAY,
		       "stream-start: chip in PLAY mode, refresh applied\n");

	mutex_unlock(&priv->lock);
}

/* ========================================================================= */
/* DAI callbacks                                                              */
/* ========================================================================= */

static int tas5782m_trigger(struct snd_pcm_substream *substream, int cmd,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *comp = dai->component;
	struct tas5782m_priv *priv = snd_soc_component_get_drvdata(comp);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		tas5782m_trace(priv, TAS5782M_PHASE_PLAY,
			       "trigger(START) — scheduling init work\n");
		schedule_work(&priv->work);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		/* HI-Z teardown is done in DAPM PRE_PMD */
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static int tas5782m_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
	struct snd_soc_component *comp = dai->component;
	struct tas5782m_priv *priv = snd_soc_component_get_drvdata(comp);

	if (stream != SNDRV_PCM_STREAM_PLAYBACK)
		return 0;

	tas5782m_trace(priv, TAS5782M_PHASE_MUTE,
		       "mute_stream mute=%d\n", mute);

	mutex_lock(&priv->lock);
	priv->is_muted = mute;
	if (priv->is_powered)
		tas5782m_refresh(priv);
	mutex_unlock(&priv->lock);

	return 0;
}

/* ========================================================================= */
/* DAPM event — PRE_PMD only, for HI-Z teardown                              */
/* ========================================================================= */

static int tas5782m_dac_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *k, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct tas5782m_priv *priv = snd_soc_component_get_drvdata(comp);

	if (event & SND_SOC_DAPM_PRE_PMD) {
		cancel_work_sync(&priv->work);

		mutex_lock(&priv->lock);
		if (priv->is_powered) {
			priv->is_powered = false;
			tas5782m_write_traced(priv, TAS5782M_REG_PAGE, 0x00);
			tas5782m_write_traced(priv, TAS5782M_REG_BOOK, 0x00);
			tas5782m_write_traced(priv, TAS5782M_CTRL2,
					      TAS5782M_MODE_HIZ);
			tas5782m_trace(priv, TAS5782M_PHASE_DAPM,
				       "PRE_PMD: chip to standby (HIZ)\n");
		} else {
			tas5782m_trace(priv, TAS5782M_PHASE_DAPM,
				       "PRE_PMD: chip was not powered, no action\n");
		}
		mutex_unlock(&priv->lock);
	}

	return 0;
}

/* ========================================================================= */
/* DAPM topology                                                              */
/* ========================================================================= */

static const struct snd_soc_dapm_widget tas5782m_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("DAC IN", "Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC_E("DAC", NULL, SND_SOC_NOPM, 0, 0,
			   tas5782m_dac_event, SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUTPUT("OUT"),
};

static const struct snd_soc_dapm_route tas5782m_dapm_routes[] = {
	{ "DAC", NULL, "DAC IN" },
	{ "OUT", NULL, "DAC"    },
};

/* ========================================================================= */
/* Volume kcontrol                                                            */
/* ========================================================================= */

static int tas5782m_vol_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = TAS5782M_VOLUME_MIN;
	uinfo->value.integer.max = TAS5782M_VOLUME_MAX;
	return 0;
}

static int tas5782m_vol_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *comp = snd_kcontrol_chip(kcontrol);
	struct tas5782m_priv *priv = snd_soc_component_get_drvdata(comp);

	mutex_lock(&priv->lock);
	ucontrol->value.integer.value[0] = priv->vol[0];
	ucontrol->value.integer.value[1] = priv->vol[1];
	mutex_unlock(&priv->lock);
	return 0;
}

static int tas5782m_vol_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *comp = snd_kcontrol_chip(kcontrol);
	struct tas5782m_priv *priv = snd_soc_component_get_drvdata(comp);
	int v0 = ucontrol->value.integer.value[0];
	int v1 = ucontrol->value.integer.value[1];
	int changed = 0;

	if (v0 < TAS5782M_VOLUME_MIN || v0 > TAS5782M_VOLUME_MAX ||
	    v1 < TAS5782M_VOLUME_MIN || v1 > TAS5782M_VOLUME_MAX)
		return -EINVAL;

	mutex_lock(&priv->lock);
	if (priv->vol[0] != v0 || priv->vol[1] != v1) {
		priv->vol[0] = v0;
		priv->vol[1] = v1;
		changed = 1;
		if (priv->is_powered)
			tas5782m_refresh(priv);

		tas5782m_trace(priv, TAS5782M_PHASE_VOL,
			       "vol_put L=%d R=%d\n", v0, v1);
	}
	mutex_unlock(&priv->lock);
	return changed;
}

static const struct snd_kcontrol_new tas5782m_controls[] = {
	{
		.iface  = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name   = "Master Playback Volume",
		.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |
			  SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info   = tas5782m_vol_info,
		.get    = tas5782m_vol_get,
		.put    = tas5782m_vol_put,
	},
};

static const struct snd_soc_dai_ops tas5782m_dai_ops = {
	.trigger         = tas5782m_trigger,
	.mute_stream     = tas5782m_mute_stream,
	.no_capture_mute = 1,
};

struct snd_soc_dai_driver tas5782m_dai = {
	/* Name kept as "tas5805m-amplifier" for ABI compatibility with the
	 * existing machine-driver/DT setup that was wired for the legacy
	 * tas5805m driver fork. The chip is a TAS5782M; only the DAI string
	 * is preserved. */
	.name           = "tas5805m-amplifier",
	.playback = {
		.stream_name  = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates        = SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000,
		.formats      = SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops = &tas5782m_dai_ops,
};

const struct snd_soc_component_driver tas5782m_component = {
	.controls         = tas5782m_controls,
	.num_controls     = ARRAY_SIZE(tas5782m_controls),
	.dapm_widgets     = tas5782m_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tas5782m_dapm_widgets),
	.dapm_routes      = tas5782m_dapm_routes,
	.num_dapm_routes  = ARRAY_SIZE(tas5782m_dapm_routes),
	.use_pmdown_time  = 1,
	.endianness       = 1,
};

/* ========================================================================= */
/* I2C probe / remove                                                         */
/* ========================================================================= */

static int tas5782m_i2c_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct tas5782m_priv *priv;
	const char *config_name;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = i2c;
	mutex_init(&priv->lock);
	INIT_WORK(&priv->work, tas5782m_do_work);
	priv->vol[0] = TAS5782M_VOLUME_MIN;
	priv->vol[1] = TAS5782M_VOLUME_MIN;
	priv->is_muted = true;
	priv->is_powered = false;

#ifdef TAS5782M_DEBUG
	priv->dbg.trace_mask = TAS5782M_PHASE_PROBE | TAS5782M_PHASE_INIT |
			       TAS5782M_PHASE_DAPM  | TAS5782M_PHASE_PLAY |
			       TAS5782M_PHASE_MUTE;
	atomic_set(&priv->dbg.n_probe, 1);
#endif

	tas5782m_trace(priv, TAS5782M_PHASE_PROBE,
		       "probe start (i2c 0x%02x)\n", i2c->addr);

	ret = of_property_read_string(dev->of_node,
				      "ti,dsp-config-name", &config_name);
	if (ret) {
		dev_info(dev, "no ti,dsp-config-name; using 'default'\n");
		config_name = "default";
	}
	strscpy(priv->dsp_config_name, config_name,
		sizeof(priv->dsp_config_name));

	priv->pvdd = devm_regulator_get(dev, "pvdd");
	if (IS_ERR(priv->pvdd))
		return dev_err_probe(dev, PTR_ERR(priv->pvdd),
				     "failed to get pvdd supply\n");

	priv->gpio_pdn = devm_gpiod_get_optional(dev, "pdn", GPIOD_OUT_LOW);
	if (IS_ERR(priv->gpio_pdn))
		return dev_err_probe(dev, PTR_ERR(priv->gpio_pdn),
				     "failed to get pdn gpio\n");

	priv->regmap = devm_regmap_init_i2c(i2c, &tas5782m_regmap_config);
	if (IS_ERR(priv->regmap))
		return dev_err_probe(dev, PTR_ERR(priv->regmap),
				     "failed to create regmap\n");

	i2c_set_clientdata(i2c, priv);

	ret = tas5782m_cache_firmware(priv);
	if (ret)
		return ret;

	ret = regulator_enable(priv->pvdd);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable pvdd\n");

	/*
	 * FelixKa note: hold the chip in PDN for ~100 ms after PVDD comes up
	 * so the ADR pin samples cleanly. Otherwise the I2C address can come
	 * up unpredictable.
	 */
	usleep_range(100000, 150000);

	if (priv->gpio_pdn)
		gpiod_set_value_cansleep(priv->gpio_pdn, 1);

	/* Allow internal regulator/PLL domains to settle after PDN release. */
	usleep_range(10000, 15000);

	tas5782m_trace(priv, TAS5782M_PHASE_INIT,
		       "probe: firmware cached, deferring chip init until trigger(START)\n");

	tas5782m_dbg_init(priv);

	ret = devm_snd_soc_register_component(dev, &tas5782m_component,
					      &tas5782m_dai, 1);
	if (ret) {
		dev_err(dev, "failed to register ASoC component: %d\n", ret);
		tas5782m_dbg_remove(priv);
		goto err_disable_pvdd;
	}

	dev_info(dev, "TAS5782M ready (i2c=0x%02x config='%s')\n",
		 i2c->addr, priv->dsp_config_name);
	return 0;

err_disable_pvdd:
	if (priv->gpio_pdn)
		gpiod_set_value_cansleep(priv->gpio_pdn, 0);
	regulator_disable(priv->pvdd);
	return ret;
}

static void tas5782m_i2c_remove(struct i2c_client *i2c)
{
	struct tas5782m_priv *priv = i2c_get_clientdata(i2c);

	cancel_work_sync(&priv->work);

	if (priv->gpio_pdn)
		gpiod_set_value_cansleep(priv->gpio_pdn, 0);

	tas5782m_dbg_remove(priv);

	regulator_disable(priv->pvdd);
	dev_info(&i2c->dev, "TAS5782M removed\n");
}

/* ========================================================================= */
/* Module scaffolding                                                         */
/* ========================================================================= */

static const struct of_device_id tas5782m_of_ids[] = {
	{ .compatible = "ti,tas5782m" },
	{ }
};
MODULE_DEVICE_TABLE(of, tas5782m_of_ids);

static const struct i2c_device_id tas5782m_id[] = {
	{ "tas5782m", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tas5782m_id);

static struct i2c_driver tas5782m_driver = {
	.driver = {
		.name           = "tas5782m",
		.of_match_table = tas5782m_of_ids,
	},
	.probe    = tas5782m_i2c_probe,
	.remove   = tas5782m_i2c_remove,
	.id_table = tas5782m_id,
};

/*
 * Only register the module entry points in the production build.
 * The debug build (tas5782m_dbgbuild.c, compiled with -DTAS5782M_DEBUG)
 * #includes this file and must define its own module_init/module_exit so
 * that the KCFI type hash for init_module matches what the running kernel
 * expects.  If module_i2c_driver() were expanded here under
 * -DTAS5782M_DEBUG, the altered compilation environment changes the KCFI
 * annotation and causes a CFI failure at do_one_initcall.
 */
#ifndef TAS5782M_DEBUG
module_i2c_driver(tas5782m_driver);

MODULE_AUTHOR("Based on FelixKa / Andy Liu / Daniel Beer");
MODULE_DESCRIPTION("TAS5782M Smart Amplifier ASoC CODEC Driver");
MODULE_LICENSE("GPL v2");
#endif /* !TAS5782M_DEBUG */
