/*
 * Implementation of the TI TMS9928A / TMS9929A video chip.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include <SDL2/SDL.h>

#include "../util.h"
#include "../snepulator.h"

#include "tms9928a.h"

extern Snepulator_State state;
extern pthread_mutex_t video_mutex;
TMS9928A_State tms9928a_state;

const char *tms9928a_mode_name [16] = {
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

/* "Datasheet" palette */
#define TMS9928A_PALETTE_DATASHEET { \
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
    { 204 / 255.0f, 204 / 255.0f, 204 / 255.0f }, /* Grey */ \
    { 255 / 255.0f, 255 / 255.0f, 255 / 255.0f }  /* White */ \
}

/* Gamma corrected palette (Wikipedia) */
#define TMS9928A_PALETTE { \
    { 0x00 / 255.0f, 0x00 / 255.0f, 0x00 / 255.0f }, /* Transparent */ \
    { 0x00 / 255.0f, 0x00 / 255.0f, 0x00 / 255.0f }, /* Black */ \
    { 0x0a / 255.0f, 0xad / 255.0f, 0x1e / 255.0f }, /* Medium Green */ \
    { 0x34 / 255.0f, 0xc8 / 255.0f, 0x4c / 255.0f }, /* Light Green */ \
    { 0x2b / 255.0f, 0x2d / 255.0f, 0xe3 / 255.0f }, /* Dark Blue */ \
    { 0x51 / 255.0f, 0x4b / 255.0f, 0xfb / 255.0f }, /* Light blue */ \
    { 0xbd / 255.0f, 0x29 / 255.0f, 0x25 / 255.0f }, /* Dark Red */ \
    { 0x1e / 255.0f, 0xe2 / 255.0f, 0xef / 255.0f }, /* Cyan */ \
    { 0xfb / 255.0f, 0x2c / 255.0f, 0x2b / 255.0f }, /* Medium Red */ \
    { 0xff / 255.0f, 0x5f / 255.0f, 0x4c / 255.0f }, /* Light Red */ \
    { 0xbd / 255.0f, 0xa2 / 255.0f, 0x2b / 255.0f }, /* Dark Yellow */ \
    { 0xd7 / 255.0f, 0xb4 / 255.0f, 0x54 / 255.0f }, /* Light Yellow */ \
    { 0x0a / 255.0f, 0x8c / 255.0f, 0x18 / 255.0f }, /* Dark Green */ \
    { 0xaf / 255.0f, 0x32 / 255.0f, 0x9a / 255.0f }, /* Magenta */ \
    { 0xb2 / 255.0f, 0xb2 / 255.0f, 0xb2 / 255.0f }, /* Grey */ \
    { 0xff / 255.0f, 0xff / 255.0f, 0xff / 255.0f }  /* White */ \
}

/* Display mode details */
static const TMS9928A_Config Mode0_PAL = {
    .mode = TMS9928A_MODE_0,
    .lines_active = 192,
    .lines_total = 313,
    .palette = TMS9928A_PALETTE
};
static const TMS9928A_Config Mode0_NTSC = {
    .mode = TMS9928A_MODE_0,
    .lines_active = 192,
    .lines_total = 262,
    .palette = TMS9928A_PALETTE
};
static const TMS9928A_Config Mode2_PAL = {
    .mode = TMS9928A_MODE_2,
    .lines_active = 192,
    .lines_total = 313,
    .palette = TMS9928A_PALETTE
};
static const TMS9928A_Config Mode2_NTSC = {
    .mode = TMS9928A_MODE_2,
    .lines_active = 192,
    .lines_total = 262,
    .palette = TMS9928A_PALETTE
};


/*
 * Supply a human-readable string describing the specified mode.
 */
const char *tms9928a_mode_name_get (TMS9928A_Mode mode)
{
    return tms9928a_mode_name [mode & 0x0f];
}


/*
 * Read one byte from the tms9928a data port.
 */
uint8_t tms9928a_data_read ()
{
    uint8_t data = tms9928a_state.read_buffer;

    tms9928a_state.first_byte_received = false;

    tms9928a_state.read_buffer = tms9928a_state.vram [tms9928a_state.address];

    tms9928a_state.address = (tms9928a_state.address + 1) & 0x3fff;

    return data;

}


/*
 * Write one byte to the tms9928a data port.
 */
void tms9928a_data_write (uint8_t value)
{
    tms9928a_state.first_byte_received = false;

    switch (tms9928a_state.code)
    {
        case TMS9928A_CODE_VRAM_READ:
        case TMS9928A_CODE_VRAM_WRITE:
        case TMS9928A_CODE_REG_WRITE:
            tms9928a_state.vram [tms9928a_state.address] = value;
            break;

        default:
            break;
    }
    tms9928a_state.address = (tms9928a_state.address + 1) & 0x3fff;
}


/*
 * Read one byte from the tms9928a control (status) port.
 */
uint8_t tms9928a_status_read ()
{
    uint8_t status = tms9928a_state.status;
    tms9928a_state.first_byte_received = false;

    /* Clear on read */
    tms9928a_state.status = 0x00;
    tms9928a_state.line_interrupt = false; /* "The flag remains set until the control port (IO port 0xbf) is read */

    return status;
}


/*
 * Write one byte to the tms9928a control port.
 */
void tms9928a_control_write (uint8_t value)
{
    if (!tms9928a_state.first_byte_received) /* First byte */
    {
        tms9928a_state.first_byte_received = true;
        tms9928a_state.address = (tms9928a_state.address & 0x3f00) | ((uint16_t) value << 0);
    }
    else /* Second byte */
    {
        tms9928a_state.first_byte_received = false;
        tms9928a_state.address = (tms9928a_state.address & 0x00ff) | ((uint16_t) (value & 0x3f) << 8);
        tms9928a_state.code = value & 0xc0;

        switch (tms9928a_state.code)
        {
            case TMS9928A_CODE_VRAM_READ:
                tms9928a_state.read_buffer = tms9928a_state.vram[tms9928a_state.address++];
                break;
            case TMS9928A_CODE_VRAM_WRITE:
                break;
            case TMS9928A_CODE_REG_WRITE:
                if ((value & 0x0f) <= 10)
                {
                    ((uint8_t *) &tms9928a_state.regs) [value & 0x0f] = tms9928a_state.address & 0x00ff;
                }
                break;
            default:
                break;
        }
    }
}


/*
 * Check if the tms9928a is currently requesting an interrupt.
 */
bool tms9928a_get_interrupt (void)
{
    bool interrupt = false;

    /* Frame interrupt */
    if ((tms9928a_state.regs.ctrl_1 & TMS9928A_CTRL_1_FRAME_INT_EN) && (tms9928a_state.status & TMS9928A_STATUS_INT))
    {
        interrupt = true;
    }

    return interrupt;
}


/*
 * Render one line of an 8x8 pattern.
 */
static void tms9928a_render_pattern_line (const TMS9928A_Config *config, uint16_t line, TMS9928A_Pattern *pattern_base,
                                          uint8_t tile_colours, int32_Point_2D offset, bool sprite, bool magnify)
{
    uint8_t line_data;

    uint8_t background_colour = tile_colours & 0x0f;
    uint8_t foreground_colour = tile_colours >> 4;

    line_data = pattern_base->data [(line - offset.y) >> magnify];

    for (uint32_t x = 0; x < (8 << magnify); x++)
    {
        /* Don't draw texture pixels that fall outside of the screen */
        if (x + offset.x >= 256)
            continue;

        uint8_t colour_index = ((line_data & (0x80 >> (x >> magnify))) ? foreground_colour : background_colour);

        if (colour_index == TMS9928A_COLOUR_TRANSPARENT)
        {
            if (sprite == true)
            {
                continue;
            }
            else
            {
                colour_index = tms9928a_state.regs.background_colour & 0x0f;
            }
        }

        float_Colour pixel = config->palette [colour_index];
        tms9928a_state.frame_current [(offset.x + x + VIDEO_SIDE_BORDER) + (state.video_out_first_active_line + line) * VIDEO_BUFFER_WIDTH] = pixel;
    }
}


/*
 * Render this line's sprites for modes with sprite support.
 * Sprites can be either 8×8 pixels, or 16×16.
 */
void tms9928a_render_sprites_line (const TMS9928A_Config *config, uint16_t line)
{
    uint16_t sprite_attribute_table_base = (((uint16_t) tms9928a_state.regs.sprite_attr_table_base) << 7) & 0x3f80;
    uint16_t pattern_generator_base = (((uint16_t) tms9928a_state.regs.sprite_pg_base) << 11) & 0x3800;
    uint8_t sprite_size = (tms9928a_state.regs.ctrl_1 & TMS9928A_CTRL_1_SPRITE_SIZE) ? 16 : 8;
    TMS9928A_Sprite *line_sprite_buffer [4];
    uint8_t line_sprite_count = 0;
    TMS9928A_Pattern *pattern;
    int32_Point_2D position;
    bool magnify = false;

    /* Sprite magnification */
    if (tms9928a_state.regs.ctrl_1 & TMS9928A_CTRL_1_SPRITE_MAG)
    {
        magnify = true;
    }

    /* Traverse the sprite list, filling the line sprite buffer */
    for (int i = 0; i < 32 && line_sprite_count < 4; i++)
    {
        TMS9928A_Sprite *sprite = (TMS9928A_Sprite *) &tms9928a_state.vram [sprite_attribute_table_base + i * sizeof (TMS9928A_Sprite)];

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
        if (line >= position.y && line < position.y + (sprite_size << magnify))
        {
            line_sprite_buffer [line_sprite_count++] = sprite;
        }
    }

    /* Render the sprites in the line sprite buffer.
     * Done in reverse order so that the first sprite is the one left on the screen */
    while (line_sprite_count--)
    {
        TMS9928A_Sprite *sprite = line_sprite_buffer [line_sprite_count];
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

        if (tms9928a_state.regs.ctrl_1 & TMS9928A_CTRL_1_SPRITE_SIZE)
        {
            pattern_index &= 0xfc;
            pattern = (TMS9928A_Pattern *) &tms9928a_state.vram [pattern_generator_base + (pattern_index * sizeof (TMS9928A_Pattern))];
            int32_Point_2D sub_position;

            for (int i = 0; i < 4; i++)
            {
                sub_position.x = position.x + ((i & 2) ? (8 << magnify) : 0);
                sub_position.y = position.y + ((i & 1) ? (8 << magnify) : 0);
                tms9928a_render_pattern_line (config, line, pattern + i, sprite->colour_ec << 4, sub_position, true, magnify);
            }
        }
        else
        {
            pattern = (TMS9928A_Pattern *) &tms9928a_state.vram [pattern_generator_base + (pattern_index * sizeof (TMS9928A_Pattern))];
            tms9928a_render_pattern_line (config, line, pattern, sprite->colour_ec << 4, position, true, magnify);
        }
    }
}


/*
 * Render one line of the mode0 background layer.
 */
void tms9928a_render_mode0_background_line (const TMS9928A_Config *config, uint16_t line)
{
    uint16_t name_table_base;
    uint16_t pattern_generator_base;
    uint16_t colour_table_base;
    uint32_t tile_y = line / 8;
    int32_Point_2D position;

    name_table_base = (((uint16_t) tms9928a_state.regs.name_table_base) << 10) & 0x3c00;

    pattern_generator_base = (((uint16_t) tms9928a_state.regs.background_pg_base) << 11) & 0x3800;

    colour_table_base = (((uint16_t) tms9928a_state.regs.colour_table_base) << 6) & 0x3fc0;

    for (uint32_t tile_x = 0; tile_x < 32; tile_x++)
    {
        uint16_t tile = tms9928a_state.vram [name_table_base + ((tile_y << 5) | tile_x)];

        TMS9928A_Pattern *pattern = (TMS9928A_Pattern *) &tms9928a_state.vram [pattern_generator_base + (tile * sizeof (TMS9928A_Pattern))];
        uint8_t colours = tms9928a_state.vram [colour_table_base + (tile >> 3)];

        position.x = 8 * tile_x;
        position.y = 8 * tile_y;
        tms9928a_render_pattern_line (config, line, pattern, colours, position, false, false);
    }
}


/*
 * Render one line of the mode2 background layer.
 */
void tms9928a_render_mode2_background_line (const TMS9928A_Config *config, uint16_t line)
{
    uint16_t name_table_base;
    uint16_t pattern_generator_base;
    uint16_t colour_table_base;
    uint32_t tile_y = line / 8;
    int32_Point_2D position;

    name_table_base = (((uint16_t) tms9928a_state.regs.name_table_base) << 10) & 0x3c00;

    pattern_generator_base = (((uint16_t) tms9928a_state.regs.background_pg_base) << 11) & 0x2000;

    colour_table_base = (((uint16_t) tms9928a_state.regs.colour_table_base) << 6) & 0x2000;

    for (uint32_t tile_x = 0; tile_x < 32; tile_x++)
    {
        uint16_t tile = tms9928a_state.vram [name_table_base + ((tile_y << 5) | tile_x)];

        /* The screen is broken into three 8-row sections */
        if (tile_y >= 8 && tile_y < 16)
        {
            tile += 0x100;
        }
        else if (tile_y >= 16)
        {
            tile += 0x200;
        }
        uint16_t pattern_tile = tile & ((((uint16_t) tms9928a_state.regs.background_pg_base) << 8) | 0xff);
        uint16_t colour_tile  = tile & ((((uint16_t) tms9928a_state.regs.colour_table_base) << 3) | 0x07);

        TMS9928A_Pattern *pattern = (TMS9928A_Pattern *) &tms9928a_state.vram [pattern_generator_base + (pattern_tile * sizeof (TMS9928A_Pattern))];

        uint8_t colours = tms9928a_state.vram [colour_table_base + colour_tile * 8 + (line & 0x07)];

        position.x = 8 * tile_x;
        position.y = 8 * tile_y;
        tms9928a_render_pattern_line (config, line, pattern, colours, position, false, false);
    }
}


/*
 * Assemble the three mode-bits.
 */
static uint8_t tms9928a_mode_get (void)
{
    return ((tms9928a_state.regs.ctrl_1 & TMS9928A_CTRL_1_MODE_1) ? BIT_0 : 0) +
           ((tms9928a_state.regs.ctrl_0 & TMS9928A_CTRL_0_MODE_2) ? BIT_1 : 0) +
           ((tms9928a_state.regs.ctrl_1 & TMS9928A_CTRL_1_MODE_3) ? BIT_2 : 0);
}


/*
 * Render one active line of output for the tms9928a.
 */
void tms9928a_render_line (const TMS9928A_Config *config, uint16_t line)
{
    float_Colour video_background =     { .r = 0.0f, .g = 0.0f, .b = 0.0f };

    state.video_out_first_active_line = (VIDEO_BUFFER_LINES - config->lines_active) / 2;

    /* Background */
    if (!(tms9928a_state.regs.ctrl_1 & TMS9928A_CTRL_1_BLANK))
    {
        /* Display is blank */
    }
    else
    {
        video_background = config->palette [tms9928a_state.regs.background_colour & 0x0f];
    }

    /* Note: The top/bottom borders use the background colour of the first and last active lines. */

    /* Top border */
    if (line == 0)
    {
        for (uint32_t top_line = 0; top_line < state.video_out_first_active_line; top_line++)
        {
            for (uint32_t x = 0; x < VIDEO_BUFFER_WIDTH; x++)
            {
                tms9928a_state.frame_current [x + top_line * VIDEO_BUFFER_WIDTH] = video_background;
            }
        }
    }

    /* Side borders */
    for (int x = 0; x < VIDEO_BUFFER_WIDTH; x++)
    {
        tms9928a_state.frame_current [x + (state.video_out_first_active_line + line) * VIDEO_BUFFER_WIDTH] = video_background;
    }

    /* Bottom border */
    if (line == config->lines_active - 1)
    {
        for (uint32_t bottom_line = state.video_out_first_active_line + config->lines_active; bottom_line < VIDEO_BUFFER_LINES; bottom_line++)
        {
            for (uint32_t x = 0; x < VIDEO_BUFFER_WIDTH; x++)
            {
                tms9928a_state.frame_current [x + bottom_line * VIDEO_BUFFER_WIDTH] = video_background;
            }
        }
    }

    /* Return without rendering patterns if BLANK is enabled */
    if (!(tms9928a_state.regs.ctrl_1 & TMS9928A_CTRL_1_BLANK))
    {
        return;
    }

    if (config->mode == TMS9928A_MODE_0)
    {
        tms9928a_render_mode0_background_line (config, line);
        tms9928a_render_sprites_line (config, line);
    }
    else if (config->mode & TMS9928A_MODE_2)
    {
        tms9928a_render_mode2_background_line (config, line);
        tms9928a_render_sprites_line (config, line);
    }
}


/*
 * Run one scanline on the tms9928a.
 */
void tms9928a_run_one_scanline (void)
{
    static uint16_t line = 0;

    const TMS9928A_Config *config;
    TMS9928A_Mode mode = tms9928a_mode_get ();

    switch (mode)
    {
        case TMS9928A_MODE_0: /* Mode 0: 32 x 24 8-byte tiles, sprites enabled. */
            config = (state.format == VIDEO_FORMAT_NTSC) ? &Mode0_NTSC : &Mode0_PAL;
            break;

        case TMS9928A_MODE_2: /* Mode 2: 32 × 24 8-byte tiles, sprites enabled, three colour/pattern tables */
            config = (state.format == VIDEO_FORMAT_NTSC) ? &Mode2_NTSC : &Mode2_PAL;
            break;

        default:
            /* TMS9928A_MODE_1 not implemented */
            return;
    }

    /* If this is an active line, render it */
    if (line < config->lines_active)
    {
        tms9928a_render_line (config, line);
    }

    /* If this the final active line, copy to the frame buffer */
    if (line == config->lines_active - 1)
    {
        pthread_mutex_lock (&video_mutex);
        state.video_width = 256;
        state.video_height = 192;
        memcpy (state.video_out_data, tms9928a_state.frame_current, sizeof (tms9928a_state.frame_current));
        pthread_mutex_unlock (&video_mutex);

        /* Update statistics (rolling average) */
        static int vdp_previous_completion_time = 0;
        static int vdp_current_time = 0;
        vdp_current_time = SDL_GetTicks ();
        if (vdp_previous_completion_time)
        {
            state.vdp_framerate *= 0.95;
            state.vdp_framerate += 0.05 * (1000.0 / (vdp_current_time - vdp_previous_completion_time));
        }
        vdp_previous_completion_time = vdp_current_time;
    }

    /* Check for frame interrupt */
    if (line == config->lines_active + 1)
        tms9928a_state.status |= TMS9928A_STATUS_INT;

    /* Update values for the next line */
    line = (line + 1) % config->lines_total;
}


/*
 * Reset the tms9928a registers and memory to power-on defaults.
 */
void tms9928a_init (void)
{
    memset (&tms9928a_state.regs, 0, sizeof (tms9928a_state.regs));
    memset (&tms9928a_state.vram, 0, sizeof (tms9928a_state.vram));
}
