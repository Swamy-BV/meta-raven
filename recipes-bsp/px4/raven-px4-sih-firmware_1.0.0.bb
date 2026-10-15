SUMMARY = "Raven Cortex-M7 bench-only PX4 SIH firmware"
DESCRIPTION = "Builds and deploys PX4 with simulated sensors and motor outputs for bench tests"
HOMEPAGE = "https://github.com/Swamy-BV/PX4-Autopilot"
BUGTRACKER = "https://github.com/Swamy-BV/PX4-Autopilot/issues"
SECTION = "bsp"

require raven-px4-firmware.inc
RAVEN_PX4_TARGET = "raven_frdm_sih"
# Explicitly built/deployed; the production imx-boot recipe retains default firmware.
