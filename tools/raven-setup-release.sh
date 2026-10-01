#!/bin/sh
#
# NXP Build Environment Setup Script
#
# Copyright 2025 NXP
#

. sources/meta-imx/tools/setup-utils.sh

CWD=`pwd`
PROGNAME="setup-environment"

exit_message()
{
    echo "To return to this build environment later please run:"
    echo "    source setup-environment <build_dir>"
}

usage()
{
    echo -e "\nDescription: raven-setup-release.sh will setup the bblayers and local.conf for an ROS2 build."
    echo -e "\nUsage: source raven-setup-release.sh
    Optional parameters: [-b build-dir] [-r jazzy] [-h]"
echo "
    * [-b build-dir]: Build directory, if unspecified, script uses 'build' as the output directory
    * [-r ROS2_DISTRO]: the ROS2 distro (jazzy)
    * [-h]: help
"
echo -e "\n
    Supported machines: "raven-frdm-imx95, imx95-15x15-lpddr4x-frdm, imx95-19x19-lpddr5-evk"

    Supported NXP's robotics-edge distros: `echo; ls sources/meta-raven/conf/distro/*.conf \
        | sed s/\.conf//g | sed -r 's/^.+\///' | xargs -I% echo -e "\t%"`
"
}

cleanup()
{
    echo "Cleaning up variables"
    unset CWD BUILD_DIR FSLDISTRO
    unset fsl_setup_help fsl_setup_error fsl_setup_flag
    unset usage clean_up
    unset ARM_DIR META_FSL_BSP_RELEASE
}

# get command line options
OLD_OPTIND=$OPTIND

unset FSLDISTRO
unset BUILD_DIR

# Read command line parameters
while getopts "b:r:h" nxp_setup_flag
do
    case $nxp_setup_flag in
        b) BUILD_DIR="$OPTARG";
           echo -e "\n Build directory is $BUILD_DIR" ;
           ;;
        r) ROS2_DISTRO="$OPTARG";
           echo -e "\n ROS2 distro is $ROS2_DISTRO" ;
           ;;
        h) nxp_setup_help='true';
           ;;
        \?) nxp_setup_error='true';
           ;;
    esac
done

shift $((OPTIND-1))
if [ $# -ne 0 ]; then
	nxp_setup_error=true
	echo -e "Invalid command line ending: '$@'"
fi
OPTIND=$OLD_OPTIND
if test $nxp_setup_help; then
	usage && cleanup && return 1
elif test $nxp_setup_error; then
	cleanup && return 1
fi

if [ -z "$DISTRO" ]; then
    if [ -z "$FSLDISTRO" ]; then
        FSLDISTRO='raven'
    fi
else
    FSLDISTRO="$DISTRO"
fi

if [ -z "$BUILD_DIR" ]; then
    BUILD_DIR='build'
fi

if [ -z "$MACHINE" ]; then
    echo setting to default machine
    MACHINE='raven-frdm-imx95'
fi

if [ -z "$SDKMACHINE" ]; then
    echo setting to default SDK machine
    SDKMACHINE='x86_64'
fi
RAVEN_SDKMACHINE=$SDKMACHINE

if [ -z "$ROS2_DISTRO" ]; then
    echo setting to default ROS2 distro
    ROS2_DISTRO='jazzy'
fi

if [ "$ROS2_DISTRO" != "jazzy" ]; then
    echo "Only jazzy is configured in the Raven layer."
    cleanup
    return 1
fi
RAVEN_EULA=${EULA:-1}
case "$RAVEN_EULA" in
    0|1) EULA=$RAVEN_EULA ;;
    *) echo "EULA must be 0 or 1"; return 1 ;;
esac
# Override the click-through in meta-freescale
FSL_EULA_FILE=$CWD/sources/meta-imx/LICENSE.txt

# Set up the basic yocto environment
DISTRO=$FSLDISTRO MACHINE=$MACHINE . ./$PROGNAME "$BUILD_DIR" || return 1

# Point to the current directory since the last command changed the directory to $BUILD_DIR
BUILD_DIR=.

if [ ! -e $BUILD_DIR/conf/local.conf ]; then
    echo -e "\n ERROR - No build directory is set yet. Run the 'setup-environment' script before running this script to create " $BUILD_DIR
    echo -e "\n"
    return 1
fi

# On the first script run, backup the local.conf file
# Consecutive runs, it restores the backup and changes are appended on this one.
if [ ! -e $BUILD_DIR/conf/local.conf.org ]; then
    cp $BUILD_DIR/conf/local.conf $BUILD_DIR/conf/local.conf.org
else
    cp $BUILD_DIR/conf/local.conf.org $BUILD_DIR/conf/local.conf
fi

# NXP restores local.conf.org on each setup. Apply the requested choice again.
sed -i '/^ACCEPT_FSL_EULA[[:space:]]*=/d' $BUILD_DIR/conf/local.conf
echo "ACCEPT_FSL_EULA = \"$RAVEN_EULA\"" >> $BUILD_DIR/conf/local.conf

echo >> $BUILD_DIR/conf/local.conf
echo "# Share cache" >> $BUILD_DIR/conf/local.conf
echo "SSTATE_DIR ?= \"\${BSPDIR}/sstate-cache\"" >> $BUILD_DIR/conf/local.conf

echo >> conf/local.conf
echo "# Switch to Debian packaging and include package-management in the image" >> conf/local.conf
echo "PACKAGE_CLASSES = \"package_deb\"" >> conf/local.conf
echo "EXTRA_IMAGE_FEATURES += \"package-management\"" >> conf/local.conf

if [ ! -e $BUILD_DIR/conf/bblayers.conf.org ]; then
    cp $BUILD_DIR/conf/bblayers.conf $BUILD_DIR/conf/bblayers.conf.org
else
    cp $BUILD_DIR/conf/bblayers.conf.org $BUILD_DIR/conf/bblayers.conf
fi

META_FSL_BSP_RELEASE="${CWD}/sources/meta-imx/meta-imx-bsp"

echo "" >> $BUILD_DIR/conf/bblayers.conf
echo "# i.MX Yocto Project Release layers" >> $BUILD_DIR/conf/bblayers.conf
hook_in_layer meta-imx/meta-imx-bsp
hook_in_layer meta-imx/meta-imx-sdk
hook_in_layer meta-imx/meta-imx-ml
hook_in_layer meta-imx/meta-imx-v2x
hook_in_layer meta-nxp-demo-experience
hook_in_layer meta-nxp-connectivity/meta-nxp-matter-baseline
hook_in_layer meta-nxp-connectivity/meta-nxp-openthread

echo "" >> $BUILD_DIR/conf/bblayers.conf
echo "BBLAYERS += \"\${BSPDIR}/sources/meta-arm/meta-arm\"" >> $BUILD_DIR/conf/bblayers.conf
echo "BBLAYERS += \"\${BSPDIR}/sources/meta-arm/meta-arm-toolchain\"" >> $BUILD_DIR/conf/bblayers.conf
echo "BBLAYERS += \"\${BSPDIR}/sources/meta-clang\"" >> $BUILD_DIR/conf/bblayers.conf
echo "BBLAYERS += \"\${BSPDIR}/sources/meta-openembedded/meta-gnome\"" >> $BUILD_DIR/conf/bblayers.conf
echo "BBLAYERS += \"\${BSPDIR}/sources/meta-openembedded/meta-networking\"" >> $BUILD_DIR/conf/bblayers.conf
echo "BBLAYERS += \"\${BSPDIR}/sources/meta-openembedded/meta-filesystems\"" >> $BUILD_DIR/conf/bblayers.conf
echo "BBLAYERS += \"\${BSPDIR}/sources/meta-openembedded/meta-perl\"" >> $BUILD_DIR/conf/bblayers.conf
echo "BBLAYERS += \"\${BSPDIR}/sources/meta-qt6\"" >> $BUILD_DIR/conf/bblayers.conf
echo "BBLAYERS += \"\${BSPDIR}/sources/meta-security/meta-parsec\"" >> $BUILD_DIR/conf/bblayers.conf
echo "BBLAYERS += \"\${BSPDIR}/sources/meta-security/meta-tpm\"" >> $BUILD_DIR/conf/bblayers.conf
echo "BBLAYERS += \"\${BSPDIR}/sources/meta-virtualization\"" >> $BUILD_DIR/conf/bblayers.conf
echo "BBLAYERS += \"\${BSPDIR}/sources/meta-freescale-ml\"" >> $BUILD_DIR/conf/bblayers.conf

echo "BBLAYERS += \"\${BSPDIR}/sources/meta-browser/meta-chromium\"" >> $BUILD_DIR/conf/bblayers.conf

echo -e "\n# Robotics Edge ROS2 layers" >> $BUILD_DIR/conf/bblayers.conf
echo "BBLAYERS += \" \${BSPDIR}/sources/meta-ros/meta-ros2\"" >> $BUILD_DIR/conf/bblayers.conf
echo "BBLAYERS += \" \${BSPDIR}/sources/meta-ros/meta-ros-common\"" >> $BUILD_DIR/conf/bblayers.conf

echo "ROS_DISTRO = \"$ROS2_DISTRO\"" >> $BUILD_DIR/conf/local.conf
echo "SDKMACHINE = \"$RAVEN_SDKMACHINE\"" >> $BUILD_DIR/conf/local.conf
echo "BB_NUMBER_THREADS = \"4\"" >> $BUILD_DIR/conf/local.conf
echo "PARALLEL_MAKE = \"-j 2\"" >> $BUILD_DIR/conf/local.conf
echo "BBLAYERS += \" \${BSPDIR}/sources/meta-ros/meta-ros2-jazzy\"" >> $BUILD_DIR/conf/bblayers.conf
echo -e "\n# Raven layer" >> $BUILD_DIR/conf/bblayers.conf
hook_in_layer meta-raven

cd  $BUILD_DIR
cleanup
exit_message
echo "Raven image target: bitbake raven-image-ros"
