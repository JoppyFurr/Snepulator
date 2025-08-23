/*
 * Snepulator
 * Path handling.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "snepulator.h"


/*
 * Get the directory that the snepulator internal files reside in.
 * The returned string should not be freed.
 *
 * Linux: ${USER}/.snepulator/
 * Windows: %{AppData}/Snepulator/
 */
int32_t path_base (char **path_ptr)
{
    static char *path = NULL;

    /*
     * Linux: $HOME/.snepulator/
     * Windows $AppData/Snepulator/
     */

    if (path == NULL)
    {
#ifdef TARGET_WINDOWS
        char *parent = getenv ("AppData");
        char *name = "Snepulator";
#else
        char *parent = getenv ("HOME");
        char *name = ".snepulator";
#endif
        struct stat stat_buf;
        int len;

        if (parent == NULL)
        {
            snepulator_error ("Environment Error", "Missing environment variable.");
            return -1;
        }

        /* Create the string */
        len = snprintf (NULL, 0, "%s/%s", parent, name) + 1;
        path = calloc (len, 1);
        snprintf (path, len, "%s/%s", parent, name);

        /* Create the directory if it doesn't exist */
        if (stat (path, &stat_buf) == -1)
        {
            if (errno == ENOENT)
            {
                if (mkdir (path, S_IRWXU) == -1)
                {
                    snepulator_error ("Filesystem Error", "Unable to create configuration directory.");
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
 * Intended for internal use, should only be called once per name.
 */
int32_t path_get_dir (char **path_ptr, const char *name)
{
    char *path;
    struct stat stat_buf;
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

    *path_ptr = path;

    return 0;
}


/*
 * Generate a timestamped path for a screenshot.
 * This string needs to be freed.
 *
 * Linux: ${HOME}/Snepulator/Screenshot 2024-01-01 10:00:00.png
 * Windows ${USERPROFILE}/Documents/Snepulator/Screenshot 2024-01-01 10_00_00.png
 */
char *path_screenshot (void)
{
    time_t time_val;
    time (&time_val);
    struct tm *time_ptr = localtime (&time_val);
    struct stat stat_buf;

    /* Create the containing directory string */
#ifdef TARGET_WINDOWS
    char *parent = getenv ("USERPROFILE");
    int len = snprintf (NULL, 0, "%s/Documents/Snepulator", parent) + 1;
    char *directory = calloc (len, 1);
    snprintf (directory, len, "%s/Documents/Snepulator", parent);
#else
    char *parent = getenv ("HOME");
    int len = snprintf (NULL, 0, "%s/Snepulator", parent) + 1;
    char *directory = calloc (len, 1);
    snprintf (directory, len, "%s/Snepulator", parent);
#endif

    /* Create the directory if it doesn't exist */
    if (stat (directory, &stat_buf) == -1)
    {
        if (errno == ENOENT)
        {
            if (mkdir (directory, S_IRWXU) == -1)
            {
                snepulator_error ("Filesystem Error", "Unable to create screenshot directory.");
            }
        }
        else
        {
            snepulator_error ("Filesystem Error", "Unable to stat screenshot directory.");
            return NULL;
        }
    }

    /* Create the full path string */
    len = snprintf (NULL, 0, "%s/Screenshot 2024-01-01 10:00:00.png", directory) + 1;
    char *path = calloc (len, 1);
    snprintf (path, len,
#if defined (TARGET_WINDOWS) || defined (TARGET_DARWIN)
              "%s/Screenshot %04d-%02d-%02d %02d_%02d_%02d.png",
#else
              "%s/Screenshot %04d-%02d-%02d %02d:%02d:%02d.png",
#endif
              directory,
              time_ptr->tm_year + 1900, time_ptr->tm_mon + 1, time_ptr->tm_mday,
              time_ptr->tm_hour, time_ptr->tm_min, time_ptr->tm_sec);

    free (directory);

    return path;
}


/*
 * Generate the SRAM backup path.
 * This string needs to be freed.
 */
char *path_sram (uint8_t rom_hash [HASH_LENGTH])
{
    static char *dir = NULL;
    char *path;
    int len;

    if (dir == NULL)
    {
        if (path_get_dir (&dir, "sram") == -1)
        {
            return NULL;
        }
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
 * Generate the path for a save slot.
 * This string needs to be freed.
 */
char *path_save_slot (uint32_t slot, uint8_t rom_hash [HASH_LENGTH])
{
    static char *dir = NULL;
    char *path;
    int len;

    if (dir == NULL)
    {
        if (path_get_dir (&dir, "state") == -1)
        {
            return NULL;
        }
    }

    /* Get the path length */
    len = snprintf (NULL, 0, "%s/000000000000000000000000.save%02d", dir, slot) + 1;

    /* Create the string */
    path = calloc (len, 1);
    snprintf (path, len, "%s/%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x.save%02d", dir,
            rom_hash [ 0], rom_hash [ 1], rom_hash [ 2], rom_hash [ 3],
            rom_hash [ 4], rom_hash [ 5], rom_hash [ 6], rom_hash [ 7],
            rom_hash [ 8], rom_hash [ 9], rom_hash [10], rom_hash [11], slot);

    return path;
}
