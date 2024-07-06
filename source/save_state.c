/*
 * Snepulator
 * Save state implementation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snepulator.h"
#include "util.h"
#include "save_state.h"

/*
 * Header:
 *  0x00: 'SNEPSAVE'        (8 bytes text)
 *  0x08: Console ID        (4 bytes text)
 *  0x0c: Section count     (4 bytes BE)
 *
 * Sections:
 *  0x00: Section ID        (4 bytes text)
 *  0x04: Section version   (4 bytes BE)
 *  0x08: Section size      (4 bytes BE)
 *  0x0c: Data
 */

static uint8_t *buffer = NULL;
static uint32_t buffer_size = 0;
static uint32_t buffer_used = 0;
static uint32_t section_count = 0;


/*
 * Begin creating a new save state.
 */
void save_state_begin (const char *console_id)
{
    /* Make sure we have a clean start */
    if (buffer != NULL)
    {
        free (buffer);
        buffer = NULL;
    }
    buffer_size = 0;
    buffer_used = 0;
    section_count = 0;

    /* Start with 32 KiB */
    buffer_size = SIZE_32K;
    buffer = calloc (buffer_size, 1);
    if (buffer == NULL)
    {
        /* TODO: This error should not be fatal */
        snepulator_error ("Error", "Unable to allocate memory for save-state buffer.");
        return;
    }

    /* The first 12 bytes contain the header */
    memcpy (&buffer[0], SAVE_STATE_MAGIC, 8);
    memcpy (&buffer[8], console_id, 4);
    buffer_used = 16;
}


/*
 * Add a section to the save state.
 */
void save_state_section_add (const char *section_id, uint32_t version, uint32_t size, void *data)
{
    uint32_t version_be;
    uint32_t size_be;

    /* Sanity-check */
    if (buffer == NULL)
    {
        return;
    }

    /* Expand the buffer if needed */
    while ((buffer_size - buffer_used) < (size + 12))
    {
        buffer_size *= 2;
        buffer = realloc (buffer, buffer_size);

        if (buffer == NULL)
        {
            /* TODO: This error should not be fatal */
            snepulator_error ("Error", "Unable to allocate memory for save-state buffer.");
            return;
        }
    }

    version_be = util_hton32 (version);
    size_be = util_hton32 (size);
    memcpy (&buffer[buffer_used +  0], section_id, 4);
    memcpy (&buffer[buffer_used +  4], &version_be, 4);
    memcpy (&buffer[buffer_used +  8], &size_be, 4);
    memcpy (&buffer[buffer_used + 12], data, size);

    buffer_used += size + 12;
    section_count++;
}


/*
 * Write the save state to disk.
 */
void save_state_write (const char *filename)
{
    uint32_t section_count_be;
    uint32_t bytes_written = 0;
    FILE *state_file;

    /* Sanity-check */
    if (buffer == NULL)
    {
        return;
    }

    state_file = fopen (filename, "wb");
    if (state_file == NULL)
    {
        /* TODO: This error should not be fatal */
        snepulator_error ("Error", "Unable to write state to file.");
        return;
    }

    /* Store the section count */
    section_count_be = util_hton32 (section_count);
    memcpy (&buffer[12], &section_count_be, 4);

    /* Write the buffer to file */
    while (bytes_written < buffer_used)
    {
        bytes_written += fwrite (buffer + bytes_written, 1, buffer_used - bytes_written, state_file);
    }
    fclose (state_file);

    free (buffer);
    buffer = NULL;
    buffer_size = 0;
    buffer_used = 0;
}


/*
 * Load a state buffer from file.
 * Returns -1 if the file was not found.
 */
int load_state_begin (const char *filename, const char **console_id, uint32_t *sections_loaded)
{
    FILE *state_file;
    uint32_t bytes_read = 0;
    uint32_t sections_loaded_be;

    /* Make sure we have a clean start */
    if (buffer != NULL)
    {
        free (buffer);
        buffer = NULL;
    }
    section_count = 0;
    buffer_size = 0;
    buffer_used = 0;

    /* Open the file */
    state_file = fopen (filename, "rb");
    if (state_file == NULL)
    {
        /* TODO: Check error code */
        return -1;
    }
    fseek (state_file, 0, SEEK_END);
    buffer_size = ftell (state_file);
    rewind (state_file);

    /* Copy to buffer */
    buffer = malloc (buffer_size);
    if (buffer == NULL)
    {
        snepulator_error ("Error", "Unable to allocate memory for load-state buffer.");
        return -1;
    }
    while (bytes_read < buffer_size)
    {
        bytes_read += fread (buffer + bytes_read, 1, buffer_size - bytes_read, state_file);
    }

    /* Close the file */
    fclose (state_file);

    /* Check the magic number */
    if (memcmp (&buffer [0], SAVE_STATE_MAGIC, 8))
    {
        snepulator_error ("Error", "Invalid save-state file.");
        return -1;
    }

    *console_id = (char *) &buffer [8];

    memcpy (&sections_loaded_be, &buffer[12], 4);
    *sections_loaded = util_ntoh32 (sections_loaded_be);

    buffer_used = 16;
    return 0;
}


/*
 * Get a pointer to the next section.
 */
void load_state_section (const char **section_id, uint32_t *version, uint32_t *size, void **data)
{
    uint32_t version_be;
    uint32_t size_be;

    *section_id = (char *) &buffer [buffer_used];

    memcpy (&version_be, &buffer [buffer_used + 4], 4);
    *version = util_ntoh32 (version_be);

    memcpy (&size_be, &buffer [buffer_used + 8], 4);
    *size = util_ntoh32 (size_be);

    *data = &buffer [buffer_used + 12];
    buffer_used += 12 + *size;
}


/*
 * Free the state buffer.
 */
void load_state_end (void)
{
    free (buffer);
    buffer = NULL;
    buffer_size = 0;
}
