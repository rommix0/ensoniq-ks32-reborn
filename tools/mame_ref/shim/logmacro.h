// Stand-in for MAME's logmacro.h (tools/mame_ref shim). Same macro names and masking; output
// goes to device_t::logerror, which the shim discards. The arguments are still compiled.
#ifndef VERBOSE
#define VERBOSE 0
#endif

#ifndef LOG_OUTPUT_FUNC
#define LOG_OUTPUT_FUNC logerror
#endif

#ifndef LOG_GENERAL
#define LOG_GENERAL (1U << 0)
#endif

#define LOGMASKED(mask, ...) \
    do {                     \
        if (VERBOSE & (mask)) (LOG_OUTPUT_FUNC)(__VA_ARGS__); \
    } while (false)

#define LOG(...) LOGMASKED(LOG_GENERAL, __VA_ARGS__)
