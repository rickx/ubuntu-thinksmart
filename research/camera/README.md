# Camera Research

This directory is the public-safe camera subset for the `ubuntu-thinksmart` repo.

## Current Status

- live hardware identifies Samsung S5KC505A as the active front sensor
- canonical sensor ID is `0x5034` at `0x0000/0x0001`
- Linux driver work is in progress and the camera is not working yet in Ubuntu

## What Is Included Here

- current driver plan in `analysis/s5kc505a-driver-plan.md`
- reverse-engineering notes in `analysis/s5kc505a-reverse-notes.md`
- supporting public-safe inventories in `inventories/`

## What Is Intentionally Not Included

The proprietary Android vendor camera blobs used during reverse engineering are not copied into this repo by default.

Those binaries remain local research inputs outside the publication tree unless there is an explicit compliance decision to publish them.