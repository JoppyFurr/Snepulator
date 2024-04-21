/*
 * Snepulator
 * Compatibility helper - To try avoid OS-specific ifdefs in the implementations.
 */

#ifdef TARGET_WINDOWS

/* Extra Includes */
#include <direct.h>

/* Definitions */
#define USER_DIR "APPDATA"
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC

/* Functions */
#define mkdir(PATH,FLAGS) _mkdir (PATH)

#else

/* Definitions */
#define USER_DIR "HOME"

#endif
