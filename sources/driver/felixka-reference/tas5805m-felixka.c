// SPDX-License-Identifier: GPL-2.0
//
// Driver for the TAS5805M Audio Amplifier
//
// Author: Andy Liu <andy-liu@ti.com>
// Author: Daniel Beer <daniel.beer@igorinstitute.com>
// Author: Felix Kaechele <felix@kaechele.ca> - TAS5872M support
//
// This is based on a driver originally written by Andy Liu at TI and
// posted here:
//
//    https://e2e.ti.com/support/audio-group/audio/f/audio-forum/722027/linux-tas5825m-linux-drivers
//
// It has been simplified a little and reworked for the 5.x ALSA SoC API.

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>

#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/initval.h>

#include "tas5805m.h"

#undef dev_dbg
#define dev_dbg dev_err

enum tas_model {
	TAS5782M,
	TAS5805M,
};

struct tas_of_data {
	enum	tas_model	model;
};

static const struct tas_of_data tas5805m_of_data = {
	.model = TAS5805M,
};

static const struct tas_of_data tas5782m_of_data = {
	.model = TAS5782M,
};

struct tas5805m_priv {
	enum tas_model			model;
	struct i2c_client		*i2c;
	struct regulator		*pvdd;
	struct gpio_desc		*gpio_pdn_n;

	uint8_t				*dsp_init;
	int				dsp_init_len;
	uint8_t				*dsp_cfg_data;
	int				dsp_cfg_len;

	struct regmap			*regmap;

	int				vol[2];
	bool				is_powered;
	bool				is_muted;

	struct work_struct		work;
	struct mutex			lock;
};

static void set_dsp_scale(struct regmap *rm, int offset, int vol)
{
	uint8_t v[4];
	uint32_t x = tas5805m_volume[vol];
	int i;

	for (i = 0; i < 4; i++) {
		v[3 - i] = x;
		x >>= 8;
	}

	regmap_bulk_write(rm, offset, v, ARRAY_SIZE(v));
}

static void tas5805m_refresh(struct tas5805m_priv *tas5805m)
{
	struct regmap *rm = tas5805m->regmap;

	dev_dbg(&tas5805m->i2c->dev, "refresh: is_muted=%d, vol=%d/%d\n",
		tas5805m->is_muted, tas5805m->vol[0], tas5805m->vol[1]);

	regmap_write(rm, REG_PAGE, 0x00);
	regmap_write(rm, REG_BOOK, 0x8c);
	regmap_write(rm, REG_PAGE, 0x2a);

	/* Refresh volume. The actual volume control documented in the
	 * datasheet doesn't seem to work correctly. This is a pair of
	 * DSP registers which are *not* documented in the datasheet.
	 */
	set_dsp_scale(rm, 0x24, tas5805m->vol[0]);
	set_dsp_scale(rm, 0x28, tas5805m->vol[1]);

	regmap_write(rm, REG_PAGE, 0x00);
	regmap_write(rm, REG_BOOK, 0x00);

	/* Set/clear digital soft-mute */
	switch(tas5805m->model) {
	case TAS5782M:
		regmap_write(rm, TAS5782M_REG_3,
			tas5805m->is_muted ?
			TAS5782M_REG_3_MUTE :
			TAS5782M_REG_3_NORMAL_VOLUME);
		break;
	case TAS5805M:
		regmap_write(rm, REG_DEVICE_CTRL_2,
			(tas5805m->is_muted ? DCTRL2_MUTE : 0) |
			DCTRL2_MODE_PLAY);
	}
}

static int tas5805m_vol_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;

	uinfo->value.integer.min = TAS5805M_VOLUME_MIN;
	uinfo->value.integer.max = TAS5805M_VOLUME_MAX;
	return 0;
}

static int tas5805m_vol_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct tas5805m_priv *tas5805m =
		snd_soc_component_get_drvdata(component);

	mutex_lock(&tas5805m->lock);
	ucontrol->value.integer.value[0] = tas5805m->vol[0];
	ucontrol->value.integer.value[1] = tas5805m->vol[1];
	mutex_unlock(&tas5805m->lock);

	return 0;
}

static inline int volume_is_valid(int v)
{
	return (v >= TAS5805M_VOLUME_MIN) && (v <= TAS5805M_VOLUME_MAX);
}

static int tas5805m_vol_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct tas5805m_priv *tas5805m =
		snd_soc_component_get_drvdata(component);
	int ret = 0;

	if (!(volume_is_valid(ucontrol->value.integer.value[0]) &&
	      volume_is_valid(ucontrol->value.integer.value[1])))
		return -EINVAL;

	mutex_lock(&tas5805m->lock);
	if (tas5805m->vol[0] != ucontrol->value.integer.value[0] ||
	    tas5805m->vol[1] != ucontrol->value.integer.value[1]) {
		tas5805m->vol[0] = ucontrol->value.integer.value[0];
		tas5805m->vol[1] = ucontrol->value.integer.value[1];
		dev_dbg(component->dev, "set vol=%d/%d (is_powered=%d)\n",
			tas5805m->vol[0], tas5805m->vol[1],
			tas5805m->is_powered);
		if (tas5805m->is_powered)
			tas5805m_refresh(tas5805m);
		ret = 1;
	}
	mutex_unlock(&tas5805m->lock);

	return ret;
}

static const struct snd_kcontrol_new tas5805m_snd_controls[] = {
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name	= "Master Playback Volume",
		.access	= SNDRV_CTL_ELEM_ACCESS_TLV_READ |
			  SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info	= tas5805m_vol_info,
		.get	= tas5805m_vol_get,
		.put	= tas5805m_vol_put,
	},
};

static void send_cfg(struct regmap *rm,
		     const uint8_t *s, unsigned int len)
{
	unsigned int i;

	for (i = 0; i + 1 < len; i += 2)
		regmap_write(rm, s[i], s[i + 1]);
}

/* The TAS5805M DSP can't be configured until the I2S clock has been
 * present and stable for 5ms, or else it won't boot and we get no
 * sound.
 */
static int tas5805m_trigger(struct snd_pcm_substream *substream, int cmd,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct tas5805m_priv *tas5805m =
		snd_soc_component_get_drvdata(component);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		dev_dbg(component->dev, "clock start\n");
		schedule_work(&tas5805m->work);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static void do_work(struct work_struct *work)
{
	struct tas5805m_priv *tas5805m =
	       container_of(work, struct tas5805m_priv, work);
	struct regmap *rm = tas5805m->regmap;

	dev_dbg(&tas5805m->i2c->dev, "DSP startup\n");

	mutex_lock(&tas5805m->lock);
	/* We mustn't issue any I2C transactions until the I2S
	 * clock is stable. Furthermore, we must allow a 5ms
	 * delay after the first set of register writes to
	 * allow the DSP to boot before configuring it.
	 */
	usleep_range(5000, 10000);
	send_cfg(rm, tas5805m->dsp_init, tas5805m->dsp_init_len);
	usleep_range(5000, 15000);
	send_cfg(rm, tas5805m->dsp_cfg_data, tas5805m->dsp_cfg_len);

	tas5805m->is_powered = true;
	tas5805m_refresh(tas5805m);
	mutex_unlock(&tas5805m->lock);
}

static int tas5805m_dac_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct tas5805m_priv *tas5805m =
		snd_soc_component_get_drvdata(component);
	struct regmap *rm = tas5805m->regmap;

	if (event & SND_SOC_DAPM_PRE_PMD) {
		unsigned int chan, global1, global2;

		dev_dbg(component->dev, "DSP shutdown\n");
		cancel_work_sync(&tas5805m->work);

		mutex_lock(&tas5805m->lock);
		if (tas5805m->is_powered) {
			tas5805m->is_powered = false;

			regmap_write(rm, REG_PAGE, 0x00);
			regmap_write(rm, REG_BOOK, 0x00);

			switch(tas5805m->model){
			case TAS5782M:
				/* TAS5782M datasheet does not indicate there are fault registers */
				regmap_write(rm, TAS5782M_REG_2, TAS5782M_REG_2_MODE_STANDBY);
				break;
			default:
				regmap_read(rm, REG_CHAN_FAULT, &chan);
				regmap_read(rm, REG_GLOBAL_FAULT1, &global1);
				regmap_read(rm, REG_GLOBAL_FAULT2, &global2);

				dev_dbg(component->dev, "fault regs: CHAN=%02x, "
					"GLOBAL1=%02x, GLOBAL2=%02x\n",
					chan, global1, global2);

				regmap_write(rm, REG_DEVICE_CTRL_2, DCTRL2_MODE_HIZ);
			}
		}
		mutex_unlock(&tas5805m->lock);
	}

	return 0;
}

static const struct snd_soc_dapm_route tas5805m_audio_map[] = {
	{ "DAC", NULL, "DAC IN" },
	{ "OUT", NULL, "DAC" },
};

static const struct snd_soc_dapm_widget tas5805m_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("DAC IN", "Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC_E("DAC", NULL, SND_SOC_NOPM, 0, 0,
		tas5805m_dac_event, SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUTPUT("OUT")
};

static const struct snd_soc_component_driver soc_codec_dev_tas5805m = {
	.controls		= tas5805m_snd_controls,
	.num_controls		= ARRAY_SIZE(tas5805m_snd_controls),
	.dapm_widgets		= tas5805m_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tas5805m_dapm_widgets),
	.dapm_routes		= tas5805m_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(tas5805m_audio_map),
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static int tas5805m_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	struct tas5805m_priv *tas5805m =
		snd_soc_component_get_drvdata(component);

	mutex_lock(&tas5805m->lock);
	dev_dbg(component->dev, "set mute=%d (is_powered=%d)\n",
		mute, tas5805m->is_powered);

	tas5805m->is_muted = mute;
	if (tas5805m->is_powered)
		tas5805m_refresh(tas5805m);
	mutex_unlock(&tas5805m->lock);

	return 0;
}

static const struct snd_soc_dai_ops tas5805m_dai_ops = {
	.trigger		= tas5805m_trigger,
	.mute_stream		= tas5805m_mute,
	.no_capture_mute	= 1,
};

static struct snd_soc_dai_driver tas5805m_dai = {
	.name		= "tas5805m-amplifier",
	.playback	= {
		.stream_name	= "Playback",
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= SNDRV_PCM_RATE_48000,
		.formats	= SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops		= &tas5805m_dai_ops,
};

static const struct regmap_config tas5805m_regmap = {
	.reg_bits	= 8,
	.val_bits	= 8,

	/* We have quite a lot of multi-level bank switching and a
	 * relatively small number of register writes between bank
	 * switches.
	 */
	.cache_type	= REGCACHE_NONE,
};

static int tas5805m_i2c_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct regmap *regmap;
	struct tas5805m_priv *tas5805m;
	const struct tas_of_data *data;
	char filename[128];
	const char *config_name;
	const struct firmware *fw;
	int ret;

	dev_err(dev, "probing TAS");
	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	regmap = devm_regmap_init_i2c(i2c, &tas5805m_regmap);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(dev, "unable to allocate register map: %d\n", ret);
		return ret;
	}

	tas5805m = devm_kzalloc(dev, sizeof(struct tas5805m_priv), GFP_KERNEL);
	if (!tas5805m)
		return -ENOMEM;

	tas5805m->i2c = i2c;
	tas5805m->pvdd = devm_regulator_get(dev, "pvdd");
	if (IS_ERR(tas5805m->pvdd)) {
		dev_err(dev, "failed to get pvdd supply: %ld\n",
			PTR_ERR(tas5805m->pvdd));
		return PTR_ERR(tas5805m->pvdd);
	}

	dev_set_drvdata(dev, tas5805m);
	tas5805m->regmap = regmap;
	tas5805m->gpio_pdn_n = devm_gpiod_get(dev, "pdn", GPIOD_OUT_LOW);
	if (IS_ERR(tas5805m->gpio_pdn_n)) {
		dev_err(dev, "error requesting PDN gpio: %ld\n",
			PTR_ERR(tas5805m->gpio_pdn_n));
		return PTR_ERR(tas5805m->gpio_pdn_n);
	}

	tas5805m->model = data->model;
	switch(tas5805m->model) {
	case TAS5782M:
		tas5805m->dsp_init = (uint8_t *)&tas5782m_dsp_init;
		tas5805m->dsp_init_len = ARRAY_SIZE(tas5782m_dsp_init);
		break;
	default:
		tas5805m->dsp_init = (uint8_t *)&tas5805m_dsp_init;
		tas5805m->dsp_init_len = ARRAY_SIZE(tas5805m_dsp_init);
	}

	/* This configuration must be generated by PPC3. The file loaded
	 * consists of a sequence of register writes, where bytes at
	 * even indices are register addresses and those at odd indices
	 * are register values.
	 *
	 * The fixed portion of PPC3's output prior to the 5ms delay
	 * should be omitted.
	 */
	if (device_property_read_string(dev, "ti,dsp-config-name",
					&config_name))
		config_name = "default";

	switch(tas5805m->model){
	case TAS5782M:
		snprintf(filename, sizeof(filename), "tas5728m_dsp_%s.bin",
			config_name);
		break;
	default:
		snprintf(filename, sizeof(filename), "tas5805m_dsp_%s.bin",
			config_name);
	}

	ret = request_firmware(&fw, filename, dev);
	if (ret)
		return ret;

	if ((fw->size < 2) || (fw->size & 1)) {
		dev_err(dev, "firmware is invalid\n");
		release_firmware(fw);
		return -EINVAL;
	}

	tas5805m->dsp_cfg_len = fw->size;
	tas5805m->dsp_cfg_data = devm_kmemdup(dev, fw->data, fw->size, GFP_KERNEL);
	if (!tas5805m->dsp_cfg_data) {
		release_firmware(fw);
		return -ENOMEM;
	}

	release_firmware(fw);

	/* Do the first part of the power-on here, while we can expect
	 * the I2S interface to be quiet. We must raise PDN# and then
	 * wait 5ms before any I2S clock is sent, or else the internal
	 * regulator apparently won't come on.
	 *
	 * Also, we must keep the device in power down for 100ms or so
	 * after PVDD is applied, or else the ADR pin is sampled
	 * incorrectly and the device comes up with an unpredictable I2C
	 * address.
	 */
	tas5805m->vol[0] = TAS5805M_VOLUME_MIN;
	tas5805m->vol[1] = TAS5805M_VOLUME_MIN;

	ret = regulator_enable(tas5805m->pvdd);
	if (ret < 0) {
		dev_err(dev, "failed to enable pvdd: %d\n", ret);
		return ret;
	}

	usleep_range(100000, 150000);
	gpiod_set_value(tas5805m->gpio_pdn_n, 1);
	usleep_range(10000, 15000);

	INIT_WORK(&tas5805m->work, do_work);
	mutex_init(&tas5805m->lock);

	/* Don't register through devm. We need to be able to unregister
	 * the component prior to deasserting PDN#
	 */
	ret = snd_soc_register_component(dev, &soc_codec_dev_tas5805m,
					 &tas5805m_dai, 1);
	if (ret < 0) {
		dev_err(dev, "unable to register codec: %d\n", ret);
		gpiod_set_value(tas5805m->gpio_pdn_n, 0);
		regulator_disable(tas5805m->pvdd);
		return ret;
	}

	return 0;
}

static void tas5805m_i2c_remove(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct tas5805m_priv *tas5805m = dev_get_drvdata(dev);

	cancel_work_sync(&tas5805m->work);
	snd_soc_unregister_component(dev);
	gpiod_set_value(tas5805m->gpio_pdn_n, 0);
	usleep_range(10000, 15000);
	regulator_disable(tas5805m->pvdd);
}

static const struct i2c_device_id tas5805m_i2c_id[] = {
	{ "tas5805m", },
	{ "tas5782m", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tas5805m_i2c_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id tas5805m_of_match[] = {
	{ .compatible = "ti,tas5805m", .data = &tas5805m_of_data },
	{ .compatible = "ti,tas5782m", .data = &tas5782m_of_data },
	{ }
};
MODULE_DEVICE_TABLE(of, tas5805m_of_match);
#endif

static struct i2c_driver tas5805m_i2c_driver = {
	.probe		= tas5805m_i2c_probe,
	.remove		= tas5805m_i2c_remove,
	.id_table	= tas5805m_i2c_id,
	.driver		= {
		.name		= "tas5805m",
		.of_match_table = of_match_ptr(tas5805m_of_match),
	},
};

module_i2c_driver(tas5805m_i2c_driver);

MODULE_AUTHOR("Andy Liu <andy-liu@ti.com>");
MODULE_AUTHOR("Daniel Beer <daniel.beer@igorinstitute.com>");
MODULE_DESCRIPTION("TAS5782M/TAS5805M Audio Amplifier Driver");
MODULE_LICENSE("GPL v2");
