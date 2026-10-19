SUMMARY = "Pinned MobileNet classifiers and SSD COCO detector"
DESCRIPTION = "Two original quantized NXP MobileNet classifiers and Google Coral SSD MobileNet V2 with built-in box \
               decoding and NMS for CPU inference"
HOMEPAGE = "https://huggingface.co/nxp/mobilenet-v1-imx"
BUGTRACKER = "https://github.com/NXP/eiq-model-zoo/issues"
SECTION = "multimedia"
LICENSE = "Apache-2.0"
# nooelint: oelint.var.licenseremotefile Upstream model card identifies Apache-2.0.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/Apache-2.0;md5=89aea4e17d99a7cacdbeed46a0096b10"
# editorconfig-checker-disable
# Keep pinned download URLs and fetcher parameters intact.
SRC_URI = "https://huggingface.co/nxp/mobilenet-v1-imx/resolve/6bfd024abc1898dd1c47aa2508de206bc8d950a2/original_model/mobilenet_v1_0.25_128_quant.tflite;name=small;downloadfilename=vision-engine-${PV}-small.tflite \
           https://huggingface.co/nxp/mobilenet-v1-imx/resolve/6bfd024abc1898dd1c47aa2508de206bc8d950a2/original_model/mobilenet_v1_1.0_224_quant.tflite;name=full;downloadfilename=vision-engine-${PV}-full.tflite \
           https://huggingface.co/nxp/mobilenet-v1-imx/resolve/6bfd024abc1898dd1c47aa2508de206bc8d950a2/labels_mobilenet.txt;name=labels;downloadfilename=vision-engine-${PV}-labels.txt \
           https://raw.githubusercontent.com/nxp-imx/tensorflow-imx/c49e242b7546dfdcc839b24dd67929de8d642232/tensorflow/lite/examples/label_image/testdata/grace_hopper.bmp;name=demo;downloadfilename=vision-engine-${PV}-demo.bmp \
           https://raw.githubusercontent.com/google-coral/test_data/104342d2d3480b3e66203073dac24f4e2dbb4c41/ssd_mobilenet_v2_coco_quant_postprocess.tflite;name=ssd;downloadfilename=vision-engine-${PV}-ssd.tflite \
           https://raw.githubusercontent.com/google-coral/test_data/104342d2d3480b3e66203073dac24f4e2dbb4c41/coco_labels.txt;name=coco;downloadfilename=vision-engine-${PV}-coco-labels.txt \
           file://registry.json"
# editorconfig-checker-enable
SRC_URI[ssd.sha256sum] = "42fb3d70ffb7bb37dd518f730f7be784b831c2078f30c497d0019cc2e987fa26"
SRC_URI[coco.sha256sum] = "dc183f003fc753c4c43fae6fdf7f387559449573f13fa32e517fb7453fd380f1"
SRC_URI[small.sha256sum] = "3799e7cdb3ec44beacaf47ef5c884111c919e51bb50b34ac836244b08d4b953b"
SRC_URI[full.sha256sum] = "ecc3a67c47c5a609ec35f6a58a7d97532834e43df4cb7d3f1204a8164b7d20dd"
SRC_URI[labels.sha256sum] = "536feacc519de3d418de26b2effb4d75694a8c4c0063e36499a46fa8061e2da9"
SRC_URI[demo.sha256sum] = "8c1165a143b3ac5c37fba13a918101133d549d3419c7fc474ae70a2f29263b80"
S = "${UNPACKDIR}"

inherit allarch

do_install() {
    install -d ${D}${datadir}/vision-engine/models
    install -m 0644 ${S}/vision-engine-${PV}-small.tflite ${D}${datadir}/vision-engine/models/small.tflite
    install -m 0644 ${S}/vision-engine-${PV}-full.tflite ${D}${datadir}/vision-engine/models/full.tflite
    install -m 0644 ${S}/vision-engine-${PV}-labels.txt ${D}${datadir}/vision-engine/models/labels.txt
    install -m 0644 ${S}/registry.json ${D}${datadir}/vision-engine/
    install -m 0644 ${S}/vision-engine-${PV}-ssd.tflite ${D}${datadir}/vision-engine/models/ssd.tflite
    install -m 0644 ${S}/vision-engine-${PV}-coco-labels.txt ${D}${datadir}/vision-engine/models/coco-labels.txt
    install -m 0644 ${S}/vision-engine-${PV}-demo.bmp ${D}${datadir}/vision-engine/demo.bmp
}
FILES:${PN} += "${datadir}/vision-engine"
