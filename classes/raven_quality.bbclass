# Opt in only for Raven-owned CMake projects. Vendor recipes keep upstream policy.
RAVEN_QUALITY ?= "1"
RAVEN_QUALITY_FORMAT ?= "${RAVEN_QUALITY}"
RAVEN_QUALITY_TIDY ?= "${RAVEN_QUALITY}"
RAVEN_QUALITY_CPPCHECK ?= "0"
RAVEN_QUALITY_IWYU ?= "${RAVEN_QUALITY}"
RAVEN_QUALITY_WARNINGS ?= "${RAVEN_QUALITY}"
RAVEN_QUALITY_WERROR ?= "${RAVEN_QUALITY}"
RAVEN_SANITIZER ?= "none"
RAVEN_NATIVE_CLANG ?= "${@'1' if d.getVar('RAVEN_SANITIZER') == 'thread' else '0'}"

RAVEN_QUALITY_CONFIGDIR = "${@os.path.dirname(bb.utils.which(d.getVar('BBPATH'), 'config/.clang-format'))}"
# nooelint: oelint.vars.fileextrapaths,oelint.vars.outofcontext Shared class adds its own config search path.
FILESEXTRAPATHS:prepend := "${RAVEN_QUALITY_CONFIGDIR}:"
# editorconfig-checker-disable-next-line: keep the inline Python expression intact.
DEPENDS += "${@'clang-native' if d.getVar('RAVEN_QUALITY_FORMAT') == '1' or d.getVar('RAVEN_QUALITY_TIDY') == '1' or d.getVar('RAVEN_QUALITY_IWYU') == '1' else ''}"
DEPENDS += "${@'cppcheck-native' if d.getVar('RAVEN_QUALITY_CPPCHECK') == '1' else ''}"
DEPENDS += "${@'include-what-you-use-native' if d.getVar('RAVEN_QUALITY_IWYU') == '1' else ''}"
# Thread sanitization uses the release-matched LLVM runtime rather than host libtsan.
# editorconfig-checker-disable-next-line: keep the inline Python expression intact.
DEPENDS:append:class-native = " ${@'clang-native compiler-rt-sanitizers-native' if d.getVar('RAVEN_NATIVE_CLANG') == '1' else ''}"
# editorconfig-checker-disable-next-line: keep the inline Python expression intact.
OECMAKE_CXX_COMPILER:class-native = "${@'clang++' if d.getVar('RAVEN_NATIVE_CLANG') == '1' else oecmake_map_compiler('CXX', d)[0]}"

SRC_URI += "file://.clang-format;subdir=raven-quality \
            file://.clang-tidy;subdir=raven-quality \
            file://iwyu.imp;subdir=raven-quality \
            file://RavenQuality.cmake;subdir=raven-quality"
EXTRA_OECMAKE += "-DCMAKE_PROJECT_INCLUDE=${UNPACKDIR}/raven-quality/RavenQuality.cmake \
                  -DRAVEN_QUALITY_FORMAT=${RAVEN_QUALITY_FORMAT} \
                  -DRAVEN_QUALITY_TIDY=${RAVEN_QUALITY_TIDY} \
                  -DRAVEN_QUALITY_CPPCHECK=${RAVEN_QUALITY_CPPCHECK} \
                  -DRAVEN_QUALITY_IWYU=${RAVEN_QUALITY_IWYU} \
                  -DRAVEN_QUALITY_WARNINGS=${RAVEN_QUALITY_WARNINGS} \
                  -DRAVEN_QUALITY_WERROR=${RAVEN_QUALITY_WERROR} \
                  -DRAVEN_SANITIZER=${RAVEN_SANITIZER}"
# GCC-only canonicalisation is not understood by the Clang analysis driver.
# Retain the actual debug/file/macro prefix maps used for reproducibility.
# editorconfig-checker-disable-next-line: keep the inline Python expression intact.
CXXFLAGS:remove = "${@'-fcanon-prefix-map' if d.getVar('RAVEN_QUALITY_TIDY') == '1' or d.getVar('RAVEN_QUALITY_IWYU') == '1' else ''}"
