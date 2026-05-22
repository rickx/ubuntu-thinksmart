# Flashing Ubuntu On Lenovo ThinkSmart View

This device does not use a stock Android partition layout for the Ubuntu image. Repartitioning is required before flashing the Ubuntu system image.

Only Qualcomm EDL should be used for flashing on this device. fastboot may appear to work as a transport, but actual flashing fails because the bootloader is OEM-locked and requires signed images.

## Requirements

- Lenovo ThinkSmart View CD-18781Y
- host with [bkerler/edl](https://github.com/bkerler/edl)
- USB cable
- [prebuilt/lk2nd.img](prebuilt/lk2nd.img)
- generic bootstrap image `bootstrap-pmos-ssh-generic-qcom-msm8953.img` ([GitHub Release](https://github.com/rickx/ubuntu-thinksmart/releases/tag/bootstrap-2026-05-22), includes `.sha256`)
- Ubuntu image `ubuntu-qcom-msm8953.img` ([MEGA download](https://mega.nz/file/UnsAjSyK#HmUBaxrxxT-Uej4K7eL1vsyYq0l6NygX_w3D5G6hnDo))
- GPT layout artifacts from `partitions/`:
	- `partitions/ubuntu_layout.sfdisk`
	- `partitions/gpt_ubuntu_main.bin`
	- `partitions/gpt_ubuntu_backup.bin`

## Bootstrap Image

The first-time install uses a temporary SSH-capable bootstrap image so you can rewrite the GPT from the device before flashing the final Ubuntu image.

Start from the generic release asset, then create a personalized copy with `sources/scripts/prepare-pmos-ssh-bootstrap-image.sh`.

That personalized bootstrap image keeps `sshd` enabled, can inject your WiFi profile, and carries the files needed for the GPT rewrite:

- `/usr/local/sbin/apply-ubuntu-gpt.sh`
- `/usr/local/share/bootstrap/ubuntu_layout.sfdisk`
- `/usr/local/share/bootstrap/BOOTSTRAP-NEXT-STEPS.txt`

The prep script strips saved WiFi profiles, SSH host keys, and `machine-id` from the base image before you flash it.

## Enter EDL

1. Power the device off.
2. Hold both volume buttons.
3. Power the device on while holding them.
4. Connect USB.

The device should enumerate in Qualcomm EDL mode.

## Flash Sequence

```powershell
# Stage 0: personalize the generic bootstrap image with your WiFi credentials
wsl -e bash -lc "cd /mnt/c/path/to/ubuntu-thinksmart && bash sources/scripts/prepare-pmos-ssh-bootstrap-image.sh --src /mnt/c/path/to/bootstrap-pmos-ssh-generic-qcom-msm8953.img --out /mnt/c/path/to/bootstrap-pmos-ssh-personalized-qcom-msm8953.img --ssid 'YOUR_WIFI_SSID' --psk 'YOUR_WIFI_PASSWORD' --login-password thinksmart"

# Stage 1: flash lk2nd and the personalized bootstrap image
python edl.py w boot prebuilt/lk2nd.img
python edl.py w system <your-personalized-bootstrap>.img

# Stage 2: let the bootstrap image join WiFi, SSH in, and run:
#   sudo /usr/local/sbin/apply-ubuntu-gpt.sh

# Stage 3: after GPT rewrite, run this command
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
3. run `python edl.py w system ubuntu-qcom-msm8953.img`

## First Boot

1. Unplug USB and power the device on normally.
2. Wait for Plasma Mobile to boot.
3. Connect the device to WiFi using the touchscreen.
4. Only after WiFi is configured, connect over SSH.

## Notes

- the bootstrap image is published as a GitHub Release asset at [bootstrap-2026-05-22](https://github.com/rickx/ubuntu-thinksmart/releases/tag/bootstrap-2026-05-22)
- the Ubuntu image is too large for a normal GitHub release asset and is hosted on [MEGA](https://mega.nz/file/UnsAjSyK#HmUBaxrxxT-Uej4K7eL1vsyYq0l6NygX_w3D5G6hnDo)
- GPT background notes belong in [partitions/LAYOUT.md](partitions/LAYOUT.md)
- generate your personalized bootstrap image locally with `sources/scripts/prepare-pmos-ssh-bootstrap-image.sh` before flashing it