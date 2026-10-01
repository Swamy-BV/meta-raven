# meta-raven

Raven board support and ROS 2 image metadata for NXP BSP 6.18.37-2.1.0 / Wrynose.

- `conf/machine`: Raven machine configurations, including FRDM-IMX95.
- `conf/distro`: shared Raven distro.
- `recipes-core/images`: ROS 2 Jazzy image recipe.
- `tools/raven-setup-release.sh`: NXP Robotics Edge setup script adapted for this single layer.

```sh
repo init -u https://github.com/Swamy-BV/raven-manifest.git -b develop -m raven.xml
repo sync -c -j8 --no-tags
source ./raven-setup-release.sh -b build -r jazzy
```

The manifest creates the setup symlink. The script defaults to
`MACHINE=raven-frdm-imx95`, `DISTRO=raven`, and `SDKMACHINE=x86_64`.
Use the single active `build` folder.
Setup follows NXP's `setup-environment`, configuration backups and
`hook_in_layer` flow. To re-enter the configured environment:

```sh
source ./setup-environment build
```

The setup script defaults to `EULA=1`, accepting NXP's license. Set `EULA=0`
to decline it. The license text is at `sources/meta-imx/LICENSE.txt`.
Setup does not compile. Build the Linux image with `bitbake raven-image` and
the A/B rootfs, kernel, and FRDM DTB staging bundle with
`bitbake raven-image-swu`.
The image build also compiles the pinned Raven System Manager source with the
`raven_frdm_drone` partition and packs it into `imx-boot`.
Setup also limits BitBake to four tasks and two compile jobs to fit the 30 GiB
WSL build host. Adjust `build/conf/local.conf` after setup for a different host.

The U-Boot append builds redundant environments at 0x700000 and
0x704000, selects boot/rootfs A or B with `bootslot`, and rolls back after
three unconfirmed boots when `upgrade_available=1`. The SWU does not change
`bootslot` or flash `imx-boot` yet: first verify the target boot medium and
Linux device path, install the updated boot container through a recovery
path, and configure `fw_env.config` and boot confirmation for the hardware.
The current SWU selects partitions by label, so verify those labels resolve
to the booted disk before use, especially if SD and eMMC contain identical
Raven images.

The Raven SM partition defines peripheral ownership. PX4 packaging and
shared-memory integration remain separate bring-up work.
