# Building The TAS5782M Module

This repo ships the TAS5782M driver source under `sources/driver/` and a prebuilt debug module under `prebuilt/`.

## Prerequisites

- WSL or Linux build host
- `clang-18`
- `aarch64-linux-gnu-` cross toolchain
- prepared kernel build tree for the target kernel (`KSRC`)
- writable staging directory for the out-of-tree module (`WSLSRC`, defaults to `$HOME/tas5782m-src`)

## Why Clang-18 Is Mandatory

The target kernel was built with Clang and KCFI enabled. A GCC-built module will appear to load but its init function will fail the type-hash check and probe will never run.

Use:

```bash
CC=clang-18
```

## Repo Layout

- driver sources: `sources/driver/`
- helper build script: `sources/scripts/build-wsl.sh`
- build output tracked in repo: `prebuilt/snd-soc-tas5782m-dbg.ko`

## Recommended Build Path

From WSL or a Linux shell:

```bash
export KSRC=/path/to/kernel-build-v619
# optional; defaults to $HOME/tas5782m-src
export WSLSRC=/path/to/tas5782m-src

bash sources/scripts/build-wsl.sh
```

That script:

1. copies `sources/driver/` into `$WSLSRC`
2. builds against `$KSRC`
3. writes the new debug module back to `prebuilt/`

## Manual Build

If you prefer to do the same build steps yourself:

```bash
export KSRC=/path/to/kernel-build-v619
export WSLSRC=/path/to/tas5782m-src

mkdir -p "$WSLSRC"
cp sources/driver/* "$WSLSRC"/

make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- CC=clang-18 \
     KBUILD_MODPOST_WARN=1 \
     -C "$KSRC" \
     M="$WSLSRC" \
     modules
```

Useful verification after build:

```bash
sha256sum "$WSLSRC"/snd-soc-tas5782m-dbg.ko
modinfo "$WSLSRC"/snd-soc-tas5782m-dbg.ko | grep -E 'vermagic|alias'
```

## S5KC505A Camera Module

The camera driver (`sources/driver/s5kc505a/`) builds the same way. A full
kernel build is not needed: a vanilla `linux-6.19.5` tree configured with the
device's own `/boot/config` is enough. `CONFIG_MODVERSIONS` is off, so the
missing `Module.symvers` only needs `KBUILD_MODPOST_WARN=1`.

```bash
# WSL / Ubuntu host: clang-18 lld-18 llvm-18 make flex bison bc libssl-dev libelf-dev
curl -LO https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.19.5.tar.xz
tar -xf linux-6.19.5.tar.xz
scp ubuntu@<device>:/boot/config linux-6.19.5/.config     # CONFIG_LOCALVERSION="-msm8953"
make -C linux-6.19.5 ARCH=arm64 LLVM=-18 CC=clang-18 HOSTCC=clang-18 olddefconfig
make -C linux-6.19.5 ARCH=arm64 LLVM=-18 CC=clang-18 HOSTCC=clang-18 -j"$(nproc)" modules_prepare

mkdir -p cam && cp sources/driver/s5kc505a/{s5kc505a.c,s5kc505a_regs.h,Makefile} cam/
make -C linux-6.19.5 ARCH=arm64 LLVM=-18 CC=clang-18 KBUILD_MODPOST_WARN=1 M="$PWD/cam" modules
modinfo cam/s5kc505a.ko | grep vermagic   # 6.19.5-msm8953 SMP preempt mod_unload aarch64
```

The running kernel was built with clang 21; clang-18 modules load and probe
fine (KCFI type hashes match). Check that `-fsanitize=kcfi` is in
`cam/.s5kc505a.o.cmd`.

The module is packaged together with libcamera and the PipeWire plugin; see
[sources/camera/ubuntu/README.md](sources/camera/ubuntu/README.md).

## Related Docs

- audio behavior and driver notes: [AUDIO.md](AUDIO.md)
- camera on the Ubuntu image: [sources/camera/ubuntu/README.md](sources/camera/ubuntu/README.md)
- deployment helpers: `sources/scripts/`