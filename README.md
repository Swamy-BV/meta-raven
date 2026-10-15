# meta-raven

Yocto integration for Raven, a modular drone platform. Linux services run on A55;
PX4/NuttX runs on M7. Development starts on FRDM-IMX95.

## Build

```sh
repo init -u https://github.com/Swamy-BV/raven-manifest.git -b develop -m raven.xml
repo sync -c -j8 --no-tags
source ./raven-setup-release.sh -b build
bitbake raven-image
```

Defaults: `raven-frdm-imx95`, distro `raven`, ROS 2 Jazzy and `build`.
Return with `source ./setup-environment build`.
Build updates with `bitbake raven-image-swu`.
Setup accepts NXP's EULA; use `EULA=0` to decline.

Component recipes, code and deployment files live together under `src/`.
See the [component interaction diagram](src/README.md).

## License

Copyright (c) 2026 Swamy BV. Raven-owned code and layer metadata are licensed
under AGPL-3.0-only; see [LICENSE](LICENSE). Third-party software, models and
upstream-derived patches retain their respective licenses and notices.
