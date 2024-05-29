/*
 * Snepulator
 * Utility functions implementation
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
#include <zlib.h>

#include <SDL2/SDL.h>

#include "snepulator_compat.h"
#include "snepulator_types.h"
#include "snepulator.h"
#include "path.h"

#include "blake3.h"
#include "spng.h"

/* Global state */
extern Snepulator_State state;

/* File state */
static struct timespec time_start;

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
 * 32-bit endian conversion - Host to network.
 * Assumes a little-endian host.
 */
uint32_t util_hton32 (uint32_t h)
{
    return ((h & 0x000000ff) << 24)
         | ((h & 0x0000ff00) << 8)
         | ((h & 0x00ff0000) >> 8)
         | ((h & 0xff000000) >> 24);
}


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
 * 32-bit endian conversion - Network to host.
 * Assumes a little-endian host.
 */
uint32_t util_ntoh32 (uint32_t n)
{
    return ((n & 0x000000ff) << 24)
         | ((n & 0x0000ff00) << 8)
         | ((n & 0x00ff0000) >> 8)
         | ((n & 0xff000000) >> 24);
}


/*
 * Set the start time for util_get_ticks.
 */
void util_ticks_init (void)
{
    clock_gettime (CLOCK_MONOTONIC_RAW, &time_start);
}


/*
 * Get the number of 1ms ticks that have passed.
 *
 * Based on SDL_GetTicks ()
 * Assumes that monotonic time is supported.
 */
uint32_t util_get_ticks (void)
{
    struct timespec now;
    uint32_t ticks;

    clock_gettime (CLOCK_MONOTONIC_RAW, &now);
    ticks = (uint32_t) ((now.tv_sec  - time_start.tv_sec) * 1000
                      + (now.tv_nsec - time_start.tv_nsec) / 1000000);

    return ticks;
}


/*
 * Get the number of 1us ticks that have passed.
 */
uint64_t util_get_ticks_us (void)
{
    struct timespec now;
    uint64_t ticks;

    clock_gettime (CLOCK_MONOTONIC_RAW, &now);
    ticks = ((now.tv_sec  - time_start.tv_sec) * 1000000
           + (now.tv_nsec - time_start.tv_nsec) / 1000);

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
 * Load a file into a buffer.
 * The buffer should be freed when no-longer needed.
 */
int32_t util_load_file (uint8_t **buffer, uint32_t *file_size, char *filename)
{
    uint32_t bytes_read = 0;
    uint32_t size;

    /* Open the file */
    FILE *file = fopen (filename, "rb");
    if (!file)
    {
        snepulator_error ("Load Error", strerror (errno));
        return -1;
    }

    /* Get file size */
    fseek (file, 0, SEEK_END);
    size = ftell (file);
    fseek (file, 0, SEEK_SET);

    /* Allocate memory */
    *buffer = (uint8_t *) calloc (size, 1);
    if (!*buffer)
    {
        snepulator_error ("Load Error", strerror (errno));
        return -1;
    }

    /* Copy to memory */
    while (bytes_read < size)
    {
        bytes_read += fread (*buffer + bytes_read, 1, size - bytes_read, file);
    }

    *file_size = size;
    fclose (file);

    return EXIT_SUCCESS;
}


/*
 * Load a gzip-compressed file into a buffer.
 * The buffer should be freed when no-longer needed.
 */
int32_t util_load_gzip_file (uint8_t **buffer, uint32_t *content_size, char *filename)
{
    uint8_t scratch [128] = { 0 }; /* Temporary decompression area, for getting the decompressed size */
    uint32_t bytes_read = 0;
    uint32_t size;

    /* Open the file */
    gzFile file = gzopen (filename, "rb");
    if (!file)
    {
        snepulator_error ("Load Error", strerror (errno));
        return -1;
    }

    /* Use a 128 byte buffer to decompress the file, to get the size */
    while (gzread (file, scratch, 128) == 128);
    size = gztell (file);
    gzrewind (file);

    /* Allocate memory */
    *buffer = (uint8_t *) calloc (size, 1);
    if (!*buffer)
    {
        snepulator_error ("Load Error", strerror (errno));
        return -1;
    }

    /* Copy to memory */
    while (bytes_read < size)
    {
        bytes_read += gzread (file, *buffer + bytes_read, size - bytes_read);
    }

    *content_size = size;
    gzclose (file);

    return EXIT_SUCCESS;
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

    /* TODO: Move this somewhere SMS-specific.
     *       Or, at least, check if we're a power-of-two
     *       first, otherwise, this could trigger on a
     *       512-byte intro.  */

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
    uint32_t stride = VIDEO_BUFFER_WIDTH;
    uint32_t start_x = state.video_start_x;
    uint32_t start_y = state.video_start_y;
    struct spng_ihdr ihdr = {
        .width = state.video_width,
        .height = state.video_height,
        .color_type = SPNG_COLOR_TYPE_TRUECOLOR,
        .bit_depth = 8
    };

    /* Open the output file */
    char *path = path_screenshot ();
    FILE *output_file = fopen (path, "wb");
    free (path);
    if (output_file == NULL)
    {
        snepulator_error ("File Error", "Cannot open screenshot file");
        return;
    }

    /* Calculate the size of the image buffer size */
    if (state.console == CONSOLE_MASTER_SYSTEM)
    {
        start_x += state.video_blank_left;
        ihdr.width -= state.video_blank_left;
    }

    /* Allocate with 24-bits per pixel */
    uint32_t image_size = ihdr.width * ihdr.height * 3;
    uint8_t *buffer = malloc (image_size);

    /* Fill the newly allocated buffer */
    for (uint32_t y = 0; y < ihdr.height; y++)
    {
        for (uint32_t x = 0; x < ihdr.width; x++)
        {
            buffer [(x + y * ihdr.width) * 3 + 0] = state.video_out_data [start_x + x + (start_y + y) * stride].r;
            buffer [(x + y * ihdr.width) * 3 + 1] = state.video_out_data [start_x + x + (start_y + y) * stride].g;
            buffer [(x + y * ihdr.width) * 3 + 2] = state.video_out_data [start_x + x + (start_y + y) * stride].b;
        }
    }

    /* Encode the image with libspng */
    spng_ctx *spng_context = spng_ctx_new (SPNG_CTX_ENCODER);
    spng_set_png_file (spng_context, output_file);
    spng_set_ihdr (spng_context, &ihdr);
    int ret = spng_encode_image (spng_context, buffer, image_size, SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE);

    if (ret != 0)
    {
        snepulator_error ("File Error", "Failed to encode screenshot");
    }

    spng_ctx_free (spng_context);
    fclose (output_file);
    free (buffer);
}


/*
 * Convert a uint_pixel to greyscale.
 *
 * TODO: Reduce floating-point math.
 */
uint_pixel util_to_greyscale (uint_pixel c)
{
    float float_r = c.r / 255.0f;
    float float_g = c.g / 255.0f;
    float float_b = c.b / 255.0f;

    /* Convert to linear colour */
    float_r = (float_r < 0.04045) ? (float_r / 12.92) : pow ((float_r + 0.055) / 1.055, 2.4);
    float_g = (float_g < 0.04045) ? (float_g / 12.92) : pow ((float_g + 0.055) / 1.055, 2.4);
    float_b = (float_b < 0.04045) ? (float_b / 12.92) : pow ((float_b + 0.055) / 1.055, 2.4);

    /* Convert linear colour to greyscale */
    float_r = float_g = float_b = float_r * 0.2126 + float_g * 0.7152 + float_b * 0.0722;

    /* Convert back to sRGB */
    float_r = (float_r <= 0.0031308) ? (12.92 * float_r) : (1.055 * pow (float_r, 1.0 / 2.4) - 0.055);
    float_g = (float_g <= 0.0031308) ? (12.92 * float_g) : (1.055 * pow (float_g, 1.0 / 2.4) - 0.055);
    float_b = (float_b <= 0.0031308) ? (12.92 * float_b) : (1.055 * pow (float_b, 1.0 / 2.4) - 0.055);

    c.r = float_r * 255;
    c.g = float_g * 255;
    c.b = float_b * 255;

    return c;
}


/*
 * Reduce saturation of a uint_pixel.
 *
 * TODO: Reduce floating-point math.
 */
uint_pixel util_colour_saturation (uint_pixel c, float saturation)
{
    float float_r = c.r / 255.0f;
    float float_g = c.g / 255.0f;
    float float_b = c.b / 255.0f;

    float mix_r;
    float mix_g;
    float mix_b;

    /* Convert to linear colour */
    float_r = (float_r < 0.04045) ? (float_r / 12.92) : pow ((float_r + 0.055) / 1.055, 2.4);
    float_g = (float_g < 0.04045) ? (float_g / 12.92) : pow ((float_g + 0.055) / 1.055, 2.4);
    float_b = (float_b < 0.04045) ? (float_b / 12.92) : pow ((float_b + 0.055) / 1.055, 2.4);

    /* Desaturate */
    mix_r = saturation * float_r + (1.0 - saturation) * (float_r * 0.2126 + float_g * 0.7152 + float_b * 0.0722);
    mix_g = saturation * float_g + (1.0 - saturation) * (float_r * 0.2126 + float_g * 0.7152 + float_b * 0.0722);
    mix_b = saturation * float_b + (1.0 - saturation) * (float_r * 0.2126 + float_g * 0.7152 + float_b * 0.0722);

    /* Convert back to sRGB */
    float_r = (mix_r <= 0.0031308) ? (12.92 * mix_r) : (1.055 * pow (mix_r, 1.0 / 2.4) - 0.055);
    float_g = (mix_g <= 0.0031308) ? (12.92 * mix_g) : (1.055 * pow (mix_g, 1.0 / 2.4) - 0.055);
    float_b = (mix_b <= 0.0031308) ? (12.92 * mix_b) : (1.055 * pow (mix_b, 1.0 / 2.4) - 0.055);

    c.r = float_r * 255;
    c.g = float_g * 255;
    c.b = float_b * 255;

    return c;
}
