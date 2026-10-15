SUMMARY = "COCO labels for optional YOLO detectors"
DESCRIPTION = "Pinned 80-class label ordering shared by YOLO model adapters"
HOMEPAGE = "https://huggingface.co/nxp/yolov4-tiny-imx"
BUGTRACKER = "https://github.com/NXP/eiq-model-zoo/issues"
SECTION = "multimedia"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://yolo-labels-${PV}-LICENSE;md5=56daa20283d6136bc769caf7e195a421"
# editorconfig-checker-disable
# Keep pinned download URLs and fetcher parameters intact.
SRC_URI = "https://huggingface.co/nxp/yolov4-tiny-imx/resolve/751ad986c32995149b987d9086547f83c8aa72af/coco-labels-2014_2017.txt;name=labels;downloadfilename=yolo-labels-${PV}.txt \
           https://huggingface.co/nxp/yolov4-tiny-imx/resolve/751ad986c32995149b987d9086547f83c8aa72af/LICENSE;name=license;downloadfilename=yolo-labels-${PV}-LICENSE"
# editorconfig-checker-enable
SRC_URI[labels.sha256sum] = "d7654b26101572841ed1cd80aa03aa60e35f1b8acb4aea6906c4066886f16e07"
SRC_URI[license.sha256sum] = "45e0719304d0c86ce5cbe628b086655ccebf0c23477a1436975e9dbcf1ac2bff"
S = "${UNPACKDIR}"

inherit allarch

do_install() {
    install -D -m 0644 ${S}/yolo-labels-${PV}.txt ${D}${datadir}/model-runner/models/yolo-labels.txt
    install -D -m 0644 ${S}/yolo-labels-${PV}-LICENSE ${D}${datadir}/model-runner/licenses/${PN}/LICENSE
}
FILES:${PN} += "${datadir}/model-runner"
