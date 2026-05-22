# Repo Log

This file keeps a few repo-level facts that do not fit naturally into the main docs.

## Current Public State

- Public repo: `https://github.com/rickx/ubuntu-thinksmart`
- Bootstrap release: `https://github.com/rickx/ubuntu-thinksmart/releases/tag/bootstrap-2026-05-22`
- Published Ubuntu image: `https://mega.nz/file/UnsAjSyK#HmUBaxrxxT-Uej4K7eL1vsyYq0l6NygX_w3D5G6hnDo`
- Final published login: `ubuntu` / `thinksmart`
- Final published hostname: `thinksmarter`

## Flashing Model

- Flashing is EDL-only on this device. fastboot is present but not usable for unsigned image flashing.
- The published first-landing flow is: flash `lk2nd`, flash a personalized copy of `bootstrap-pmos-ssh-generic-qcom-msm8953.img`, let it join WiFi, run `sudo /usr/local/sbin/apply-ubuntu-gpt.sh` over SSH, then run `python edl.py w system ubuntu-qcom-msm8953.img` after re-entering EDL.
- The generic pmOS bootstrap release asset has saved WiFi profiles, SSH host keys, and `machine-id` stripped before use.

## Useful Operational Notes

- `edl r PrimaryGPT` is not a valid read path here; use `edl printgpt` for inspection and `partitions/ubuntu_layout.sfdisk` for the documented GPT rewrite path.
- License split is final: `GPL-2.0-only` for driver/patches, `MIT` for userspace helpers, `CC-BY-4.0` for documentation.