SUMMARY = "Optional YOLO evaluation models"
DESCRIPTION = "YOLOX Nano/Tiny, NXP YOLOv4-tiny, YOLOv6 Nano and YOLOv7 models with native inference adapters"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "multimedia"
LICENSE = "AGPL-3.0-only"
# Local metadata uses AGPL-3.0-only; payload packages retain upstream licenses.
# nooelint: oelint.var.licenseremotefile Local Raven sources use the common AGPL-3.0-only text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"

inherit packagegroup

RDEPENDS:${PN} = "\
    model-yolo-labels \
    model-yolov4-tiny \
    model-yolov6-nano \
    model-yolov7 \
    model-yolox-nano \
    model-yolox-tiny \
    vision-engine \
"
