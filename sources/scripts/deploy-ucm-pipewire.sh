#!/bin/sh
set -eu

# Deploy UCM2 + WirePlumber policy for cd18781y/cd-18781y
# Run on device as root (via scripts/ssh_ubuntu.py)

UCM_BASE="/usr/share/alsa/ucm2"
WP_DIR="/etc/wireplumber/main.lua.d"

mkdir -p "$UCM_BASE/conf.d/cd-18781y"
mkdir -p "$UCM_BASE/conf.d/cd18781y"
mkdir -p "$UCM_BASE/Lenovo/cd-18781y"
mkdir -p "$UCM_BASE/Qualcomm/cd-18781y"
mkdir -p "$WP_DIR"

cat > "$UCM_BASE/conf.d/cd-18781y/cd-18781y.conf" << 'EOF_CONF_D_DASH'
Syntax 6

SectionUseCase."HiFi" {
	File "/Lenovo/cd-18781y/HiFi.conf"
	Comment "Play and record HiFi quality Music"
}

Include.card-init.File "/lib/card-init.conf"
Include.ctl-remap.File "/lib/ctl-remap.conf"

BootSequence [
	cset "name='TX1 Digital Volume' 115"
	cset "name='TX2 Digital Volume' 115"
]
EOF_CONF_D_DASH

cat > "$UCM_BASE/conf.d/cd18781y/cd18781y.conf" << 'EOF_CONF_D_NODASH'
Syntax 6

SectionUseCase."HiFi" {
	File "/Lenovo/cd-18781y/HiFi.conf"
	Comment "Play and record HiFi quality Music"
}

Include.card-init.File "/lib/card-init.conf"
Include.ctl-remap.File "/lib/ctl-remap.conf"

BootSequence [
	cset "name='TX1 Digital Volume' 115"
	cset "name='TX2 Digital Volume' 115"
]
EOF_CONF_D_NODASH

cat > "$UCM_BASE/Lenovo/cd-18781y/HiFi.conf" << 'EOF_HIFI'
SectionVerb {
	EnableSequence [
		cset "name='QUAT_MI2S_RX Audio Mixer MultiMedia1' 1"
		cset "name='MultiMedia2 Mixer TERT_MI2S_TX' 1"
	]
	DisableSequence [
		cset "name='QUAT_MI2S_RX Audio Mixer MultiMedia1' 0"
		cset "name='MultiMedia2 Mixer TERT_MI2S_TX' 0"
	]

	Value {
		TQ "HiFi"
	}
}

SectionDevice."Speaker" {
	Comment "Speaker"

	Value {
		PlaybackPCM "hw:${CardId},0"
		PlaybackPriority 100
		PlaybackChannels 2
	}
}

SectionDevice."Microphone" {
	Comment "Stereo Microphone"

	EnableSequence [
		cset "name='DEC1 MUX' DMIC1"
		cset "name='DEC2 MUX' DMIC2"
		cset "name='CIC1 MUX' DMIC"
		cset "name='CIC2 MUX' DMIC"
	]

	DisableSequence [
		cset "name='DEC1 MUX' ZERO"
		cset "name='DEC2 MUX' ZERO"
	]

	Value {
		CapturePCM "hw:${CardId},1"
		CaptureChannels 2
		CapturePriority 100
	}
}
EOF_HIFI

cat > "$UCM_BASE/Qualcomm/cd-18781y/cd-18781y.conf" << 'EOF_QCOM_CARD'
Syntax 3

SectionUseCase."HiFi" {
	File "/Lenovo/cd-18781y/HiFi.conf"
	Comment "Play and record HiFi quality Music"
}
EOF_QCOM_CARD

cp -f "$UCM_BASE/Lenovo/cd-18781y/HiFi.conf" "$UCM_BASE/Qualcomm/cd-18781y/HiFi.conf"

cat > "$WP_DIR/99-cd18781y-alsa.lua" << 'EOF_WP'
alsa_monitor.rules = alsa_monitor.rules or {}

-- Prefer ACP routing and disable mmap probes that fail on this card.
table.insert(alsa_monitor.rules, {
  matches = {
    {
      { "device.name", "equals", "alsa_card.platform-c051000.sound-card" },
    },
  },
  apply_properties = {
    ["api.alsa.use-acp"] = true,
    ["api.alsa.use-ucm"] = true,
    ["api.acp.auto-profile"] = true,
    ["api.acp.auto-port"] = true,
    ["api.alsa.disable-mmap"] = true,
  },
})

-- Keep the speaker node active and force stable S16 playback format.
table.insert(alsa_monitor.rules, {
	matches = {
		{ { "node.name", "equals", "alsa_output.platform-c051000.sound-card.HiFi__hw_cd18781y_0__sink" } },
		{ { "node.name", "equals", "alsa_output.platform-c051000.sound-card.playback.0.0" } },
	},
	apply_properties = {
		["node.pause-on-idle"] = false,
		["session.suspend-timeout-seconds"] = 0,
		["audio.format"] = "S16LE",
		["audio.rate"] = 48000,
		["audio.channels"] = 2,
	},
})

-- Hide non-speaker endpoints to reduce wrong default route selection.
table.insert(alsa_monitor.rules, {
  matches = {
    { { "node.name", "equals", "alsa_output.platform-c051000.sound-card.playback.2.0" } },
    { { "node.name", "equals", "alsa_output.platform-c051000.sound-card.playback.4.0" } },
    { { "node.name", "equals", "alsa_input.platform-c051000.sound-card.capture.4.0" } },
  },
  apply_properties = {
    ["node.disabled"] = true,
  },
})
EOF_WP

echo "=== UCM summary ==="
ls -la "$UCM_BASE/conf.d/cd-18781y" "$UCM_BASE/conf.d/cd18781y" "$UCM_BASE/Lenovo/cd-18781y" "$UCM_BASE/Qualcomm/cd-18781y"

echo "=== WirePlumber summary ==="
ls -la "$WP_DIR/99-cd18781y-alsa.lua"

echo "=== Restarting user audio services ==="
sudo -u user XDG_RUNTIME_DIR=/run/user/1000 systemctl --user restart wireplumber pipewire pipewire-pulse || true
sleep 2

sudo -u user XDG_RUNTIME_DIR=/run/user/1000 wpctl status | sed -n '1,120p' || true

echo "DONE"
