/*
 * Snepulator
 * Text utility functions implementation
 */

#include <ctype.h>
#ifndef TARGET_WINDOWS
#include <iconv.h>
#endif
#include <stdlib.h>

#include "snepulator.h"

/*
 * Convert a Shift-JIS string to UTF-8.
 * Returns an allocated string that needs to be freed,
 * or NULL if the conversion failed.
 *
 * TODO: For now, conversion is not enabled on Windows. This
 *       requires sorting out extra mingw libraries.
 */
char *util_shift_jis_to_utf_8 (const uint8_t *input, size_t input_bytes)
{
#ifdef TARGET_WINDOWS
    return NULL;
#else
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
#endif
}


/*
 * Convert a UTF-8 string to Title Case.
 * This includes stripping out excess whitespace.
 *
 * Note: This works in-place, modifying the original string.
 *
 * TODO:
 *  - Consider first tokenizing the string into words and re-building the string
 *  - Consider special cases for minor words like 'of', and 'and'
 *  - Consider special cases fro Roman numerals like 'II'
 */
void util_text_to_title_case (char *text)
{
    size_t length = 0;
    bool previous_was_space = false;
    bool previous_was_upper = false;

    for (size_t i = 0; text [i] != '\0'; i++)
    {
        /* Don't modify non-ASCII characters */
        if (text [i] & 0x80)
        {
            text [length++] = text [i];
            previous_was_space = false;
            previous_was_upper = false;
        }
        else
        {
            /* Capture isspace and isupper first, because
             * in-place modification can change the result. */
            bool is_space = isspace (text [i]);
            bool is_capital = isupper (text [i]);

            /* Strip out extra spaces */
            if (isspace (text [i]) && previous_was_space)
            {
                continue;
            }

            /* Replace all-caps with title-case */
            if (previous_was_upper)
            {
                text [length++] = tolower (text [i]);
            }
            else
            {
                text [length++] = text [i];
            }

            previous_was_space = is_space;
            previous_was_upper = is_capital;
        }
    }

    text [length] = '\0';
}
