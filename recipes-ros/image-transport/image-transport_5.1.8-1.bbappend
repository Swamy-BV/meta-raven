FILESEXTRAPATHS:prepend:raven := "${THISDIR}/files:"

SRC_URI:append:raven = " file://0001-handle-topics-outside-publisher-namespace.patch"
