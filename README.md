# meta-raven

Raven is an experimental drone project based on NXP i.MX95. This Yocto layer
builds its Linux image for the FRDM-IMX95 development board; a separate machine
configuration will be added for custom Raven hardware.

## Status

The `raven-image` build passes. It produces a console Linux image with ROS 2
Jazzy, a Raven System Manager partition, PX4 M7 firmware in `imx-boot`, and
an A/B eMMC layout. The image has **not yet been validated on hardware**.

Track bring-up in GitHub issues:

1. [Boot and validate the FRDM image](https://github.com/Swamy-BV/meta-raven/issues/1)
2. [Bring up sensors and PX4 I/O](https://github.com/Swamy-BV/meta-raven/issues/2)
3. [Validate updates and migrate to custom hardware](https://github.com/Swamy-BV/meta-raven/issues/3)

## Build

```sh
repo init -u https://github.com/Swamy-BV/raven-manifest.git -b develop -m raven.xml
repo sync -c -j8 --no-tags
source ./raven-setup-release.sh -b build -r jazzy
bitbake raven-image
```

The manifest links the setup script from this layer. It defaults to
`raven-frdm-imx95`, the `raven` distro, ROS 2 Jazzy, and the `build` directory.
To return to the environment, run `source ./setup-environment build`.
The script accepts NXP's EULA by default; set `EULA=0` to decline it.

The factory build deploys matching `.wic.zst` and `.wic.bmap` files. Keep them
together when flashing an SD card with `bmaptool copy`, or pass `-bmap` to UUU
when flashing eMMC. Confirm the target device before writing the image.

`bitbake raven-px4-firmware` builds the M7 firmware alone.
`bitbake raven-image-swu` builds an A/B update bundle. It includes the rootfs,
kernel, and FRDM device tree. The bundle does not update `imx-boot`.
Boot-container updates, boot-slot confirmation, rollback, and inter-core
communication need hardware validation.
