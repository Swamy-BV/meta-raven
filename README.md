# meta-raven

Raven board support and ROS 2 image metadata for NXP BSP 6.18.37-2.1.0 / Wrynose.

- `conf/machine`: Raven machine configurations, including FRDM-IMX95.
- `conf/distro`: shared Raven distro.
- `recipes-core/images`: ROS 2 Jazzy image recipe.
- `tools/raven-setup-release.sh`: NXP Robotics Edge setup script adapted for this single layer.

```sh
repo init -u https://github.com/Swamy-BV/raven-manifest.git -b develop -m raven.xml
repo sync -c -j8 --no-tags
MACHINE=raven-frdm-imx95 DISTRO=raven EULA=0 SDKMACHINE=x86_64 \
    source ./raven-setup-release.sh -b build -r jazzy
```

The manifest creates the setup symlink. Use the single active `build` folder.
Setup follows NXP's `setup-environment`, configuration backups and
`hook_in_layer` flow. To re-enter the configured environment:

```sh
source ./setup-environment build
```

`EULA=0` leaves the NXP license unaccepted. Read `sources/meta-imx/LICENSE.txt`
before accepting it. Setup does not compile. When requested and after license
acceptance, the image target is `bitbake raven-image-ros`.

The machine retains stock FRDM boot firmware and peripheral ownership. Raven
SM/PX4 packaging and shared-memory integration remain separate bring-up work.
