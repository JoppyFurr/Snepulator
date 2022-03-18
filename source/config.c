/*
 * Configuration file implementation.
 */

#define _DEFAULT_SOURCE
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "snepulator_types.h"
#include "path.h"

#define MAX_STRING_SIZE 1024

typedef enum ConfigType_e
{
    ENTRY_TYPE_NONE = 0,
    ENTRY_TYPE_STRING,
    ENTRY_TYPE_UINT,
} ConfigType;

typedef struct ConfigEntry_s
{
    char           *key;
    ConfigType      type;
    void           *value;
} ConfigEntry;

typedef struct ConfigSection_s
{
    char           *name;
    uint32_t        entry_count;
    ConfigEntry    *entry;
} ConfigSection;

typedef struct Config_s
{
    uint32_t        section_count;
    ConfigSection  *section;
} Config;

static Config config;

/*
 * Get a pointer to a section of config.
 * If create is true, create the section if it does not exist.
 */
static int32_t config_section_get (ConfigSection **section_ptr, char const *section_name, bool create)
{
    ConfigSection *section = NULL;

    /* First, check if the section exists */
    for (uint32_t i = 0; i < config.section_count; i++)
    {
        if (strcmp (section_name, config.section [i].name) == 0)
        {
            section = &config.section [i];
            break;
        }
    }

    /* Create the section if we've been asked to */
    if (section == NULL && create == true)
    {
        /* Extend the section array */
        config.section = realloc (config.section, sizeof (ConfigSection) * (config.section_count + 1));
        if (config.section == NULL)
        {
            fprintf (stderr, "Error: Unable to reallocate memory.\n");
            return -1;
        }
        section = &config.section [config.section_count];
        config.section_count++;

        /* Initialise the section */
        memset (section, 0, sizeof (ConfigSection));
        section->name = strdup (section_name);
        if (section->name == NULL)
        {
            fprintf (stderr, "Error: Unable to duplicate string.\n");
            return -1;
        }

    }

    /* If the section was not found */
    if (section == NULL)
    {
        return -1;
    }

    *section_ptr = section;

    return 0;
}


/*
 * Find an entry in the config data structure.
 * If create is true, create the entry if it does not exist.
 */
static int32_t config_entry_get (ConfigEntry **entry_ptr, char const *section_name, char const *key, bool create)
{
    ConfigSection *section = NULL;
    ConfigEntry *entry = NULL;

    /* First, get the section for the entry */
    if (config_section_get (&section, section_name, create) == -1)
    {
        return -1;
    }

    /* Check if the entry already exists */
    for (uint32_t i = 0; i < section->entry_count; i++)
    {
        if (strcmp (key, section->entry [i].key) == 0)
        {
            entry = &section->entry [i];
        }
    }

    /* Create the new entry if we've been asked to */
    if (entry == NULL && create == true)
    {
        /* Extend the entry array */
        section->entry = realloc (section->entry, sizeof (ConfigEntry) * (section->entry_count + 1));

        if (section->entry == NULL)
        {
            fprintf (stderr, "Error: Unable to reallocate memory.\n");
            return -1;
        }
        entry = &section->entry [section->entry_count];
        section->entry_count++;

        /* Initialise the entry */
        memset (entry, 0, sizeof (ConfigEntry));
        entry->key = strdup (key);
        if (entry->key == NULL)
        {
            fprintf (stderr, "Error: Unable to duplicate string.\n");
            return -1;
        }
    }

    /* If the entry was not found */
    if (entry == NULL)
    {
        return -1;
    }

    *entry_ptr = entry;

    return 0;
}


/*
 * Get an unsigned integer from the config data structure.
 */
int32_t config_uint_get (char const *section_name, char const *key, uint32_t *value)
{
    ConfigEntry *entry = NULL;

    /* Find the entry */
    if (config_entry_get (&entry, section_name, key, false) == -1)
    {
        return -1;
    }

    /* Sanity check */
    if (entry->type != ENTRY_TYPE_UINT)
    {
        fprintf (stderr, "Config Error: Incorrect type for [%s] -> %s.\n", section_name, key);
        return -1;
    }

    *value = (uint32_t)(uintptr_t) entry->value;
    return 0;
}


/*
 * Store an unsigned integer into the config data structure.
 */
int32_t config_uint_set (char const *section_name, char const *key, uint32_t value)
{
    ConfigEntry *entry = NULL;

    /* First, check if the entry already exists */
    if (config_entry_get (&entry, section_name, key, true) == -1)
    {
        return -1;
    }

    entry->type = ENTRY_TYPE_UINT;
    entry->value = (void *)(uintptr_t) value;

    return 0;
}


/*
 * Get a string from the config data structure.
 *
 * The pointer is only valid until the config is changed.
 */
int32_t config_string_get (char const *section_name, char const *key, char **value)
{
    ConfigEntry *entry = NULL;

    /* Find the entry */
    if (config_entry_get (&entry, section_name, key, false) == -1)
    {
        return -1;
    }

    /* Sanity check */
    if (entry->type != ENTRY_TYPE_STRING)
    {
        fprintf (stderr, "Config Error: Incorrect type for [%s] -> %s.\n", section_name, key);
        return -1;
    }

    *value = (char *) entry->value;
    return 0;
}


/*
 * Store a string into the config data structure.
 */
int32_t config_string_set (char const *section_name, char const *key, char const *value)
{
    ConfigEntry *entry = NULL;

    if (config_entry_get (&entry, section_name, key, true) == -1)
    {
        return -1;
    }

    /* Free the old string if any */
    if ((entry->type = ENTRY_TYPE_STRING) && (entry->value != NULL))
    {
        free (entry->value);
    }

    entry->type = ENTRY_TYPE_STRING;
    entry->value = (void *) strdup (value);

    if (entry->value == NULL)
    {
        return -1;
    }

    return 0;
}


/*
 * Remove an entry from the configuration.
 */
int32_t config_entry_remove (char const *section_name, char const *key)
{
    ConfigSection *section = NULL;
    ConfigEntry *entry = NULL;
    uint32_t i;

    /* First, get the section for the entry */
    if (config_section_get (&section, section_name, 0) == -1)
    {
        return 0;
    }

    /* Find the entry */
    for (i = 0; i < section->entry_count; i++)
    {
        if (strcmp (key, section->entry [i].key) == 0)
        {
            entry = &section->entry [i];
            break;
        }
    }

    if (entry != NULL)
    {
        /* If there is a string, free it. */
        if (entry->type == ENTRY_TYPE_STRING && entry->value != NULL)
        {
            free (entry->value);
        }
        section->entry_count--;

        /* Shuffle remaining entries down to fill the gap. */
        for ( ; i < section->entry_count; i++)
        {
            section->entry [i] = section->entry [i + 1];
        }

        /* We don't worry about resizing the section allocation to be smaller. */
    }

    return 0;
}


/*
 * Given an index, get a section name.
 */
const char *config_get_section_name (uint32_t index)
{
    if (index >= config.section_count)
    {
        return NULL;
    }
    else
    {
        return config.section [index].name;
    }
}


/*
 * Get the full path to the config file.
 */
static int32_t config_path (char **path_ptr)
{
    static char *path = NULL;

    if (path == NULL)
    {
        char *dir;
        int len;

        if (path_base (&dir) == -1)
        {
            return -1;
        }

        /* Get the path length */
        len = snprintf (NULL, 0, "%s/snepulator.cfg", dir) + 1;

        /* Create the string */
        path = calloc (len, 1);
        snprintf (path, len, "%s/snepulator.cfg", dir);
    }

    *path_ptr = path;

    return 0;
}


/*
 * Opens the configuration file in either mode "r" or "w".
 */
static int32_t config_open (FILE **config_file, char *mode)
{
    char        *directory;
    char        *path;
    struct stat  stat_buf;

    if (path_base (&directory) == -1)
    {
        return -1;
    }

    if (config_path (&path) == -1)
    {
        return -1;
    }

    /* First, check if the directory exists */
    if (stat (directory, &stat_buf) == -1)
    {
        if (errno == ENOENT)
        {
            perror ("Error: .snepulator directory not found.");
            return -1;
        }
        else
        {
            perror ("Error: Unable to stat configuration directory.");
            return -1;
        }
    }

    /* Now, check if the file exists */
    if (stat (path, &stat_buf) == -1)
    {
        if (errno == ENOENT)
        {
            if (strcmp (mode, "r") == 0)
            {
                /* No configuration file to read */
                return 0;
            }
        }
        else
        {
            perror ("Error: Unable to stat configuration file.");
            return -1;
        }
    }

    *config_file = fopen (path, mode);
    if (*config_file == NULL)
    {
        fprintf (stderr, "Error: Unable to open %s.\n", path);
        return -1;
    }

    return 0;
}


/*
 * Read the configuration file.
 */
int32_t config_read (void)
{
    FILE    *config_file = NULL;
    char     section [MAX_STRING_SIZE];
    char     key     [MAX_STRING_SIZE];
    char     buffer  [MAX_STRING_SIZE];
    uint32_t length;

    if (config_open (&config_file, "r") == -1)
    {
        return -1;
    }

    if (config_file == NULL)
    {
        /* Nothing to read */
        return 0;
    }

    while (fscanf (config_file, "%1023s", buffer) != EOF)
    {
        length = strlen (buffer);

        /* Section */
        if (buffer [0] == '[' && buffer [length - 1] == ']')
        {
            buffer [length - 1] = '\0';
            strcpy (section, &buffer [1]);
        }

        /* Entry */
        else {
            strcpy (key, buffer);
            if (fscanf (config_file, "%1023s", buffer) == EOF)
            {
                fprintf (stderr, "Error: Unexpected end of file.\n");
                return -1;
            }
            if (strcmp (buffer, "=") != 0)
            {
                fprintf (stderr, "Error: Expected \"=\", found \"%s\".\n", buffer);
                return -1;
            }
            if (fscanf (config_file, " %1023[^\n]", buffer) == EOF)
            {
                fprintf (stderr, "Error: Unexpected end of file.\n");
                return -1;
            }
            length = strlen (buffer);

            /* String */
            if (buffer [0] == '"' && buffer [length - 1] == '"')
            {
                buffer [length - 1] = '\0';
                config_string_set (section, key, &buffer [1]);
            }

            /* Unsigned Integer */
            else if (isdigit (buffer [0]))
            {
                config_uint_set (section, key, strtoul (buffer, NULL, 0));
            }

            /* Unknown */
            else
            {
                fprintf (stderr, "Error: Unknown data type: \"%s\"\n", buffer);
                return -1;
            }
        }
    }

    fclose (config_file);

    return 0;
}


/*
 * Write the configuration file.
 */
int32_t config_write (void)
{
    FILE        *config_file;

    /* TODO: Sort the sections */

    if (config_open (&config_file, "w") == -1)
    {
        return -1;
    }

    /* Now, export the configuration structure to file */
    for (uint32_t s = 0; s < config.section_count; s++)
    {
        ConfigSection *section = &config.section [s];

        fprintf (config_file, "[%s]\n", section->name);

        for (uint32_t e = 0; e < section->entry_count; e++)
        {
            ConfigEntry *entry = &section->entry [e];

            switch (entry->type)
            {
                case ENTRY_TYPE_STRING:
                    fprintf (config_file, "%s = \"%s\"\n", entry->key, (char *) entry->value);
                    break;
                case ENTRY_TYPE_UINT:
                    fprintf (config_file, "%s = %u\n", entry->key, (uint32_t) (uintptr_t) entry->value);
                    break;
                default:
                    fprintf (stderr, "Error: Invalid config entry type.\n");
            }
        }

        fprintf (config_file, "\n");
    }

    fclose (config_file);

    return 0;
}
