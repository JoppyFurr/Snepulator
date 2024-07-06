/*
 * Snepulator
 * Compatibility helper - To try avoid OS-specific ifdefs in the implementations.
 */

#ifdef TARGET_WINDOWS

#include <windows.h>
#include <direct.h>

/* Definition overrides */
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC

/* Function overrides */
#define mkdir(PATH,FLAGS) _mkdir (PATH)

#endif
