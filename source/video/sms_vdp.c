/*
 * Snepulator
 * Sega Master System VDP implementation.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#define SMS_VDP_TO_UINT_PIXEL(C) { .r = ((((C) >> 0) & 0x03) * (0xff / 3)), \
                                   .g = ((((C) >> 2) & 0x03) * (0xff / 3)), \
                                   .b = ((((C) >> 4) & 0x03) * (0xff / 3)) }

#define GG_VDP_TO_UINT_PIXEL(C) { .r = ((((C) >> 0) & 0x0f) * (0xff / 15)), \
                                  .g = ((((C) >> 4) & 0x0f) * (0xff / 15)), \
                                  .b = ((((C) >> 8) & 0x0f) * (0xff / 15)) }

/* Palette used by the Sega Master System VDP */
uint_pixel_t sms_vdp_legacy_palette [16] = {
    SMS_VDP_TO_UINT_PIXEL (0x00), /* Transparent */
    SMS_VDP_TO_UINT_PIXEL (0x00), /* Black */
    SMS_VDP_TO_UINT_PIXEL (0x08), /* Medium Green */
    SMS_VDP_TO_UINT_PIXEL (0x0c), /* Light Green */
    SMS_VDP_TO_UINT_PIXEL (0x10), /* Dark Blue */
    SMS_VDP_TO_UINT_PIXEL (0x30), /* Light blue */
    SMS_VDP_TO_UINT_PIXEL (0x01), /* Dark Red */
    SMS_VDP_TO_UINT_PIXEL (0x3c), /* Cyan */
    SMS_VDP_TO_UINT_PIXEL (0x02), /* Medium Red */
    SMS_VDP_TO_UINT_PIXEL (0x03), /* Light Red */
    SMS_VDP_TO_UINT_PIXEL (0x05), /* Dark Yellow */
    SMS_VDP_TO_UINT_PIXEL (0x0f), /* Light Yellow */
    SMS_VDP_TO_UINT_PIXEL (0x04), /* Dark Green */
    SMS_VDP_TO_UINT_PIXEL (0x33), /* Magenta */
    SMS_VDP_TO_UINT_PIXEL (0x15), /* Grey */
    SMS_VDP_TO_UINT_PIXEL (0x3f)  /* White */
};



/* Display mode details */
static const TMS9928A_ModeInfo Mode0_PAL = {
    .mode = TMS9928A_MODE_0,
    .lines_active = 192,
    .lines_total = 313,
    .v_counter_map = { { .first = 0x00, .last = 0xf2 },
                       { .first = 0xba, .last = 0xff } }
};
static const TMS9928A_ModeInfo Mode0_NTSC = {
    .mode = TMS9928A_MODE_0,
    .lines_active = 192,
    .lines_total = 262,
    .v_counter_map = { { .first = 0x00, .last = 0xda },
                       { .first = 0xd5, .last = 0xff } }
};
static const TMS9928A_ModeInfo Mode1_PAL = {
    .mode = TMS9928A_MODE_1,
    .lines_active = 192,
    .lines_total = 313,
    .v_counter_map = { { .first = 0x00, .last = 0xf2 },
                       { .first = 0xba, .last = 0xff } }
};
static const TMS9928A_ModeInfo Mode1_NTSC = {
    .mode = TMS9928A_MODE_1,
    .lines_active = 192,
    .lines_total = 262,
    .v_counter_map = { { .first = 0x00, .last = 0xda },
                       { .first = 0xd5, .last = 0xff } }
};
static const TMS9928A_ModeInfo Mode2_PAL = {
    .mode = TMS9928A_MODE_2,
    .lines_active = 192,
    .lines_total = 313,
    .v_counter_map = { { .first = 0x00, .last = 0xf2 },
                       { .first = 0xba, .last = 0xff } }
};
static const TMS9928A_ModeInfo Mode2_NTSC = {
    .mode = TMS9928A_MODE_2,
    .lines_active = 192,
    .lines_total = 262,
    .v_counter_map = { { .first = 0x00, .last = 0xda },
                       { .first = 0xd5, .last = 0xff } }
};
static const TMS9928A_ModeInfo Mode3_PAL = {
    .mode = TMS9928A_MODE_3,
    .lines_active = 192,
    .lines_total = 313,
    .v_counter_map = { { .first = 0x00, .last = 0xf2 },
                       { .first = 0xba, .last = 0xff } }
};
static const TMS9928A_ModeInfo Mode3_NTSC = {
    .mode = TMS9928A_MODE_3,
    .lines_active = 192,
    .lines_total = 262,
    .v_counter_map = { { .first = 0x00, .last = 0xda },
                       { .first = 0xd5, .last = 0xff } }
};
static const TMS9928A_ModeInfo Mode4_PAL192 = {
    .mode = SMS_VDP_MODE_4,
    .lines_active = 192,
    .lines_total = 313,
    .v_counter_map = { { .first = 0x00, .last = 0xf2 },
                       { .first = 0xba, .last = 0xff } }
};
static const TMS9928A_ModeInfo Mode4_PAL224 = {
    .mode = SMS_VDP_MODE_4_224,
    .lines_active = 224,
    .lines_total = 313,
    .v_counter_map = { { .first = 0x00, .last = 0xff },
                       { .first = 0x00, .last = 0x02 },
                       { .first = 0xca, .last = 0xff } }
};
static const TMS9928A_ModeInfo Mode4_PAL240 = {
    .mode = SMS_VDP_MODE_4_240,
    .lines_active = 240,
    .lines_total = 313,
    .v_counter_map = { { .first = 0x00, .last = 0xff },
                       { .first = 0x00, .last = 0x0a },
                       { .first = 0xd2, .last = 0xff } }
};
static const TMS9928A_ModeInfo Mode4_NTSC192 = {
    .mode = SMS_VDP_MODE_4,
    .lines_active = 192,
    .lines_total = 262,
    .v_counter_map = { { .first = 0x00, .last = 0xda },
                       { .first = 0xd5, .last = 0xff } }
};
static const TMS9928A_ModeInfo Mode4_NTSC224 = {
    .mode = SMS_VDP_MODE_4_224,
    .lines_active = 224,
    .lines_total = 262,
    .v_counter_map = { { .first = 0x00, .last = 0xea },
                       { .first = 0xe5, .last = 0xff } }
};
static const TMS9928A_ModeInfo Mode4_NTSC240 = {
    .mode = SMS_VDP_MODE_4_240,
    .lines_active = 240,
    .lines_total = 262,
    .v_counter_map = { { .first = 0x00, .last = 0xff },
                       { .first = 0x00, .last = 0x06 } }
};


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
                    context->state.cram [(context->state.address >> 1) & 0x1f] =
                        (uint_pixel_t) GG_VDP_TO_UINT_PIXEL ((((uint16_t) value) << 8) | context->state.cram_latch);
                }
            }
            else
            {
                context->state.cram [context->state.address & 0x1f] = (uint_pixel_t) SMS_VDP_TO_UINT_PIXEL (value);
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
                    ((uint8_t *) &context->state.regs) [value & 0x0f] = context->state.address & 0x00ff;
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

    int32_t delta_x = scan_x - state.cursor_x;
    int32_t delta_y = scan_y - state.cursor_y;

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

    /* If a phaser game is being played, update the h_counter to
     * where the counter will have latched on this line. */
    if (gamepad [1].type == GAMEPAD_TYPE_SMS_PHASER &&
        context->state.line >= (state.cursor_y - SMS_PHASER_RADIUS) &&
        context->state.line <= (state.cursor_y + SMS_PHASER_RADIUS))
    {
        int32_t y_offset = state.cursor_y - context->state.line;
        int32_t x_offset = sqrt (SMS_PHASER_RADIUS * SMS_PHASER_RADIUS + y_offset * y_offset);
        int32_t phaser_latch = (state.cursor_x - x_offset) / 2;

        /* Games seem to add a left-offset, possibly to account for signal delays */
        phaser_latch += 24;

        /* For now, limit the phaser latching to the active area */
        ENFORCE_MINIMUM (phaser_latch, 0x00);
        ENFORCE_MAXIMUM (phaser_latch, 0x7f);

        context->state.h_counter = phaser_latch;
    }

    return context->state.h_counter;
}


/*
 * Update the latched h_counter value.
 * This is called on the rising-edge of the TH pins.
 */
void sms_vdp_update_h_counter (TMS9928A_Context *context, uint64_t cycle_count)
{
    /* Begin the count pattern at the discontinuity that occurs during H-sync */
    uint8_t count_start = 0xe9;

    /* The count pattern repeats once per scanline (228 cpu cycles) */
    cycle_count %= 228;

    /* Note the result is truncated to 8 bits */
    context->state.h_counter = count_start + (cycle_count * 3 + 1) / 4;
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
                                                   SMS_VDP_Palette palette, int_point_t position, bool flip_h, bool flip_v, bool priority)
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

    /* Account for the destination frame-buffer start position, which may be smaller than
     * the native SMS VDP resolution. Eg, due to left-column-blanking or Game Gear cropping */
    if (line < context->crop_start.y || line - context->crop_start.y >= context->frame_buffer.height)
    {
        return;
    }
    int32_t destination_start = position.x - context->crop_start.x + (line - context->crop_start.y) * context->frame_buffer.width;

    for (int32_t x = 0; x < 8; x++)
    {
        /* Nothing to do outside of the active area. Continue if we're to the left,
         * as the next pixel might be on-screen. Return if we're to the right. */
        if (x + position.x < context->crop_start.x)
        {
            continue;
        }
        else if (x + position.x - context->crop_start.x >= context->frame_buffer.width)
        {
            return;
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

        uint_pixel_t pixel = context->state.cram [palette + colour_index];
        context->frame_buffer.active_area [destination_start + x] = pixel;
    }
}


/*
 * Render one line of the background layer.
 */
static void sms_vdp_mode4_draw_background (TMS9928A_Context *context, uint16_t line, bool priority)
{
    uint16_t name_table_base;
    uint8_t num_rows = (context->lines_active == 192) ? 28 : 32;

    /* Name-table row and starting-column for this line */
    uint8_t table_row = ((context->state.regs.bg_scroll_y + line) >> 3) % num_rows;
    uint8_t table_col = 32 - (context->state.bg_scroll_x_latch >> 3);

    uint8_t fine_scroll_x = context->state.bg_scroll_x_latch & 0x07;
    uint8_t fine_scroll_y = context->state.regs.bg_scroll_y & 0x07;
    uint32_t tile_y = (line + fine_scroll_y) >> 3;

    int_point_t position; /* Position of the pattern on the display */

    if (context->lines_active == 192)
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
        table_col = 0;
        fine_scroll_x = 0;
    }

    for (uint32_t tile_x = 0; tile_x < 32; tile_x++)
    {
        /* Bit 7 in ctrl_0 can disable vertical scrolling for the rightmost eight columns */
        if (tile_x == 24 && context->state.regs.ctrl_0_lock_col_24_31)
        {
            table_row = line / 8;
            tile_y = line / 8;
            fine_scroll_y = 0;
        }

        uint16_t tile_address = name_table_base + ((table_row << 6) | ((table_col + tile_x) % 32 << 1));

        /* SMS1 VDP name-table mirroring */
        if (context->sms1_vdp_hint && !(context->state.regs.name_table_base & 0x01))
        {
            tile_address &= ~0x0400;
        }

        uint16_t tile = ((uint16_t)(context->vram [tile_address])) +
                        (((uint16_t)(context->vram [tile_address + 1])) << 8);

        /* Don't redraw a non-priority tile on the priority layer.
         * A priority tile, or at least its background colour, needs to be drawn on
         * the non-priority layer in case there is no sprite to be transparent to. */
        if (priority && !(tile & 0x1000))
        {
            continue;
        }

        bool flip_h = !!(tile & SMS_VDP_PATTERN_HORIZONTAL_FLIP);
        bool flip_v = !!(tile & SMS_VDP_PATTERN_VERTICAL_FLIP);

        SMS_VDP_Mode4_Pattern *pattern = (SMS_VDP_Mode4_Pattern *) &context->vram [(tile & 0x1ff) * sizeof (SMS_VDP_Mode4_Pattern)];

        SMS_VDP_Palette palette = (tile & BIT_11) ? SMS_VDP_PALETTE_SPRITE : SMS_VDP_PALETTE_BACKGROUND;

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
                                               int_point_t position, bool magnify)
{
    uint32_t draw_width = (magnify) ? 16 : 8;
    uint32_t pattern_line = ((uint32_t *) pattern_base) [(line - position.y) >> magnify];
    uint32_t pixel_bit;

    int32_t destination_start = position.x - context->crop_start.x + (line - context->crop_start.y) * context->frame_buffer.width;

    for (uint32_t x = 0; x < draw_width; x++)
    {
        /* Nothing to do off the right side of the screen */
        if (x + position.x >= 256)
        {
            return;
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

        /* Don't actually render to the outside of the active area. */
        if (x + position.x < context->crop_start.x ||
            x + position.x - context->crop_start.x >= context->frame_buffer.width ||
            line < context->crop_start.y ||
            line - context->crop_start.y >= context->frame_buffer.height)
        {
            continue;
        }

        uint_pixel_t pixel = context->state.cram [SMS_VDP_PALETTE_SPRITE + colour_index];
        context->frame_buffer.active_area [destination_start + x] = pixel;
    }
}


/*
 * Render one line of the sprite layer.
 */
static void sms_vdp_mode4_draw_sprites (TMS9928A_Context *context, uint16_t line)
{
    uint16_t sprite_attribute_table_base = (((uint16_t) context->state.regs.sprite_attr_table_base) << 7) & 0x3f00;
    uint16_t sprite_pattern_offset = (context->state.regs.sprite_pg_base & 0x04) ? 256 : 0;
    uint8_t pattern_height = context->state.regs.ctrl_1_sprite_mag ? 16 : 8;
    uint8_t sprite_height = context->state.regs.ctrl_1_sprite_size ? (pattern_height << 1) : pattern_height;
    uint8_t line_sprite_buffer [64];
    uint8_t line_sprite_count = 0;
    SMS_VDP_Mode4_Pattern *pattern;
    int_point_t position;
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
        if (context->lines_active == 192 && y == 0xd0)
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
 * A simplified copy of the sprite overflow check to use when blanking is enabled.
 */
static void sms_vdp_mode4_check_sprite_overflow (TMS9928A_Context *context, uint16_t line)
{
    uint16_t sprite_attribute_table_base = (((uint16_t) context->state.regs.sprite_attr_table_base) << 7) & 0x3f00;
    uint8_t pattern_height = context->state.regs.ctrl_1_sprite_mag ? 16 : 8;
    uint8_t sprite_height = context->state.regs.ctrl_1_sprite_size ? (pattern_height << 1) : pattern_height;
    uint8_t line_sprite_count = 0;
    int_point_t position;

    /* Traverse the sprite list, filling the line sprite buffer */
    for (int i = 0; i < 64; i++)
    {
        uint8_t y = context->vram [sprite_attribute_table_base + i];

        /* Break if there are no more sprites */
        if (context->lines_active == 192 && y == 0xd0)
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
            line_sprite_count++;
        }
    }
}


/*
 * Render one active line of output for the SMS VDP.
 */
static void sms_vdp_render_line (TMS9928A_Context *context, uint16_t line)
{
    uint_pixel_t video_backdrop;

    if (context->mode & SMS_VDP_MODE_4)
    {
        video_backdrop = context->state.cram [16 + (context->state.regs.background_colour & 0x0f)];
    }
    else
    {
        video_backdrop = context->palette [context->state.regs.background_colour & 0x0f];
    }

    /* Only draw the backdrop if this line is included in the active area */
    if (line >= context->crop_start.y &&
        line - context->crop_start.y < context->frame_buffer.height)
    {
        /* Note: For now the top/bottom borders just copy the background from the first
         *       and last active lines. Do any games change the value outside of this? */
        context->frame_buffer.backdrop [line - context->crop_start.y] = video_backdrop;

        /* If blanking is enabled, fill the active area with the backdrop colour. */
        if (!context->state.regs.ctrl_1_blank && !context->disable_blanking)
        {
            uint32_t line_start = (line - context->crop_start.y) * context->frame_buffer.width;
            for (int x = 0; x < context->frame_buffer.width; x++)
            {
                context->frame_buffer.active_area [line_start + x] = video_backdrop;
            }
        }
    }

    /* Don't actually render if BLANK is enabled, only check
     * the sprite overflow flag. */
    if (!context->state.regs.ctrl_1_blank && !context->disable_blanking && context->state.regs.ctrl_0_mode_4)
    {
        sms_vdp_mode4_check_sprite_overflow (context, line);
        return;
    }

    if (context->state.regs.ctrl_0_mode_4)
    {
        sms_vdp_mode4_draw_background (context, line, false);
        sms_vdp_mode4_draw_sprites (context, line);
        sms_vdp_mode4_draw_background (context, line, true);
    }
    else if (context->mode == TMS9928A_MODE_0)
    {
        tms9928a_mode0_draw_background (context, line);
        tms9928a_draw_sprites (context, line);
    }
    else if (context->mode == TMS9928A_MODE_1)
    {
        tms9928a_mode1_draw_background (context, line);
    }
    else if (context->mode == TMS9928A_MODE_2)
    {
        tms9928a_mode2_draw_background (context, line);
        tms9928a_draw_sprites (context, line);
    }
    else if (context->mode == TMS9928A_MODE_3)
    {
        tms9928a_mode3_draw_background (context, line);
        tms9928a_draw_sprites (context, line);
    }
}


/*
 * Called once per frame to update parameters based on the mode.
 */
void sms_vdp_update_mode (TMS9928A_Context *context)
{
    const TMS9928A_ModeInfo *config;
    TMS9928A_Mode mode = sms_vdp_get_mode (context);

    switch (mode)
    {
        case TMS9928A_MODE_0:
            config = (context->format == VIDEO_FORMAT_NTSC) ? &Mode0_NTSC : &Mode0_PAL;
            break;

        case TMS9928A_MODE_1:
            config = (context->format == VIDEO_FORMAT_NTSC) ? &Mode1_NTSC : &Mode1_PAL;
            break;

        case TMS9928A_MODE_2: /* Mode 2: 32 × 24 8-byte tiles, sprites enabled, three colour/pattern tables */
            config = (context->format == VIDEO_FORMAT_NTSC) ? &Mode2_NTSC : &Mode2_PAL;
            break;

        case TMS9928A_MODE_3:
            config = (context->format == VIDEO_FORMAT_NTSC) ? &Mode3_NTSC : &Mode3_PAL;
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
            /* Some games briefly pass through an unsupported mode.
             * Assume Mode 0 to keep line counters and interrupts ticking. */
            config = (context->format == VIDEO_FORMAT_NTSC) ? &Mode0_NTSC : &Mode0_PAL;
            break;
    }

    context->mode         = config->mode;
    context->lines_total  = config->lines_total;
    context->lines_active = config->lines_active;
    memcpy (context->v_counter_map, config->v_counter_map, sizeof (context->v_counter_map));
}


/*
 * Latch the scroll register.
 */
void sms_vdp_update_x_scroll_latch (TMS9928A_Context *context)
{
    context->state.bg_scroll_x_latch = context->state.regs.bg_scroll_x;
}


/*
 * Run one scanline on the VDP.
 */
void sms_vdp_run_one_scanline (TMS9928A_Context *context)
{
    /* Update the V-Counter */
    context->state.line = (context->state.line + 1) % context->lines_total;

    /* If this is the first line, update the mode */
    if (context->state.line == 0)
    {
        sms_vdp_update_mode (context);
        context->crop_start.x = 0;
        context->crop_start.y = 0;

        /* The Master System supports multiple resolutions that can be changed on the fly. */
        if (context->is_game_gear)
        {
            context->frame_buffer.width = 160;
            context->frame_buffer.height = 144;
            context->crop_start.x = 48;
            context->crop_start.y = 24;
        }
        else
        {
            /* Treat mode-4's left-column-blanking as a lower resolution mode */
            if (context->state.regs.ctrl_0_mode_4 && context->state.regs.ctrl_0_mask_col_1)
            {
                context->frame_buffer.width = 248;
                context->crop_start.x = 8;
            }
            else if (context->mode == TMS9928A_MODE_1)
            {
                context->frame_buffer.width = 240;
            }
            else
            {
                context->frame_buffer.width = 256;
            }
            context->frame_buffer.height = context->lines_active;
        }
    }

    /* Update the 8-bit V-Counter */
    uint16_t temp_line = context->state.line;
    for (int range = 0; range < 3; range++)
    {
        if (temp_line < context->v_counter_map [range].last - context->v_counter_map [range].first + 1)
        {
            context->state.v_counter = temp_line + context->v_counter_map [range].first;
            break;
        }
        else
        {
            temp_line -= context->v_counter_map [range].last - context->v_counter_map [range].first + 1;
        }
    }

    /* If this is an active line, render it */
    if (context->state.line < context->lines_active)
    {
        sms_vdp_render_line (context, context->state.line);
    }

    /* If this the final active line, copy the frame for output to the user */
    /* TODO: This is okay for single-threaded code, but locking may be needed if multi-threading is added */
    if (context->state.line == context->lines_active - 1)
    {
        context->frame_done (context->parent);

#if DEVELOPER_BUILD
        /* Update statistics (rolling average) */
        /* TODO: Move into a common "frame complete" function */
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
 * Check the line counter and update the line interrupt.
 */
void sms_vdp_update_line_interrupt (TMS9928A_Context *context)
{
    /* Decrement the line interrupt counter during the active display period.
     * Reset outside of the active display period (but not the first line after) */
    if (context->state.line <= context->lines_active)
    {
        context->state.line_interrupt_counter--;
    }
    else
    {
        context->state.line_interrupt_counter = context->state.regs.line_counter_reset;
    }

    /* Check for line interrupt */
    if (context->state.line <= context->lines_active && context->state.line_interrupt_counter == 0xff)
    {
        context->state.line_interrupt_counter = context->state.regs.line_counter_reset;
        context->state.line_interrupt = true;
    }
}


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

    /* Set console-specific parameters */
    if (console == CONSOLE_GAME_GEAR)
    {
        context->is_game_gear = true;
    }

    context->palette = sms_vdp_legacy_palette;
    context->parent = parent;
    context->frame_done = frame_done;

    sms_vdp_update_mode (context);

    return context;
}
