S = "${UNPACKDIR}"

inherit allarch

FILES:${PN} += "${datadir}/vision-engine"
RDEPENDS:${PN} = "model-yolo-labels"
YOLO_LICENSE_FILES ?= "${BPN}-${PV}-LICENSE"

do_install() {
    install -d ${D}${datadir}/vision-engine/models ${D}${datadir}/vision-engine/models.d
    install -m 0644 ${S}/${BPN}-${PV}-${YOLO_FILE} ${D}${datadir}/vision-engine/models/${YOLO_FILE}
    install -m 0644 ${S}/${BPN}.json ${D}${datadir}/vision-engine/models.d/
    install -d ${D}${datadir}/vision-engine/licenses/${PN}
    for notice in ${YOLO_LICENSE_FILES}; do
        install -m 0644 ${S}/$notice ${D}${datadir}/vision-engine/licenses/${PN}/
    done
    printf "%s\n" "Upstream: ${HOMEPAGE}" "Source revision: ${YOLO_SOURCE_REV}" \
        "Pinned payloads: ${SRC_URI}" > ${D}${datadir}/vision-engine/licenses/${PN}/PROVENANCE.txt
}
