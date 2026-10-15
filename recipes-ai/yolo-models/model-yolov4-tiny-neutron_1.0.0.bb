SUMMARY = "i.MX95 Neutron compiled YOLOv4-tiny detector"
DESCRIPTION = "NXP precompiled i.MX95 detector for the Neutron NPU with pinned checksums and upstream license notices"
HOMEPAGE = "https://huggingface.co/nxp/yolov4-tiny-imx"
BUGTRACKER = "https://github.com/NXP/eiq-model-zoo/issues"
SECTION = "multimedia"
LICENSE = "MIT & LicenseRef-YOLO"
LIC_FILES_CHKSUM = "\
    file://${BPN}-${PV}-LICENSE;md5=56daa20283d6136bc769caf7e195a421 \
    file://${BPN}-${PV}-MODEL_LICENSE;md5=624666acaf862eaf3f25a2e3f4ce0bf3 \
"
YOLO_FILE = "yolov4-tiny-neutron.tflite"
YOLO_SOURCE_REV = "751ad986c32995149b987d9086547f83c8aa72af"
# editorconfig-checker-disable
# Keep pinned download URLs and fetcher parameters intact.
SRC_URI = "https://huggingface.co/nxp/yolov4-tiny-imx/resolve/751ad986c32995149b987d9086547f83c8aa72af/imx95/3.2.1/yolov4-tiny_416_quant_converted.tflite;name=model;downloadfilename=${BPN}-${PV}-yolov4-tiny-neutron.tflite \
           https://huggingface.co/nxp/yolov4-tiny-imx/resolve/751ad986c32995149b987d9086547f83c8aa72af/LICENSE;name=license;downloadfilename=${BPN}-${PV}-LICENSE \
           https://huggingface.co/nxp/yolov4-tiny-imx/resolve/751ad986c32995149b987d9086547f83c8aa72af/MODEL_LICENSE;name=modellicense;downloadfilename=${BPN}-${PV}-MODEL_LICENSE \
           file://${BPN}.json"
# editorconfig-checker-enable
NO_GENERIC_LICENSE[LicenseRef-YOLO] = "${BPN}-${PV}-MODEL_LICENSE"
YOLO_LICENSE_FILES = "${BPN}-${PV}-LICENSE ${BPN}-${PV}-MODEL_LICENSE"
SRC_URI[modellicense.sha256sum] = "6ab19c96c892dee8579fa2717b2b9210af73e66e7f6a87e027bae20eb862d318"
SRC_URI[model.sha256sum] = "5399df8f990a16d6f1393f88f9dd5bb9932a9ed91d678754f417c10328720518"
SRC_URI[license.sha256sum] = "45e0719304d0c86ce5cbe628b086655ccebf0c23477a1436975e9dbcf1ac2bff"

require yolo-model.inc
