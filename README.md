# meta-raven

Raven board support and ROS 2 image metadata for NXP BSP 6.18.37-2.1.0 / Wrynose.

- `conf/machine`: Raven machine configurations, including FRDM-IMX95.
- `conf/distro`: shared Raven distro.
- `recipes-core/images`: ROS 2 Jazzy image recipe.
- `tools/raven-setup-release.sh`: NXP Robotics Edge setup script adapted for this single layer.

```sh
repo init -u https://github.com/Swamy-BV/raven-manifest.git -b develop -m raven.xml
repo sync -c -j8 --no-tags
MACHINE=raven-frdm-imx95 DISTRO=raven SDKMACHINE=x86_64 \
    source ./raven-setup-release.sh -b build -r jazzy
```

The manifest creates the setup symlink. Use the single active `build` folder.
Setup follows NXP's `setup-environment`, configuration backups and
`hook_in_layer` flow. To re-enter the configured environment:

```sh
source ./setup-environment build
```

The setup script defaults to `EULA=1`, accepting NXP's license. Set `EULA=0`
to decline it. The license text is at `sources/meta-imx/LICENSE.txt`.
Setup does not compile. The image target is `bitbake raven-image-ros`.

The machine retains stock FRDM boot firmware and peripheral ownership. Raven
SM/PX4 packaging and shared-memory integration remain separate bring-up work.
