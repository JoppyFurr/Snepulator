/*
 * Basic configuration file API.
 */

/* Get an unsigned integer from the config data structure. */
int32_t config_uint_get (char const *section_name, char const *key, uint32_t *value);

/* Store an unsigned integer into the config data structure. */
int32_t config_uint_set (char const *section_name, char const *key, uint32_t value);

/* Get a string from the config data structure. */
int32_t config_string_get (char const *section_name, char const *key, char **value);

/* Store a string into the config data structure. */
int32_t config_string_set (char const *section_name, char const *key, char const *value);

/* Read the configuration file. */
int32_t config_read (void);

/* Write the configuration file. */
int32_t config_write (void);
