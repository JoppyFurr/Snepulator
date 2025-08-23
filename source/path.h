/*
 * Snepulator
 * Path handling header.
 */

/* Get the directory that the snepulator files reside in. */
int32_t path_base (char **path_ptr);

/* Get the path to the requested subdirectory. */
int32_t path_get_dir (char **path_ptr, const char *name);

/* Generate a timestamped path for a screenshot. Needs to be freed. */
char *path_screenshot (void);

/* Generate the SRAM backup path for the current rom. Needs to be freed. */
char *path_sram (uint8_t rom_hash [HASH_LENGTH]);

/* Generate the path for a save slot. Needs to be freed. */
char *path_save_slot (uint32_t slot, uint8_t rom_hash [HASH_LENGTH]);

