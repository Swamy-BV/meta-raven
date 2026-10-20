DESCRIPTION = "Pinned INT8 COCO detector compiled with the board-compatible Neutron runtime"
HOMEPAGE = "https://huggingface.co/EdgeFirst/yolov8-det"
BUGTRACKER = "https://huggingface.co/EdgeFirst/yolov8-det/discussions"
SECTION = "multimedia"
LICENSE = "AGPL-3.0-only"
LIC_FILES_CHKSUM = "\
    file://${BPN}-${PV}-README.md;beginline=2;endline=2;md5=25624168f38a72bee22423b376fb4933 \
"
YOLO_FILE = "yolov8${YOLO_SIZE}-neutron.tflite"
YOLO_SOURCE_REV = "7fb4413528af0e22c489e7b85839df6e21fa221f"

DEPENDS += "nxp-neutron-compiler-native"

# editorconfig-checker-disable
# Keep the immutable upstream model URL and fetcher parameters intact.
SRC_URI = "https://huggingface.co/EdgeFirst/yolov8-det/resolve/${YOLO_SOURCE_REV}/tflite/yolov8${YOLO_SIZE}-det-int8.tflite;name=model;downloadfilename=${BPN}-${PV}-source.tflite \
           https://huggingface.co/EdgeFirst/yolov8-det/resolve/${YOLO_SOURCE_REV}/README.md;name=license;downloadfilename=${BPN}-${PV}-README.md \
           file://${BPN}.json"
# editorconfig-checker-enable
SRC_URI[model.sha256sum] = "${YOLO_MODEL_SHA256}"
SRC_URI[license.sha256sum] = "a55b8f710f31e4e005640fc486265be1bbd3b7da44ab036a2adf251165895f76"
S = "${UNPACKDIR}"

inherit allarch

FILES:${PN} += "${datadir}/vision-engine"
RDEPENDS:${PN} += "model-yolo-labels"

do_compile() {
    neutron-compiler --input ${S}/${BPN}-${PV}-source.tflite \
        --output ${B}/${YOLO_FILE} --target imx95
}

do_install() {
    install -d ${D}${datadir}/vision-engine/models ${D}${datadir}/vision-engine/models.d
    install -m 0644 ${B}/${YOLO_FILE} ${D}${datadir}/vision-engine/models/
    model_sha=$(sha256sum ${B}/${YOLO_FILE} | cut -d ' ' -f 1)
    sed "s/@MODEL_SHA256@/$model_sha/" ${S}/${BPN}.json \
        > ${D}${datadir}/vision-engine/models.d/${BPN}.json
    install -d ${D}${datadir}/vision-engine/licenses/${PN}
    install -m 0644 ${COMMON_LICENSE_DIR}/AGPL-3.0-only \
        ${D}${datadir}/vision-engine/licenses/${PN}/LICENSE
    install -m 0644 ${S}/${BPN}-${PV}-README.md \
        ${D}${datadir}/vision-engine/licenses/${PN}/MODEL_CARD.md
    printf "%s\n" "Upstream: ${HOMEPAGE}" "Source revision: ${YOLO_SOURCE_REV}" \
        "Compiler: NXP Neutron 3.2.1; target: imx95" \
        > ${D}${datadir}/vision-engine/licenses/${PN}/PROVENANCE.txt
}
