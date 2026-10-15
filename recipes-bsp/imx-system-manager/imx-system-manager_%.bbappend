# Keep the SM source reproducible and update this revision deliberately.
IMX_SYSTEM_MANAGER_SRC:raven-frdm-imx95 = "git://github.com/Swamy-BV/imx-sm.git;protocol=https"
SRCBRANCH:raven-frdm-imx95 = "develop"
# nooelint: oelint.append.protvars.SRCREV Raven intentionally pins its SM fork for reproducible firmware.
SRCREV:raven-frdm-imx95 = "cba46fe8ede4bb542bfca7b59c7d1dcb7fa3c78e"
