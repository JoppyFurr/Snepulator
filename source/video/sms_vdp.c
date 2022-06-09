/*
 * Implementation for the Sega Master System VDP chip.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>

#include "../snepulator_types.h"
#include "../snepulator.h"
#include "../util.h"
#include "../gamepad.h"
#include "../database/sms_db.h"

extern Snepulator_State state;
extern Snepulator_Gamepad gamepad [3];

#include "tms9928a.h"
#include "sms_vdp.h"

/* Constants */
#define SMS_VDP_CRAM_SIZE (32)

/* Macros */
#define SMS_VDP_TO_FLOAT(C) { .r = ((0xff / 3) * (((C) & 0x03) >> 0)) / 255.0f, \
                              .g = ((0xff / 3) * (((C) & 0x0c) >> 2)) / 255.0f, \
                              .b = ((0xff / 3) * (((C) & 0x30) >> 4)) / 255.0f }

#define GG_VDP_TO_FLOAT(C) { .r = ((0xff / 15) * (((C) & 0x000f) >> 0)) / 255.0f, \
                             .g = ((0xff / 15) * (((C) & 0x00f0) >> 4)) / 255.0f, \
                             .b = ((0xff / 15) * (((C) & 0x0f00) >> 8)) / 255.0f }

#define SMS_VDP_LEGACY_PALETTE { \
    SMS_VDP_TO_FLOAT (0x00), /* Transparent */ \
    SMS_VDP_TO_FLOAT (0x00), /* Black */ \
    SMS_VDP_TO_FLOAT (0x08), /* Medium Green */ \
    SMS_VDP_TO_FLOAT (0x0c), /* Light Green */ \
    SMS_VDP_TO_FLOAT (0x10), /* Dark Blue */ \
    SMS_VDP_TO_FLOAT (0x30), /* Light blue */ \
    SMS_VDP_TO_FLOAT (0x01), /* Dark Red */ \
    SMS_VDP_TO_FLOAT (0x3c), /* Cyan */ \
    SMS_VDP_TO_FLOAT (0x02), /* Medium Red */ \
    SMS_VDP_TO_FLOAT (0x03), /* Light Red */ \
    SMS_VDP_TO_FLOAT (0x05), /* Dark Yellow */ \
    SMS_VDP_TO_FLOAT (0x0f), /* Light Yellow */ \
    SMS_VDP_TO_FLOAT (0x04), /* Dark Green */ \
    SMS_VDP_TO_FLOAT (0x33), /* Magenta */ \
    SMS_VDP_TO_FLOAT (0x15), /* Grey */ \
    SMS_VDP_TO_FLOAT (0x3f)  /* White */ \
}


/* Display mode details */
static const TMS9928A_Config Mode0_PAL = {
    .mode = TMS9928A_MODE_0,
    .lines_active = 192,
    .lines_total = 313,
    .palette = SMS_VDP_LEGACY_PALETTE,
    .v_counter_map = { { .first = 0x00, .last = 0xf2 },
                       { .first = 0xba, .last = 0xff } }
};
static const TMS9928A_Config Mode0_NTSC = {
    .mode = TMS9928A_MODE_0,
    .lines_active = 192,
    .lines_total = 262,
    .palette = SMS_VDP_LEGACY_PALETTE,
    .v_counter_map = { { .first = 0x00, .last = 0xda },
                       { .first = 0xd5, .last = 0xff } }
};
static const TMS9928A_Config Mode2_PAL = {
    .mode = TMS9928A_MODE_2,
    .lines_active = 192,
    .lines_total = 313,
    .palette = SMS_VDP_LEGACY_PALETTE,
    .v_counter_map = { { .first = 0x00, .last = 0xf2 },
                       { .first = 0xba, .last = 0xff } }
};
static const TMS9928A_Config Mode2_NTSC = {
    .mode = TMS9928A_MODE_2,
    .lines_active = 192,
    .lines_total = 262,
    .palette = SMS_VDP_LEGACY_PALETTE,
    .v_counter_map = { { .first = 0x00, .last = 0xda },
                       { .first = 0xd5, .last = 0xff } }
};
static const TMS9928A_Config Mode4_PAL192 = {
    .mode = SMS_VDP_MODE_4,
    .lines_active = 192,
    .lines_total = 313,
    .v_counter_map = { { .first = 0x00, .last = 0xf2 },
                       { .first = 0xba, .last = 0xff } }
};
static const TMS9928A_Config Mode4_PAL224 = {
    .mode = SMS_VDP_MODE_4_224,
    .lines_active = 224,
    .lines_total = 313,
    .v_counter_map = { { .first = 0x00, .last = 0xff },
                       { .first = 0x00, .last = 0x02 },
                       { .first = 0xca, .last = 0xff } }
};
static const TMS9928A_Config Mode4_PAL240 = {
    .mode = SMS_VDP_MODE_4_240,
    .lines_active = 240,
    .lines_total = 313,
    .v_counter_map = { { .first = 0x00, .last = 0xff },
                       { .first = 0x00, .last = 0x0a },
                       { .first = 0xd2, .last = 0xff } }
};
static const TMS9928A_Config Mode4_NTSC192 = {
    .mode = SMS_VDP_MODE_4,
    .lines_active = 192,
    .lines_total = 262,
    .v_counter_map = { { .first = 0x00, .last = 0xda },
                       { .first = 0xd5, .last = 0xff } }
};
static const TMS9928A_Config Mode4_NTSC224 = {
    .mode = SMS_VDP_MODE_4_224,
    .lines_active = 224,
    .lines_total = 262,
    .v_counter_map = { { .first = 0x00, .last = 0xea },
                       { .first = 0xe5, .last = 0xff } }
};
static const TMS9928A_Config Mode4_NTSC240 = {
    .mode = SMS_VDP_MODE_4_240,
    .lines_active = 240,
    .lines_total = 262,
    .v_counter_map = { { .first = 0x00, .last = 0xff },
                       { .first = 0x00, .last = 0x06 } }
};


/*
 * Create a SMS VDP context with power-on defaults.
 */
TMS9928A_Context *sms_vdp_init (void *parent, void (* frame_done) (void *), Console console)
{
    TMS9928A_Context *context;

    context = calloc (1, sizeof (TMS9928A_Context));
    if (context == NULL)
    {
        snepulator_error ("Error", "Unable to allocate memory for TMS9928A_Context");
        return NULL;
    }

    /* TODO: Only calculate each table once, choose the table based on console.
     *       Could this be combined with the tms9928A .palette? */

    /* Populate vdp_to_float colour table */
    if (console == CONSOLE_GAME_GEAR)
    {
        context->is_game_gear = true;
        context->video_width = 160;
        context->video_height = 144;

        for (uint32_t i = 0; i < 4096; i++)
        {
            context->vdp_to_float [i] = (float_Colour) GG_VDP_TO_FLOAT (i);
        }
        context->vdp_to_float_mask = 0x0fff;
    }
    else
    {
        context->video_width = 256;
        context->video_height = 192;

        for (uint32_t i = 0; i < 64; i++)
        {
            context->vdp_to_float [i] = (float_Colour) SMS_VDP_TO_FLOAT (i);
        }
        context->vdp_to_float_mask = 0x3f;
    }

    context->parent = parent;
    context->frame_done = frame_done;

    return context;
}


/* TODO: Use tms9928a versions where identical */
/*
 * Read one byte from the VDP data port.
 */
uint8_t sms_vdp_data_read (TMS9928A_Context *context)
{
    uint8_t data = context->state.read_buffer;

    context->state.first_byte_received = false;

    context->state.read_buffer = context->vram [context->state.address];

    context->state.address = (context->state.address + 1) & 0x3fff;

    return data;

}


/*
 * Write one byte to the VDP data port.
 */
void sms_vdp_data_write (TMS9928A_Context *context, uint8_t value)
{
    context->state.first_byte_received = false;
    context->state.read_buffer = value;

    switch (context->state.code)
    {
        case TMS9928A_CODE_VRAM_READ:
        case TMS9928A_CODE_VRAM_WRITE:
        case TMS9928A_CODE_REG_WRITE:
            context->vram [context->state.address] = value;
            break;

        case SMS_VDP_CODE_CRAM_WRITE:
            if (context->is_game_gear)
            {
                if ((context->state.address & 0x01) == 0x00)
                {
                    context->state.cram_latch = value;
                }
                else
                {
                    context->state.cram [(context->state.address >> 1) & 0x1f] = (((uint16_t) value) << 8) | context->state.cram_latch;
                }
            }
            else
            {
                context->state.cram [context->state.address & 0x1f] = value;
            }
            break;

        default:
            break;
    }
    context->state.address = (context->state.address + 1) & 0x3fff;
}


/*
 * Read one byte from the VDP control (status) port.
 */
uint8_t sms_vdp_status_read (TMS9928A_Context *context)
{
    uint8_t status = context->state.status;
    context->state.first_byte_received = false;

    /* Clear on read */
    context->state.status = 0x00;
    context->state.line_interrupt = false; /* "The flag remains set until the control port (IO port 0xbf) is read */

    return status;
}


/*
 * Write one byte to the VDP control port.
 */
void sms_vdp_control_write (TMS9928A_Context *context, uint8_t value)
{
    if (!context->state.first_byte_received) /* First byte */
    {
        context->state.first_byte_received = true;
        context->state.address = (context->state.address & 0x3f00) | ((uint16_t) value << 0);
    }
    else /* Second byte */
    {
        context->state.first_byte_received = false;
        context->state.address = (context->state.address & 0x00ff) | ((uint16_t) (value & 0x3f) << 8);
        context->state.code = value & 0xc0;

        switch (context->state.code)
        {
            case TMS9928A_CODE_VRAM_READ:
                context->state.read_buffer = context->vram [context->state.address++];
                break;
            case TMS9928A_CODE_VRAM_WRITE:
                break;
            case TMS9928A_CODE_REG_WRITE:
                if ((value & 0x0f) <= 10)
                {
                    ((uint8_t *) &context->state.regs_buffer) [value & 0x0f] = context->state.address & 0x00ff;

                    /* Enabling interrupts should take affect immediately */
                    context->state.regs.ctrl_1_frame_int_en = context->state.regs_buffer.ctrl_1_frame_int_en;
                }
                break;
            case SMS_VDP_CODE_CRAM_WRITE:
                break;
            default:
                break;
        }
    }
}


/*
 * Assemble the four mode-bits.
 */
uint8_t sms_vdp_get_mode (TMS9928A_Context *context)
{
    return ((context->state.regs.ctrl_1_mode_1) ? BIT_0 : 0) +
           ((context->state.regs.ctrl_0_mode_2) ? BIT_1 : 0) +
           ((context->state.regs.ctrl_1_mode_3) ? BIT_2 : 0) +
           ((context->state.regs.ctrl_0_mode_4) ? BIT_3 : 0);
}


/*
 * Check if the light phaser is receiving light
 */
bool sms_vdp_get_phaser_th (TMS9928A_Context *context, uint64_t z80_cycle)
{
    int32_t scan_x = ((z80_cycle % 228) * 342) / 228;
    int32_t scan_y = context->state.line;

    int32_t delta_x = scan_x - state.phaser_x;
    int32_t delta_y = scan_y - state.phaser_y;

    if ((delta_x * delta_x + delta_y * delta_y) < (SMS_PHASER_RADIUS * SMS_PHASER_RADIUS))
    {
        return true;
    }

    return false;
}


/*
 * Read the 8-bit h-counter.
 *
 * 342 'pixels' per scanline:
 *
 *  - 256 pixels of active display:    (  0 - 127)
 *  -  15 pixels of right border:      (128 - 135)
 *  -  58 pixels of blanking and sync: (136 - 248)
 *  -  13 pixels of left border:       (249 - 255)
 */
uint8_t sms_vdp_get_h_counter (TMS9928A_Context *context)
{
    if (gamepad [1].type == GAMEPAD_TYPE_SMS_PHASER &&
        context->state.line >= (state.phaser_y - SMS_PHASER_RADIUS) &&
        context->state.line <= (state.phaser_y + SMS_PHASER_RADIUS))
    {
        int32_t y_offset = state.phaser_y - context->state.line;
        int32_t x_offset = sqrt (SMS_PHASER_RADIUS * SMS_PHASER_RADIUS + y_offset * y_offset);

        context->state.h_counter = (state.phaser_x - x_offset) / 2;

        if (context->state.h_counter < 0)
        {
            context->state.h_counter = 0;
        }
        if (context->state.h_counter > 0x7f)
        {
            context->state.h_counter = 0x7f;
        }

        /* Games seem to add their own left-offset */
        context->state.h_counter += 24;
    }

    return context->state.h_counter;
}


/*
 * Read the 8-bit v-counter.
 */
uint8_t sms_vdp_get_v_counter (TMS9928A_Context *context)
{
    return context->state.v_counter;
}


/*
 * Check if the VDP is currently requesting an interrupt.
 */
bool sms_vdp_get_interrupt (TMS9928A_Context *context)
{
    bool interrupt = false;

    /* Frame interrupt */
    if (context->state.regs.ctrl_1_frame_int_en && (context->state.status & TMS9928A_STATUS_INT))
    {
        interrupt = true;
    }

    /* Line interrupt */
    if (context->state.regs.ctrl_0_line_int_en && context->state.line_interrupt)
    {
        interrupt = true;
    }

    return interrupt;
}


#define SMS_VDP_PATTERN_HORIZONTAL_FLIP     BIT_9
#define SMS_VDP_PATTERN_VERTICAL_FLIP       BIT_10


/*
 * Render one line of an 8×8 pattern.
 * Background version.
 * Supports vertical and horizontal mirroring.
 */
static void sms_vdp_mode4_draw_pattern_background (TMS9928A_Context *context, uint16_t line, SMS_VDP_Mode4_Pattern *pattern_base,
                                                   SMS_VDP_Palette palette, int32_Point_2D position, bool flip_h, bool flip_v, bool priority)
{
    uint32_t pattern_line;
    uint32_t pixel_bit;

    /* Get the line within the pattern */
    if (flip_v)
    {
        pattern_line = ((uint32_t *) pattern_base) [position.y - line + 7];
    }
    else
    {
        pattern_line = ((uint32_t *) pattern_base) [line - position.y];
    }

    for (uint32_t x = 0; x < 8; x++)
    {
        /* Nothing to do off the right side of the screen */
        if (x + position.x >= 256)
        {
            return;
        }

        /* Don't draw the left-most eight pixels if BIT_5 of CTRL_1 is set */
        if (context->state.regs.ctrl_0_mask_col_1 && x + position.x < 8)
        {
            continue;
        }

        if (flip_h)
        {
            pixel_bit = x;
        }
        else
        {
            pixel_bit = 7 - x;
        }

        uint8_t colour_index = ((pattern_line >> (pixel_bit     )) & 0x01) |
                               ((pattern_line >> (pixel_bit +  7)) & 0x02) |
                               ((pattern_line >> (pixel_bit + 14)) & 0x04) |
                               ((pattern_line >> (pixel_bit + 21)) & 0x08);

        /* Colour 0 is transparency */
        if (colour_index == 0 && priority)
        {
            continue;
        }

        uint16_t pixel = context->state.cram [palette + colour_index];

        context->frame_buffer [(position.x + x + VIDEO_SIDE_BORDER) +
                               (context->render_start_y + line) * VIDEO_BUFFER_WIDTH] = context->vdp_to_float [pixel & context->vdp_to_float_mask];
    }
}


/*
 * Render one line of the background layer.
 */
static void sms_vdp_mode4_draw_background (TMS9928A_Context *context, const TMS9928A_Config *mode, uint16_t line, bool priority)
{
    uint16_t name_table_base;
    uint8_t num_rows = (mode->lines_active == 192) ? 28 : 32;
    uint8_t start_column = 32 - ((context->state.regs.bg_scroll_x & 0xf8) >> 3);
    uint8_t fine_scroll_x = context->state.regs.bg_scroll_x & 0x07;
    uint8_t start_row = ((context->state.regs.bg_scroll_y % (8 * num_rows)) & 0xf8) >> 3;
    uint8_t fine_scroll_y = (context->state.regs.bg_scroll_y % (8 * num_rows)) & 0x07;
    uint32_t tile_y = (line + fine_scroll_y) / 8;
    int32_Point_2D position;

    if (mode->lines_active == 192)
    {
        name_table_base = (((uint16_t) context->state.regs.name_table_base) << 10) & 0x3800;
    }
    else
    {
        name_table_base = ((((uint16_t) context->state.regs.name_table_base) << 10) & 0x3000) + 0x700;
    }

    /* Bit 6 in ctrl_0 can disable horizontal scrolling for the first two rows */
    if (context->state.regs.ctrl_0_lock_row_0_1 && line < 16)
    {
        start_column = 0;
        fine_scroll_x = 0;
    }

    for (uint32_t tile_x = 0; tile_x < 32; tile_x++)
    {
        /* Bit 7 in ctrl_0 can disable vertical scrolling for the rightmost eight columns */
        if (tile_x == 24 && context->state.regs.ctrl_0_lock_col_24_31)
        {
            start_row = 0;
            fine_scroll_y = 0;
            tile_y = line / 8;
        }

        uint16_t tile_address = name_table_base + ((((tile_y + start_row) % num_rows) << 6) | ((tile_x + start_column) % 32 << 1));

        /* SMS1 VDP name-table mirroring */
        if (context->sms1_vdp_hint && !(context->state.regs.name_table_base & 0x01))
        {
            tile_address &= ~0x0400;
        }

        uint16_t tile = ((uint16_t)(context->vram [tile_address])) +
                        (((uint16_t)(context->vram [tile_address + 1])) << 8);

        /* If we are rendering the "priority" layer, skip any non-priority tiles */
        if (priority && !(tile & 0x1000))
        {
            continue;
        }

        bool flip_h = !!(tile & SMS_VDP_PATTERN_HORIZONTAL_FLIP);
        bool flip_v = !!(tile & SMS_VDP_PATTERN_VERTICAL_FLIP);

        SMS_VDP_Mode4_Pattern *pattern = (SMS_VDP_Mode4_Pattern *) &context->vram [(tile & 0x1ff) * sizeof (SMS_VDP_Mode4_Pattern)];

        SMS_VDP_Palette palette = (tile & (1 << 11)) ? SMS_VDP_PALETTE_SPRITE : SMS_VDP_PALETTE_BACKGROUND;

        position.x = 8 * tile_x + fine_scroll_x;
        position.y = 8 * tile_y - fine_scroll_y;
        sms_vdp_mode4_draw_pattern_background (context, line, pattern, palette, position, flip_h, flip_v, priority);
    }
}


/*
 * Render one line of an 8×8 pattern.
 * Sprite version.
 * Supports magnification.
 */
static void sms_vdp_mode4_draw_pattern_sprite (TMS9928A_Context *context, uint16_t line, SMS_VDP_Mode4_Pattern *pattern_base,
                                                      int32_Point_2D position, bool magnify)
{
    uint32_t draw_width = (magnify) ? 16 : 8;
    uint32_t pattern_line = ((uint32_t *) pattern_base) [(line - position.y) >> magnify];
    uint32_t pixel_bit;

    for (uint32_t x = 0; x < draw_width; x++)
    {
        /* Nothing to do off the right side of the screen */
        if (x + position.x >= 256)
        {
            return;
        }

        /* Don't draw the left-most eight pixels if BIT_5 of CTRL_1 is set */
        if (context->state.regs.ctrl_0_mask_col_1 && x + position.x < 8)
        {
            continue;
        }

        pixel_bit = 7 - (x >> magnify);

        uint8_t colour_index = ((pattern_line >> (pixel_bit     )) & 0x01) |
                               ((pattern_line >> (pixel_bit +  7)) & 0x02) |
                               ((pattern_line >> (pixel_bit + 14)) & 0x04) |
                               ((pattern_line >> (pixel_bit + 21)) & 0x08);

        /* Colour 0 is transparency */
        if (colour_index == 0)
        {
            continue;
        }

        /* Sprite collision detection */
        if (context->state.collision_buffer [x + position.x])
        {
            context->state.status |= TMS9928A_SPRITE_COLLISION;
        }
        context->state.collision_buffer [x + position.x] = true;

        uint16_t pixel = context->state.cram [SMS_VDP_PALETTE_SPRITE + colour_index];

        context->frame_buffer [(position.x + x + VIDEO_SIDE_BORDER) +
                               (context->render_start_y + line) * VIDEO_BUFFER_WIDTH] = context->vdp_to_float [pixel & context->vdp_to_float_mask];
    }
}


/*
 * Render one line of the sprite layer.
 */
static void sms_vdp_mode4_draw_sprites (TMS9928A_Context *context, const TMS9928A_Config *mode, uint16_t line)
{
    uint16_t sprite_attribute_table_base = (((uint16_t) context->state.regs.sprite_attr_table_base) << 7) & 0x3f00;
    uint16_t sprite_pattern_offset = (context->state.regs.sprite_pg_base & 0x04) ? 256 : 0;
    uint8_t pattern_height = context->state.regs.ctrl_1_sprite_mag ? 16 : 8;
    uint8_t sprite_height = context->state.regs.ctrl_1_sprite_size ? (pattern_height << 1) : pattern_height;
    uint8_t line_sprite_buffer [64];
    uint8_t line_sprite_count = 0;
    SMS_VDP_Mode4_Pattern *pattern;
    int32_Point_2D position;
    bool magnify = false;

    /* Sprite magnification */
    if (context->state.regs.ctrl_1_sprite_mag)
    {
        magnify = true;
    }

    /* Traverse the sprite list, filling the line sprite buffer */
    for (int i = 0; i < 64; i++)
    {
        uint8_t y = context->vram [sprite_attribute_table_base + i];

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
            if (line_sprite_count == 8 && !context->remove_sprite_limit)
            {
                context->state.status |= TMS9928A_SPRITE_OVERFLOW;
                break;
            }
            line_sprite_buffer [line_sprite_count++] = i;
        }
    }

    /* Clear the sprite collision buffer */
    memset (context->state.collision_buffer, 0, sizeof (context->state.collision_buffer));

    /* Render the sprites in the line sprite buffer.
     * Done in reverse order so that the first sprite is the one left on the screen */
    while (line_sprite_count--)
    {
        uint16_t i = line_sprite_buffer [line_sprite_count];
        uint8_t y = context->vram [sprite_attribute_table_base + i];
        uint8_t x = context->vram [sprite_attribute_table_base + 0x80 + i * 2];
        uint8_t pattern_index = context->vram [sprite_attribute_table_base + 0x80 + i * 2 + 1];

        position.x = x;

        /* Bit 3 in ctrl_0 shifts all sprites eight pixels to the left */
        if (context->state.regs.ctrl_0_ec)
        {
            position.x -= 8;
        }

        /* TODO: Can we remove these duplicated lines? */
        if (y >= 0xe0)
            position.y = ((int8_t) y) + 1;
        else
            position.y = y + 1;

        if (context->state.regs.ctrl_1_sprite_size)
            pattern_index &= 0xfe;

        pattern = (SMS_VDP_Mode4_Pattern *) &context->vram [(sprite_pattern_offset + pattern_index) * sizeof (SMS_VDP_Mode4_Pattern)];
        sms_vdp_mode4_draw_pattern_sprite (context, line, pattern, position, magnify);

        if (context->state.regs.ctrl_1_sprite_size)
        {
            position.y += pattern_height;
            pattern = (SMS_VDP_Mode4_Pattern *) &context->vram [(sprite_pattern_offset + pattern_index + 1) * sizeof (SMS_VDP_Mode4_Pattern)];
            sms_vdp_mode4_draw_pattern_sprite (context, line, pattern, position, magnify);
        }
    }
}


/*
 * Render one active line of output for the SMS VDP.
 */
static void sms_vdp_render_line (TMS9928A_Context *context, const TMS9928A_Config *config, uint16_t line)
{
    float_Colour video_background =     { .r = 0.0f, .g = 0.0f, .b = 0.0f };

    /* Background */
    if (!context->state.regs.ctrl_1_blank && !context->disable_blanking)
    {
        /* Display is blank */
    }
    else if (config->mode & SMS_VDP_MODE_4)
    {
        uint8_t bg_colour;
        bg_colour = context->state.cram [16 + (context->state.regs.background_colour & 0x0f)];
        video_background = context->vdp_to_float [bg_colour & context->vdp_to_float_mask];
    }
    else
    {
        video_background = config->palette [context->state.regs.background_colour & 0x0f];
    }
    /* Note: For now the top/bottom borders just copy the background from the first
     *       and last active lines. Do any games change the value outside of this? */

    /* Top border */
    if (line == 0)
    {
        for (uint32_t border_line = 0; border_line < context->render_start_y; border_line++)
        {
            for (uint32_t x = 0; x < VIDEO_BUFFER_WIDTH; x++)
            {
                context->frame_buffer [x + border_line * VIDEO_BUFFER_WIDTH] = video_background;
            }
        }
    }

    /* Side borders */
    for (int x = 0; x < VIDEO_BUFFER_WIDTH; x++)
    {
        context->frame_buffer [x + (context->render_start_y + line) * VIDEO_BUFFER_WIDTH] = video_background;
    }

    if (!context->is_game_gear && context->state.regs.ctrl_0_mask_col_1)
    {
        context->video_blank_left = 8;
    }
    else
    {
        context->video_blank_left = 0;
    }

    /* Bottom border */
    if (line == config->lines_active - 1)
    {
        for (uint32_t border_line = context->render_start_y + config->lines_active; border_line < VIDEO_BUFFER_LINES; border_line++)
        {
            for (uint32_t x = 0; x < VIDEO_BUFFER_WIDTH; x++)
            {
                context->frame_buffer [x + border_line * VIDEO_BUFFER_WIDTH] = video_background;
            }
        }
    }

    /* Return without rendering patterns if BLANK is enabled */
    if (!context->state.regs.ctrl_1_blank && !context->disable_blanking)
    {
        return;
    }

    if (context->state.regs.ctrl_0_mode_4)
    {
        sms_vdp_mode4_draw_background (context, config, line, false);
        sms_vdp_mode4_draw_sprites (context, config, line);
        sms_vdp_mode4_draw_background (context, config, line, true);
    }
    else if (context->state.regs.ctrl_0_mode_2)
    {
        tms9928a_render_mode2_background_line (context, config, line);
        tms9928a_render_sprites_line (context, config, line);
    }
}


/*
 * Run one scanline on the VDP.
 */
void sms_vdp_run_one_scanline (TMS9928A_Context *context)
{
    const TMS9928A_Config *config;
    TMS9928A_Mode mode = sms_vdp_get_mode (context);

    switch (mode)
    {
        case TMS9928A_MODE_0:
            config = (context->format == VIDEO_FORMAT_NTSC) ? &Mode0_NTSC : &Mode0_PAL;
            break;

        case TMS9928A_MODE_2: /* Mode 2: 32 × 24 8-byte tiles, sprites enabled, three colour/pattern tables */
            config = (context->format == VIDEO_FORMAT_NTSC) ? &Mode2_NTSC : &Mode2_PAL;
            break;

        case SMS_VDP_MODE_4: /* Mode 4, 192 lines */
        case SMS_VDP_MODE_4_2:
        case SMS_VDP_MODE_4_3:
        case SMS_VDP_MODE_4_3_2_1:
            config = (context->format == VIDEO_FORMAT_NTSC) ? &Mode4_NTSC192 : &Mode4_PAL192;
            break;

        case SMS_VDP_MODE_4_224:
            config = (context->format == VIDEO_FORMAT_NTSC) ? &Mode4_NTSC224 : &Mode4_PAL224;
            break;

        case SMS_VDP_MODE_4_240:
            config = (context->format == VIDEO_FORMAT_NTSC) ? &Mode4_NTSC240 : &Mode4_PAL240;
            break;

        default:
            /* Other modes not implemented. */
            return;
    }

    /* The Master System supports multiple resolutions that can be changed on the fly. */
    if (!context->is_game_gear)
    {
        context->render_start_y = (VIDEO_BUFFER_LINES - config->lines_active) / 2;
        context->video_start_y  = context->render_start_y;
        context->video_height   = config->lines_active;
    }

    /* If this is an active line, render it */
    if (context->state.line < config->lines_active)
    {
        sms_vdp_render_line (context, config, context->state.line);
    }

    /* If this the final active line, copy the frame for output to the user */
    /* TODO: This is okay for single-threaded code, but locking may be needed if multi-threading is added */
    if (context->state.line == config->lines_active - 1)
    {
        context->frame_done (context->parent);

        /* Update statistics (rolling average) */
        /* TODO: Move into a common "frame complete" function */
        static int vdp_previous_completion_time = 0;
        static int vdp_current_time = 0;
        vdp_current_time = util_get_ticks ();
        int frame_time_taken = vdp_current_time - vdp_previous_completion_time;
        if (frame_time_taken != 0)
        {
            state.vdp_framerate *= 0.95;
            state.vdp_framerate += 0.05 * (1000.0 / frame_time_taken);
        }
        vdp_previous_completion_time = vdp_current_time;
    }

    /* Update values for the next line */
    context->state.line = (context->state.line + 1) % config->lines_total;

    uint16_t temp_line = context->state.line;
    for (int range = 0; range < 3; range++)
    {
        if (temp_line < config->v_counter_map [range].last - config->v_counter_map [range].first + 1)
        {
            context->state.v_counter = temp_line + config->v_counter_map [range].first;
            break;
        }
        else
        {
            temp_line -= config->v_counter_map [range].last - config->v_counter_map [range].first + 1;
        }
    }

    /* Propagate register writes that occurred during this line. */
    context->state.regs = context->state.regs_buffer;

    /* Check for frame interrupt */
    if (context->state.line == config->lines_active + 1)
        context->state.status |= TMS9928A_STATUS_INT;

    /* Decrement the line interrupt counter during the active display period.
     * Reset outside of the active display period (but not the first line after) */
    if (context->state.line <= config->lines_active)
        context->state.line_interrupt_counter--;
    else
        context->state.line_interrupt_counter = context->state.regs.line_counter_reset;

    /* Check for line interrupt */
    if (context->state.line <= config->lines_active && context->state.line_interrupt_counter == 0xff)
    {
        context->state.line_interrupt_counter = context->state.regs.line_counter_reset;
        context->state.line_interrupt = true;
    }
}
