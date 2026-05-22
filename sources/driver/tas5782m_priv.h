/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _TAS5782M_PRIV_H
#define _TAS5782M_PRIV_H

#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>

#include "tas5782m.h"

/* Private per-chip state shared by core and debug translation units. */
struct tas5782m_priv {
	struct i2c_client        *client;
	struct regmap            *regmap;
	struct regulator         *pvdd;
	struct gpio_desc         *gpio_pdn;

	char                      dsp_config_name[64];
	u8                       *dsp_cfg_data;
	size_t                    dsp_cfg_len;

	int                       vol[2];
	bool                      is_powered;
	bool                      is_muted;

	struct work_struct        work;
	struct mutex              lock;
	struct tas5782m_dbg_state dbg;
};

#endif /* _TAS5782M_PRIV_H */
