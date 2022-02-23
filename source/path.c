
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "snepulator_types.h"
#include "snepulator.h"


/*
 * Get the directory that the snepulator files reside in.
 */
int32_t path_base (char **path_ptr)
{
    static char *path = NULL;

    if (path == NULL)
    {
        struct stat  stat_buf;
        char *home = getenv ("HOME");
        int len;

        if (home == NULL)
        {
            snepulator_error ("Environment Error", "${HOME} not defined.");
            return -1;
        }

        /* Get the path length */
        len = snprintf (NULL, 0, "%s/.snepulator", home) + 1;

        /* Create the string */
        path = calloc (len, 1);
        snprintf (path, len, "%s/.snepulator", home);

        /* Create the directory if it doesn't exist */
        if (stat (path, &stat_buf) == -1)
        {
            if (errno == ENOENT)
            {
                if (mkdir (path, S_IRWXU) == -1)
                {
                    snepulator_error ("Filesystem Error", "Unable to create .snepulator directory.");
                }
            }
            else
            {
                snepulator_error ("Filesystem Error", "Unable to stat configuration directory.");
                return -1;
            }
        }
    }

    *path_ptr = path;

    return 0;
}


/*
 * Get the path to the requested subdirectory.
 */
int32_t path_get_dir (char **path_ptr, char *name)
{
    static char *path = NULL;

    if (path == NULL)
    {
        struct stat  stat_buf;
        char *base;
        int len;

        if (path_base (&base) == -1)
        {
            return -1;
        }

        /* Get the path length */
        len = snprintf (NULL, 0, "%s/%s", base, name) + 1;

        /* Create the string */
        path = calloc (len, 1);
        snprintf (path, len, "%s/%s", base, name);

        /* Create the directory if it doesn't exist */
        if (stat (path, &stat_buf) == -1)
        {
            if (errno == ENOENT)
            {
                if (mkdir (path, S_IRWXU) == -1)
                {
                    snepulator_error ("Filesystem Error", "Unable to create directory.");
                }
            }
            else
            {
                snepulator_error ("Filesystem Error", "Unable to stat directory.");
                return -1;
            }
        }
    }

    *path_ptr = path;

    return 0;
}


/*
 * Generate the SRAM backup path.
 *
 * This string needs to be freed.
 */
char *path_sram (uint8_t rom_hash [HASH_LENGTH])
{
    static char *path;
    char *dir;
    int len;

    if (path_get_dir (&dir, "sram") == -1)
    {
        return NULL;
    }

    /* Get the path length */
    len = snprintf (NULL, 0, "%s/000000000000000000000000.sram", dir) + 1;

    /* Create the string */
    path = calloc (len, 1);
    snprintf (path, len, "%s/%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x.sram", dir,
            rom_hash [ 0], rom_hash [ 1], rom_hash [ 2], rom_hash [ 3], rom_hash [ 4], rom_hash [ 5],
            rom_hash [ 6], rom_hash [ 7], rom_hash [ 8], rom_hash [ 9], rom_hash [10], rom_hash [11]);

    return path;
}


/*
 * Generate the quick-save path.
 *
 * This string needs to be freed.
 */
char *path_quicksave (uint8_t rom_hash [HASH_LENGTH])
{
    static char *path;
    char *dir;
    int len;

    if (path_get_dir (&dir, "state") == -1)
    {
        return NULL;
    }

    /* Get the path length */
    len = snprintf (NULL, 0, "%s/000000000000000000000000.quicksave", dir) + 1;

    /* Create the string */
    path = calloc (len, 1);
    snprintf (path, len, "%s/%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x.quicksave", dir,
            rom_hash [ 0], rom_hash [ 1], rom_hash [ 2], rom_hash [ 3], rom_hash [ 4], rom_hash [ 5],
            rom_hash [ 6], rom_hash [ 7], rom_hash [ 8], rom_hash [ 9], rom_hash [10], rom_hash [11]);

    return path;
}
