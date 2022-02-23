/*
 * Common utilities.
 */

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#include <SDL2/SDL.h>

#include "snepulator_types.h"
#include "snepulator.h"

#include "../libraries/BLAKE3/blake3.h"
#include "../libraries/SDL_SavePNG/savepng.h"

/* Global state */
extern Snepulator_State state;

/*
 * Get the number of ticks that have passed.
 *
 * Based on SDL_GetTicks ()
 * Assumes that monotonic time is supported.
 */
uint32_t util_get_ticks (void)
{
    static struct timespec start;
    static bool ticks_started = false;
    struct timespec now;
    uint32_t ticks;

    if (!ticks_started)
    {
        ticks_started = true;
        clock_gettime (CLOCK_MONOTONIC_RAW, &start);
    }

    clock_gettime (CLOCK_MONOTONIC_RAW, &now);
    ticks = (uint32_t) ((now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000);

    return ticks;
}


/*
 * Delay for a number of ticks.
 *
 * Based on SDL_Delay ()
 */
void util_delay (uint32_t ticks)
{
    struct timespec request;
    struct timespec remaining;
    int ret;

    remaining.tv_sec = ticks / 1000;
    remaining.tv_nsec = (ticks % 1000) * 1000000;

    do
    {
        errno = 0;
        request.tv_sec = remaining.tv_sec;
        request.tv_nsec = remaining.tv_nsec;
        ret = nanosleep (&request, &remaining);
    } while (ret && (errno == EINTR));
}


/*
 * Round up to the next power-of-two
 */
uint32_t util_round_up (uint32_t n)
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
void util_hash_rom (const uint8_t *rom, uint32_t rom_size, uint8_t rom_hash [HASH_LENGTH])
{
    blake3_hasher hasher;

    blake3_hasher_init (&hasher);
    blake3_hasher_update (&hasher, rom, rom_size);
    blake3_hasher_finalize (&hasher, rom_hash, HASH_LENGTH);
}


/*
 * Load a rom file into a buffer.
 * If the rom is not a power-of-two size, the buffer will be rounded up and
 * padded with zeros. The buffer should be freed when no-longer needed.
 */
int32_t util_load_rom (uint8_t **buffer, uint32_t *rom_size, char *filename)
{
    uint32_t bytes_read = 0;
    uint32_t skip = 0;
    uint32_t file_size;

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

    /* Some ROM files begin with a 512 byte header, possibly added when dumped by
     * a Super Magic Drive. Only the first two bytes of this header are nonzero. */
    if ((file_size & 0x3ff) == 512)
    {
        uint8_t zeros [512] = { 0 };
        uint8_t header [512] = { 0 };

        rewind (rom_file);
        (void) (fread (header, 1, 512, rom_file) == 0);

        if (memcmp (&header [2], zeros, 510) == 0)
        {
            skip = 512;
        }
    }

    fseek (rom_file, skip, SEEK_SET);
    *rom_size = file_size - skip;

    /* Allocate memory, rounded to a power-of-two */
    *buffer = (uint8_t *) calloc (util_round_up (*rom_size), 1);
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

    return EXIT_SUCCESS;
}


/*
 * Take a screenshot.
 */
void util_take_screenshot (void)
{
    uint32_t width = state.video_width;
    uint32_t height = state.video_height;
    uint32_t stride = VIDEO_BUFFER_WIDTH;
    uint32_t start_x = state.video_start_x;
    uint32_t start_y = state.video_start_y;

    uint8_t *buffer;
    char    *home = getenv ("HOME");
    char     path [80] = { '\0' };

    if (home == NULL)
    {
        snepulator_error ("Environment Error", "${HOME} not defined.");
        return;
    }

    /* Crop out the blank column */
    if (state.console == CONSOLE_MASTER_SYSTEM)
    {
        start_x += state.video_blank_left;
        width -= state.video_blank_left;
    }

    /* 24-bits per pixel */
    buffer = malloc (state.video_width * state.video_height * 3);

    /* Convert from float to uint8_t */
    for (uint32_t y = 0; y < height; y++)
    {
        for (uint32_t x = 0; x < width; x++)
        {
            buffer [(x + y * width) * 3 + 0] = state.video_out_data [start_x + x + (start_y + y) * stride].r * 255.0;
            buffer [(x + y * width) * 3 + 1] = state.video_out_data [start_x + x + (start_y + y) * stride].g * 255.0;
            buffer [(x + y * width) * 3 + 2] = state.video_out_data [start_x + x + (start_y + y) * stride].b * 255.0;
        }
    }

    SDL_Surface *screenshot_surface = SDL_CreateRGBSurfaceFrom (buffer, width, height,
                                      24, width * 3, 0xff << 0, 0xff << 8, 0xff << 16, 0x00);

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
float_Colour util_to_greyscale (float_Colour c)
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
float_Colour util_colour_saturation (float_Colour c, float saturation)
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
