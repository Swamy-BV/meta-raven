# meta-raven

Raven Yocto layer and build entry point for FRDM-IMX95 development. NXP BSP 6.18.37-2.1.0,
Yocto Wrynose, ROS 2 Jazzy. Use a Linux filesystem for the workspace.

```sh
git clone -b develop https://github.com/Swamy-BV/meta-raven.git
./meta-raven/scripts/sync.sh /home/dev/raven/linux
./meta-raven/scripts/setup.sh /home/dev/raven/linux
# Build only when desired; read sources/meta-imx/LICENSE.txt before accepting.
ACCEPT_FSL_EULA=1 ./meta-raven/scripts/build.sh /home/dev/raven/linux
```

This layer pins the manifest commit in `release.env`. `raven.xml` contains all pinned NXP release projects and meta-ros, and follows
only meta-raven's develop branch. It is shared across boards; board selection
belongs in meta-raven machine configurations. Setup alone leaves the NXP
license unaccepted and does not invoke a build. Each build writes `resolved-manifest.xml` to
record the exact revisions; use that file for a reproducible release.

Image: `raven-image-ros`; machine: `raven-frdm-imx95`.
The image includes ROS base, C++ talker/listener demos and SSH, without a
desktop. NXP's development configuration allows an empty root password.
Boot firmware and peripheral ownership still use the stock FRDM baseline.
Raven SM, PX4 loading and shared memory are not integrated in this image yet.
Compiling this image does not flash the board.
