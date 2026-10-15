SUMMARY = "Pinned yolox-tiny COCO detector"
DESCRIPTION = "Optional yolox-tiny evaluation model with validated native CPU adapter, checksums and upstream license \
               notices"
HOMEPAGE = "https://github.com/Megvii-BaseDetection/YOLOX"
BUGTRACKER = "https://github.com/Megvii-BaseDetection/YOLOX/issues"
SECTION = "multimedia"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://${BPN}-${PV}-LICENSE;md5=caf47af132637aeda75ed7d6ee713b7b"
YOLO_FILE = "yolox-tiny.onnx"
YOLO_SOURCE_REV = "6ddff4824372906469a7fae2dc3206c7aa4bbaee"
# editorconfig-checker-disable
# Keep pinned download URLs and fetcher parameters intact.
SRC_URI = "https://github.com/Megvii-BaseDetection/YOLOX/releases/download/0.1.1rc0/yolox_tiny.onnx;name=model;downloadfilename=${BPN}-${PV}-yolox-tiny.onnx \
           https://raw.githubusercontent.com/Megvii-BaseDetection/YOLOX/e1052df71842031413f6030723c3607b839c80ce/LICENSE;name=license;downloadfilename=${BPN}-${PV}-LICENSE \
           file://${BPN}.json"
# editorconfig-checker-enable
SRC_URI[model.sha256sum] = "427cc366d34e27ff7a03e2899b5e3671425c262ea2291f88bb942bc1cc70b0f7"
SRC_URI[license.sha256sum] = "577c03d505ec80f667ebf96ebd0cc4f6825c817ca1088ee348c48aaabd51bd92"

require yolo-model.inc
