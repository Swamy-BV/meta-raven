# Keep cppcheck's configuration lookup relocatable in recipe sysroots and SDKs.
do_install:append:class-native:raven() {
    ln -s ../share/Cppcheck/cfg ${D}${bindir}/cfg
}

do_install:append:class-nativesdk:raven() {
    ln -s ../share/Cppcheck/cfg ${D}${bindir}/cfg
}
