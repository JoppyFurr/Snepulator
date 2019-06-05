#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include "../snepulator.h"
#include "../sms.h"
extern Snepulator snepulator;

#include "sega_vdp.h"

/* Constants */
#define VDP_CRAM_SIZE (32)
#define VDP_VRAM_SIZE (16 << 10)

/* Macros */
#define VDP_TO_RED(C)   ((0xff / 3) * ((C & 0x03) >> 0))
#define VDP_TO_GREEN(C) ((0xff / 3) * ((C & 0x0c) >> 2))
#define VDP_TO_BLUE(C)  ((0xff / 3) * ((C & 0x30) >> 4))

/* VDP State */
static Vdp_Regs vdp_regs;
static uint8_t cram [VDP_CRAM_SIZE];
static uint8_t vram [VDP_VRAM_SIZE];
Float3 vdp_frame_complete [VDP_BUFFER_WIDTH * VDP_BUFFER_LINES];
Float3 vdp_frame_current  [VDP_BUFFER_WIDTH * VDP_BUFFER_LINES];

void vdp_init (void)
{
    /* TODO: Are there any nonzero default values? */
    memset (&vdp_regs, 0, sizeof (vdp_regs));
    memset (vram,      0, sizeof (vram));
    memset (cram,      0, sizeof (cram));
}

static bool first_byte_received = false;

uint8_t vdp_data_read ()
{
    uint8_t data = vdp_regs.read_buffer;

    first_byte_received = false;

    vdp_regs.read_buffer = vram[vdp_regs.address];

    vdp_regs.address = (vdp_regs.address + 1) & 0x3fff;

    return data;

}

void vdp_data_write (uint8_t value)
{
    first_byte_received = false;

    switch (vdp_regs.code)
    {
        case VDP_CODE_VRAM_READ:
        case VDP_CODE_VRAM_WRITE:
        case VDP_CODE_REG_WRITE:
            vram[vdp_regs.address] = value;
            break;

        case VDP_CODE_CRAM_WRITE:
            cram[vdp_regs.address & 0x1f] = value;
            break;

        default:
            break;
    }
    vdp_regs.address = (vdp_regs.address + 1) & 0x3fff;
}

uint8_t vdp_status_read ()
{
    uint8_t status = vdp_regs.status;
    first_byte_received = false;

    /* Clear on read */
    vdp_regs.status = 0x00;
    vdp_regs.line_interrupt = false; /* "The flag remains set until the control port (IO port 0xbf) is read */

    return status;
}

void vdp_control_write (uint8_t value)
{
    if (!first_byte_received) /* First byte */
    {
        first_byte_received = true;
        vdp_regs.address = (vdp_regs.address & 0x3f00) | ((uint16_t) value << 0);
    }
    else /* Second byte */
    {
        first_byte_received = false;
        vdp_regs.address = (vdp_regs.address & 0x00ff) | ((uint16_t) (value & 0x3f) << 8);
        vdp_regs.code = value & 0xc0;

        switch (vdp_regs.code)
        {
            case VDP_CODE_VRAM_READ:
                vdp_regs.read_buffer = vram[vdp_regs.address++];
                break;
            case VDP_CODE_VRAM_WRITE:
                break;
            case VDP_CODE_REG_WRITE:
                if ((value & 0x0f) <= 10)
                {
                    ((uint8_t *) &vdp_regs) [value & 0x0f] = vdp_regs.address & 0x00ff;
                }
                break;
            case VDP_CODE_CRAM_WRITE:
                break;
            default:
                break;
        }
    }
}

/* TODO: For now, assuming 256x192 PAL */
/* 50 frames per second, 313 scanlines per frame */
/* TODO: Implement some kind of "Run CPU until frame completes" code */
/* TODO: We seem to run a touch faster than real hardware. It looks like the clock rate doesn't divide evenly into
 *       frames/lines, should we do the timing calculations with floats? */

/* PAL 256x192:
 * 192 - Active display
 *  48 - Bottom border
 *   3 - Bottom blanking
 *   3 - Vertical blanking
 *  13 - Top blanking
 *  54 - Top border
 *
 *  0x00 -> 0xf2, 0xba -> 0xff
 */

/* NTSC 256x192:
 * 192 - Active display
 *  24 - Bottom border
 *   3 - Bottom blanking
 *   3 - Vertical blanking
 *  13 - Top blanking
 *  27 - Top border
 *
 *  0x00 -> 0xda, 0xd5 -> 0xff
 */

/* TEMP */
#include <SDL2/SDL.h>
extern SMS_Framerate framerate;

void vdp_render_line_mode4 (Vdp_Display_Mode *mode, uint16_t line);

/*
 * Assemble the four mode-bits.
 */
static uint8_t vdp_get_mode (void)
{
    return ((vdp_regs.mode_ctrl_2 & VDP_MODE_CTRL_2_MODE_1) ? BIT_0 : 0) +
           ((vdp_regs.mode_ctrl_1 & VDP_MODE_CTRL_1_MODE_2) ? BIT_1 : 0) +
           ((vdp_regs.mode_ctrl_2 & VDP_MODE_CTRL_2_MODE_3) ? BIT_2 : 0) +
           ((vdp_regs.mode_ctrl_1 & VDP_MODE_CTRL_1_MODE_4) ? BIT_3 : 0);
}

/*
 * Supply a human-readable string describing the current mode.
 */
const char *vdp_get_mode_name (void)
{
    const char *vdp_mode_names[16] = {
        "Mode 0 - Graphic I",
        "Mode 1 - Text",
        "Mode 2 - Graphic II",
        "Mode 1+2 - Undocumented",
        "Mode 3 - Multicolour",
        "Mode 1+3 - Undocumented",
        "Mode 2+3 - Undocumented",
        "Mode 1+2+3 - Undocumented",
        "Mode 4 - 192 lines",
        "Mode 4+1 - Undocumented",
        "Mode 4 - 192 lines",
        "Mode 4 - 224 lines",
        "Mode 4 - 192 lines",
        "Mode 4+3+2 - Undocumented",
        "Mode 4 - 240 lines",
        "Mode 4 - 192 lines" };

    return vdp_mode_names[vdp_get_mode ()];
}

const Vdp_Display_Mode Mode4_PAL192 = {
    .lines_active = 192,
    .lines_total = 313,
    .v_counter_map = { { .first = 0x00, .last = 0xf2 },
                       { .first = 0xba, .last = 0xff } }
};
const Vdp_Display_Mode Mode4_PAL224 = {
    .lines_active = 224,
    .lines_total = 313,
    .v_counter_map = { { .first = 0x00, .last = 0xff },
                       { .first = 0x00, .last = 0x02 },
                       { .first = 0xca, .last = 0xff } }
};
const Vdp_Display_Mode Mode4_PAL240 = {
    .lines_active = 240,
    .lines_total = 313,
    .v_counter_map = { { .first = 0x00, .last = 0xff },
                       { .first = 0x00, .last = 0x0a },
                       { .first = 0xd2, .last = 0xff } }
};
const Vdp_Display_Mode Mode4_NTSC192 = {
    .lines_active = 192,
    .lines_total = 262,
    .v_counter_map = { { .first = 0x00, .last = 0xda },
                       { .first = 0xd5, .last = 0xff } }
};
const Vdp_Display_Mode Mode4_NTSC224 = {
    .lines_active = 224,
    .lines_total = 262,
    .v_counter_map = { { .first = 0x00, .last = 0xea },
                       { .first = 0xe5, .last = 0xff } }
};
const Vdp_Display_Mode Mode4_NTSC240 = {
    .lines_active = 240,
    .lines_total = 262,
    .v_counter_map = { { .first = 0x00, .last = 0xff },
                       { .first = 0x00, .last = 0x06 } }
};

/*
 * Run one scanline on the VDP
 */
void vdp_run_one_scanline ()
{
    static uint16_t line = 0;

    Vdp_Display_Mode mode;

    switch (vdp_get_mode ())
    {
        case 0x8: /* Mode 4, 192 lines */
        case 0xa:
        case 0xc:
        case 0xf:
            mode = (framerate == FRAMERATE_NTSC) ? Mode4_NTSC192 : Mode4_PAL192;
            break;

        case 0xb: /* "Mode 4, 224 lines */
            mode = (framerate == FRAMERATE_NTSC) ? Mode4_NTSC224 : Mode4_PAL224;
            break;

        case 0xe: /* "Mode 4, 240 lines */
            mode = (framerate == FRAMERATE_NTSC) ? Mode4_NTSC240 : Mode4_PAL240;
            break;

        default: /* Unsupported */
            return;
    }

    /* If this is an active line, render it */
    if (line < mode.lines_active)
        vdp_render_line_mode4 (&mode, line);

    /* If this the final active line, copy to the frame buffer */
    /* TODO: This is okay for single-threaded code, but locking may be needed if multi-threading is added */
    if (line == mode.lines_active - 1)
    {
        memcpy (vdp_frame_complete, vdp_frame_current, sizeof (vdp_frame_current));

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
    if (line == mode.lines_active + 1)
        vdp_regs.status |= VDP_STATUS_INT;

    /* Decrement the line interrupt counter during the active display period.
     * Reset outside of the active display period (but not the first line after) */
    if (line <= mode.lines_active)
        vdp_regs.line_interrupt_counter--;
    else
        vdp_regs.line_interrupt_counter = vdp_regs.line_counter;

    /* On underflow, we reset the line interrupt counter and set the pending flag */
    if (line <= mode.lines_active && vdp_regs.line_interrupt_counter == 0xff)
    {
        vdp_regs.line_interrupt_counter = vdp_regs.line_counter;
        vdp_regs.line_interrupt = true;
    }

    /* Update values for the next line */
    line = (line + 1) % mode.lines_total;

    uint16_t temp_line = line;
    for (int range = 0; range < 3; range++)
    {
        if (temp_line < mode.v_counter_map[range].last - mode.v_counter_map[range].first + 1)
        {
            vdp_regs.v_counter = temp_line + mode.v_counter_map[range].first;
            break;
        }
        else
        {
            temp_line -= mode.v_counter_map[range].last - mode.v_counter_map[range].first + 1;
        }
    }
}

uint8_t vdp_get_v_counter (void)
{
    return vdp_regs.v_counter;
}

bool vdp_get_interrupt (void)
{
    bool interrupt = false;

    /* Frame interrupt */
    if ((vdp_regs.mode_ctrl_2 & VDP_MODE_CTRL_2_FRAME_INT_EN) && (vdp_regs.status & VDP_STATUS_INT))
    {
        interrupt = true;
    }

    /* Line interrupt */
    if ((vdp_regs.mode_ctrl_1 & VDP_MODE_CTRL_1_LINE_INT_EN) && vdp_regs.line_interrupt)
    {
        interrupt = true;
    }

    return interrupt;
}

typedef struct Point2D_s {
    int32_t x;
    int32_t y;
} Point2D;

void vdp_render_line_mode4_pattern (Vdp_Display_Mode *mode, uint16_t line, Vdp_Pattern *pattern_base, Vdp_Palette palette,
                                    Point2D offset, bool h_flip, bool v_flip, bool transparency)
{
    int border_lines_top = (VDP_BUFFER_LINES - mode->lines_active) / 2;
    char *line_base;

    /* Early abort if the pattern doesn't belong on this line */
    if (line < offset.y || line >= offset.y + 8)
        return;

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
        if (vdp_regs.mode_ctrl_1 & VDP_MODE_CTRL_1_MASK_COL_1 && x + offset.x < 8)
            continue;

        int shift;

        if (h_flip)
            shift = 7 - x;
        else
            shift = x;

        uint8_t bit0 = (line_base[0] & (0x80 >> shift)) ? 0x01 : 0x00;
        uint8_t bit1 = (line_base[1] & (0x80 >> shift)) ? 0x02 : 0x00;
        uint8_t bit2 = (line_base[2] & (0x80 >> shift)) ? 0x04 : 0x00;
        uint8_t bit3 = (line_base[3] & (0x80 >> shift)) ? 0x08 : 0x00;

        if (transparency == true && (bit0 | bit1 | bit2 | bit3) == 0)
            continue;

        uint8_t pixel = cram[((palette == VDP_PALETTE_SPRITE) ? 16 : 0) + (bit0 | bit1 | bit2 | bit3)];

        vdp_frame_current [(offset.x + x + VDP_SIDE_BORDER) + (border_lines_top + line) * VDP_BUFFER_WIDTH].data[0] = VDP_TO_RED   (pixel) / 256.0f;
        vdp_frame_current [(offset.x + x + VDP_SIDE_BORDER) + (border_lines_top + line) * VDP_BUFFER_WIDTH].data[1] = VDP_TO_GREEN (pixel) / 256.0f;
        vdp_frame_current [(offset.x + x + VDP_SIDE_BORDER) + (border_lines_top + line) * VDP_BUFFER_WIDTH].data[2] = VDP_TO_BLUE  (pixel) / 256.0f;
    }
}

#define VDP_PATTERN_HORIZONTAL_FLIP     BIT_9
#define VDP_PATTERN_VERTICAL_FLIP       BIT_10

/* TODO: Optimize this for one-line-at-a-time rendering */
void vdp_render_line_mode4_background (Vdp_Display_Mode *mode, uint16_t line, bool priority)
{
    uint16_t name_table_base;
    uint8_t num_rows = (mode->lines_active == 192) ? 28 : 32;
    uint8_t start_column = 32 - ((vdp_regs.background_x_scroll & 0xf8) >> 3);
    uint8_t fine_scroll_x = vdp_regs.background_x_scroll & 0x07;
    uint8_t start_row = ((vdp_regs.background_y_scroll % (8 * num_rows)) & 0xf8) >> 3;
    uint8_t fine_scroll_y = (vdp_regs.background_y_scroll % (8 * num_rows)) & 0x07;
    Point2D position;

    if (mode->lines_active == 192)
    {
        name_table_base = (((uint16_t) vdp_regs.name_table_addr) << 10) & 0x3800;
    }
    else
    {
        name_table_base = ((((uint16_t) vdp_regs.name_table_addr) << 10) & 0x3000) + 0x700;
    }

    /* A bit in mode_ctrl_1 can disable scrolling for the first two rows */
    if (vdp_regs.mode_ctrl_1 & VDP_MODE_CTRL_1_SCROLL_DISABLE_ROW_0_1 && line < 16)
    {
        start_column = 0;
        fine_scroll_x = 0;
    }

    /* TODO: Implement VDP_MODE_CTRL_1_SCROLL_DISABLE_COL_24_31 */
    /* TODO: The vertical scroll value should only take affect between active frames, not mid-frame */
    for (uint32_t tile_y = 0; tile_y < num_rows; tile_y++)
    {
        for (uint32_t tile_x = 0; tile_x < 32; tile_x++)
        {
            uint16_t tile_address = name_table_base + ((((tile_y + start_row) % num_rows) << 6) | ((tile_x + start_column) % 32 << 1));

            uint16_t tile = ((uint16_t)(vram [tile_address])) +
                            (((uint16_t)(vram [tile_address + 1])) << 8);

            /* If we are rendering the "priority" layer, skip any non-priority tiles */
            if (priority && !(tile & 0x1000))
                continue;

            bool h_flip = tile & VDP_PATTERN_HORIZONTAL_FLIP;
            bool v_flip = tile & VDP_PATTERN_VERTICAL_FLIP;

            Vdp_Pattern *pattern = (Vdp_Pattern *) &vram[(tile & 0x1ff) * sizeof (Vdp_Pattern)];

            Vdp_Palette palette = (tile & (1 << 11)) ? VDP_PALETTE_SPRITE : VDP_PALETTE_BACKGROUND;

            position.x = 8 * tile_x + fine_scroll_x;
            position.y = 8 * tile_y - fine_scroll_y;
            vdp_render_line_mode4_pattern (mode, line, pattern, palette, position, h_flip, v_flip, false | priority);

        }
    }
}

/* TODO: Limit of eight sprites per line / set sprite-overflow flag */
/* TODO: Collision detection */
/* TODO: VDP Flag BIT_3 of ctrl_1 subtracts 8 from x position of sprites */
/* TODO: Pixel-doubling */
/* TODO: Draw order: the first entry in the line's sprite buffer with a non-transparent pixel is the one that sticks */
void vdp_render_line_mode4_sprites (Vdp_Display_Mode *mode, uint16_t line)
{
    uint16_t sprite_attribute_table_base = (((uint16_t) vdp_regs.sprite_attr_table_addr) << 7) & 0x3f00;
    uint16_t sprite_pattern_offset = (vdp_regs.sprite_pattern_generator_addr & 0x04) ? 256 : 0;
    Vdp_Pattern *pattern;
    Point2D position;

    /* TODO: Once a pixel is set by a sprite, we should stop processing them for that pixel.
     *       This could be emulated by traversing the sprite list backwards. */
    for (int i = 0; i < 64; i++)
    {
        uint8_t y = vram [sprite_attribute_table_base + i];
        uint8_t x = vram [sprite_attribute_table_base + 0x80 + i * 2];
        uint8_t pattern_index = vram [sprite_attribute_table_base + 0x80 + i * 2 + 1];

        /* Break if there are no more sprites */
        if (mode->lines_active == 192 && y == 0xd0)
            break;

        position.x = x;

        if (y >= 0xe0)
            position.y = ((int8_t) y) + 1;
        else
            position.y = y + 1;

        /* Skip sprites not on this line */
        if (line < position.y || line > position.y + 32)
            continue;

        if (vdp_regs.mode_ctrl_2 & VDP_MODE_CTRL_2_SPRITE_TALL)
            pattern_index &= 0xfe;

        pattern = (Vdp_Pattern *) &vram [(sprite_pattern_offset + pattern_index) * sizeof (Vdp_Pattern)];
        vdp_render_line_mode4_pattern (mode, line, pattern, VDP_PALETTE_SPRITE, position, false, false, true);

        if (vdp_regs.mode_ctrl_2 & VDP_MODE_CTRL_2_SPRITE_TALL)
        {
            position.y += 8;
            pattern = (Vdp_Pattern *) &vram [(sprite_pattern_offset + pattern_index + 1) * sizeof (Vdp_Pattern)];
            vdp_render_line_mode4_pattern (mode, line, pattern, VDP_PALETTE_SPRITE, position, false, false, true);
        }
    }
}

void vdp_render_line_mode4 (Vdp_Display_Mode *mode, uint16_t line)
{
    Float3 video_background = { .data = { 0.0f, 0.0f, 0.0f } };
    Float3 video_background_dim = { .data = { 0.0f, 0.0f, 0.0f } };

    int border_lines_top = (VDP_BUFFER_LINES - mode->lines_active) / 2;

    /* Background */
    if (!(vdp_regs.mode_ctrl_2 & VDP_MODE_CTRL_2_BLANK))
    {
        /* Display is blank */
    }
    else
    {
        /* Overscan colour. Is this mode4-specific? */
        uint8_t bg_colour;
        bg_colour = cram [16 + (vdp_regs.background_colour & 0x0f)];
        video_background.data[0] = VDP_TO_RED   (bg_colour) / 256.0f;
        video_background.data[1] = VDP_TO_GREEN (bg_colour) / 256.0f;
        video_background.data[2] = VDP_TO_BLUE  (bg_colour) / 256.0f;
        video_background_dim.data[0] = video_background.data[0] * 0.5;
        video_background_dim.data[1] = video_background.data[1] * 0.5;
        video_background_dim.data[2] = video_background.data[2] * 0.5;
    }

    /* TODO: For now the top/bottom borders just copy the background from the first
     *       and last active lines. Do any games change the value outside of this? */

    /* Top border */
    if (line == 0)
    {
        for (int top_line = 0; top_line < border_lines_top; top_line++)
        {
            for (int x = 0; x < VDP_BUFFER_WIDTH; x++)
            {
                vdp_frame_current [x + top_line * VDP_BUFFER_WIDTH] = video_background_dim;
            }
        }
    }

    /* Side borders */
    for (int x = 0; x < VDP_BUFFER_WIDTH; x++)
    {
        bool border = x < VDP_SIDE_BORDER || x >= VDP_SIDE_BORDER + 256 ||
                      (vdp_regs.mode_ctrl_1 & VDP_MODE_CTRL_1_MASK_COL_1 && x < VDP_SIDE_BORDER + 8);

        vdp_frame_current [x + (border_lines_top + line) * VDP_BUFFER_WIDTH] = (border ? video_background_dim : video_background);
    }

    /* Bottom border */
    if (line == mode->lines_active - 1)
    {
        for (int bottom_line = border_lines_top + mode->lines_active; bottom_line < VDP_BUFFER_LINES; bottom_line++)
        {
            for (int x = 0; x < VDP_BUFFER_WIDTH; x++)
            {
                vdp_frame_current [x + bottom_line * VDP_BUFFER_WIDTH] = video_background_dim;
            }
        }
    }

    /* Return without rendering patterns if BLANK is enabled */
    if (!(vdp_regs.mode_ctrl_2 & VDP_MODE_CTRL_2_BLANK))
    {
        return;
    }

    vdp_render_line_mode4_background (mode, line, false);
    vdp_render_line_mode4_sprites (mode, line);
    vdp_render_line_mode4_background (mode, line, true);
}

/* Note: For now, we will assume 192 lines, as this is what the vast majority of
 *       SMS games actually use */
void vdp_copy_latest_frame (void)
{
    memcpy (snepulator.sms_vdp_texture_data, vdp_frame_complete, sizeof (vdp_frame_complete));
}
