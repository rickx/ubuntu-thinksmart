# Flashing Ubuntu On Lenovo ThinkSmart View

This device does not use a stock Android partition layout for the Ubuntu image. Repartitioning is required before flashing the Ubuntu system image.

Only Qualcomm EDL should be used for flashing on this device. fastboot may appear to work as a transport, but actual flashing fails because the bootloader is OEM-locked and requires signed images.

## Requirements

- Lenovo ThinkSmart View CD-18781Y
- host with [bkerler/edl](https://github.com/bkerler/edl)
- USB cable
- [prebuilt/lk2nd.img](prebuilt/lk2nd.img)
- generic bootstrap image `bootstrap-pmos-ssh-generic-qcom-msm8953.img`
- Ubuntu image `ubuntu-qcom-msm8953.img` ([MEGA download](https://mega.nz/file/Qn1SBS4Y#A449WO9ZHH9Pw2JtBwHXAW008tw4uUX2POrSnNHAs_A))
- GPT layout artifacts from `partitions/`:
	- `partitions/ubuntu_layout.sfdisk`
	- `partitions/gpt_ubuntu_main.bin`
	- `partitions/gpt_ubuntu_backup.bin`

## Current Publication Caveat

The official first-time flashing flow is now the smaller SSH-capable bootstrap image plus an on-device GPT rewrite with `sfdisk`. The current public-safe bootstrap base lives at `rootfs/bootstrap-pmos-ssh-generic-qcom-msm8953.img`; it was generated from `Felix_known_working/pmos-customized-rootfs.img` by `sources/scripts/prepare-pmos-ssh-bootstrap-image.sh` and includes:

- `/usr/local/sbin/apply-ubuntu-gpt.sh`
- `/usr/local/sbin/bootstrap-auto-apply-ubuntu-gpt.sh`
- `/etc/init.d/bootstrap-apply-ubuntu-gpt`
- `/usr/local/share/bootstrap/ubuntu_layout.sfdisk`
- `/usr/local/share/bootstrap/BOOTSTRAP-NEXT-STEPS.txt`

The new public-prep builder strips saved WiFi profiles, SSH host keys, and machine-id from the Felix source image, keeps `sshd` enabled, and can optionally inject a user-provided WiFi profile before flashing. The older local `rootfs/bootstrap-pmos-ssh-qcom-msm8953.img` remains a private staging artifact only because it still preserves a saved WiFi profile from the Felix base.

The separate small Ubuntu SSH bootstrap path remains possible in the future via `sources/scripts/prepare-ubuntu-ssh-bootstrap-image.sh`, but it is no longer the blocker for publication because the Felix-based SSH bootstrap already satisfies the safer WiFi+SSH requirement.

## Enter EDL

1. Power the device off.
2. Hold both volume buttons.
3. Power the device on while holding them.
4. Connect USB.

The device should enumerate in Qualcomm EDL mode.

## Flash Sequence

```powershell
# Stage 0: personalize the generic bootstrap image with your WiFi credentials
wsl -e bash -lc "cd /mnt/c/pmos-temp/FINAL && bash sources/scripts/prepare-pmos-ssh-bootstrap-image.sh --src /mnt/c/pmos-temp/FINAL/rootfs/bootstrap-pmos-ssh-generic-qcom-msm8953.img --out /mnt/c/pmos-temp/FINAL/rootfs/bootstrap-pmos-ssh-personalized-qcom-msm8953.img --ssid 'YOUR_WIFI_SSID' --psk 'YOUR_WIFI_PASSWORD' --login-password thinksmart"

# Stage 1: flash lk2nd and the personalized bootstrap image
python edl.py w boot prebuilt/lk2nd.img
python edl.py w system rootfs/bootstrap-pmos-ssh-personalized-qcom-msm8953.img

# Stage 2: let the bootstrap image join WiFi, SSH in, and run:
#   sudo /usr/local/sbin/apply-ubuntu-gpt.sh

# Stage 3: flash the full Ubuntu image after GPT rewrite
python edl.py w system ubuntu-qcom-msm8953.img
```

## Bootstrap SSH Step

Boot the personalized bootstrap image, let it connect to WiFi, then SSH in as `pmos` with password `thinksmart` and run:

```sh
sudo /usr/local/sbin/apply-ubuntu-gpt.sh
```

That uses the staged `/usr/local/share/bootstrap/ubuntu_layout.sfdisk` to rewrite the outer GPT. After it completes:

1. power the device off or reboot it
2. re-enter EDL
3. flash the full Ubuntu image

## Experimental Auto Step

The repo still contains an experimental unattended bootstrap path. If you intentionally build with
`ENABLE_UNATTENDED_BOOTSTRAP=1`, booting the image will:

1. runs `/usr/local/sbin/bootstrap-auto-apply-ubuntu-gpt.sh`
2. rewrites the outer eMMC GPT using `/usr/local/share/bootstrap/ubuntu_layout.sfdisk`
3. powers the device off

No keyboard, SSH session, or other user intervention is required for the repartition step. Once the device powers off, re-enter EDL and flash the full Ubuntu image.

Do not ship or recommend this as the default first-time flashing path until it has been validated once on hardware.

## Ubuntu SSH Bootstrap Candidate

The alternative future path is:

1. build or recover a small Ubuntu image that still fits the stock 1.5 GiB `system` partition
2. run `sources/scripts/prepare-ubuntu-ssh-bootstrap-image.sh` to inject WiFi, enable SSH, and stage `apply-ubuntu-gpt.sh`
3. flash that image with EDL
4. let it join WiFi automatically
5. SSH in and run `sudo /usr/local/sbin/apply-ubuntu-gpt.sh`
6. re-enter EDL and flash the full Ubuntu image

## First Boot

1. Unplug USB and power the device on normally.
2. Wait for Plasma Mobile to boot.
3. Connect the device to WiFi using the touchscreen.
4. Only after WiFi is configured, connect over SSH.

## Notes

- the bootstrap image should be published as a GitHub Release asset rather than committed into git history
- the Ubuntu image is too large for a normal GitHub release asset and is currently hosted on [MEGA](https://mega.nz/file/Qn1SBS4Y#A449WO9ZHH9Pw2JtBwHXAW008tw4uUX2POrSnNHAs_A)
- the working copies under `rootfs/` are local staging artifacts until the bootstrap asset is attached to a GitHub Release
- GPT background notes belong in [partitions/LAYOUT.md](partitions/LAYOUT.md)
- the current public-safe bootstrap base is `rootfs/bootstrap-pmos-ssh-generic-qcom-msm8953.img`; the personalized bootstrap image should be generated locally with `sources/scripts/prepare-pmos-ssh-bootstrap-image.sh` before flashing
- the separate small Ubuntu bootstrap route is still optional future work, not the current blocker
- the unattended OpenRC path should remain experimental until one hardware validation confirms it does not strand the device in a broken GPT state