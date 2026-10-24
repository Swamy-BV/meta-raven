# WFB-ng integration

`raven-image` includes `packagegroup-raven-wfb`. The radio service is disabled
until a dedicated adapter and a unique pairing key have been provisioned.

Select one external driver in `local.conf` or the machine/distro configuration:

```bitbake
VIRTUAL-RUNTIME_wfb-radio = "rtl8812au-wfb" # default
# VIRTUAL-RUNTIME_wfb-radio = "rtl8812eu-wfb"
# VIRTUAL-RUNTIME_wfb-radio = "" # software-only image
```

Build with `bitbake packagegroup-raven-wfb`, or build the normal SWU image.
Both drivers use the standard `module` class. Their packages depend on the
matching kernel ABI; rebuild them whenever the kernel changes. Selecting a
different radio does not change the NXP kernel source or configuration.

Recipe ownership is separate: `wfb-ng` builds transport userspace and installs
radio/integration configuration; `rtl8812au-wfb` and `rtl8812eu-wfb` each build
one external driver. The package group selects the driver. `media-capture`
owns both video codecs; `camera-mavlink` reports the selected codec.

Use a dedicated adapter outside normal network management. Start
`wfb-ng@<interface>.service` with its actual interface name. The service checks
for the interface and the airborne key before starting. Do not select the
board's normal management Wi-Fi interface.

Generate a unique pair with upstream `wfb_keygen` in a private directory
(`umask 077`). Provision `drone.key` in the board's persistent WFB-ng state
directory and `gs.key` on the ground receiver. Never put keys into the image
or Git. Systemd/tmpfiles creates the private state directory under `var/lib`.

The airborne profile receives video on loopback UDP 5602 and MAVLink on 14560.
The packaged router fragment forwards PX4 traffic to that local MAVLink port;
`mavlink-router` retains sole ownership of the RPMsg endpoint. No DDS changes.

The optional `media-capture.yaml` under the installed WFB-ng data directory is
a second ROS parameter file, applied after the board's existing camera config.
It sets only the radio destination and inherits the codec from `media-capture`;
streaming remains an explicit camera control operation. The camera defaults
to H.264. To select H.265, set `preview_codec` and `video_codec` to `h265` in
the camera configuration and use an H.265 receiver; select `h264` for H.264.
Recording and streaming share one encoder and require matching active profiles.
The existing camera service is not automatically restarted or redirected by
installing this package. It continues to own capture, hardware
encoding, recording and ROS image publication.

`ground.cfg` in the same data directory is the matching receiver example.
Select it with `WIFIBROADCAST_CFG` and run upstream
`wfb-server --profiles gs --wlans <interface>`. Ground video goes to local
UDP 5600; MAVLink goes to 14550 for QGroundControl. Both ends must use matching
channel, region, link-domain and pairing settings. Initial encoding is fixed
bitrate; automatic encoder adaptation is not implemented.

Validated: both driver builds against NXP Linux 6.18, native module load/unload,
and Yocto package QA. Native userspace, profile parsing, key generation and
14 upstream encryption/FEC loopback tests also passed on i.MX95. Two RTL8812AU
adapters (USB 0bda:8812) also passed a paired 5.8 GHz bench packet test from
i.MX95 to a USBIP/WSL receiver: 1,000/1,000 payloads intact after session-key
acquisition. The cold-start test received 768/1,000 before key acquisition
completed. IMX477 hardware H.264 video also reached the laptop at 720p/15 fps,
about 2 Mbit/s, with a verified 68.5-second recording. A separate latency run
measured approximately 39 ms median / 293 ms p95 packet delivery and 0.7% packet
loss after FEC. These short-range USBIP/WSL results need latency tuning; they
exclude exposure, encoding and display delay. Normal streaming used 14.3% of
the board's six-core CPU capacity, including existing perception services.
Glass-to-glass latency and range remain untested.

Hardware H.265 also passed 720p/15 fps radio reception, ROS state/image
publication, and onboard recording. The received 38.5-second HEVC recording
decoded successfully. Normal board CPU was 13.5% of six-core capacity; packet
delivery was approximately 48 ms median / 271 ms p95 with 0.9% packet loss.
This used the same 2 Mbit/s target, not a quality-matched bitrate comparison.
