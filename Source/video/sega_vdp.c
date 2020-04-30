/*
 * Implementation for the Sega Master System VDP chip.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include "../util.h"
#include "../snepulator.h"
#include "../sms.h"
extern Snepulator snepulator;

#include "tms9918a.h"
#include "sega_vdp.h"

/* Constants */
#define VDP_CRAM_SIZE (32)

/* Macros */
#define VDP_TO_RED(C)   ((0xff / 3) * (((C) & 0x03) >> 0))
#define VDP_TO_GREEN(C) ((0xff / 3) * (((C) & 0x0c) >> 2))
#define VDP_TO_BLUE(C)  ((0xff / 3) * (((C) & 0x30) >> 4))

/* TODO: Does the v_counter exist outside of Mode4? */

#define VDP_TO_FLOAT(C) { .r = VDP_TO_RED (C) / 255.0f, .g = VDP_TO_GREEN (C) / 255.0f, .b = VDP_TO_BLUE (C) / 255.0f }

#define VDP_LEGACY_PALETTE { \
    VDP_TO_FLOAT (0x00), /* Transparent */ \
    VDP_TO_FLOAT (0x00), /* Black */ \
    VDP_TO_FLOAT (0x08), /* Medium Green */ \
    VDP_TO_FLOAT (0x0c), /* Light Green */ \
    VDP_TO_FLOAT (0x10), /* Dark Blue */ \
    VDP_TO_FLOAT (0x30), /* Light blue */ \
    VDP_TO_FLOAT (0x01), /* Dark Red */ \
    VDP_TO_FLOAT (0x3c), /* Cyan */ \
    VDP_TO_FLOAT (0x02), /* Medium Red */ \
    VDP_TO_FLOAT (0x03), /* Light Red */ \
    VDP_TO_FLOAT (0x05), /* Dark Yellow */ \
    VDP_TO_FLOAT (0x0f), /* Light Yellow */ \
    VDP_TO_FLOAT (0x04), /* Dark Green */ \
    VDP_TO_FLOAT (0x33), /* Magenta */ \
    VDP_TO_FLOAT (0x15), /* Gray */ \
    VDP_TO_FLOAT (0x3f)  /* White */ \
}

/* Display mode details */
const TMS9918A_Config Mode2_PAL = {
    .mode = TMS9918A_MODE_2,
    .lines_active = 192,
    .lines_total = 313,
    .palette = VDP_LEGACY_PALETTE
};
const TMS9918A_Config Mode2_NTSC = {
    .mode = TMS9918A_MODE_2,
    .lines_active = 192,
    .lines_total = 262,
    .palette = VDP_LEGACY_PALETTE
};
const TMS9918A_Config Mode4_PAL192 = {
    .lines_active = 192,
    .lines_total = 313,
    .v_counter_map = { { .first = 0x00, .last = 0xf2 },
                       { .first = 0xba, .last = 0xff } }
};
const TMS9918A_Config Mode4_PAL224 = {
    .lines_active = 224,
    .lines_total = 313,
    .v_counter_map = { { .first = 0x00, .last = 0xff },
                       { .first = 0x00, .last = 0x02 },
                       { .first = 0xca, .last = 0xff } }
};
const TMS9918A_Config Mode4_PAL240 = {
    .lines_active = 240,
    .lines_total = 313,
    .v_counter_map = { { .first = 0x00, .last = 0xff },
                       { .first = 0x00, .last = 0x0a },
                       { .first = 0xd2, .last = 0xff } }
};
const TMS9918A_Config Mode4_NTSC192 = {
    .lines_active = 192,
    .lines_total = 262,
    .v_counter_map = { { .first = 0x00, .last = 0xda },
                       { .first = 0xd5, .last = 0xff } }
};
const TMS9918A_Config Mode4_NTSC224 = {
    .lines_active = 224,
    .lines_total = 262,
    .v_counter_map = { { .first = 0x00, .last = 0xea },
                       { .first = 0xe5, .last = 0xff } }
};
const TMS9918A_Config Mode4_NTSC240 = {
    .lines_active = 240,
    .lines_total = 262,
    .v_counter_map = { { .first = 0x00, .last = 0xff },
                       { .first = 0x00, .last = 0x06 } }
};


/* SMS VDP State */
extern TMS9918A_State tms9918a_state;
static uint8_t cram [VDP_CRAM_SIZE];

/*
 * Reset the VDP registers and memory to power-on defaults.
 */
void vdp_init (void)
{
    /* TODO: Are there any nonzero default values? */
    memset (&tms9918a_state.regs, 0, sizeof (tms9918a_state.regs));
    memset (&tms9918a_state.vram, 0, sizeof (tms9918a_state.vram));
    memset (cram,                 0, sizeof (cram));
}

static bool first_byte_received = false;


/*
 * Read one byte from the VDP data port.
 */
uint8_t vdp_data_read ()
{
    uint8_t data = tms9918a_state.read_buffer;

    first_byte_received = false;

    tms9918a_state.read_buffer = tms9918a_state.vram[tms9918a_state.address];

    tms9918a_state.address = (tms9918a_state.address + 1) & 0x3fff;

    return data;

}


/*
 * Write one byte to the VDP data port.
 */
void vdp_data_write (uint8_t value)
{
    first_byte_received = false;

    switch (tms9918a_state.code)
    {
        case TMS9918A_CODE_VRAM_READ:
        case TMS9918A_CODE_VRAM_WRITE:
        case TMS9918A_CODE_REG_WRITE:
            tms9918a_state.vram[tms9918a_state.address] = value;
            break;

        case SMS_VDP_CODE_CRAM_WRITE:
            cram[tms9918a_state.address & 0x1f] = value;
            break;

        default:
            break;
    }
    tms9918a_state.address = (tms9918a_state.address + 1) & 0x3fff;
}


/*
 * Read one byte from the VDP control (status) port.
 */
uint8_t vdp_status_read ()
{
    uint8_t status = tms9918a_state.status;
    first_byte_received = false;

    /* Clear on read */
    tms9918a_state.status = 0x00;
    tms9918a_state.line_interrupt = false; /* "The flag remains set until the control port (IO port 0xbf) is read */

    return status;
}


/*
 * Write one byte to the VDP control port.
 */
void vdp_control_write (uint8_t value)
{
    if (!first_byte_received) /* First byte */
    {
        first_byte_received = true;
        tms9918a_state.address = (tms9918a_state.address & 0x3f00) | ((uint16_t) value << 0);
    }
    else /* Second byte */
    {
        first_byte_received = false;
        tms9918a_state.address = (tms9918a_state.address & 0x00ff) | ((uint16_t) (value & 0x3f) << 8);
        tms9918a_state.code = value & 0xc0;

        switch (tms9918a_state.code)
        {
            case TMS9918A_CODE_VRAM_READ:
                tms9918a_state.read_buffer = tms9918a_state.vram[tms9918a_state.address++];
                break;
            case TMS9918A_CODE_VRAM_WRITE:
                break;
            case TMS9918A_CODE_REG_WRITE:
                if ((value & 0x0f) <= 10)
                {
                    ((uint8_t *) &tms9918a_state.regs) [value & 0x0f] = tms9918a_state.address & 0x00ff;
                }
                break;
            case SMS_VDP_CODE_CRAM_WRITE:
                break;
            default:
                break;
        }
    }
}

/* TODO: Implement some kind of "Run CPU until frame completes" code */
/* TODO: We seem to run a touch faster than real hardware. It looks like the clock rate doesn't divide evenly into
 *       frames/lines, should we do the timing calculations with floats? */

/* TEMP */
#include <SDL2/SDL.h>
extern SMS_Framerate framerate;

/*
 * Assemble the four mode-bits.
 */
static uint8_t vdp_get_mode (void)
{
    return ((tms9918a_state.regs.ctrl_1 & TMS9918A_CTRL_1_MODE_1) ? BIT_0 : 0) +
           ((tms9918a_state.regs.ctrl_0 & TMS9918A_CTRL_0_MODE_2) ? BIT_1 : 0) +
           ((tms9918a_state.regs.ctrl_1 & TMS9918A_CTRL_1_MODE_3) ? BIT_2 : 0) +
           ((tms9918a_state.regs.ctrl_0 & SMS_VDP_CTRL_0_MODE_4)  ? BIT_3 : 0);
}




/*
 * Read the 8-bit v-counter.
 */
uint8_t vdp_get_v_counter (void)
{
    return tms9918a_state.v_counter;
}


/*
 * Check if the VDP is currently requesting an interrupt.
 */
bool vdp_get_interrupt (void)
{
    bool interrupt = false;

    /* Frame interrupt */
    if ((tms9918a_state.regs.ctrl_1 & TMS9918A_CTRL_1_FRAME_INT_EN) && (tms9918a_state.status & TMS9918A_STATUS_INT))
    {
        interrupt = true;
    }

    /* Line interrupt */
    if ((tms9918a_state.regs.ctrl_0 & SMS_VDP_CTRL_0_LINE_INT_EN) && tms9918a_state.line_interrupt)
    {
        interrupt = true;
    }

    return interrupt;
}


/*
 * Render one line of an 8x8 pattern.
 */
void vdp_render_mode4_pattern_line (const TMS9918A_Config *mode, uint16_t line, Vdp_Mode4_Pattern *pattern_base, Vdp_Palette palette,
                                    int32_Point_2D offset, bool h_flip, bool v_flip, bool transparency)
{
    int border_lines_top = (VIDEO_BUFFER_LINES - mode->lines_active) / 2;
    char *line_base;

    if (v_flip)
        line_base = (char *)(&pattern_base->data[(offset.y - line + 7) * 4]);
    else
        line_base = (char *)(&pattern_base->data[(line - offset.y) * 4]);

    for (uint32_t x = 0; x < 8; x++)
    {
        /* Don't draw texture pixels that fall outside of the screen */
        if (x + offset.x >= 256)
            continue;

        /* Don't draw the left-most eight pixels if BIT_5 of CTRL_1 is set */
        if (tms9918a_state.regs.ctrl_0 & SMS_VDP_CTRL_0_MASK_COL_1 && x + offset.x < 8)
            continue;

        int shift;

        if (h_flip)
            shift = 7 - x;
        else
            shift = x;

        uint8_t colour_index = ((line_base[0] & (0x80 >> shift)) ? 0x01 : 0x00) |
                               ((line_base[1] & (0x80 >> shift)) ? 0x02 : 0x00) |
                               ((line_base[2] & (0x80 >> shift)) ? 0x04 : 0x00) |
                               ((line_base[3] & (0x80 >> shift)) ? 0x08 : 0x00);

        if (transparency == true && colour_index == 0)
            continue;

        uint8_t pixel = cram[palette + colour_index];

        tms9918a_state.frame_current [(offset.x + x + VIDEO_SIDE_BORDER) + (border_lines_top + line) * VIDEO_BUFFER_WIDTH].r = VDP_TO_RED   (pixel) / 255.0f;
        tms9918a_state.frame_current [(offset.x + x + VIDEO_SIDE_BORDER) + (border_lines_top + line) * VIDEO_BUFFER_WIDTH].g = VDP_TO_GREEN (pixel) / 255.0f;
        tms9918a_state.frame_current [(offset.x + x + VIDEO_SIDE_BORDER) + (border_lines_top + line) * VIDEO_BUFFER_WIDTH].b = VDP_TO_BLUE  (pixel) / 255.0f;
    }
}

#define VDP_PATTERN_HORIZONTAL_FLIP     BIT_9
#define VDP_PATTERN_VERTICAL_FLIP       BIT_10


/*
 * Render one line of the background layer.
 */
void vdp_render_mode4_background_line (const TMS9918A_Config *mode, uint16_t line, bool priority)
{
    uint16_t name_table_base;
    uint8_t num_rows = (mode->lines_active == 192) ? 28 : 32;
    uint8_t start_column = 32 - ((tms9918a_state.regs.bg_scroll_x & 0xf8) >> 3);
    uint8_t fine_scroll_x = tms9918a_state.regs.bg_scroll_x & 0x07;
    uint8_t start_row = ((tms9918a_state.regs.bg_scroll_y % (8 * num_rows)) & 0xf8) >> 3;
    uint8_t fine_scroll_y = (tms9918a_state.regs.bg_scroll_y % (8 * num_rows)) & 0x07;
    uint32_t tile_y = (line + fine_scroll_y) / 8;
    int32_Point_2D position;

    if (mode->lines_active == 192)
    {
        name_table_base = (((uint16_t) tms9918a_state.regs.name_table_base) << 10) & 0x3800;
    }
    else
    {
        name_table_base = ((((uint16_t) tms9918a_state.regs.name_table_base) << 10) & 0x3000) + 0x700;
    }

    /* A bit in ctrl_0 can disable scrolling for the first two rows */
    if (tms9918a_state.regs.ctrl_0 & SMS_VDP_CTRL_0_LOCK_ROW_0_1 && line < 16)
    {
        start_column = 0;
        fine_scroll_x = 0;
    }

    /* TODO: Implement SMS_VDP_CTRL_0_SCROLL_DISABLE_COL_24_31 */
    /* TODO: The vertical scroll value should only take affect between active frames, not mid-frame */
    for (uint32_t tile_x = 0; tile_x < 32; tile_x++)
    {
        uint16_t tile_address = name_table_base + ((((tile_y + start_row) % num_rows) << 6) | ((tile_x + start_column) % 32 << 1));

        uint16_t tile = ((uint16_t)(tms9918a_state.vram [tile_address])) +
                        (((uint16_t)(tms9918a_state.vram [tile_address + 1])) << 8);

        /* If we are rendering the "priority" layer, skip any non-priority tiles */
        if (priority && !(tile & 0x1000))
            continue;

        bool h_flip = tile & VDP_PATTERN_HORIZONTAL_FLIP;
        bool v_flip = tile & VDP_PATTERN_VERTICAL_FLIP;

        Vdp_Mode4_Pattern *pattern = (Vdp_Mode4_Pattern *) &tms9918a_state.vram[(tile & 0x1ff) * sizeof (Vdp_Mode4_Pattern)];

        Vdp_Palette palette = (tile & (1 << 11)) ? VDP_PALETTE_SPRITE : VDP_PALETTE_BACKGROUND;

        position.x = 8 * tile_x + fine_scroll_x;
        position.y = 8 * tile_y - fine_scroll_y;
        vdp_render_mode4_pattern_line (mode, line, pattern, palette, position, h_flip, v_flip, false | priority);
    }
}


/* TODO: Set sprite-overflow flag */
/* TODO: If we allow more than eight sprites per line, will games use it? */
/* TODO: Collision detection */
/* TODO: VDP Flag BIT_3 of ctrl_1 subtracts 8 from x position of sprites */
/* TODO: Pixel-doubling */


/*
 * Render one line of the sprite layer.
 */
void vdp_render_mode4_sprites_line (const TMS9918A_Config *mode, uint16_t line)
{
    uint16_t sprite_attribute_table_base = (((uint16_t) tms9918a_state.regs.sprite_attr_table_base) << 7) & 0x3f00;
    uint16_t sprite_pattern_offset = (tms9918a_state.regs.sprite_pg_base & 0x04) ? 256 : 0;
    uint8_t sprite_height = (tms9918a_state.regs.ctrl_1 & TMS9918A_CTRL_1_SPRITE_SIZE) ? 16 : 8;
    uint8_t line_sprite_buffer [8];
    uint8_t line_sprite_count = 0;
    Vdp_Mode4_Pattern *pattern;
    int32_Point_2D position;

    /* Traverse the sprite list, filling the line sprite buffer */
    for (int i = 0; i < 64 && line_sprite_count < 8; i++)
    {
        uint8_t y = tms9918a_state.vram [sprite_attribute_table_base + i];

        /* Break if there are no more sprites */
        if (mode->lines_active == 192 && y == 0xd0)
            break;

        /* This number is treated as unsigned when the first line of
         * the sprite is on the screen, but signed when it is not */
        if (y >= 0xe0)
            position.y = ((int8_t) y) + 1;
        else
            position.y = y + 1;

        /* If the sprite is on this line, add it to the buffer */
        if (line >= position.y && line < position.y + sprite_height)
        {
            line_sprite_buffer [line_sprite_count++] = i;
        }
    }

    /* Render the sprites in the line sprite buffer.
     * Done in reverse order so that the first sprite is the one left on the screen */
    while (line_sprite_count--)
    {
        uint16_t i = line_sprite_buffer[line_sprite_count];
        uint8_t y = tms9918a_state.vram [sprite_attribute_table_base + i];
        uint8_t x = tms9918a_state.vram [sprite_attribute_table_base + 0x80 + i * 2];
        uint8_t pattern_index = tms9918a_state.vram [sprite_attribute_table_base + 0x80 + i * 2 + 1];

        position.x = x;

        /* TODO: Can we remove these duplicated lines? */
        if (y >= 0xe0)
            position.y = ((int8_t) y) + 1;
        else
            position.y = y + 1;

        if (tms9918a_state.regs.ctrl_1 & TMS9918A_CTRL_1_SPRITE_SIZE)
            pattern_index &= 0xfe;

        pattern = (Vdp_Mode4_Pattern *) &tms9918a_state.vram [(sprite_pattern_offset + pattern_index) * sizeof (Vdp_Mode4_Pattern)];
        vdp_render_mode4_pattern_line (mode, line, pattern, VDP_PALETTE_SPRITE, position, false, false, true);

        if (tms9918a_state.regs.ctrl_1 & TMS9918A_CTRL_1_SPRITE_SIZE)
        {
            position.y += 8;
            pattern = (Vdp_Mode4_Pattern *) &tms9918a_state.vram [(sprite_pattern_offset + pattern_index + 1) * sizeof (Vdp_Mode4_Pattern)];
            vdp_render_mode4_pattern_line (mode, line, pattern, VDP_PALETTE_SPRITE, position, false, false, true);
        }
    }
}


/* TODO: Consider renaming register "addr" to "base" */


/* TODO: For new functions, remove unused parameters */

/*
 * Render one line of output for the SMS VDP.
 */
void vdp_render_line (const TMS9918A_Config *config, uint16_t line)
{
    float_Colour video_background =     { .r = 0.0f, .g = 0.0f, .b = 0.0f };
    float_Colour video_background_dim = { .r = 0.0f, .g = 0.0f, .b = 0.0f };

    int border_lines_top = (VIDEO_BUFFER_LINES - config->lines_active) / 2;

    /* Background */
    if (!(tms9918a_state.regs.ctrl_1 & TMS9918A_CTRL_1_BLANK))
    {
        /* Display is blank */
    }
    else if (config->mode & SMS_VDP_MODE_4)
    {
        uint8_t bg_colour;
        bg_colour = cram [16 + (tms9918a_state.regs.background_colour & 0x0f)];
        video_background.r = VDP_TO_RED   (bg_colour) / 255.0f;
        video_background.g = VDP_TO_GREEN (bg_colour) / 255.0f;
        video_background.b = VDP_TO_BLUE  (bg_colour) / 255.0f;
    }
    else
    {
        video_background = config->palette [tms9918a_state.regs.background_colour & 0x0f];
    }
    video_background_dim.r = video_background.r * 0.5;
    video_background_dim.g = video_background.g * 0.5;
    video_background_dim.b = video_background.b * 0.5;

    /* Note: For now the top/bottom borders just copy the background from the first
     *       and last active lines. Do any games change the value outside of this? */

    /* Top border */
    if (line == 0)
    {
        for (int top_line = 0; top_line < border_lines_top; top_line++)
        {
            for (int x = 0; x < VIDEO_BUFFER_WIDTH; x++)
            {
                tms9918a_state.frame_current [x + top_line * VIDEO_BUFFER_WIDTH] = video_background_dim;
            }
        }
    }

    /* Side borders */
    for (int x = 0; x < VIDEO_BUFFER_WIDTH; x++)
    {
        bool border = x < VIDEO_SIDE_BORDER || x >= VIDEO_SIDE_BORDER + 256 ||
                      (tms9918a_state.regs.ctrl_0 & SMS_VDP_CTRL_0_MASK_COL_1 && x < VIDEO_SIDE_BORDER + 8);

        tms9918a_state.frame_current [x + (border_lines_top + line) * VIDEO_BUFFER_WIDTH] = (border ? video_background_dim : video_background);
    }

    /* Bottom border */
    if (line == config->lines_active - 1)
    {
        for (int bottom_line = border_lines_top + config->lines_active; bottom_line < VIDEO_BUFFER_LINES; bottom_line++)
        {
            for (int x = 0; x < VIDEO_BUFFER_WIDTH; x++)
            {
                tms9918a_state.frame_current [x + bottom_line * VIDEO_BUFFER_WIDTH] = video_background_dim;
            }
        }
    }

    /* Return without rendering patterns if BLANK is enabled */
    if (!(tms9918a_state.regs.ctrl_1 & TMS9918A_CTRL_1_BLANK))
    {
        return;
    }

    if (tms9918a_state.regs.ctrl_0 & SMS_VDP_CTRL_0_MODE_4)
    {
        vdp_render_mode4_background_line (config, line, false);
        vdp_render_mode4_sprites_line (config, line);
        vdp_render_mode4_background_line (config, line, true);
    }
    else if (tms9918a_state.regs.ctrl_0 & TMS9918A_CTRL_0_MODE_2)
    {
        tms9918a_render_mode2_background_line (config, line);
        tms9918a_render_sprites_line (config, line);
    }
}


/*
 * Run one scanline on the VDP.
 */
void vdp_run_one_scanline ()
{
    static uint16_t line = 0;

    const TMS9918A_Config *config;
    TMS9918A_Mode mode = vdp_get_mode ();

    switch (mode)
    {
        case TMS9918A_MODE_2: /* Mode 2: 32 × 24 8-byte tiles, sprites enabled, three colour/pattern tables */
            config = (framerate == FRAMERATE_NTSC) ? &Mode2_NTSC : &Mode2_PAL;
            break;

        case SMS_VDP_MODE_4: /* Mode 4, 192 lines */
        case SMS_VDP_MODE_4_2:
        case SMS_VDP_MODE_4_3:
        case SMS_VDP_MODE_4_3_2_1:
            config = (framerate == FRAMERATE_NTSC) ? &Mode4_NTSC192 : &Mode4_PAL192;
            break;

        case SMS_VDP_MODE_4_224:
            config = (framerate == FRAMERATE_NTSC) ? &Mode4_NTSC224 : &Mode4_PAL224;
            break;

        case SMS_VDP_MODE_4_240:
            config = (framerate == FRAMERATE_NTSC) ? &Mode4_NTSC240 : &Mode4_PAL240;
            break;

        default: /* Unsupported */
            fprintf (stderr, "Unsupported mode: %s.\n", tms9918a_mode_name_get (mode));
            return;
    }

    /* If this is an active line, render it */
    if (line < config->lines_active)
    {
        vdp_render_line (config, line);
    }

    /* If this the final active line, copy to the frame buffer */
    /* TODO: This is okay for single-threaded code, but locking may be needed if multi-threading is added */
    if (line == config->lines_active - 1)
    {
        memcpy (tms9918a_state.frame_complete, tms9918a_state.frame_current, sizeof (tms9918a_state.frame_current));

        /* Update statistics (rolling average) */
        static int vdp_previous_completion_time = 0;
        static int vdp_current_time = 0;
        vdp_current_time = SDL_GetTicks ();
        if (vdp_previous_completion_time)
        {
            snepulator.vdp_framerate *= 0.95;
            snepulator.vdp_framerate += 0.05 * (1000.0 / (vdp_current_time - vdp_previous_completion_time));
        }
        vdp_previous_completion_time = vdp_current_time;
    }

    /* Check for frame interrupt */
    if (line == config->lines_active + 1)
        tms9918a_state.status |= TMS9918A_STATUS_INT;

    /* Decrement the line interrupt counter during the active display period.
     * Reset outside of the active display period (but not the first line after) */
    if (line <= config->lines_active)
        tms9918a_state.line_interrupt_counter--;
    else
        tms9918a_state.line_interrupt_counter = tms9918a_state.regs.line_counter_reset;

    /* TODO: Does the line interrupt exist outside of mode 4? */
    /* On underflow, we reset the line interrupt counter and set the pending flag */
    if (line <= config->lines_active && tms9918a_state.line_interrupt_counter == 0xff)
    {
        tms9918a_state.line_interrupt_counter = tms9918a_state.regs.line_counter_reset;
        tms9918a_state.line_interrupt = true;
    }

    /* Update values for the next line */
    line = (line + 1) % config->lines_total;

    uint16_t temp_line = line;
    for (int range = 0; range < 3; range++)
    {
        if (temp_line < config->v_counter_map[range].last - config->v_counter_map[range].first + 1)
        {
            tms9918a_state.v_counter = temp_line + config->v_counter_map[range].first;
            break;
        }
        else
        {
            temp_line -= config->v_counter_map[range].last - config->v_counter_map[range].first + 1;
        }
    }
}


/*
 * Copy the most recently rendered frame into the texture buffer.
 */
void vdp_copy_latest_frame (void)
{
    memcpy (snepulator.sms_vdp_texture_data, tms9918a_state.frame_complete, sizeof (tms9918a_state.frame_complete));
}
