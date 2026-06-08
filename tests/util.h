/*
 * Snepulator Single-Step Tests harness.
 *
 * Utility functions header.
 */

#undef PATH_MAX
#define PATH_MAX 4096

/* 16-bit endian conversion */
uint16_t util_ntoh16 (uint16_t n);
uint16_t util_hton16 (uint16_t n);

/* Read a file into a buffer. */
int32_t util_load_file (char **buffer, const char *filename);
