/*
 * Snepulator Single-Step Tests harness.
 *
 * Utility functions.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>


/*
 * 16-bit endian conversion - Network to host.
 * Assumes a little-endian host.
 */
uint16_t util_ntoh16 (uint16_t n)
{
    return ((n & 0x00ff) << 8)
         | ((n & 0xff00) >> 8);
}

/*
 * 16-bit endian conversion - Host to network.
 * Assumes a little-endian host.
 */
uint16_t util_hton16 (uint16_t h)
{
    return ((h & 0x00ff) << 8)
         | ((h & 0xff00) >> 8);
}


/*
 * Read a file into a buffer.
 * padded with zeros. The buffer should be freed when no-longer needed.
 */
int32_t util_load_file (char **buffer, const char *filename)
{
    uint32_t bytes_read = 0;
    uint32_t file_size;

    /* Open ROM file */
    FILE *file = fopen (filename, "rb");
    if (!file)
    {
        printf ("File not found,'%s'\n", filename);
        return -1;
    }

    /* Get file size */
    fseek (file, 0, SEEK_END);
    file_size = ftell (file);

    fseek (file, 0, SEEK_SET);

    /* Allocate memory, rounded to a power-of-two */
    *buffer = (char *) calloc (file_size, 1);
    if (!*buffer)
    {
        printf ("Load Error: %s.\n", strerror (errno));
        return -1;
    }

    /* Copy to memory */
    while (bytes_read < file_size)
    {
        bytes_read += fread (*buffer + bytes_read, 1, file_size - bytes_read, file);
    }

    fclose (file);

    return EXIT_SUCCESS;
}


