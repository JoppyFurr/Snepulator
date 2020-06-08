/*
 * Common utilities.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <SDL2/SDL.h>

#include "util.h"
#include "snepulator.h"

#include "../Libraries/SDL_SavePNG/savepng.h"

/* Global state */
extern Snepulator_State state;


/*
 * Load a rom file into a buffer.
 * The buffer should be freed when no-longer needed.
 */
int32_t snepulator_load_rom (uint8_t **buffer, uint32_t *buffer_size, char *filename)
{
    uint32_t bytes_read = 0;
    uint32_t file_size = 0;
    uint32_t skip = 0;

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

    /* Some roms seem to have an extra header at the start. Skip this. */
    skip = file_size & 0x3ff;
    fseek (rom_file, skip, SEEK_SET);
    *buffer_size = file_size - skip;

    /* Allocate memory */
    *buffer = (uint8_t *) malloc (*buffer_size);
    if (!*buffer)
    {
        snepulator_error ("Load Error", strerror (errno));
        return -1;
    }

    /* Copy to memory */
    while (bytes_read < *buffer_size)
    {
        bytes_read += fread (*buffer + bytes_read, 1, *buffer_size - bytes_read, rom_file);
    }

    fclose (rom_file);

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

    /* 24-bits per pixel */
    buffer = malloc (state.video_width * state.video_height * 3);

    /* Convert from float to uint8_t */
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            buffer [(x + y * width) * 3 + 0] = state.video_out_texture_data [x + VIDEO_SIDE_BORDER + (y + state.video_out_first_active_line) * stride].r * 255.0;
            buffer [(x + y * width) * 3 + 1] = state.video_out_texture_data [x + VIDEO_SIDE_BORDER + (y + state.video_out_first_active_line) * stride].g * 255.0;
            buffer [(x + y * width) * 3 + 2] = state.video_out_texture_data [x + VIDEO_SIDE_BORDER + (y + state.video_out_first_active_line) * stride].b * 255.0;
        }
    }

    SDL_Surface *screenshot_surface = SDL_CreateRGBSurfaceFrom (buffer, width, height,
                                      24, state.video_width * 3, 0xff << 0, 0xff << 8, 0xff << 16, 0x00);

    if (SDL_SavePNG (screenshot_surface, "screenshot.png") < 0)
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
