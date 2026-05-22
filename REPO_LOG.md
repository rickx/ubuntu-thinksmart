# Repo Log

This file preserves the small amount of repo-publication context that was still unique in the old workspace-level `REPO_PLAN.md`.

## Current Public State

- Public repo: `https://github.com/rickx/ubuntu-thinksmart`
- Bootstrap release: `https://github.com/rickx/ubuntu-thinksmart/releases/tag/bootstrap-2026-05-22`
- Published Ubuntu image: `https://mega.nz/file/UnsAjSyK#HmUBaxrxxT-Uej4K7eL1vsyYq0l6NygX_w3D5G6hnDo`
- Final published login: `ubuntu` / `thinksmart`
- Final published hostname: `thinksmarter`

## Flashing Model

- Flashing is EDL-only on this device. fastboot is present but not usable for unsigned image flashing.
- The published first-landing flow is: flash `lk2nd`, flash a personalized copy of `bootstrap-pmos-ssh-generic-qcom-msm8953.img`, let it join WiFi, run `sudo /usr/local/sbin/apply-ubuntu-gpt.sh` over SSH, then run `python edl.py w system ubuntu-qcom-msm8953.img` after re-entering EDL.
- The generic pmOS bootstrap release asset is public-safe: saved WiFi profiles, SSH host keys, and machine-id were stripped before publication.
- The unattended GPT auto-run path still exists in the repo, but it remains experimental and is not the default documented flow.

## Local-Only Artifacts Kept Out Of Release

- `rootfs_audio_proximity_venus.img` remains the untouched private backup image.
- `FINAL/rootfs/bootstrap-pmos-ssh-qcom-msm8953.img` remains local-only because it preserves a saved WiFi profile from the Felix base.
- The separate small Ubuntu SSH bootstrap path remains optional future work rather than a release requirement.

## Useful Operational Notes

- `edl r PrimaryGPT` is not a valid read path here; use `edl printgpt` for inspection and `partitions/ubuntu_layout.sfdisk` for the documented GPT rewrite path.
- The public docs in `README.md`, `FLASHING.md`, and `partitions/LAYOUT.md` now reflect the shipped flow and current release URLs.
- License split is final: `GPL-2.0-only` for driver/patches, `MIT` for userspace helpers, `CC-BY-4.0` for documentation.