/*
 * Snepulator
 * Text utility functions implementation
 */

#include <iconv.h>
#include <stdlib.h>

#include "snepulator.h"

/*
 * Convert a Shift-JIS string to UTF-8.
 * Returns an allocated string that needs to be freed,
 * or NULL if the conversion failed.
 *
 * TODO: Confirm this works on Windows - May need an extra library to either be
 *       included in the .zip or to be statically linked.
 */
char *util_shift_jis_to_utf_8 (const uint8_t *input, size_t input_bytes)
{
    iconv_t cd = iconv_open ("UTF-8", "SHIFT-JIS");
    if (cd == (iconv_t) -1)
    {
        return NULL;
    }

    /* Three times the input size is the worst-case for
     * conversion, plus one extra for the null-terminator. */
    char *output_buffer = calloc (input_bytes * 3 + 1, 1);
    if (output_buffer == NULL)
    {
        iconv_close (cd);
        return NULL;
    }

    /* iconv wants to modify the pointers, so make it a copy */
    char *input_buffer_pt = (char *) input;
    char *output_buffer_pt = output_buffer;
    size_t input_bytes_left = input_bytes;
    size_t output_bytes_left = input_bytes * 3;

    size_t result = iconv (cd, &input_buffer_pt, &input_bytes_left, &output_buffer_pt, &output_bytes_left);
    if (result == (size_t) -1)
    {
        iconv_close (cd);
        free (output_buffer);
        return NULL;
    }

    iconv_close (cd);
    return output_buffer;
}
