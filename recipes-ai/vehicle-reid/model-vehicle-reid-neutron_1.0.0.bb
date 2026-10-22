SUMMARY = "Experimental PP-LCNet vehicle identity embeddings for Neutron"
DESCRIPTION = "Pinned INT8 VERI-Wild vehicle ReID model, compiled for i.MX95 with explicit preprocessing and provenance"
HOMEPAGE = "https://github.com/PaddlePaddle/PaddleDetection"
BUGTRACKER = "https://github.com/PaddlePaddle/PaddleDetection/issues"
SECTION = "multimedia"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=86d3f3a95c324c9479bd8986968f4327"
DEPENDS = "nxp-neutron-compiler-native"
SRC_URI = "\
    file://LICENSE \
    file://provenance.json \
    file://vehicle-reid-int8.tflite \
"
S = "${UNPACKDIR}"
VEHICLE_REID_SOURCE_SHA256 = "ed1dbbe179da28699a079cd78a98756883683a07633086c35d6d7a11ca1a75f3"

inherit allarch

FILES:${PN} += "${datadir}/target-tracker"

do_compile() {
    printf '%s  %s\n' ${VEHICLE_REID_SOURCE_SHA256} ${S}/vehicle-reid-int8.tflite | sha256sum -c -
    neutron-compiler --input ${S}/vehicle-reid-int8.tflite \
        --output ${B}/vehicle-reid-neutron.tflite --target imx95
}

do_install() {
    install -d ${D}${datadir}/target-tracker/licenses/vehicle-reid
    install -m 0644 ${B}/vehicle-reid-neutron.tflite ${D}${datadir}/target-tracker/
    install -m 0644 ${S}/provenance.json ${D}${datadir}/target-tracker/vehicle-reid.json
    install -m 0644 ${S}/LICENSE ${D}${datadir}/target-tracker/licenses/vehicle-reid/LICENSE
}
