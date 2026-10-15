SUMMARY = "Pinned yolov7 COCO detector"
DESCRIPTION = "Optional yolov7 evaluation model with validated native CPU adapter, checksums and upstream license \
               notices"
HOMEPAGE = "https://github.com/WongKinYiu/yolov7"
BUGTRACKER = "https://github.com/WongKinYiu/yolov7/issues"
SECTION = "multimedia"
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://${BPN}-${PV}-LICENSE;md5=1ebbd3e34237af26da5dc08a4e440464"
YOLO_FILE = "yolov7.onnx"
YOLO_SOURCE_REV = "9a00c173a44bfcf3a58cc2a60e37deb6a1fb9e03"
# editorconfig-checker-disable
# Keep pinned download URLs and fetcher parameters intact.
SRC_URI = "https://github.com/WongKinYiu/yolov7/releases/download/v0.1/yolov7-nms-640.onnx;name=model;downloadfilename=${BPN}-${PV}-yolov7.onnx \
           https://raw.githubusercontent.com/WongKinYiu/yolov7/a207844b1ce82d204ab36d87d496728d3d2348e7/LICENSE.md;name=license;downloadfilename=${BPN}-${PV}-LICENSE \
           file://${BPN}.json"
# editorconfig-checker-enable
SRC_URI[model.sha256sum] = "db605e553548408ba4bfb9db636e47551ac73bedd0c86537d0afc3590c64e85f"
SRC_URI[license.sha256sum] = "3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986"

require yolo-model.inc
