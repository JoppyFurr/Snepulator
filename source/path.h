/*
 * Path handling.
 */

/* Get the directory that the snepulator files reside in. */
int32_t path_base (char **path_ptr);

/* Get the path to the requested subdirectory. */
int32_t path_get_dir (char **path_ptr, char *name);

/* Generate the SRAM backup path for the current rom. Needs to be freed. */
char *path_sram (uint8_t rom_hash [HASH_LENGTH]);

/* Generate the quick-save path for the current rom. Needs to be freed. */
char *path_quicksave (uint8_t rom_hash [HASH_LENGTH]);

