// CI stand-in for a user's gitignored src/config_defaults.h, found through -I
// because CI has no src/config_defaults.h. The enum is deliberately not
// idempotent: it only compiles if the file is read once per translation unit.
enum MhiConfigDefaultsFixture { MHI_CONFIG_DEFAULTS_READ_ONCE };

#define HOSTNAME "MHI-AC-Ctrl-ci"
#define POWERON_WHEN_CHANGING_MODE true
#define PAYLOAD_MODE_FAN "fan_only"
