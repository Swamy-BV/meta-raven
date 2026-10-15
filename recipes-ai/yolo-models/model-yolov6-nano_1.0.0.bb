SUMMARY = "Pinned yolov6-nano COCO detector"
DESCRIPTION = "Optional yolov6-nano evaluation model with validated native CPU adapter, checksums and upstream license \
               notices"
HOMEPAGE = "https://github.com/meituan/YOLOv6"
BUGTRACKER = "https://github.com/meituan/YOLOv6/issues"
SECTION = "multimedia"
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://${BPN}-${PV}-LICENSE;md5=1ebbd3e34237af26da5dc08a4e440464"
YOLO_FILE = "yolov6n.onnx"
YOLO_SOURCE_REV = "7123c541e7d5c3e168ea535376e4f2fabfd6e66f"
# editorconfig-checker-disable
# Keep pinned download URLs and fetcher parameters intact.
SRC_URI = "https://github.com/meituan/YOLOv6/releases/download/0.3.0/yolov6n.onnx;name=model;downloadfilename=${BPN}-${PV}-yolov6n.onnx \
           https://raw.githubusercontent.com/meituan/YOLOv6/7123c541e7d5c3e168ea535376e4f2fabfd6e66f/LICENSE;name=license;downloadfilename=${BPN}-${PV}-LICENSE \
           file://${BPN}.json"
# editorconfig-checker-enable
SRC_URI[model.sha256sum] = "f716bde8432f6960d1133cbbebfe576d7f7b11b0222ef502b982ddefddaee709"
SRC_URI[license.sha256sum] = "3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986"

require yolo-model.inc
