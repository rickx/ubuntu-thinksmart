// SPDX-License-Identifier: GPL-2.0-only
/* tas5782m_dbgbuild.c — compiles tas5782m.c with -DTAS5782M_DEBUG. */
#include "tas5782m.c"

static int __init tas5782m_dbg_module_init(void)
{
	return i2c_add_driver(&tas5782m_driver);
}
module_init(tas5782m_dbg_module_init);

static void __exit tas5782m_dbg_module_exit(void)
{
	i2c_del_driver(&tas5782m_driver);
}
module_exit(tas5782m_dbg_module_exit);

MODULE_AUTHOR("Based on FelixKa / Andy Liu / Daniel Beer");
MODULE_DESCRIPTION("TAS5782M Smart Amplifier ASoC CODEC Driver (debug build)");
MODULE_LICENSE("GPL v2");