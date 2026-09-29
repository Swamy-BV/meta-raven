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
    echo -e "\nDescription: robotics-edge-setup.sh will setup the bblayers and local.conf for an ROS2 build."
    echo -e "\nUsage: source robotics-edge-setup.sh
    Optional parameters: [-b build-dir] [-r jazzy] [-h]"
echo "
    * [-b build-dir]: Build directory, if unspecified, script uses 'build' as the output directory
    * [-r ROS2_DISTRO]: the ROS2 distro (jazzy or rolling)
    * [-h]: help
"
echo -e "\n
    Supported machines: "imx8mpevk, imx95evk"

    Supported NXP's robotics-edge distros: `echo; ls sources/meta-ros2-app/conf/distro/robotics-edge*.conf \
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
while getopts "k:r:t:b:e:gh" nxp_setup_flag
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
        FSLDISTRO='robotics-edge'
    fi
else
    FSLDISTRO="$DISTRO"
fi

if [ -z "$BUILD_DIR" ]; then
    BUILD_DIR='build'
fi

if [ -z "$MACHINE" ]; then
    echo setting to default machine
    MACHINE='imx95evk'
fi

if [ -z "$ROS2_DISTRO" ]; then
    echo setting to default ROS2 distro
    ROS2_DISTRO='jazzy'
fi

# Override the click-through in meta-freescale
FSL_EULA_FILE=$CWD/sources/meta-imx/LICENSE.txt

# Set up the basic yocto environment
DISTRO=$FSLDISTRO MACHINE=$MACHINE . ./$PROGNAME $BUILD_DIR

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

echo -e "\n# i.MX Matter Advanced Layers" >> $BUILD_DIR/conf/bblayers.conf
hook_in_layer meta-nxp-connectivity/meta-nxp-matter-advanced
hook_in_layer meta-nxp-connectivity/meta-nxp-otbr

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

echo "BBLAYERS += \"\${BSPDIR}/sources/meta-browser/meta-chromium\"" >> $BUILD_DIR/conf/bblayers.conf

echo -e "\n# Real-time Edge layers" >> $BUILD_DIR/conf/bblayers.conf
echo "BBLAYERS += \"\${BSPDIR}/sources/meta-rtos-industrial\"" >> $BUILD_DIR/conf/bblayers.conf
echo "BBLAYERS += \"\${BSPDIR}/sources/meta-nxp-harpoon\"" >> $BUILD_DIR/conf/bblayers.conf
echo "BBLAYERS += \"\${BSPDIR}/sources/meta-nxp-avb\"" >> $BUILD_DIR/conf/bblayers.conf
echo "BBLAYERS += \"\${BSPDIR}/sources/meta-real-time-edge\"" >> $BUILD_DIR/conf/bblayers.conf

echo -e "\n# Robotics Edge ROS2 layers" >> $BUILD_DIR/conf/bblayers.conf
echo "BBLAYERS += \" \${BSPDIR}/sources/meta-ros/meta-ros2\"" >> $BUILD_DIR/conf/bblayers.conf
echo "BBLAYERS += \" \${BSPDIR}/sources/meta-ros/meta-ros-common\"" >> $BUILD_DIR/conf/bblayers.conf

case $ROS2_DISTRO in
    jazzy)
        echo "ROS_DISTRO = \"jazzy\"" >> $BUILD_DIR/conf/local.conf
        echo "BBLAYERS += \" \${BSPDIR}/sources/meta-ros/meta-ros2-jazzy\"" >> $BUILD_DIR/conf/bblayers.conf
        echo "BBLAYERS += \" \${BSPDIR}/sources/meta-ros2-nxp/meta-ros2-jazzy\"" >> $BUILD_DIR/conf/bblayers.conf
    ;;
    rolling)
        echo "ROS_DISTRO = \"rolling\"" >> $BUILD_DIR/conf/local.conf
        echo "BBLAYERS += \" \${BSPDIR}/sources/meta-ros/meta-ros2-rolling\"" >> $BUILD_DIR/conf/bblayers.conf
        echo "BBLAYERS += \" \${BSPDIR}/sources/meta-ros2-nxp/meta-ros2-rolling\"" >> $BUILD_DIR/conf/bblayers.conf
    ;;
esac

echo "BBLAYERS += \" \${BSPDIR}/sources/meta-ros2-nxp/meta-ros-common\"" >> $BUILD_DIR/conf/bblayers.conf
echo "BBLAYERS += \" \${BSPDIR}/sources/meta-ros2-nxp/meta-ros2\"" >> $BUILD_DIR/conf/bblayers.conf
echo "BBLAYERS += \" \${BSPDIR}/sources/meta-ros2-bsp\"" >> $BUILD_DIR/conf/bblayers.conf
echo "BBLAYERS += \" \${BSPDIR}/sources/meta-robotics-edge\"" >> $BUILD_DIR/conf/bblayers.conf

cd  $BUILD_DIR
cleanup
exit_message
