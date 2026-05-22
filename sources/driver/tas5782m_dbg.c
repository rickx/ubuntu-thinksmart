// SPDX-License-Identifier: GPL-2.0-only
/*
 * tas5782m_dbg.c — debugfs implementation for TAS5782M debug build.
 *
 * Compiled ONLY when TAS5782M_DEBUG is defined.  The header provides
 * no-op stubs for the production build so this file is not linked there.
 *
 * Provides:
 *   /sys/kernel/debug/tas5782m-<bus>-<addr>/
 *     trace_mask   — rw hex u32; controls which phases emit log lines
 *     status       — ro text; per-stream counters for post-mortem analysis
 */

#ifdef TAS5782M_DEBUG

#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include "tas5782m_priv.h"
#include "tas5782m_dbg.h"

/* ========================================================================= */
/* status file: read-only text dump of all counters                          */
/* ========================================================================= */

static int tas5782m_dbg_status_show(struct seq_file *s, void *unused)
{
	struct tas5782m_priv *priv = s->private;
	const struct {
		u32   bit;
		const char *name;
	} phases[] = { TAS5782M_PHASE_NAMES };
	int i;

	seq_printf(s, "TAS5782M debug status\n");
	seq_printf(s, "=====================\n");

	seq_printf(s, "trace_mask:       0x%02x  (active phases: ",
		   priv->dbg.trace_mask);
	for (i = 0; i < ARRAY_SIZE(phases); i++) {
		if (priv->dbg.trace_mask & phases[i].bit)
			seq_printf(s, "%s ", phases[i].name);
	}
	seq_printf(s, ")\n");

	seq_printf(s, "n_probe:          %d\n",
		   atomic_read(&priv->dbg.n_probe));
	seq_printf(s, "n_play_attempted: %d\n",
		   atomic_read(&priv->dbg.n_play_attempted));
	seq_printf(s, "n_play_success:   %d\n",
		   atomic_read(&priv->dbg.n_play_success));
	seq_printf(s, "n_fault_seen:     %d\n",
		   atomic_read(&priv->dbg.n_fault_seen));
	seq_printf(s, "n_bclk_miss:      %d  (MODE_PLAY sent before BCLK?)\n",
		   atomic_read(&priv->dbg.n_bclk_miss));
	seq_printf(s, "n_firmware_load:  %d\n",
		   atomic_read(&priv->dbg.n_firmware_load));
	seq_printf(s, "last_fault_reg:   0x%02x\n",
		   priv->dbg.last_fault_reg);

	return 0;
}

static int tas5782m_dbg_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, tas5782m_dbg_status_show, inode->i_private);
}

static const struct file_operations tas5782m_dbg_status_fops = {
	.open    = tas5782m_dbg_status_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* ========================================================================= */
/* trace_mask file: read/write hex u32                                       */
/* ========================================================================= */

static ssize_t tas5782m_dbg_mask_read(struct file *file, char __user *ubuf,
				      size_t count, loff_t *ppos)
{
	struct tas5782m_priv *priv = file->private_data;
	char buf[16];
	int len = scnprintf(buf, sizeof(buf), "0x%02x\n", priv->dbg.trace_mask);
	return simple_read_from_buffer(ubuf, count, ppos, buf, len);
}

static ssize_t tas5782m_dbg_mask_write(struct file *file,
					const char __user *ubuf,
					size_t count, loff_t *ppos)
{
	struct tas5782m_priv *priv = file->private_data;
	char buf[16];
	u32 val;

	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;
	buf[count] = '\0';

	if (kstrtou32(buf, 0, &val))
		return -EINVAL;

	priv->dbg.trace_mask = val;
	dev_info(&priv->client->dev,
		 "trace_mask updated to 0x%02x\n", val);
	return count;
}

static const struct file_operations tas5782m_dbg_mask_fops = {
	.open  = simple_open,
	.read  = tas5782m_dbg_mask_read,
	.write = tas5782m_dbg_mask_write,
};

/* ========================================================================= */
/* Init / remove                                                              */
/* ========================================================================= */

int tas5782m_dbg_init(struct tas5782m_priv *priv)
{
	char dir_name[32];

	snprintf(dir_name, sizeof(dir_name), "tas5782m-%d-%04x",
		 priv->client->adapter->nr, priv->client->addr);

	priv->dbg.debugfs_root = debugfs_create_dir(dir_name, NULL);
	if (IS_ERR_OR_NULL(priv->dbg.debugfs_root)) {
		dev_warn(&priv->client->dev,
			 "debugfs_create_dir failed — debug files unavailable\n");
		priv->dbg.debugfs_root = NULL;
		return 0;  /* non-fatal */
	}

	debugfs_create_file("status", 0444, priv->dbg.debugfs_root,
			    priv, &tas5782m_dbg_status_fops);
	debugfs_create_file("trace_mask", 0644, priv->dbg.debugfs_root,
			    priv, &tas5782m_dbg_mask_fops);

	dev_info(&priv->client->dev,
		 "debugfs: /sys/kernel/debug/%s/\n", dir_name);
	return 0;
}

void tas5782m_dbg_remove(struct tas5782m_priv *priv)
{
	debugfs_remove_recursive(priv->dbg.debugfs_root);
	priv->dbg.debugfs_root = NULL;
}

#endif /* TAS5782M_DEBUG */
