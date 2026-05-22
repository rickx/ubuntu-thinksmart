# Partition Layout Notes

The Ubuntu image for this device does not fit into the stock Android `system` partition layout.

## Verified Android Baseline

In the stock Android layout:

- `system` starts at sector `923648`
- Android `system` size is `3145728` sectors = `1.5 GiB`
- Android also had separate `vendor`, `cache`, `persist`, `oem`, `logdump`, `resource`, and `userdata` areas after that

That is why repartitioning is mandatory: the Ubuntu image reuses the same `system` start sector but grows to roughly 7 GB.

## Current Ubuntu Layout Evidence

The repo includes GPT binary dumps for the Ubuntu-installed layout:

- `gpt_ubuntu_main.bin`
	- size: `17408` bytes
	- sha256: `c62e6b310ff0550a7adff647babdcad69ecb264095d4af67f1061c10023d8682`
- `gpt_ubuntu_backup.bin`
	- size: `16896` bytes
	- sha256: `c87f1cd620d3ed08b001749c694409b9c231931e435a5abdb489d8170a43d873`

These binaries are the byte-exact reproduction artifacts for the known-good Ubuntu-installed GPT.

The repo also now includes a text restore file:

- `ubuntu_layout.sfdisk`

This is the cleanest documented Linux-side way to rewrite the GPT from a running rescue/bootstrap system:

```bash
sudo sfdisk /dev/mmcblk0 < ubuntu_layout.sfdisk
sync
reboot
```

The installed Ubuntu layout places:

- `system` as partition `p24`, start sector `923648`, size `14280671`
- `persist` as partition `p27`, start sector `15204319`, size `65536`

Recorded `edl printgpt` output is also useful, even though one saved capture is truncated near the tail entries.

Key points from the captured output:

- total disk size: `0x1d2000000` bytes
- total sectors: `0x0e90000`
- `system` starts at offset `0x1c300000` (same 923648 start sector as Android)
- `system` length in the captured GPT output is `0x1b3cfbe00`

## Important Tooling Note

`edl r PrimaryGPT` is not a valid read command in this setup. `PrimaryGPT` is not exposed as a real partition name by the firehose client, so that read path fails even though `edl printgpt` works.

That means there are now two distinct write paths:

1. Linux-side, from a running bootstrap/rescue system: verified in documentation via `sfdisk` text restore, with binary `dd` as a byte-exact fallback.
2. Host-side, directly from EDL: still not finalized as a public-safe `edl.py` recipe.

Byte-exact Linux fallback, if needed:

```bash
sudo dd if=gpt_ubuntu_main.bin of=/dev/mmcblk0 bs=512 count=34 conv=fsync
sudo dd if=gpt_ubuntu_backup.bin of=/dev/mmcblk0 bs=512 seek=15269855 count=33 conv=fsync
sudo partprobe /dev/mmcblk0 || true
sync
reboot
```