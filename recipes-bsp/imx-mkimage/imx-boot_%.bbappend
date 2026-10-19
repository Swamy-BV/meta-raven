# NXP stages the M7 payload from mcore-demos after its deploy task.
IMX_M4_DEMOS:raven-frdm-imx95 = "raven-px4-firmware:do_deploy"

# Start Raven PX4 on M7 and U-Boot on A55 in the same boot container.
# Use the DDR address validated on FRDM-IMX95 B0 through Windows UUU.
# Keep the container load address paired with Raven's U-Boot defconfig.
MKIMAGE_EXTRA_ARGS:append:raven-frdm-imx95 = " \
    M7_DDR_ADDR=0x80000000 \
    SPL_LOAD_ADDR_M33_VIEW=0x88240000 \
"
