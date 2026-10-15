# Read-only snapshot. Validity refers to local receipt age, not source acquisition time.
builtin_interfaces/Time stamp
# Changes on each backend attachment; not a persistent vehicle identity.
uint64 connection_epoch
uint32 system_id
bool connected
bool armed_valid
bool armed
bool mode_valid
string<=32 mode
