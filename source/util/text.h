/*
 * Snepulator
 * Text utility functions header.
 */

/* Conversion from Shift-JIS text to UTF-8 */
char *util_shift_jis_to_utf_8 (const uint8_t *input, size_t input_bytes);

/* Convert a UTF-8 string to Title Case. */
void util_text_to_title_case (char *text);
