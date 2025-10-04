/*
 * Snepulator
 * TI TMS99xx video chip implementation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../snepulator.h"
#include "../util.h"
#include "../save_state.h"

#include "tms9928a.h"

extern Snepulator_State state;

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
uint_pixel_t tms9928a_palette_uncorrected [16] = {
    {   0,   0,   0 }, /* Transparent */
    {   0,   0,   0 }, /* Black */
    {  33, 200,  66 }, /* Medium Green */
    {  94, 220, 120 }, /* Light Green */
    {  84,  85, 237 }, /* Dark Blue */
    { 125, 118, 252 }, /* Light blue */
    { 212,  82,  77 }, /* Dark Red */
    {  66, 235, 245 }, /* Cyan */
    { 252,  85,  84 }, /* Medium Red */
    { 255, 121, 120 }, /* Light Red */
    { 212, 193,  84 }, /* Dark Yellow */
    { 230, 206, 128 }, /* Light Yellow */
    {  33, 176,  59 }, /* Dark Green */
    { 201,  91, 186 }, /* Magenta */
    { 204, 204, 204 }, /* Grey */
    { 255, 255, 255 }  /* White */
};

/* Gamma corrected palette (Wikipedia) */
uint_pixel_t tms9928a_palette [16] = {
    { 0x00 , 0x00, 0x00 }, /* Transparent */
    { 0x00 , 0x00, 0x00 }, /* Black */
    { 0x0a , 0xad, 0x1e }, /* Medium Green */
    { 0x34 , 0xc8, 0x4c }, /* Light Green */
    { 0x2b , 0x2d, 0xe3 }, /* Dark Blue */
    { 0x51 , 0x4b, 0xfb }, /* Light blue */
    { 0xbd , 0x29, 0x25 }, /* Dark Red */
    { 0x1e , 0xe2, 0xef }, /* Cyan */
    { 0xfb , 0x2c, 0x2b }, /* Medium Red */
    { 0xff , 0x5f, 0x4c }, /* Light Red */
    { 0xbd , 0xa2, 0x2b }, /* Dark Yellow */
    { 0xd7 , 0xb4, 0x54 }, /* Light Yellow */
    { 0x0a , 0x8c, 0x18 }, /* Dark Green */
    { 0xaf , 0x32, 0x9a }, /* Magenta */
    { 0xb2 , 0xb2, 0xb2 }, /* Grey */
    { 0xff , 0xff, 0xff }  /* White */
};

/* Display mode details */
static const TMS9928A_ModeInfo Mode0_PAL = {
    .mode = TMS9928A_MODE_0,
    .lines_active = 192,
    .lines_total = 313,
};
static const TMS9928A_ModeInfo Mode0_NTSC = {
    .mode = TMS9928A_MODE_0,
    .lines_active = 192,
    .lines_total = 262,
};
static const TMS9928A_ModeInfo Mode2_PAL = {
    .mode = TMS9928A_MODE_2,
    .lines_active = 192,
    .lines_total = 313,
};
static const TMS9928A_ModeInfo Mode2_NTSC = {
    .mode = TMS9928A_MODE_2,
    .lines_active = 192,
    .lines_total = 262,
};
static const TMS9928A_ModeInfo Mode3_PAL = {
    .mode = TMS9928A_MODE_3,
    .lines_active = 192,
    .lines_total = 313,
};
static const TMS9928A_ModeInfo Mode3_NTSC = {
    .mode = TMS9928A_MODE_3,
    .lines_active = 192,
    .lines_total = 262,
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
uint8_t tms9928a_data_read (TMS9928A_Context *context)
{
    uint8_t data = context->state.read_buffer;

    context->state.first_byte_received = false;

    context->state.read_buffer = context->vram [context->state.address];

    context->state.address = (context->state.address + 1) & 0x3fff;

    return data;
}


/*
 * Write one byte to the tms9928a data port.
 */
void tms9928a_data_write (TMS9928A_Context *context, uint8_t value)
{
    context->state.first_byte_received = false;

    switch (context->state.code)
    {
        case TMS9928A_CODE_VRAM_READ:
        case TMS9928A_CODE_VRAM_WRITE:
        case TMS9928A_CODE_REG_WRITE:
            context->vram [context->state.address] = value;
            break;

        default:
            break;
    }
    context->state.address = (context->state.address + 1) & 0x3fff;
}


/*
 * Read one byte from the tms9928a control (status) port.
 */
uint8_t tms9928a_status_read (TMS9928A_Context *context)
{
    uint8_t status = context->state.status;
    context->state.first_byte_received = false;

    /* Clear on read */
    context->state.status = 0x00;

    return status;
}


/*
 * Write one byte to the tms9928a control port.
 */
void tms9928a_control_write (TMS9928A_Context *context, uint8_t value)
{
    if (!context->state.first_byte_received) /* First byte */
    {
        context->state.first_byte_received = true;
        context->state.address = (context->state.address & 0x3f00) | value;
    }
    else /* Second byte */
    {
        context->state.first_byte_received = false;
        context->state.address = (context->state.address & 0x00ff) | ((uint16_t) (value & 0x3f) << 8);
        context->state.code = value & 0xc0;

        switch (context->state.code)
        {
            case TMS9928A_CODE_VRAM_READ:
                context->state.read_buffer = context->vram[context->state.address++];
                break;
            case TMS9928A_CODE_VRAM_WRITE:
                break;
            case TMS9928A_CODE_REG_WRITE:
                if ((value & 0x0f) <= 10)
                {
                    ((uint8_t *) &context->state.regs) [value & 0x0f] = context->state.address & 0x00ff;
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
bool tms9928a_get_interrupt (TMS9928A_Context *context)
{
    bool interrupt = false;

    /* Frame interrupt */
    if (context->state.regs.ctrl_1_frame_int_en && (context->state.status & TMS9928A_STATUS_INT))
    {
        interrupt = true;
    }

    return interrupt;
}


/*
 * Render one line of an 8x8 pattern.
 */
static void tms9928a_draw_pattern_background (TMS9928A_Context *context, uint16_t line, TMS9928A_Pattern *pattern_base,
                                              uint8_t tile_colours, int_point_t offset)
{
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

        if (colour_index == TMS9928A_COLOUR_TRANSPARENT)
        {
            colour_index = context->state.regs.background_colour & 0x0f;
        }

        uint_pixel_t pixel = context->palette [colour_index];
        context->frame_buffer.active_area [(offset.x + x) + line * context->frame_buffer.width] = pixel;
    }
}


/*
 * Render one line of an 8x8 pattern.
 * Sprite version.
 */
static void tms9928a_draw_pattern_sprite (TMS9928A_Context *context, uint16_t line, TMS9928A_Pattern *pattern_base,
                                          uint8_t tile_colours, int_point_t position, bool magnify)
{
    uint8_t line_data;

    uint8_t background_colour = tile_colours & 0x0f;
    uint8_t foreground_colour = tile_colours >> 4;

    line_data = pattern_base->data [(line - position.y) >> magnify];

    for (uint32_t x = 0; x < (8 << magnify); x++)
    {
        /* Don't draw texture pixels that fall outside of the screen */
        if (x + position.x >= 256)
            continue;

        uint8_t colour_index = ((line_data & (0x80 >> (x >> magnify))) ? foreground_colour : background_colour);

        if (colour_index == TMS9928A_COLOUR_TRANSPARENT)
        {
            continue;
        }

        /* Sprite collision detection */
        if (context->state.collision_buffer [x + position.x])
        {
            context->state.status |= TMS9928A_SPRITE_COLLISION;
        }
        context->state.collision_buffer [x + position.x] = true;

        uint_pixel_t pixel = context->palette [colour_index];
        context->frame_buffer.active_area [(position.x + x) + line * context->frame_buffer.width] = pixel;
    }
}


/*
 * Render one line of the sprite layer.
 * Sprites can be either 8×8 pixels, or 16×16.
 */
void tms9928a_draw_sprites (TMS9928A_Context *context, uint16_t line)
{
    uint16_t sprite_attribute_table_base = (((uint16_t) context->state.regs.sprite_attr_table_base) << 7) & 0x3f80;
    uint16_t pattern_generator_base = (((uint16_t) context->state.regs.sprite_pg_base) << 11) & 0x3800;
    uint8_t sprite_size = context->state.regs.ctrl_1_sprite_size ? 16 : 8;
    TMS9928A_Sprite *line_sprite_buffer [32];
    uint8_t line_sprite_count = 0;
    TMS9928A_Pattern *pattern;
    int_point_t position;
    bool magnify = false;

    /* Sprite magnification */
    if (context->state.regs.ctrl_1_sprite_mag)
    {
        magnify = true;
    }

    /* Traverse the sprite list, filling the line sprite buffer */
    for (int i = 0; i < 32; i++)
    {
        TMS9928A_Sprite *sprite = (TMS9928A_Sprite *) &context->vram [sprite_attribute_table_base + i * sizeof (TMS9928A_Sprite)];

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
            /* Update the fifth-sprite number when setting the flag. */
            /* Note: On real hardware with no overflow, the fifth-sprite-number
             *       in the status register contains the number of sprites in
             *       the sprite list. List length 31 and 32 share a value of 31.
             *       Does anything rely on this behaviour? */
            if (line_sprite_count == 4 && !context->remove_sprite_limit)
            {
                if (!(context->state.status & TMS9928A_SPRITE_OVERFLOW))
                {
                    context->state.status |= TMS9928A_SPRITE_OVERFLOW;
                    context->state.status = (context->state.status & 0xe0) | i;
                }
                break;
            }
            line_sprite_buffer [line_sprite_count++] = sprite;
        }
    }

    /* Clear the sprite collision buffer */
    memset (context->state.collision_buffer, 0, sizeof (context->state.collision_buffer));

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

        if (context->state.regs.ctrl_1_sprite_size)
        {
            pattern_index &= 0xfc;
            pattern = (TMS9928A_Pattern *) &context->vram [pattern_generator_base + (pattern_index * sizeof (TMS9928A_Pattern))];
            int_point_t sub_position;

            for (int i = 0; i < 4; i++)
            {
                sub_position.x = position.x + ((i & 2) ? (8 << magnify) : 0);
                sub_position.y = position.y + ((i & 1) ? (8 << magnify) : 0);
                tms9928a_draw_pattern_sprite (context, line, pattern + i, sprite->colour_ec << 4, sub_position, magnify);
            }
        }
        else
        {
            pattern = (TMS9928A_Pattern *) &context->vram [pattern_generator_base + (pattern_index * sizeof (TMS9928A_Pattern))];
            tms9928a_draw_pattern_sprite (context, line, pattern, sprite->colour_ec << 4, position, magnify);
        }
    }
}


/*
 * Render one line of the mode0 background layer.
 */
void tms9928a_mode0_draw_background (TMS9928A_Context *context, uint16_t line)
{
    uint16_t name_table_base;
    uint16_t pattern_generator_base;
    uint16_t colour_table_base;
    uint32_t tile_y = line / 8;
    int_point_t position;

    name_table_base = (((uint16_t) context->state.regs.name_table_base) << 10) & 0x3c00;

    pattern_generator_base = (((uint16_t) context->state.regs.background_pg_base) << 11) & 0x3800;

    colour_table_base = (((uint16_t) context->state.regs.colour_table_base) << 6) & 0x3fc0;

    for (uint32_t tile_x = 0; tile_x < 32; tile_x++)
    {
        uint16_t tile = context->vram [name_table_base + ((tile_y << 5) | tile_x)];

        TMS9928A_Pattern *pattern = (TMS9928A_Pattern *) &context->vram [pattern_generator_base + (tile * sizeof (TMS9928A_Pattern))];
        uint8_t colours = context->vram [colour_table_base + (tile >> 3)];

        position.x = 8 * tile_x;
        position.y = 8 * tile_y;
        tms9928a_draw_pattern_background (context, line, pattern, colours, position);
    }
}


/*
 * Render one line of the mode2 background layer.
 */
void tms9928a_mode2_draw_background (TMS9928A_Context *context, uint16_t line)
{
    uint16_t name_table_base;
    uint16_t pattern_generator_base;
    uint16_t colour_table_base;
    uint32_t tile_y = line / 8;
    int_point_t position;

    name_table_base = (((uint16_t) context->state.regs.name_table_base) << 10) & 0x3c00;

    pattern_generator_base = (((uint16_t) context->state.regs.background_pg_base) << 11) & 0x2000;

    colour_table_base = (((uint16_t) context->state.regs.colour_table_base) << 6) & 0x2000;

    for (uint32_t tile_x = 0; tile_x < 32; tile_x++)
    {
        uint16_t tile = context->vram [name_table_base + ((tile_y << 5) | tile_x)];

        /* The screen is broken into three 8-row sections */
        if (tile_y >= 8 && tile_y < 16)
        {
            tile += 0x100;
        }
        else if (tile_y >= 16)
        {
            tile += 0x200;
        }
        uint16_t pattern_tile = tile & ((((uint16_t) context->state.regs.background_pg_base) << 8) | 0xff);
        uint16_t colour_tile  = tile & ((((uint16_t) context->state.regs.colour_table_base) << 3) | 0x07);

        TMS9928A_Pattern *pattern = (TMS9928A_Pattern *) &context->vram [pattern_generator_base + (pattern_tile * sizeof (TMS9928A_Pattern))];

        uint8_t colours = context->vram [colour_table_base + colour_tile * 8 + (line & 0x07)];

        position.x = 8 * tile_x;
        position.y = 8 * tile_y;
        tms9928a_draw_pattern_background (context, line, pattern, colours, position);
    }
}


/*
 * Render one line of the mode3 background layer.
 */
void tms9928a_mode3_draw_background (TMS9928A_Context *context, uint16_t line)
{
    uint16_t name_table_base;
    uint16_t pattern_generator_base;
    uint32_t tile_y = line / 8;

    name_table_base = (((uint16_t) context->state.regs.name_table_base) << 10) & 0x3c00;
    pattern_generator_base = (((uint16_t) context->state.regs.background_pg_base) << 11) & 0x3800;

    for (uint32_t tile_x = 0; tile_x < 32; tile_x++)
    {
        uint16_t tile = context->vram [name_table_base + ((tile_y << 5) | tile_x)];

        TMS9928A_Pattern *pattern = (TMS9928A_Pattern *) &context->vram [pattern_generator_base + (tile * sizeof (TMS9928A_Pattern))];
        uint8_t colour_left  = pattern->data [((tile_y & 0x03) << 1) + ((line / 4) & 1)] >> 4;
        uint8_t colour_right = pattern->data [((tile_y & 0x03) << 1) + ((line / 4) & 1)] & 0x0f;

        if (colour_left == TMS9928A_COLOUR_TRANSPARENT)
        {
            colour_left = context->state.regs.background_colour & 0x0f;
        }
        uint_pixel_t pixel_left = context->palette [colour_left];
        context->frame_buffer.active_area [(8 * tile_x + 0) + line * context->frame_buffer.width] = pixel_left;
        context->frame_buffer.active_area [(8 * tile_x + 1) + line * context->frame_buffer.width] = pixel_left;
        context->frame_buffer.active_area [(8 * tile_x + 2) + line * context->frame_buffer.width] = pixel_left;
        context->frame_buffer.active_area [(8 * tile_x + 3) + line * context->frame_buffer.width] = pixel_left;

        if (colour_right == TMS9928A_COLOUR_TRANSPARENT)
        {
            colour_right = context->state.regs.background_colour & 0x0f;
        }
        uint_pixel_t pixel_right = context->palette [colour_right];
        context->frame_buffer.active_area [(8 * tile_x + 4) + line * context->frame_buffer.width] = pixel_right;
        context->frame_buffer.active_area [(8 * tile_x + 5) + line * context->frame_buffer.width] = pixel_right;
        context->frame_buffer.active_area [(8 * tile_x + 6) + line * context->frame_buffer.width] = pixel_right;
        context->frame_buffer.active_area [(8 * tile_x + 7) + line * context->frame_buffer.width] = pixel_right;
    }
}


/*
 * Assemble the three mode-bits.
 */
uint8_t tms9928a_get_mode (TMS9928A_Context *context)
{
    return (context->state.regs.ctrl_1_mode_1 ? BIT_0 : 0) +
           (context->state.regs.ctrl_0_mode_2 ? BIT_1 : 0) +
           (context->state.regs.ctrl_1_mode_3 ? BIT_2 : 0);
}


/*
 * Render one active line of output for the tms9928a.
 */
void tms9928a_render_line (TMS9928A_Context *context, uint16_t line)
{
    uint_pixel_t video_backdrop;

    /* Background */
    video_backdrop = context->palette [context->state.regs.background_colour & 0x0f];

    /* Note: The top/bottom borders use the background colour of the first and last active lines. */
    context->frame_buffer.backdrop [line] = video_backdrop;

    /* If blanking is enabled, fill the whole screen with the backdrop colour.
     * Otherwise, fill only the border. */
    if (!context->state.regs.ctrl_1_blank && !context->disable_blanking)
    {
        for (int x = 0; x < context->frame_buffer.width; x++)
        {
            context->frame_buffer.active_area [x + line * context->frame_buffer.width] = video_backdrop;
        }

        /* Return without rendering patterns if BLANK is enabled */
        return;
    }

    if (context->mode == TMS9928A_MODE_0)
    {
        tms9928a_mode0_draw_background (context, line);
        tms9928a_draw_sprites (context, line);
    }
    else if (context->mode & TMS9928A_MODE_2)
    {
        tms9928a_mode2_draw_background (context, line);
        tms9928a_draw_sprites (context, line);
    }
    else if (context->mode & TMS9928A_MODE_3)
    {
        tms9928a_mode3_draw_background (context, line);
        tms9928a_draw_sprites (context, line);
    }
}


/*
 * Called once per frame to update parameters based on the mode.
 */
static void tms9928a_vdp_update_mode (TMS9928A_Context *context)
{
    const TMS9928A_ModeInfo *config;
    TMS9928A_Mode mode = tms9928a_get_mode (context);

    switch (mode)
    {
        case TMS9928A_MODE_0: /* Mode 0: 32 x 24 8-byte tiles, sprites enabled. */
            config = (context->format == VIDEO_FORMAT_NTSC) ? &Mode0_NTSC : &Mode0_PAL;
            break;

        case TMS9928A_MODE_2: /* Mode 2: 32 × 24 8-byte tiles, sprites enabled, three colour/pattern tables */
            config = (context->format == VIDEO_FORMAT_NTSC) ? &Mode2_NTSC : &Mode2_PAL;
            break;

        case TMS9928A_MODE_3: /* Mode 3: "Multicolour" Mode, 4×4 pixel blocks, sprites enabled */
            config = (context->format == VIDEO_FORMAT_NTSC) ? &Mode3_NTSC : &Mode3_PAL;
            break;

        default:
            /* Some games briefly pass through an unsupported mode.
             * Assume Mode 0 to keep line counters and interrupts ticking. */
            config = (context->format == VIDEO_FORMAT_NTSC) ? &Mode0_NTSC : &Mode0_PAL;
            break;
    }

    context->mode         = config->mode;
    context->lines_total  = config->lines_total;
    context->lines_active = config->lines_active;
}


/*
 * Run one scanline on the tms9928a.
 */
void tms9928a_run_one_scanline (TMS9928A_Context *context)
{
    /* Update the V-Counter */
    context->state.line = (context->state.line + 1) % context->lines_total;

    /* If this is the first line, update the mode */
    if (context->state.line == 0)
    {
        tms9928a_vdp_update_mode (context);
        context->frame_buffer.width = 256;
        context->frame_buffer.height = 192;
    }

    /* If this is an active line, render it */
    if (context->state.line < context->lines_active)
    {
        tms9928a_render_line (context, context->state.line);
    }

    /* If this the final active line, copy to the frame buffer */
    if (context->state.line == context->lines_active - 1)
    {
        context->frame_done (context->parent);

#if DEVELOPER_BUILD
        /* Update statistics (rolling average) */
        static int64_t vdp_previous_completion_time = 0;
        static int64_t vdp_current_time = 0;
        vdp_current_time = util_get_ticks_us ();
        if (vdp_previous_completion_time)
        {
            state.vdp_framerate *= 0.98;
            state.vdp_framerate += 0.02 * (1000000.0 / (vdp_current_time - vdp_previous_completion_time));
        }
        vdp_previous_completion_time = vdp_current_time;
#endif
    }

    /* Check for frame interrupt */
    if (context->state.line == context->lines_active + 1)
    {
        context->state.status |= TMS9928A_STATUS_INT;
    }
}


/*
 * Reset the tms9928a registers and memory to power-on defaults.
 */
TMS9928A_Context *tms9928a_init (void *parent, void (* frame_done) (void *))
{
    TMS9928A_Context *context;

    context = calloc (1, sizeof (TMS9928A_Context));
    if (context == NULL)
    {
        snepulator_error ("Error", "Unable to allocate memory for TMS9928A_Context");
        return NULL;
    }

    context->parent = parent;
    context->frame_done = frame_done;

    context->palette = tms9928a_palette;

    tms9928a_vdp_update_mode (context);

    return context;
}


/*
 * Export tms9928a state.
 */
void tms9928a_state_save (TMS9928A_Context *context)
{
    TMS9928A_State tms9928a_state_be = {
        .regs =                   context->state.regs,
        .line =                   util_hton16 (context->state.line),
        .address =                util_hton16 (context->state.address),
        .first_byte_received =    context->state.first_byte_received,
        .code =                   context->state.code,
        .read_buffer =            context->state.read_buffer,
        .status =                 context->state.status,
        .collision_buffer =       {},
        .cram =                   {},
        .line_interrupt_counter = context->state.line_interrupt_counter,
        .line_interrupt =         context->state.line_interrupt,
        .h_counter =              context->state.h_counter,
        .v_counter =              context->state.v_counter,
        .cram_latch =             context->state.cram_latch
    };

    memcpy (tms9928a_state_be.collision_buffer, context->state.collision_buffer, 256);
    memcpy (tms9928a_state_be.cram, context->state.cram, sizeof (context->state.cram));

    save_state_section_add (SECTION_ID_VDP, 1, sizeof (tms9928a_state_be), &tms9928a_state_be);
}


/*
 * Import tms9928a state.
 */
void tms9928a_state_load (TMS9928A_Context *context, uint32_t version, uint32_t size, void *data)
{
    TMS9928A_State tms9928a_state_be;

    if (size == sizeof (tms9928a_state_be))
    {
        memcpy (&tms9928a_state_be, data, sizeof (tms9928a_state_be));

        context->state.regs =                   tms9928a_state_be.regs;
        context->state.line =                   util_ntoh16 (tms9928a_state_be.line);
        context->state.address =                util_ntoh16 (tms9928a_state_be.address);
        context->state.first_byte_received =    tms9928a_state_be.first_byte_received;
        context->state.code =                   tms9928a_state_be.code;
        context->state.read_buffer =            tms9928a_state_be.read_buffer;
        context->state.status =                 tms9928a_state_be.status;

        memcpy (context->state.collision_buffer, tms9928a_state_be.collision_buffer, 256);
        memcpy (context->state.cram, tms9928a_state_be.cram, sizeof (context->state.cram));

        context->state.line_interrupt_counter = tms9928a_state_be.line_interrupt_counter;
        context->state.line_interrupt =         tms9928a_state_be.line_interrupt;
        context->state.h_counter =              tms9928a_state_be.h_counter;
        context->state.v_counter =              tms9928a_state_be.v_counter;
        context->state.cram_latch =             tms9928a_state_be.cram_latch;
    }
    else
    {
        snepulator_error ("Error", "Save-state contains incompatible VDP state size");
    }
}
