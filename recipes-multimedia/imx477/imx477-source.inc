# NXP camera software pack LF6.18.2_1.0.0, shared by kernel and ISP userspace.
IMX477_URI:raven-frdm-imx95 = "git://github.com/nxp-imx-support/imx-camera-sw-pack-source.git;protocol=https"
IMX477_URI_PARAMS:raven-frdm-imx95 = "branch=LF6.18.2_1.0.0;name=imx477;destsuffix=imx477"
IMX477_URI_SUBPATH:raven-frdm-imx95 = "subpath=imx95-camera-sw-pack-imx477;fsl-eula=true"
# nooelint: oelint.vars.srcurioptions NXP EULA fetcher extension used by the upstream camera pack.
SRC_URI:append:raven-frdm-imx95 = " ${IMX477_URI};${IMX477_URI_PARAMS};${IMX477_URI_SUBPATH}"
SRCREV_imx477:raven-frdm-imx95 = "a1bba481f39c7bc177893ba3262d7d892d2e9a57"
SRCREV_FORMAT:raven-frdm-imx95 = "default_imx477"
IMX477_SOURCE:raven-frdm-imx95 = "${UNPACKDIR}/imx477/i.MX95-15x15-FRDM-LPDDR4x"

# Works with both the kernel's shell patch task and userspace's Python patch task.
# nooelint: oelint.vars.noncoreoverride Hook runs only when the machine-specific camera pack is configured.
python raven_imx477_sources() {
    import shutil
    for entry in d.getVar("IMX477_STAGE_FILES").split():
        source, target = entry.split(":")
        shutil.copyfile(d.getVar("IMX477_SOURCE") + "/" + source,
                        d.getVar("S") + "/" + target)
}
raven_imx477_sources[doc] = "Stage the pinned NXP IMX477 sensor helper and tuning files before patching"
do_patch[prefuncs] += " ${@'raven_imx477_sources' if d.getVar('IMX477_SOURCE') else ''}"
