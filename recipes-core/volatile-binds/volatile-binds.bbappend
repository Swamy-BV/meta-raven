# /var/lib has its own persistent overlay; retain OE's volatile mounts elsewhere.
# nooelint: oelint.vars.multilineident OE uses escaped newlines to separate mount pairs.
VOLATILE_BINDS:raven = "\
    ${localstatedir}/volatile/cache ${localstatedir}/cache\n\
    ${localstatedir}/volatile/spool ${localstatedir}/spool\n\
    ${localstatedir}/volatile/srv ${servicedir}\n\
"
