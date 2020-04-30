/*
 * Implementation for the TI TMS9918A video chip.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "../util.h"
#include "../snepulator.h"

#include "tms9918a.h"

extern Snepulator_State state;
TMS9918A_State tms9918a_state;

const char *tms9918a_mode_name [16] = {
    "Mode 0 - Graphics I Mode",
    "Mode 1 - Text Mode",
    "Mode 2 - Graphics II Mode",
    "Mode 2+1 - Undocumented",
    "Mode 3 - Multicolour Mode",
    "Mode 3+1 - Undocumented",
    "Mode 3+2 - Undocumented",
    "Mode 3+2+1 - Undocumented",
    "Mode 4 - SMS 192 lines",
    "Mode 4+1 - Invalid Text Mode",
    "Mode 4+2 - SMS 192 lines",
    "Mode 4+2+1 - SMS 224 lines",
    "Mode 4+3 - SMS 192 lines",
    "Mode 4+3+1 - Undocumented",
    "Mode 4+3+2 - SMS 240 lines",
    "Mode 4+3+2+1 - SMS 192 lines"
};

#define TMS9918A_NTSC_PALETTE { \
    {   0 / 255.0f,   0 / 255.0f,   0 / 255.0f }, /* Transparent */ \
    {   0 / 255.0f,   0 / 255.0f,   0 / 255.0f }, /* Black */ \
    {  33 / 255.0f, 200 / 255.0f,  66 / 255.0f }, /* Medium Green */ \
    {  94 / 255.0f, 220 / 255.0f, 120 / 255.0f }, /* Light Green */ \
    {  84 / 255.0f,  85 / 255.0f, 237 / 255.0f }, /* Dark Blue */ \
    { 125 / 255.0f, 118 / 255.0f, 252 / 255.0f }, /* Light blue */ \
    { 212 / 255.0f,  82 / 255.0f,  77 / 255.0f }, /* Dark Red */ \
    {  66 / 255.0f, 235 / 255.0f, 245 / 255.0f }, /* Cyan */ \
    { 252 / 255.0f,  85 / 255.0f,  84 / 255.0f }, /* Medium Red */ \
    { 255 / 255.0f, 121 / 255.0f, 120 / 255.0f }, /* Light Red */ \
    { 212 / 255.0f, 193 / 255.0f,  84 / 255.0f }, /* Dark Yellow */ \
    { 230 / 255.0f, 206 / 255.0f, 128 / 255.0f }, /* Light Yellow */ \
    {  33 / 255.0f, 176 / 255.0f,  59 / 255.0f }, /* Dark Green */ \
    { 201 / 255.0f,  91 / 255.0f, 186 / 255.0f }, /* Magenta */ \
    { 204 / 255.0f, 204 / 255.0f, 204 / 255.0f }, /* Gray */ \
    { 255 / 255.0f, 255 / 255.0f, 255 / 255.0f }  /* White */ \
}

/* Display mode details */
/* TODO: Confirm PAL palette */
static const TMS9918A_Config Mode2_PAL = {
    .mode = TMS9918A_MODE_2,
    .lines_active = 192,
    .lines_total = 313,
    .palette = TMS9918A_NTSC_PALETTE
};
static const TMS9918A_Config Mode2_NTSC = {
    .mode = TMS9918A_MODE_2,
    .lines_active = 192,
    .lines_total = 262,
    .palette = TMS9918A_NTSC_PALETTE
};


/*
 * Supply a human-readable string describing the specified mode.
 */
const char *tms9918a_mode_name_get (TMS9918A_Mode mode)
{
    return tms9918a_mode_name [mode];
}


/*
 * Render one line of a mode2 8x8 pattern.
 */
void tms9918a_render_mode2_pattern_line (const TMS9918A_Config *config, uint16_t line, TMS9918A_Pattern *pattern_base,
                                         uint8_t tile_colours, int32_Point_2D offset, bool sprite)
{
    int border_lines_top = (VIDEO_BUFFER_LINES - config->lines_active) / 2;
    uint8_t line_data;

    uint8_t background_colour = tile_colours & 0x0f;
    uint8_t foreground_colour = tile_colours >> 4;

    line_data = pattern_base->data [line - offset.y];

    for (uint32_t x = 0; x < 8; x++)
    {
        /* Don't draw texture pixels that fall outside of the screen */
        if (x + offset.x >= 256)
            continue;

        uint8_t colour_index = ((line_data & (0x80 >> x)) ? foreground_colour : background_colour);

        if (colour_index == TMS9918A_COLOUR_TRANSPARENT)
        {
            if (sprite == true)
            {
                continue;
            }
            else
            {
                colour_index = tms9918a_state.regs.background_colour & 0x0f;
            }
        }

        float_Colour pixel = config->palette [colour_index];

        /* TODO: Should be a single line */
        tms9918a_state.frame_current [(offset.x + x + VIDEO_SIDE_BORDER) + (border_lines_top + line) * VIDEO_BUFFER_WIDTH].r = pixel.r;
        tms9918a_state.frame_current [(offset.x + x + VIDEO_SIDE_BORDER) + (border_lines_top + line) * VIDEO_BUFFER_WIDTH].g = pixel.g;
        tms9918a_state.frame_current [(offset.x + x + VIDEO_SIDE_BORDER) + (border_lines_top + line) * VIDEO_BUFFER_WIDTH].b = pixel.b;
    }
}


/*
 * Render this line's sprites for modes with sprite support.
 * Sprites can be either 8×8 pixels, or 16×16.
 */
void tms9918a_render_sprites_line (const TMS9918A_Config *config, uint16_t line)
{
    uint16_t sprite_attribute_table_base = (((uint16_t) tms9918a_state.regs.sprite_attr_table_base) << 7) & 0x3f10;
    uint16_t pattern_generator_base = (((uint16_t) tms9918a_state.regs.sprite_pg_base) << 11) & 0x3800;
    uint8_t sprite_height = (tms9918a_state.regs.ctrl_1 & TMS9918A_CTRL_1_SPRITE_SIZE) ? 16 : 8;
    TMS9918A_Sprite *line_sprite_buffer [4];
    uint8_t line_sprite_count = 0;
    TMS9918A_Pattern *pattern;
    int32_Point_2D position;

    /* Traverse the sprite list, filling the line sprite buffer */
    for (int i = 0; i < 32 && line_sprite_count < 4; i++)
    {
        TMS9918A_Sprite *sprite = (TMS9918A_Sprite *) &tms9918a_state.vram [sprite_attribute_table_base + i * sizeof (TMS9918A_Sprite)];

        /* Break if there are no more sprites */
        if (sprite->y == 0xd0)
            break;

        /* This number is treated as unsigned when the first line of
         * the sprite is on the screen, but signed when it is not */
        if (sprite->y >= 0xe0)
            position.y = ((int8_t) sprite->y) + 1;
        else
            position.y = sprite->y + 1;

        /* If the sprite is on this line, add it to the buffer */
        if (line >= position.y && line < position.y + sprite_height)
        {
            line_sprite_buffer [line_sprite_count++] = sprite;
        }
    }

    /* Render the sprites in the line sprite buffer.
     * Done in reverse order so that the first sprite is the one left on the screen */
    while (line_sprite_count--)
    {
        TMS9918A_Sprite *sprite = line_sprite_buffer[line_sprite_count];
        uint8_t pattern_index = sprite->pattern;

        /* The most-significant bit of the colour byte decides if we 'early-clock' the sprite */
        if (sprite->colour_ec & 0x80)
        {
            position.x = sprite->x - 32;
        }
        else
        {
            position.x = sprite->x;
        }

        /* TODO: Can we remove these duplicated lines? */
        if (sprite->y >= 0xe0)
        {
            position.y = ((int8_t) sprite->y) + 1;
        }
        else
        {
            position.y = sprite->y + 1;
        }

        /* TODO: Sprite collisions */

        /* TODO: Support 'magnified' sprites, (SPRITE_MAG) */

        if (tms9918a_state.regs.ctrl_1 & TMS9918A_CTRL_1_SPRITE_SIZE)
        {
            pattern_index &= 0xfc;

            /* TODO: It looks like maybe in some places, the base address is in bytes, and in other places it is in patterns...
             *       (or is that just when it is zero?) Find out what's going on and comment. */

            /* TODO: for-loop */

            /* top left */
            pattern = (TMS9918A_Pattern *) &tms9918a_state.vram [pattern_generator_base + (pattern_index * sizeof (TMS9918A_Pattern))];
            tms9918a_render_mode2_pattern_line (config, line, pattern, sprite->colour_ec << 4, position, true);

            /* bottom left */
            pattern++;
            position.y += 8;
            tms9918a_render_mode2_pattern_line (config, line, pattern, sprite->colour_ec << 4, position, true);

            /* top right */
            pattern++;
            position.y -= 8;
            position.x += 8;
            tms9918a_render_mode2_pattern_line (config, line, pattern, sprite->colour_ec << 4, position, true);

            /* bottom right */
            pattern++;
            position.y += 8;
            tms9918a_render_mode2_pattern_line (config, line, pattern, sprite->colour_ec << 4, position, true);

        }
        else
        {
            pattern = (TMS9918A_Pattern *) &tms9918a_state.vram [pattern_generator_base + (pattern_index * sizeof (TMS9918A_Pattern))];
            tms9918a_render_mode2_pattern_line (config, line, pattern, sprite->colour_ec << 4, position, true);
        }
    }
}


/*
 * Render one line of the mode2 background layer.
 */
void tms9918a_render_mode2_background_line (const TMS9918A_Config *config, uint16_t line)
{
    uint16_t name_table_base;
    uint16_t pattern_generator_base;
    uint16_t colour_table_base;
    uint32_t tile_y = line / 8;
    int32_Point_2D position;

    name_table_base = (((uint16_t) tms9918a_state.regs.name_table_base) << 10) & 0x3c00;

    pattern_generator_base = (((uint16_t) tms9918a_state.regs.background_pg_base) << 11) & 0x2000;

    colour_table_base = (((uint16_t) tms9918a_state.regs.colour_table_base) << 6) & 0x2000;

    for (uint32_t tile_x = 0; tile_x < 32; tile_x++)
    {
        uint16_t tile = tms9918a_state.vram [name_table_base + ((tile_y << 5) | tile_x)];

        /* The screen is broken into three 8-row sections */
        if (tile_y >= 8 && tile_y < 16)
        {
            tile += 0x100;
        }
        else if (tile_y >= 16)
        {
            tile += 0x200;
        }
        uint16_t pattern_tile = tile & ((((uint16_t) tms9918a_state.regs.background_pg_base) << 8) | 0xff);
        uint16_t colour_tile  = tile & ((((uint16_t) tms9918a_state.regs.colour_table_base) << 3) | 0x07);

        TMS9918A_Pattern *pattern = (TMS9918A_Pattern *) &tms9918a_state.vram[pattern_generator_base + (pattern_tile * sizeof (TMS9918A_Pattern))];

        uint8_t colours = tms9918a_state.vram[colour_table_base + colour_tile * 8 + (line & 0x07)];

        position.x = 8 * tile_x;
        position.y = 8 * tile_y;
        tms9918a_render_mode2_pattern_line (config, line, pattern, colours, position, false);
    }
}
