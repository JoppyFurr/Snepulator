/*
 * Common utilities.
 */

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

/* Convert a float_Colour to greyscale. */
float_Colour util_to_greyscale (float_Colour c);

/* Reduce saturation of a float_Colour. */
float_Colour util_colour_saturation (float_Colour c, float saturation);

/* Round up to the next power-of-two */
uint32_t util_round_up (uint32_t n);
