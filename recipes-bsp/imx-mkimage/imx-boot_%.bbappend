# NXP's imx-boot recipe copies M4_DEFAULT_IMAGE from mcore-demos into the
# i.MX95 M7 boot-container input. Build Raven PX4 before that copy.
IMX_M4_DEMOS:raven-frdm-imx95 = "raven-px4-firmware:do_deploy"
