# Camera source and data provenance

SPDX-License-Identifier: CC-BY-4.0

New driver implementation and Kbuild sources are GPL-2.0-only; new userspace
Python/shell scripts and the systemd unit are MIT. New documentation is
CC-BY-4.0. Existing notices in third-party files take precedence over the
repository directory defaults.

The numeric register sequences in `../driver/s5kc505a/s5kc505a_regs.h` were
decoded from Lenovo CD-18781Y firmware's proprietary
`libmmcamera_s5kc505a.so`. The recorded library SHA256 prefix is
`3d90f2686b9a32da`; a full digest was not available in the recovered header.
The header was supplied with a GPL-2.0-only notice; that notice concerns the
contributed source and does not purport to license Lenovo's original binary.

`tools/s5kc505a_tuning.json` contains numeric reference values decoded from
Lenovo CD-18781Y firmware 210228.109's `libchromatix_s5kc505a_*.so`,
chromatix version `0x0309`. `config/s5kc505a.yaml` and
`tools/libcamera-ccm-lsc-sections.yaml` derive their CCM/LSC values from that
JSON. The JSON also retains reference AWB ratios and gamma data used by the
offline processing tool. No rights to Lenovo's proprietary libraries are
asserted here. Maintainers must decide whether to accept or separately host
these decoded numeric data; inclusion is not a claim that they were released
by Lenovo under GPL or MIT. No vendor ELF library, firmware blob or PDF is
added by this contribution.

The redistributed libcamera mail patches retain their LGPL-2.1-or-later
source notices. They are third-party material under
`../patches/libcamera/` and are not relicensed GPL by their directory.
The board DTS patch preserves its target file's BSD-3-Clause notice.

No unit-specific `residual.json`, `otp_dump.*`, RAW capture or private signing
key is included. The supplied userspace archive was
`thinksmart-camera-userspace-20261107.tar.gz`, SHA256
`6e2ce0981bf944386f2417b2fbe5c4643fdb6720ae2c3d224d7887f9d4ba77b5`.
The distributed YAML was regenerated from the reference JSON without reading
the archive's residual or OTP files. Therefore its lens-shading values differ
from the per-unit tuning used for the reported hardware measurements.
