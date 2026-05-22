# Building The TAS5782M Module

This repo ships the TAS5782M driver source under `sources/driver/` and a prebuilt debug module under `prebuilt/`.

## Prerequisites

- WSL or Linux build host
- `clang-18`
- `aarch64-linux-gnu-` cross toolchain
- kernel build tree at `/home/fleverato/kernel-build-v619`
- WSL staging directory at `/home/fleverato/tas5782m-src`

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

From WSL or a Linux shell with the expected local paths available:

```bash
bash sources/scripts/build-wsl.sh
```

That script:

1. copies `sources/driver/` into `/home/fleverato/tas5782m-src`
2. builds against `/home/fleverato/kernel-build-v619`
3. writes the new debug module back to `prebuilt/`

## Manual Build

If you prefer to do the same build steps yourself:

```bash
cp sources/driver/* /home/fleverato/tas5782m-src/

make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- CC=clang-18 \
     KBUILD_MODPOST_WARN=1 \
     -C /home/fleverato/kernel-build-v619 \
     M=/home/fleverato/tas5782m-src \
     modules
```

Useful verification after build:

```bash
sha256sum /home/fleverato/tas5782m-src/snd-soc-tas5782m-dbg.ko
modinfo /home/fleverato/tas5782m-src/snd-soc-tas5782m-dbg.ko | grep -E 'vermagic|alias'
```

## Related Docs

- audio behavior and driver notes: [AUDIO.md](AUDIO.md)
- deployment helpers: `sources/scripts/`