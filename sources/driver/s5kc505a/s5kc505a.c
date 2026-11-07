// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung S5KC505A 5MP MIPI CSI-2 sensor driver
 * Copyright (c) 2026 Mei Tao
 *
 * Front camera of the Lenovo ThinkSmart View (CD-18781Y). The register
 * tables were decoded from Lenovo's proprietary libmmcamera_s5kc505a.so;
 * no vendor binary is included. The sensor follows the
 * SMIA++/CCS register layout for timing, exposure and gain.
 *
 * 2026-11-07: added horizontal/vertical flip controls (register 0x0101).
 * The sensor does not compensate the Bayer phase when mirroring; measured on
 * the device: none -> GRBG, h-mirror -> RGGB, v-flip -> BGGR, both -> GBRG.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>

#include "s5kc505a_regs.h"

#define S5KC505A_REG_CHIP_ID		CCI_REG16(0x0000)
#define S5KC505A_CHIP_ID		0x5034
#define S5KC505A_REG_ALT_ID		CCI_REG16(0x300a)

#define S5KC505A_REG_MODE_SELECT	CCI_REG8(0x0100)
#define S5KC505A_MODE_STANDBY		0x00
#define S5KC505A_MODE_STREAMING		0x01

/* SMIA/CCS image_orientation: bit 0 horizontal mirror, bit 1 vertical flip */
#define S5KC505A_REG_ORIENTATION	CCI_REG8(0x0101)

#define S5KC505A_REG_GROUP_HOLD		CCI_REG8(0x0104)
#define S5KC505A_REG_EXPOSURE		CCI_REG16(0x0202)
#define S5KC505A_REG_ANALOG_GAIN	CCI_REG16(0x0204)
#define S5KC505A_REG_DGAIN_GR		CCI_REG16(0x020e)
#define S5KC505A_REG_DGAIN_R		CCI_REG16(0x0210)
#define S5KC505A_REG_DGAIN_B		CCI_REG16(0x0212)
#define S5KC505A_REG_DGAIN_GB		CCI_REG16(0x0214)
#define S5KC505A_REG_FRAME_LENGTH	CCI_REG16(0x0340)
#define S5KC505A_REG_LINE_LENGTH	CCI_REG16(0x0342)
#define S5KC505A_REG_TEST_PATTERN	CCI_REG16(0x0600)
#define S5KC505A_REG_DATA_FORMAT	CCI_REG16(0x0112)
#define S5KC505A_REG_LANE_MODE		CCI_REG8(0x0114)

/* External clock 24 MHz, set as 0x0136/0x0137 = 24.00 */
#define S5KC505A_XCLK_FREQ		24000000

/* From the vendor mode records: vt_pixel_clk 190 MHz, 960 Mbps per lane */
#define S5KC505A_PIXEL_RATE		190000000
#define S5KC505A_LINK_FREQ		480000000ULL
#define S5KC505A_DATA_LANES		2

#define S5KC505A_LINE_LENGTH		3112
#define S5KC505A_FRAME_LENGTH_MAX	0xffff
#define S5KC505A_EXPOSURE_MARGIN	8
#define S5KC505A_EXPOSURE_MIN		4

/* Analog gain code: 0x20 = 1x, 0x200 = 16x */
#define S5KC505A_AGAIN_MIN		0x20
#define S5KC505A_AGAIN_MAX		0x200
#define S5KC505A_AGAIN_DEFAULT		0x20

/* Digital gain: 8.8 fixed point, 0x100 = 1x */
#define S5KC505A_DGAIN_MIN		0x100
#define S5KC505A_DGAIN_MAX		0xfff
#define S5KC505A_DGAIN_DEFAULT		0x100

#define S5KC505A_NATIVE_WIDTH		2608
#define S5KC505A_NATIVE_HEIGHT		1960

/* GRBG, confirmed with the sensor's colour-bar test pattern. */
static int bayer_order = 1;
module_param(bayer_order, int, 0444);
MODULE_PARM_DESC(bayer_order, "0=RGGB 1=GRBG 2=GBRG 3=BGGR (default 1)");

/*
 * libcamera's software ISP manages only about 15 fps at 2608x1960 on this
 * SoC, and apps tend to pick the largest size. Offer only the 2x2 binned
 * 1304x980 mode (30 fps) unless full resolution is asked for.
 */
static bool full_res;
module_param(full_res, bool, 0444);
MODULE_PARM_DESC(full_res, "Also offer 2608x1960 (about 15 fps with software ISP)");

static bool flip_ctrls = true;
module_param(flip_ctrls, bool, 0444);
MODULE_PARM_DESC(flip_ctrls, "Expose HFLIP/VFLIP (needs libcamera >= 0.5; set 0 for libcamera 0.4 compatibility)");

/*
 * Indexed by the position of the red pixel in the 2x2 cell: index = x + 2 * y.
 * A horizontal mirror toggles x, a vertical flip toggles y.
 */
static const u32 s5kc505a_mbus_codes[] = {
	MEDIA_BUS_FMT_SRGGB10_1X10,	/* R at (0,0) */
	MEDIA_BUS_FMT_SGRBG10_1X10,	/* R at (1,0) */
	MEDIA_BUS_FMT_SGBRG10_1X10,	/* R at (0,1) */
	MEDIA_BUS_FMT_SBGGR10_1X10,	/* R at (1,1) */
};

static const char * const s5kc505a_supply_names[] = {
	"vio",	/* 1.8 V interface */
	"vana",	/* 2.8 V analog */
	"vdig",	/* 1.2 V digital core */
};

#define S5KC505A_NUM_SUPPLIES ARRAY_SIZE(s5kc505a_supply_names)

static const s64 s5kc505a_link_freq_menu[] = {
	S5KC505A_LINK_FREQ,
};

static const char * const s5kc505a_test_pattern_menu[] = {
	"Disabled",
	"Solid Colour",
	"Colour Bars",
	"Fade to Grey Colour Bars",
	"PN9",
};

struct s5kc505a_reg_list {
	const struct cci_reg_sequence *regs;
	unsigned int num;
};

struct s5kc505a_mode {
	u32 width;
	u32 height;
	u32 frame_length;
	struct v4l2_rect crop;
	struct s5kc505a_reg_list reg_list;
};

static const struct s5kc505a_mode s5kc505a_modes[] = {
	{
		.width = 2608,
		.height = 1960,
		.frame_length = 2030,
		.crop = { .left = 0, .top = 0, .width = 2608, .height = 1960 },
		.reg_list = {
			.regs = s5kc505a_mode_2608x1960_regs,
			.num = ARRAY_SIZE(s5kc505a_mode_2608x1960_regs),
		},
	},
	{
		.width = 1304,
		.height = 980,
		.frame_length = 2030,
		.crop = { .left = 0, .top = 0, .width = 2608, .height = 1960 },
		.reg_list = {
			.regs = s5kc505a_mode_1304x980_regs,
			.num = ARRAY_SIZE(s5kc505a_mode_1304x980_regs),
		},
	},
};

struct s5kc505a {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct regmap *regmap;

	struct clk *xclk;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *pwdn_gpio;
	struct regulator_bulk_data supplies[S5KC505A_NUM_SUPPLIES];

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;

	const struct s5kc505a_mode *mode;
};

static inline struct s5kc505a *to_s5kc505a(struct v4l2_subdev *sd)
{
	return container_of(sd, struct s5kc505a, sd);
}

/* Modes offered to userspace; the first one is the default. */
static const struct s5kc505a_mode *s5kc505a_mode_list(unsigned int *num)
{
	if (full_res) {
		*num = ARRAY_SIZE(s5kc505a_modes);
		return &s5kc505a_modes[0];
	}

	*num = 1;
	return &s5kc505a_modes[1];
}

/* Media bus code for the current flip state. */
static u32 s5kc505a_code(struct s5kc505a *sensor)
{
	unsigned int idx = bayer_order & 3;

	if (sensor->hflip && sensor->hflip->val)
		idx ^= 1;
	if (sensor->vflip && sensor->vflip->val)
		idx ^= 2;

	return s5kc505a_mbus_codes[idx];
}

static int s5kc505a_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct s5kc505a *sensor =
		container_of(ctrl->handler, struct s5kc505a, ctrl_handler);
	struct device *dev = sensor->sd.dev;
	const struct s5kc505a_mode *mode = sensor->mode;
	int ret = 0;

	if ((ctrl->id == V4L2_CID_HFLIP || ctrl->id == V4L2_CID_VFLIP) &&
	    (!sensor->hflip || !sensor->vflip))
		return -EINVAL;

	if (ctrl->id == V4L2_CID_VBLANK) {
		int exp_max = mode->height + ctrl->val - S5KC505A_EXPOSURE_MARGIN;

		ret = __v4l2_ctrl_modify_range(sensor->exposure,
					       sensor->exposure->minimum,
					       exp_max, sensor->exposure->step,
					       min(sensor->exposure->default_value,
						   (s64)exp_max));
		if (ret)
			return ret;
	}

	/* Mirroring changes the Bayer order: keep the active format in sync. */
	if (ctrl->id == V4L2_CID_HFLIP || ctrl->id == V4L2_CID_VFLIP) {
		struct v4l2_subdev_state *state =
			v4l2_subdev_get_locked_active_state(&sensor->sd);
		struct v4l2_mbus_framefmt *fmt =
			v4l2_subdev_state_get_format(state, 0);

		fmt->code = s5kc505a_code(sensor);
	}

	/* Values are written when streaming starts. */
	if (pm_runtime_get_if_in_use(dev) <= 0)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		cci_write(sensor->regmap, S5KC505A_REG_EXPOSURE, ctrl->val, &ret);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		cci_write(sensor->regmap, S5KC505A_REG_ANALOG_GAIN, ctrl->val,
			  &ret);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		cci_write(sensor->regmap, S5KC505A_REG_DGAIN_GR, ctrl->val, &ret);
		cci_write(sensor->regmap, S5KC505A_REG_DGAIN_R, ctrl->val, &ret);
		cci_write(sensor->regmap, S5KC505A_REG_DGAIN_B, ctrl->val, &ret);
		cci_write(sensor->regmap, S5KC505A_REG_DGAIN_GB, ctrl->val, &ret);
		break;
	case V4L2_CID_VBLANK:
		cci_write(sensor->regmap, S5KC505A_REG_FRAME_LENGTH,
			  mode->height + ctrl->val, &ret);
		break;
	case V4L2_CID_TEST_PATTERN:
		cci_write(sensor->regmap, S5KC505A_REG_TEST_PATTERN, ctrl->val,
			  &ret);
		break;
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		cci_write(sensor->regmap, S5KC505A_REG_ORIENTATION,
			  sensor->hflip->val | (sensor->vflip->val << 1), &ret);
		break;
	default:
		ret = 0;
		break;
	}

	pm_runtime_put(dev);

	return ret;
}

static const struct v4l2_ctrl_ops s5kc505a_ctrl_ops = {
	.s_ctrl = s5kc505a_set_ctrl,
};

static int s5kc505a_init_controls(struct s5kc505a *sensor)
{
	struct v4l2_ctrl_handler *hdl = &sensor->ctrl_handler;
	const struct s5kc505a_mode *mode = sensor->mode;
	struct v4l2_fwnode_device_properties props;
	struct device *dev = sensor->sd.dev;
	int exp_max, ret;

	v4l2_ctrl_handler_init(hdl, 13);

	sensor->link_freq = v4l2_ctrl_new_int_menu(hdl, &s5kc505a_ctrl_ops,
						   V4L2_CID_LINK_FREQ, 0, 0,
						   s5kc505a_link_freq_menu);
	if (sensor->link_freq)
		sensor->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	sensor->pixel_rate = v4l2_ctrl_new_std(hdl, &s5kc505a_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       S5KC505A_PIXEL_RATE,
					       S5KC505A_PIXEL_RATE, 1,
					       S5KC505A_PIXEL_RATE);

	sensor->hblank = v4l2_ctrl_new_std(hdl, &s5kc505a_ctrl_ops,
					   V4L2_CID_HBLANK,
					   S5KC505A_LINE_LENGTH - mode->width,
					   S5KC505A_LINE_LENGTH - mode->width, 1,
					   S5KC505A_LINE_LENGTH - mode->width);
	if (sensor->hblank)
		sensor->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	sensor->vblank = v4l2_ctrl_new_std(hdl, &s5kc505a_ctrl_ops,
					   V4L2_CID_VBLANK,
					   mode->frame_length - mode->height,
					   S5KC505A_FRAME_LENGTH_MAX - mode->height,
					   1, mode->frame_length - mode->height);

	exp_max = mode->frame_length - S5KC505A_EXPOSURE_MARGIN;
	sensor->exposure = v4l2_ctrl_new_std(hdl, &s5kc505a_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     S5KC505A_EXPOSURE_MIN, exp_max, 1,
					     exp_max / 2);

	v4l2_ctrl_new_std(hdl, &s5kc505a_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  S5KC505A_AGAIN_MIN, S5KC505A_AGAIN_MAX, 1,
			  S5KC505A_AGAIN_DEFAULT);

	v4l2_ctrl_new_std(hdl, &s5kc505a_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  S5KC505A_DGAIN_MIN, S5KC505A_DGAIN_MAX, 1,
			  S5KC505A_DGAIN_DEFAULT);

	v4l2_ctrl_new_std_menu_items(hdl, &s5kc505a_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(s5kc505a_test_pattern_menu) - 1,
				     0, 0, s5kc505a_test_pattern_menu);

	if (flip_ctrls) {
		sensor->hflip = v4l2_ctrl_new_std(hdl, &s5kc505a_ctrl_ops,
						  V4L2_CID_HFLIP, 0, 1, 1, 0);
		if (sensor->hflip)
			sensor->hflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

		sensor->vflip = v4l2_ctrl_new_std(hdl, &s5kc505a_ctrl_ops,
						  V4L2_CID_VFLIP, 0, 1, 1, 0);
		if (sensor->vflip)
			sensor->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;
	}

	if (hdl->error) {
		ret = hdl->error;
		goto err;
	}

	ret = v4l2_fwnode_device_parse(dev, &props);
	if (ret)
		goto err;

	ret = v4l2_ctrl_new_fwnode_properties(hdl, &s5kc505a_ctrl_ops, &props);
	if (ret)
		goto err;

	sensor->sd.ctrl_handler = hdl;
	return 0;

err:
	v4l2_ctrl_handler_free(hdl);
	return dev_err_probe(dev, ret, "failed to init controls\n");
}

static int s5kc505a_write_common(struct s5kc505a *sensor)
{
	int ret;

	ret = cci_multi_reg_write(sensor->regmap, s5kc505a_init_pre,
				  ARRAY_SIZE(s5kc505a_init_pre), NULL);
	if (ret)
		return ret;
	msleep(10);

	ret = cci_multi_reg_write(sensor->regmap, s5kc505a_init_regs,
				  ARRAY_SIZE(s5kc505a_init_regs), NULL);
	if (ret)
		return ret;

	/* External clock frequency in MHz, 8.8 fixed point */
	ret = cci_write(sensor->regmap, CCI_REG16(0x0136),
			(S5KC505A_XCLK_FREQ / 1000000) << 8, NULL);
	return ret;
}

static int s5kc505a_enable_streams(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state, u32 pad,
				   u64 streams_mask)
{
	struct s5kc505a *sensor = to_s5kc505a(sd);
	struct device *dev = sd->dev;
	const struct s5kc505a_reg_list *list = &sensor->mode->reg_list;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	ret = s5kc505a_write_common(sensor);
	if (ret) {
		dev_err(dev, "failed to write init registers: %d\n", ret);
		goto err;
	}

	ret = cci_multi_reg_write(sensor->regmap, list->regs, list->num, NULL);
	if (ret) {
		dev_err(dev, "failed to write mode registers: %d\n", ret);
		goto err;
	}

	/* Applies all controls, including the orientation register. */
	ret = __v4l2_ctrl_handler_setup(sd->ctrl_handler);
	if (ret)
		goto err;

	ret = cci_write(sensor->regmap, S5KC505A_REG_MODE_SELECT,
			S5KC505A_MODE_STREAMING, NULL);
	if (ret)
		goto err;

	/* The Bayer order must not change while streaming. */
	if (sensor->hflip)
		__v4l2_ctrl_grab(sensor->hflip, true);
	if (sensor->vflip)
		__v4l2_ctrl_grab(sensor->vflip, true);

	return 0;

err:
	pm_runtime_put_autosuspend(dev);
	return ret;
}

static int s5kc505a_disable_streams(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state, u32 pad,
				    u64 streams_mask)
{
	struct s5kc505a *sensor = to_s5kc505a(sd);
	int ret;

	ret = cci_write(sensor->regmap, S5KC505A_REG_MODE_SELECT,
			S5KC505A_MODE_STANDBY, NULL);
	if (ret)
		dev_err(sd->dev, "failed to stop streaming: %d\n", ret);

	if (sensor->hflip)
		__v4l2_ctrl_grab(sensor->hflip, false);
	if (sensor->vflip)
		__v4l2_ctrl_grab(sensor->vflip, false);

	pm_runtime_put_autosuspend(sd->dev);

	return ret;
}

static void s5kc505a_fill_format(struct s5kc505a *sensor,
				 const struct s5kc505a_mode *mode,
				 struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->code = s5kc505a_code(sensor);
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_601;
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_XFER_FUNC_NONE;
}

static int s5kc505a_set_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *state,
			    struct v4l2_subdev_format *fmt)
{
	struct s5kc505a *sensor = to_s5kc505a(sd);
	const struct s5kc505a_mode *mode, *modes;
	unsigned int num;
	int ret;

	modes = s5kc505a_mode_list(&num);
	mode = v4l2_find_nearest_size(modes, num, width, height,
				      fmt->format.width, fmt->format.height);

	s5kc505a_fill_format(sensor, mode, &fmt->format);
	*v4l2_subdev_state_get_format(state, 0) = fmt->format;
	*v4l2_subdev_state_get_crop(state, 0) = mode->crop;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	sensor->mode = mode;

	ret = __v4l2_ctrl_modify_range(sensor->hblank,
				       S5KC505A_LINE_LENGTH - mode->width,
				       S5KC505A_LINE_LENGTH - mode->width, 1,
				       S5KC505A_LINE_LENGTH - mode->width);
	if (ret)
		return ret;

	ret = __v4l2_ctrl_modify_range(sensor->vblank,
				       mode->frame_length - mode->height,
				       S5KC505A_FRAME_LENGTH_MAX - mode->height,
				       1, mode->frame_length - mode->height);
	if (ret)
		return ret;

	return __v4l2_ctrl_s_ctrl(sensor->vblank,
				  mode->frame_length - mode->height);
}

static int s5kc505a_enum_mbus_code(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = s5kc505a_code(to_s5kc505a(sd));
	return 0;
}

static int s5kc505a_enum_frame_size(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	const struct s5kc505a_mode *modes;
	unsigned int num;

	modes = s5kc505a_mode_list(&num);
	if (fse->index >= num || fse->code != s5kc505a_code(to_s5kc505a(sd)))
		return -EINVAL;

	fse->min_width = modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int s5kc505a_get_selection(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = *v4l2_subdev_state_get_crop(state, 0);
		return 0;
	case V4L2_SEL_TGT_NATIVE_SIZE:
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = S5KC505A_NATIVE_WIDTH;
		sel->r.height = S5KC505A_NATIVE_HEIGHT;
		return 0;
	}

	return -EINVAL;
}

static int s5kc505a_init_state(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state)
{
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
	};
	unsigned int num;
	const struct s5kc505a_mode *mode = s5kc505a_mode_list(&num);

	fmt.format.width = mode->width;
	fmt.format.height = mode->height;

	return s5kc505a_set_fmt(sd, state, &fmt);
}

static const struct v4l2_subdev_video_ops s5kc505a_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops s5kc505a_pad_ops = {
	.enum_mbus_code = s5kc505a_enum_mbus_code,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = s5kc505a_set_fmt,
	.get_selection = s5kc505a_get_selection,
	.enum_frame_size = s5kc505a_enum_frame_size,
	.enable_streams = s5kc505a_enable_streams,
	.disable_streams = s5kc505a_disable_streams,
};

static const struct v4l2_subdev_ops s5kc505a_subdev_ops = {
	.video = &s5kc505a_video_ops,
	.pad = &s5kc505a_pad_ops,
};

static const struct v4l2_subdev_internal_ops s5kc505a_internal_ops = {
	.init_state = s5kc505a_init_state,
};

static int s5kc505a_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct s5kc505a *sensor = to_s5kc505a(sd);
	int ret;

	gpiod_set_value_cansleep(sensor->reset_gpio, 1);
	gpiod_set_value_cansleep(sensor->pwdn_gpio, 1);

	ret = regulator_bulk_enable(S5KC505A_NUM_SUPPLIES, sensor->supplies);
	if (ret) {
		dev_err(dev, "failed to enable regulators: %d\n", ret);
		return ret;
	}
	usleep_range(1000, 2000);

	ret = clk_prepare_enable(sensor->xclk);
	if (ret) {
		dev_err(dev, "failed to enable clock: %d\n", ret);
		regulator_bulk_disable(S5KC505A_NUM_SUPPLIES, sensor->supplies);
		return ret;
	}
	usleep_range(1000, 2000);

	gpiod_set_value_cansleep(sensor->pwdn_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(sensor->reset_gpio, 0);

	/* The ID registers become readable a little after reset release. */
	msleep(20);

	return 0;
}

static int s5kc505a_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct s5kc505a *sensor = to_s5kc505a(sd);

	gpiod_set_value_cansleep(sensor->reset_gpio, 1);
	gpiod_set_value_cansleep(sensor->pwdn_gpio, 1);
	clk_disable_unprepare(sensor->xclk);
	regulator_bulk_disable(S5KC505A_NUM_SUPPLIES, sensor->supplies);

	return 0;
}

static int s5kc505a_identify(struct s5kc505a *sensor)
{
	struct device *dev = sensor->sd.dev;
	u64 id = 0, alt = 0, fmt = 0, lanes = 0;
	int ret = 0, i;

	for (i = 0; i < 5; i++) {
		ret = cci_read(sensor->regmap, S5KC505A_REG_CHIP_ID, &id, NULL);
		if (!ret && id == S5KC505A_CHIP_ID)
			break;
		msleep(20);
	}

	cci_read(sensor->regmap, S5KC505A_REG_ALT_ID, &alt, NULL);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to read chip id (0x300a=0x%04llx)\n",
				     alt);
	if (id != S5KC505A_CHIP_ID)
		return dev_err_probe(dev, -ENODEV,
				     "chip id mismatch: 0x%04llx (0x300a=0x%04llx)\n",
				     id, alt);

	cci_read(sensor->regmap, S5KC505A_REG_DATA_FORMAT, &fmt, NULL);
	cci_read(sensor->regmap, S5KC505A_REG_LANE_MODE, &lanes, NULL);
	dev_info(dev, "S5KC505A found, id 0x%04llx/0x%04llx, format 0x%04llx, lane mode %llu\n",
		 id, alt, fmt, lanes);

	return 0;
}

static int s5kc505a_check_hwcfg(struct device *dev)
{
	struct v4l2_fwnode_endpoint ep = { .bus_type = V4L2_MBUS_CSI2_DPHY };
	struct fwnode_handle *fwnode;
	unsigned long bitmap;
	int ret;

	fwnode = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!fwnode)
		return dev_err_probe(dev, -EINVAL, "endpoint not found\n");

	ret = v4l2_fwnode_endpoint_alloc_parse(fwnode, &ep);
	fwnode_handle_put(fwnode);
	if (ret)
		return dev_err_probe(dev, ret, "failed to parse endpoint\n");

	if (ep.bus.mipi_csi2.num_data_lanes != S5KC505A_DATA_LANES) {
		ret = dev_err_probe(dev, -EINVAL, "only 2 data lanes supported\n");
		goto out;
	}

	ret = v4l2_link_freq_to_bitmap(dev, ep.link_frequencies,
				       ep.nr_of_link_frequencies,
				       s5kc505a_link_freq_menu,
				       ARRAY_SIZE(s5kc505a_link_freq_menu),
				       &bitmap);
out:
	v4l2_fwnode_endpoint_free(&ep);
	return ret;
}

static int s5kc505a_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct s5kc505a *sensor;
	unsigned int i;
	int ret;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&sensor->sd, client, &s5kc505a_subdev_ops);
	sensor->sd.internal_ops = &s5kc505a_internal_ops;
	sensor->mode = s5kc505a_mode_list(&i);

	ret = s5kc505a_check_hwcfg(dev);
	if (ret)
		return ret;

	sensor->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(sensor->regmap))
		return dev_err_probe(dev, PTR_ERR(sensor->regmap),
				     "failed to init CCI regmap\n");

	sensor->xclk = devm_clk_get(dev, NULL);
	if (IS_ERR(sensor->xclk))
		return dev_err_probe(dev, PTR_ERR(sensor->xclk),
				     "failed to get clock\n");
	if (clk_get_rate(sensor->xclk) != S5KC505A_XCLK_FREQ)
		dev_warn(dev, "clock is %lu Hz, expected %u\n",
			 clk_get_rate(sensor->xclk), S5KC505A_XCLK_FREQ);

	for (i = 0; i < S5KC505A_NUM_SUPPLIES; i++)
		sensor->supplies[i].supply = s5kc505a_supply_names[i];
	ret = devm_regulator_bulk_get(dev, S5KC505A_NUM_SUPPLIES,
				      sensor->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	sensor->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(sensor->reset_gpio),
				     "failed to get reset gpio\n");

	sensor->pwdn_gpio = devm_gpiod_get_optional(dev, "powerdown",
						    GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->pwdn_gpio))
		return dev_err_probe(dev, PTR_ERR(sensor->pwdn_gpio),
				     "failed to get powerdown gpio\n");

	ret = s5kc505a_power_on(dev);
	if (ret)
		return ret;

	ret = s5kc505a_identify(sensor);
	if (ret)
		goto err_power;

	ret = s5kc505a_init_controls(sensor);
	if (ret)
		goto err_power;

	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret)
		goto err_ctrls;

	sensor->sd.state_lock = sensor->ctrl_handler.lock;
	ret = v4l2_subdev_init_finalize(&sensor->sd);
	if (ret)
		goto err_entity;

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	ret = v4l2_async_register_subdev_sensor(&sensor->sd);
	if (ret)
		goto err_subdev;

	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_idle(dev);

	return 0;

err_subdev:
	v4l2_subdev_cleanup(&sensor->sd);
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
err_entity:
	media_entity_cleanup(&sensor->sd.entity);
err_ctrls:
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
err_power:
	s5kc505a_power_off(dev);
	return ret;
}

static void s5kc505a_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5kc505a *sensor = to_s5kc505a(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev)) {
		s5kc505a_power_off(&client->dev);
		pm_runtime_set_suspended(&client->dev);
	}
}

static const struct of_device_id s5kc505a_of_match[] = {
	{ .compatible = "samsung,s5kc505a" },
	{ }
};
MODULE_DEVICE_TABLE(of, s5kc505a_of_match);

static DEFINE_RUNTIME_DEV_PM_OPS(s5kc505a_pm_ops, s5kc505a_power_off,
				 s5kc505a_power_on, NULL);

static struct i2c_driver s5kc505a_driver = {
	.driver = {
		.name = "s5kc505a",
		.of_match_table = s5kc505a_of_match,
		.pm = pm_ptr(&s5kc505a_pm_ops),
	},
	.probe = s5kc505a_probe,
	.remove = s5kc505a_remove,
};
module_i2c_driver(s5kc505a_driver);

MODULE_DESCRIPTION("Samsung S5KC505A camera sensor driver");
MODULE_LICENSE("GPL");
