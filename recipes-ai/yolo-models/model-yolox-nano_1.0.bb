SUMMARY = "Pinned yolox-nano COCO detector"
DESCRIPTION = "Optional yolox-nano evaluation model with validated native CPU adapter, checksums and upstream license \
               notices"
HOMEPAGE = "https://github.com/Megvii-BaseDetection/YOLOX"
BUGTRACKER = "https://github.com/Megvii-BaseDetection/YOLOX/issues"
SECTION = "multimedia"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://${BPN}-${PV}-LICENSE;md5=caf47af132637aeda75ed7d6ee713b7b"
YOLO_FILE = "yolox-nano.onnx"
YOLO_SOURCE_REV = "6ddff4824372906469a7fae2dc3206c7aa4bbaee"
# editorconfig-checker-disable
# Keep pinned download URLs and fetcher parameters intact.
SRC_URI = "https://github.com/Megvii-BaseDetection/YOLOX/releases/download/0.1.1rc0/yolox_nano.onnx;name=model;downloadfilename=${BPN}-${PV}-yolox-nano.onnx \
           https://raw.githubusercontent.com/Megvii-BaseDetection/YOLOX/e1052df71842031413f6030723c3607b839c80ce/LICENSE;name=license;downloadfilename=${BPN}-${PV}-LICENSE \
           file://${BPN}.json"
# editorconfig-checker-enable
SRC_URI[model.sha256sum] = "c789161ed43c8269fcd4e67c67eeeb4e80c622da2eb296a20bc6007bd18a0b7d"
SRC_URI[license.sha256sum] = "577c03d505ec80f667ebf96ebd0cc4f6825c817ca1088ee348c48aaabd51bd92"

require yolo-model.inc
