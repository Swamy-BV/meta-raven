# Stage the recipe's adjacent code directory using BitBake's local fetcher.
RAVEN_SOURCE ?= "code"
# nooelint: oelint.vars.fileextrapaths,oelint.vars.outofcontext Shared class locates recipe-local code.
FILESEXTRAPATHS:prepend := "${FILE_DIRNAME}:"
SRC_URI += "file://${RAVEN_SOURCE};subdir=${BP}"
S = "${UNPACKDIR}/${BP}/${RAVEN_SOURCE}"
