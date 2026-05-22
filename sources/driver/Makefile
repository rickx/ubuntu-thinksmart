# Build both production and debug TAS5782M modules from this directory.

obj-m += snd-soc-tas5782m.o
snd-soc-tas5782m-objs := tas5782m.o tas5782m_tables.o

obj-m += snd-soc-tas5782m-dbg.o
snd-soc-tas5782m-dbg-objs := tas5782m_dbgbuild.o tas5782m_tables_dbgbuild.o \
                              tas5782m_dbg.o

CFLAGS_tas5782m_dbgbuild.o        := -DTAS5782M_DEBUG
CFLAGS_tas5782m_tables_dbgbuild.o := -DTAS5782M_DEBUG
CFLAGS_tas5782m_dbg.o             := -DTAS5782M_DEBUG