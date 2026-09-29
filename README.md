# meta-raven

Raven Yocto metadata based on NXP BSP 6.18.37-2.1.0, Wrynose and ROS 2 Jazzy.
The repository follows meta-imx's separation of board support and distro/images:

| Layer | Contents |
| --- | --- |
| meta-raven-bsp | Machine configurations and board-specific firmware/kernel integration |
| meta-raven-sdk | Shared Raven distro, ROS image and setup templates |

The shared `raven.xml` pins the NXP and ROS sources. Only meta-raven follows
develop. Boards are selected by MACHINE; each defaults to its own build folder.

```sh
mkdir raven-workspace
cd raven-workspace
repo init -u https://github.com/Swamy-BV/raven-manifest.git -b develop -m raven.xml
repo sync -c -j8 --no-tags
MACHINE=raven-frdm-imx95 DISTRO=raven source ./raven-setup-release.sh
```

Setup creates configuration only. It leaves ACCEPT_FSL_EULA=0 unless you
explicitly supply ACCEPT_FSL_EULA=1 after reading sources/meta-imx/LICENSE.txt.
Run setup again with the same selections to re-enter an existing build folder.
Use -b to choose a different folder. Keep separate folders for different boards
and distros; downloads and sstate-cache are shared by default.

The current Raven machine inherits NXP's FRDM-IMX95 configuration. A future
custom board gets its own machine file, sharing SoC includes where appropriate.
Place board-specific modifications in machine-scoped recipe appends/files.
The BSP layer does not depend on ROS; the SDK layer supplies the ROS image.

`raven-image-ros` includes ROS base, C++ talker/listener demos and SSH without a
desktop. Compile only when desired with `bitbake raven-image-ros` after license
acceptance. Boot firmware and peripheral ownership still use stock FRDM
settings; Raven SM, PX4 loading and shared memory integration remain separate
bring-up work. No scripts automatically compile or flash the board.
