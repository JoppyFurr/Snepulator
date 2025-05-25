/*
 * Snepulator
 * Utility functions header.
 */

#define ENFORCE_MINIMUM(VAR, MIN) VAR = (VAR > MIN) ? VAR : MIN
#define ENFORCE_MAXIMUM(VAR, MAX) VAR = (VAR > MAX) ? MAX : VAR

#define MAX(A, B) (((A) > (B)) ? (A) : (B))
#define MIN(A, B) (((A) < (B)) ? (A) : (B))

/* Return B, within the limits of A <= B <= C */
#define RANGE(A, B, C) (((B) < (A)) ? (A) : ((B) > (C)) ? (C) : (B))


/* Set the start time for util_get_ticks. */
void util_ticks_init (void);

/* Get the number of ticks that have passed */
uint32_t util_get_ticks (void);

/* Get the number of ticks that have passed */
uint64_t util_get_ticks_us (void);

/* Delay for a number of ticks */
void util_delay (uint32_t ticks);

/* Use BLAKE3 to make a 12-byte hash for the current ROM. */
void util_hash_rom (const uint8_t *rom, uint32_t rom_size, uint8_t rom_hash [HASH_LENGTH]);

/* Read chosen bytes from a file into a provided buffer. */
int32_t util_file_read_bytes (uint8_t *buffer, uint32_t offset, uint32_t count, const char *filename);

/* Load a file into a buffer. The buffer should be freed when no-longer needed. */
int32_t util_load_file (uint8_t **buffer, uint32_t *file_size, const char *filename);

/* Load a gzip-compressed file into a buffer. The buffer should be freed when no-longer needed. */
int32_t util_load_gzip_file (uint8_t **buffer, uint32_t *content_size, const char *filename);

/* Load a rom file into a power-of-two sized buffer. The buffer should be freed when no-longer needed. */
int32_t util_load_rom (uint8_t **buffer, uint32_t *rom_size, const char *filename);

/* Take a screenshot. */
void util_take_screenshot (void);

/* Convert a uint_pixel to greyscale. */
uint_pixel util_to_greyscale (uint_pixel c);

/* Reduce saturation of a uint_pixel. */
uint_pixel util_colour_saturation (uint_pixel c, float saturation);

/* Round up to the next power-of-two */
uint32_t util_round_up (uint32_t n);

/* Conversion to and from network byte order */
uint16_t util_hton16 (uint16_t h);
uint32_t util_hton32 (uint32_t h);
uint16_t util_ntoh16 (uint16_t n);
uint32_t util_ntoh32 (uint32_t n);
