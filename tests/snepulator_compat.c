/*
 * Snepulator Single-Step Tests harness.
 *
 * Compatibility functions for compiling and running the
 * Snepulator CPU implementations outside of Snepulator.
 */

#include <stdio.h>
#include <stdarg.h>
#include "../source/snepulator.h"

Snepulator_State state;

void snepulator_error (const char *title, const char *format, ...)
{
    va_list args;
    char message [240] = { '\0' };

    va_start (args, format);
    vsnprintf (message, 240, format, args);
    va_end (args);

    /* All errors get printed to console */
    fprintf (stderr, "%s: %s\n", title, message);
}
