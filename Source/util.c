/*
 * Common utilities.
 */

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <SDL2/SDL.h>

#include "util.h"
#include "snepulator.h"

#include "../Libraries/BLAKE3/blake3.h"
#include "../Libraries/SDL_SavePNG/savepng.h"

/* Global state */
extern Snepulator_State state;


/*
 * Get the directory that the snepulator files reside in.
 */
int32_t snepulator_directory (char **path_ptr)
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
 * Get the directory that the SRAM files reside in.
 */
int32_t snepulator_sram_directory (char **path_ptr)
{
    static char *path = NULL;

    if (path == NULL)
    {
        struct stat  stat_buf;
        char *base;
        int len;

        if (snepulator_directory (&base) == -1)
        {
            return -1;
        }

        /* Get the path length */
        len = snprintf (NULL, 0, "%s/sram", base) + 1;

        /* Create the string */
        path = calloc (len, 1);
        snprintf (path, len, "%s/sram", base);

        /* Create the directory if it doesn't exist */
        if (stat (path, &stat_buf) == -1)
        {
            if (errno == ENOENT)
            {
                if (mkdir (path, S_IRWXU) == -1)
                {
                    snepulator_error ("Filesystem Error", "Unable to create SRAM directory.");
                }
            }
            else
            {
                snepulator_error ("Filesystem Error", "Unable to stat SRAM directory.");
                return -1;
            }
        }
    }

    *path_ptr = path;

    return 0;
}


/*
 * Round up to the next power-of-two
 */
uint32_t round_up (uint32_t n)
{
    uint32_t result = 1;

    if (n  > 0x80000000)
    {
        snepulator_error ("Size Error", "Number too large for round_up.");
        return 0;
    }

    while (result < n)
    {
        result <<= 1;
    }

    return result;
}

/*
 * Use BLAKE3 to make a 12-byte hash for the current ROM.
 */
void snepulator_hash_rom (uint8_t *buffer, uint32_t rom_size)
{
    blake3_hasher hasher;

    blake3_hasher_init (&hasher);
    blake3_hasher_update (&hasher, buffer, rom_size);
    blake3_hasher_finalize (&hasher, state.rom_hash, HASH_LENGTH);
}

/*
 * Load a rom file into a buffer.
 * If the rom is not a power-of-two size, the buffer will be rounded up and
 * padded with zeros. The buffer should be freed when no-longer needed.
 */
int32_t snepulator_load_rom (uint8_t **buffer, uint32_t *rom_size, char *filename)
{
    uint32_t bytes_read = 0;
    uint32_t file_size;
    uint32_t skip;

    /* Open ROM file */
    FILE *rom_file = fopen (filename, "rb");
    if (!rom_file)
    {
        snepulator_error ("Load Error", strerror (errno));
        return -1;
    }

    /* Get ROM size */
    fseek (rom_file, 0, SEEK_END);
    file_size = ftell (rom_file);

    /* Some ROM files begin with a 512 byte header, possibly added when dumped
     * by a Super Magic Drive. Skip over this by rounding to the nearest 1 KiB. */
    skip = file_size & 0x3ff;
    fseek (rom_file, skip, SEEK_SET);
    *rom_size = file_size - skip;

    /* Allocate memory, rounded to a power-of-two */
    *buffer = (uint8_t *) calloc (round_up (*rom_size), 1);
    if (!*buffer)
    {
        snepulator_error ("Load Error", strerror (errno));
        return -1;
    }

    /* Copy to memory */
    while (bytes_read < *rom_size)
    {
        bytes_read += fread (*buffer + bytes_read, 1, *rom_size - bytes_read, rom_file);
    }

    fclose (rom_file);

    /* Only the hash of the most recently loaded ROM is held on to,
     * so any BIOS should be loaded first, saving the ROM for last. */
    snepulator_hash_rom (*buffer, *rom_size);

    return EXIT_SUCCESS;
}


/*
 * Take a screenshot.
 */
void snepulator_take_screenshot (void)
{
    uint32_t width = state.video_width;
    uint32_t height = state.video_height;
    uint32_t stride = VIDEO_BUFFER_WIDTH;
    uint8_t *buffer;
    char    *home = getenv ("HOME");
    char     path [80] = { '\0' };

    if (home == NULL)
    {
        snepulator_error ("Environment Error", "${HOME} not defined.");
        return;
    }

    /* 24-bits per pixel */
    buffer = malloc (state.video_width * state.video_height * 3);

    /* Convert from float to uint8_t */
    for (uint32_t y = 0; y < height; y++)
    {
        for (uint32_t x = 0; x < width; x++)
        {
            buffer [(x + y * width) * 3 + 0] = state.video_out_data [state.video_start_x + x + (state.video_start_y + y) * stride].r * 255.0;
            buffer [(x + y * width) * 3 + 1] = state.video_out_data [state.video_start_x + x + (state.video_start_y + y) * stride].g * 255.0;
            buffer [(x + y * width) * 3 + 2] = state.video_out_data [state.video_start_x + x + (state.video_start_y + y) * stride].b * 255.0;
        }
    }

    SDL_Surface *screenshot_surface = SDL_CreateRGBSurfaceFrom (buffer, width, height,
                                      24, state.video_width * 3, 0xff << 0, 0xff << 8, 0xff << 16, 0x00);

    /* Include the date in the filename */
    time_t time_val;
    time (&time_val);
    struct tm *time_ptr = localtime (&time_val);
    snprintf (path, 79, "%s/Snepulator %04d-%02d-%02d %02d:%02d:%02d.png", home,
              time_ptr->tm_year + 1900, time_ptr->tm_mon + 1, time_ptr->tm_mday,
              time_ptr->tm_hour, time_ptr->tm_min, time_ptr->tm_sec);

    if (SDL_SavePNG (screenshot_surface, path) < 0)
    {
        snepulator_error ("SDL Error", SDL_GetError ());
    }

    SDL_FreeSurface (screenshot_surface);
    free (buffer);
}


/*
 * Convert a float_Colour to greyscale.
 */
float_Colour to_greyscale (float_Colour c)
{
    /* Convert to linear colour */
    c.r = (c.r < 0.04045) ? (c.r / 12.92) : pow ((c.r + 0.055) / 1.055, 2.4);
    c.g = (c.g < 0.04045) ? (c.g / 12.92) : pow ((c.g + 0.055) / 1.055, 2.4);
    c.b = (c.b < 0.04045) ? (c.b / 12.92) : pow ((c.b + 0.055) / 1.055, 2.4);

    /* Convert linear colour to greyscale */
    c.r = c.g = c.b = c.r * 0.2126 + c.g * 0.7152 + c.b * 0.0722;

    /* Convert back to sRGB */
    c.r = (c.r <= 0.0031308) ? (12.92 * c.r) : (1.055 * pow (c.r, 1.0 / 2.4) - 0.055);
    c.g = (c.g <= 0.0031308) ? (12.92 * c.g) : (1.055 * pow (c.g, 1.0 / 2.4) - 0.055);
    c.b = (c.b <= 0.0031308) ? (12.92 * c.b) : (1.055 * pow (c.b, 1.0 / 2.4) - 0.055);

    return c;
}


/*
 * Reduce saturation of a float_Colour.
 */
float_Colour colour_saturation (float_Colour c, float saturation)
{
    float_Colour mix;

    /* Convert to linear colour */
    c.r = (c.r < 0.04045) ? (c.r / 12.92) : pow ((c.r + 0.055) / 1.055, 2.4);
    c.g = (c.g < 0.04045) ? (c.g / 12.92) : pow ((c.g + 0.055) / 1.055, 2.4);
    c.b = (c.b < 0.04045) ? (c.b / 12.92) : pow ((c.b + 0.055) / 1.055, 2.4);

    /* Desaturate */
    mix.r = saturation * c.r + (1.0 - saturation) * (c.r * 0.2126 + c.g * 0.7152 + c.b * 0.0722);
    mix.g = saturation * c.g + (1.0 - saturation) * (c.r * 0.2126 + c.g * 0.7152 + c.b * 0.0722);
    mix.b = saturation * c.b + (1.0 - saturation) * (c.r * 0.2126 + c.g * 0.7152 + c.b * 0.0722);

    /* Convert back to sRGB */
    c.r = (mix.r <= 0.0031308) ? (12.92 * mix.r) : (1.055 * pow (mix.r, 1.0 / 2.4) - 0.055);
    c.g = (mix.g <= 0.0031308) ? (12.92 * mix.g) : (1.055 * pow (mix.g, 1.0 / 2.4) - 0.055);
    c.b = (mix.b <= 0.0031308) ? (12.92 * mix.b) : (1.055 * pow (mix.b, 1.0 / 2.4) - 0.055);

    return c;
}
