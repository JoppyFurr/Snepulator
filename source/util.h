/*
 * Common utilities.
 */

#define ENFORCE_MINIMUM(VAR, MIN) VAR = (VAR > MIN) ? VAR : MIN
#define ENFORCE_MAXIMUM(VAR, MAX) VAR = (VAR > MAX) ? MAX : VAR

/* Get the number of ticks that have passed */
uint32_t util_get_ticks (void);

/* Delay for a number of ticks */
void util_delay (uint32_t ticks);

/* Use BLAKE3 to make a 12-byte hash for the current ROM. */
void util_hash_rom (const uint8_t *rom, uint32_t rom_size, uint8_t rom_hash [HASH_LENGTH]);

/* Load a rom file into a buffer. The buffer should be freed when no-longer needed. */
int32_t util_load_rom (uint8_t **buffer, uint32_t *rom_size, char *filename);

/* Take a screenshot. */
void util_take_screenshot (void);

/* Convert a uint_pixel to greyscale. */
uint_pixel util_to_greyscale (uint_pixel c);

/* Reduce saturation of a uint_pixel. */
uint_pixel util_colour_saturation (uint_pixel c, float saturation);

/* Round up to the next power-of-two */
uint32_t util_round_up (uint32_t n);
