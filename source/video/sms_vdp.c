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

#include <SDL2/SDL.h>

#include "../util.h"
#include "../snepulator.h"
#include "../sms.h"
#include "../gamepad.h"
#include "../database/sms_db.h"

extern Snepulator_State state;
extern Snepulator_Gamepad gamepad_1;
extern pthread_mutex_t video_mutex;
extern SMS_3D_Field sms_3d_field;
extern uint64_t z80_cycle;

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
    .palette = SMS_VDP_LEGACY_PALETTE
};
static const TMS9928A_Config Mode0_NTSC = {
    .mode = TMS9928A_MODE_0,
    .lines_active = 192,
    .lines_total = 262,
    .palette = SMS_VDP_LEGACY_PALETTE
};
static const TMS9928A_Config Mode2_PAL = {
    .mode = TMS9928A_MODE_2,
    .lines_active = 192,
    .lines_total = 313,
    .palette = SMS_VDP_LEGACY_PALETTE
};
static const TMS9928A_Config Mode2_NTSC = {
    .mode = TMS9928A_MODE_2,
    .lines_active = 192,
    .lines_total = 262,
    .palette = SMS_VDP_LEGACY_PALETTE
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

float_Colour vdp_to_float [4096] = { };


/* SMS VDP State */
extern TMS9928A_State tms9928a_state;
extern float_Colour frame_buffer [VIDEO_BUFFER_WIDTH * VIDEO_BUFFER_LINES];
uint16_t colour_mask;

/*
 * Reset the VDP registers and memory to power-on defaults.
 */
void sms_vdp_init (void)
{
    /* Populate vdp_to_float colour table */
    if (state.console == CONSOLE_GAME_GEAR)
    {
        for (uint32_t i = 0; i < 4096; i++)
        {
            vdp_to_float [i] = (float_Colour) GG_VDP_TO_FLOAT (i);
        }
        colour_mask = 0x0fff;
    }
    else
    {
        for (uint32_t i = 0; i < 64; i++)
        {
            vdp_to_float [i] = (float_Colour) SMS_VDP_TO_FLOAT (i);
        }
        colour_mask = 0x3f;
    }

    memset (&tms9928a_state, 0, sizeof (tms9928a_state));
    memset (frame_buffer, 0, sizeof (frame_buffer));
}


/* TODO: Use tms9928a versions where identical */
/*
 * Read one byte from the VDP data port.
 */
uint8_t sms_vdp_data_read ()
{
    uint8_t data = tms9928a_state.read_buffer;

    tms9928a_state.first_byte_received = false;

    tms9928a_state.read_buffer = state.vram [tms9928a_state.address];

    tms9928a_state.address = (tms9928a_state.address + 1) & 0x3fff;

    return data;

}


/*
 * Write one byte to the VDP data port.
 */
void sms_vdp_data_write (uint8_t value)
{
    tms9928a_state.first_byte_received = false;
    tms9928a_state.read_buffer = value;

    switch (tms9928a_state.code)
    {
        case TMS9928A_CODE_VRAM_READ:
        case TMS9928A_CODE_VRAM_WRITE:
        case TMS9928A_CODE_REG_WRITE:
            state.vram [tms9928a_state.address] = value;
            break;

        case SMS_VDP_CODE_CRAM_WRITE:
            if (state.console == CONSOLE_GAME_GEAR)
            {
                if ((tms9928a_state.address & 0x01) == 0x00)
                {
                    tms9928a_state.cram_latch = value;
                }
                else
                {
                    tms9928a_state.cram [(tms9928a_state.address >> 1) & 0x1f] = (((uint16_t) value) << 8) | tms9928a_state.cram_latch;
                }
            }
            else
            {
                tms9928a_state.cram [tms9928a_state.address & 0x1f] = value;
            }
            break;

        default:
            break;
    }
    tms9928a_state.address = (tms9928a_state.address + 1) & 0x3fff;
}


/*
 * Read one byte from the VDP control (status) port.
 */
uint8_t sms_vdp_status_read ()
{
    uint8_t status = tms9928a_state.status;
    tms9928a_state.first_byte_received = false;

    /* Clear on read */
    tms9928a_state.status = 0x00;
    tms9928a_state.line_interrupt = false; /* "The flag remains set until the control port (IO port 0xbf) is read */

    return status;
}


/*
 * Write one byte to the VDP control port.
 */
void sms_vdp_control_write (uint8_t value)
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
                tms9928a_state.read_buffer = state.vram [tms9928a_state.address++];
                break;
            case TMS9928A_CODE_VRAM_WRITE:
                break;
            case TMS9928A_CODE_REG_WRITE:
                if ((value & 0x0f) <= 10)
                {
                    ((uint8_t *) &tms9928a_state.regs_buffer) [value & 0x0f] = tms9928a_state.address & 0x00ff;

                    /* Enabling interrupts should take affect immediately */
                    tms9928a_state.regs.ctrl_1 &= ~TMS9928A_CTRL_1_FRAME_INT_EN;
                    tms9928a_state.regs.ctrl_1 |= tms9928a_state.regs_buffer.ctrl_1 & TMS9928A_CTRL_1_FRAME_INT_EN;
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
uint8_t sms_vdp_mode_get (void)
{
    return ((tms9928a_state.regs.ctrl_1 & TMS9928A_CTRL_1_MODE_1) ? BIT_0 : 0) +
           ((tms9928a_state.regs.ctrl_0 & TMS9928A_CTRL_0_MODE_2) ? BIT_1 : 0) +
           ((tms9928a_state.regs.ctrl_1 & TMS9928A_CTRL_1_MODE_3) ? BIT_2 : 0) +
           ((tms9928a_state.regs.ctrl_0 & SMS_VDP_CTRL_0_MODE_4)  ? BIT_3 : 0);
}


/*
 * Check if the light phaser is receiving light
 */
bool sms_vdp_get_phaser_th (void)
{
    int32_t scan_x = ((z80_cycle % 228) * 342) / 228;
    int32_t scan_y = tms9928a_state.line;

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
uint8_t sms_vdp_get_h_counter (void)
{
    if (gamepad_1.type == GAMEPAD_TYPE_SMS_PHASER &&
        tms9928a_state.line >= (state.phaser_y - SMS_PHASER_RADIUS) &&
        tms9928a_state.line <= (state.phaser_y + SMS_PHASER_RADIUS))
    {
        int32_t y_offset = state.phaser_y - tms9928a_state.line;
        int32_t x_offset = sqrt (SMS_PHASER_RADIUS * SMS_PHASER_RADIUS + y_offset * y_offset);

        tms9928a_state.h_counter = (state.phaser_x - x_offset) / 2;

        if (tms9928a_state.h_counter < 0)
        {
            tms9928a_state.h_counter = 0;
        }
        if (tms9928a_state.h_counter > 0x7f)
        {
            tms9928a_state.h_counter = 0x7f;
        }

        /* Games seem to add their own left-offset */
        tms9928a_state.h_counter += 24;
    }

    return tms9928a_state.h_counter;
}


/*
 * Read the 8-bit v-counter.
 */
uint8_t sms_vdp_get_v_counter (void)
{
    return tms9928a_state.v_counter;
}


/*
 * Check if the VDP is currently requesting an interrupt.
 */
bool sms_vdp_get_interrupt (void)
{
    bool interrupt = false;

    /* Frame interrupt */
    if ((tms9928a_state.regs.ctrl_1 & TMS9928A_CTRL_1_FRAME_INT_EN) && (tms9928a_state.status & TMS9928A_STATUS_INT))
    {
        interrupt = true;
    }

    /* Line interrupt */
    if ((tms9928a_state.regs.ctrl_0 & SMS_VDP_CTRL_0_LINE_INT_EN) && tms9928a_state.line_interrupt)
    {
        interrupt = true;
    }

    return interrupt;
}


/*
 * Process the new 3d field to update the anaglyph output image.
 */
void sms_vdp_process_3d_field (void)
{
    bool update_red = false;
    bool update_green = false;
    bool update_blue = false;
    float_Colour pixel;

    switch (state.video_3d_mode)
    {
        case VIDEO_3D_LEFT_ONLY:
            if (sms_3d_field == SMS_3D_FIELD_LEFT)
            {
                update_red = true;
                update_green = true;
                update_blue = true;
            }
            break;

        case VIDEO_3D_RIGHT_ONLY:
            if (sms_3d_field == SMS_3D_FIELD_RIGHT)
            {
                update_red = true;
                update_green = true;
                update_blue = true;
            }
            break;

        case VIDEO_3D_RED_CYAN:
            if (sms_3d_field == SMS_3D_FIELD_LEFT)
            {
                update_red = true;
            }
            else
            {
                update_green = true;
                update_blue = true;
            }
            break;

        case VIDEO_3D_RED_GREEN:
            if (sms_3d_field == SMS_3D_FIELD_LEFT)
            {
                update_red = true;
            }
            else
            {
                update_green = true;
            }
            break;

        case VIDEO_3D_MAGENTA_GREEN:
            if (sms_3d_field == SMS_3D_FIELD_LEFT)
            {
                update_red = true;
                update_blue = true;
            }
            else
            {
                update_green = true;
            }
            break;
    }

    for (uint32_t i = 0; i < (VIDEO_BUFFER_WIDTH * VIDEO_BUFFER_LINES); i++)
    {
        pixel = colour_saturation (frame_buffer [i], state.video_3d_saturation);

        if (update_red)
        {
            state.video_out_data [i].r = pixel.r;
        }
        if (update_green)
        {
            state.video_out_data [i].g = pixel.g;
        }
        if (update_blue)
        {
            state.video_out_data [i].b = pixel.b;
        }

        /* Special case where blue is not used */
        if (state.video_3d_mode == VIDEO_3D_RED_GREEN)
        {
            state.video_out_data [i].b = 0.0;
        }
    }
}


#define PATTERN_FLIP_H          0x01
#define PATTERN_FLIP_V          0x02
#define PATTERN_TRANSPARENCY    0x04
#define PATTERN_COLLISIONS      0x08
#define PATTERN_MAGNIFY         0x10

/*
 * Render one line of an 8x8 pattern.
 */
void sms_vdp_render_mode4_pattern_line (const TMS9928A_Config *mode, uint16_t line, SMS_VDP_Mode4_Pattern *pattern_base,
                                        SMS_VDP_Palette palette, int32_Point_2D offset, uint8_t flags)
{
    char *line_base;

    uint8_t draw_width = (flags & PATTERN_MAGNIFY) ? 16 : 8;

    if (flags & PATTERN_FLIP_V)
        line_base = (char *)(&pattern_base->data [(offset.y - line + 7) * 4]);
    else
        line_base = (char *)(&pattern_base->data [((line - offset.y) >> !!(flags & PATTERN_MAGNIFY)) * 4]);

    for (uint32_t x = 0; x < draw_width; x++)
    {
        /* Don't draw texture pixels that fall outside of the screen */
        if (x + offset.x >= 256)
            continue;

        /* Don't draw the left-most eight pixels if BIT_5 of CTRL_1 is set */
        if (tms9928a_state.regs.ctrl_0 & SMS_VDP_CTRL_0_MASK_COL_1 && x + offset.x < 8)
            continue;

        int shift;

        if (flags & PATTERN_FLIP_H)
            shift = x;
        else
            shift = 7 - (x >> !!(flags & PATTERN_MAGNIFY));

        uint8_t colour_index = (((line_base [0] >> shift) & 0x01)     ) |
                               (((line_base [1] >> shift) & 0x01) << 1) |
                               (((line_base [2] >> shift) & 0x01) << 2) |
                               (((line_base [3] >> shift) & 0x01) << 3);

        if ((flags & PATTERN_TRANSPARENCY) && colour_index == 0)
            continue;

        /* Sprite collision detection */
        if (flags & PATTERN_COLLISIONS)
        {
            if (tms9928a_state.collision_buffer [x + offset.x])
            {
                tms9928a_state.status |= TMS9928A_SPRITE_COLLISION;
            }

            tms9928a_state.collision_buffer [x + offset.x] = true;
        }

        uint16_t pixel = tms9928a_state.cram [palette + colour_index];

        frame_buffer [(offset.x + x + VIDEO_SIDE_BORDER) +
                      (state.render_start_y + line) * VIDEO_BUFFER_WIDTH] = vdp_to_float [pixel & colour_mask];
    }
}


#define SMS_VDP_PATTERN_HORIZONTAL_FLIP     BIT_9
#define SMS_VDP_PATTERN_VERTICAL_FLIP       BIT_10


/*
 * Render one line of the background layer.
 */
void sms_vdp_render_mode4_background_line (const TMS9928A_Config *mode, uint16_t line, bool priority)
{
    uint16_t name_table_base;
    uint8_t num_rows = (mode->lines_active == 192) ? 28 : 32;
    uint8_t start_column = 32 - ((tms9928a_state.regs.bg_scroll_x & 0xf8) >> 3);
    uint8_t fine_scroll_x = tms9928a_state.regs.bg_scroll_x & 0x07;
    uint8_t start_row = ((tms9928a_state.regs.bg_scroll_y % (8 * num_rows)) & 0xf8) >> 3;
    uint8_t fine_scroll_y = (tms9928a_state.regs.bg_scroll_y % (8 * num_rows)) & 0x07;
    uint32_t tile_y = (line + fine_scroll_y) / 8;
    int32_Point_2D position;
    uint8_t pattern_flags;

    if (mode->lines_active == 192)
    {
        name_table_base = (((uint16_t) tms9928a_state.regs.name_table_base) << 10) & 0x3800;
    }
    else
    {
        name_table_base = ((((uint16_t) tms9928a_state.regs.name_table_base) << 10) & 0x3000) + 0x700;
    }

    /* Bit 6 in ctrl_0 can disable horizontal scrolling for the first two rows */
    if (tms9928a_state.regs.ctrl_0 & SMS_VDP_CTRL_0_LOCK_ROW_0_1 && line < 16)
    {
        start_column = 0;
        fine_scroll_x = 0;
    }

    for (uint32_t tile_x = 0; tile_x < 32; tile_x++)
    {
        pattern_flags = priority ? PATTERN_TRANSPARENCY : 0;

        /* Bit 7 in ctrl_0 can disable vertical scrolling for the rightmost eight columns */
        if (tile_x == 24 && (tms9928a_state.regs.ctrl_0 & SMS_VDP_CTRL_0_LOCK_COL_24_31))
        {
            start_row = 0;
            fine_scroll_y = 0;
            tile_y = line / 8;
        }

        uint16_t tile_address = name_table_base + ((((tile_y + start_row) % num_rows) << 6) | ((tile_x + start_column) % 32 << 1));

        /* SMS1 VDP name-table mirroring */
        if (state.rom_hints & SMS_HINT_SMS1_VDP && !(tms9928a_state.regs.name_table_base & 0x01))
        {
            tile_address &= ~0x0400;
        }

        uint16_t tile = ((uint16_t)(state.vram [tile_address])) +
                        (((uint16_t)(state.vram [tile_address + 1])) << 8);

        /* If we are rendering the "priority" layer, skip any non-priority tiles */
        if (priority && !(tile & 0x1000))
            continue;

        if (tile & SMS_VDP_PATTERN_HORIZONTAL_FLIP)
        {
            pattern_flags |= PATTERN_FLIP_H;
        }

        if (tile & SMS_VDP_PATTERN_VERTICAL_FLIP)
        {
            pattern_flags |= PATTERN_FLIP_V;
        }

        SMS_VDP_Mode4_Pattern *pattern = (SMS_VDP_Mode4_Pattern *) &state.vram [(tile & 0x1ff) * sizeof (SMS_VDP_Mode4_Pattern)];

        SMS_VDP_Palette palette = (tile & (1 << 11)) ? SMS_VDP_PALETTE_SPRITE : SMS_VDP_PALETTE_BACKGROUND;

        position.x = 8 * tile_x + fine_scroll_x;
        position.y = 8 * tile_y - fine_scroll_y;
        sms_vdp_render_mode4_pattern_line (mode, line, pattern, palette, position, pattern_flags);
    }
}


/*
 * Render one line of the sprite layer.
 */
void sms_vdp_render_mode4_sprites_line (const TMS9928A_Config *mode, uint16_t line)
{
    uint16_t sprite_attribute_table_base = (((uint16_t) tms9928a_state.regs.sprite_attr_table_base) << 7) & 0x3f00;
    uint16_t sprite_pattern_offset = (tms9928a_state.regs.sprite_pg_base & 0x04) ? 256 : 0;
    uint8_t pattern_height = (tms9928a_state.regs.ctrl_1 & TMS9928A_CTRL_1_SPRITE_MAG) ? 16 : 8;
    uint8_t sprite_height = (tms9928a_state.regs.ctrl_1 & TMS9928A_CTRL_1_SPRITE_SIZE) ? (pattern_height << 1) : pattern_height;
    uint8_t line_sprite_buffer [8];
    uint8_t line_sprite_count = 0;
    SMS_VDP_Mode4_Pattern *pattern;
    int32_Point_2D position;
    uint8_t pattern_flags = PATTERN_TRANSPARENCY | PATTERN_COLLISIONS;

    /* Sprite magnification */
    if (tms9928a_state.regs.ctrl_1 & TMS9928A_CTRL_1_SPRITE_MAG)
    {
        pattern_flags |= PATTERN_MAGNIFY;
    }

    /* Traverse the sprite list, filling the line sprite buffer */
    for (int i = 0; i < 64; i++)
    {
        uint8_t y = state.vram [sprite_attribute_table_base + i];

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
            if (line_sprite_count == 8)
            {
                tms9928a_state.status |= TMS9928A_SPRITE_OVERFLOW;
                break;
            }
            line_sprite_buffer [line_sprite_count++] = i;
        }
    }

    /* Clear the sprite collision buffer */
    memset (tms9928a_state.collision_buffer, 0, sizeof (tms9928a_state.collision_buffer));

    /* Render the sprites in the line sprite buffer.
     * Done in reverse order so that the first sprite is the one left on the screen */
    while (line_sprite_count--)
    {
        uint16_t i = line_sprite_buffer [line_sprite_count];
        uint8_t y = state.vram [sprite_attribute_table_base + i];
        uint8_t x = state.vram [sprite_attribute_table_base + 0x80 + i * 2];
        uint8_t pattern_index = state.vram [sprite_attribute_table_base + 0x80 + i * 2 + 1];

        position.x = x;

        /* Bit 3 in ctrl_0 shifts all sprites eight pixels to the left */
        if (tms9928a_state.regs.ctrl_0 & SMS_VDP_CTRL_0_EC)
        {
            position.x -= 8;
        }

        /* TODO: Can we remove these duplicated lines? */
        if (y >= 0xe0)
            position.y = ((int8_t) y) + 1;
        else
            position.y = y + 1;

        if (tms9928a_state.regs.ctrl_1 & TMS9928A_CTRL_1_SPRITE_SIZE)
            pattern_index &= 0xfe;

        pattern = (SMS_VDP_Mode4_Pattern *) &state.vram [(sprite_pattern_offset + pattern_index) * sizeof (SMS_VDP_Mode4_Pattern)];
        sms_vdp_render_mode4_pattern_line (mode, line, pattern, SMS_VDP_PALETTE_SPRITE, position, pattern_flags);

        if (tms9928a_state.regs.ctrl_1 & TMS9928A_CTRL_1_SPRITE_SIZE)
        {
            position.y += pattern_height;
            pattern = (SMS_VDP_Mode4_Pattern *) &state.vram [(sprite_pattern_offset + pattern_index + 1) * sizeof (SMS_VDP_Mode4_Pattern)];
            sms_vdp_render_mode4_pattern_line (mode, line, pattern, SMS_VDP_PALETTE_SPRITE, position, pattern_flags);
        }
    }
}


/*
 * Render one active line of output for the SMS VDP.
 */
void sms_vdp_render_line (const TMS9928A_Config *config, uint16_t line)
{
    float_Colour video_background =     { .r = 0.0f, .g = 0.0f, .b = 0.0f };

    state.render_start_y = (VIDEO_BUFFER_LINES - config->lines_active) / 2;

    /* Background */
    if (!(tms9928a_state.regs.ctrl_1 & TMS9928A_CTRL_1_BLANK))
    {
        /* Display is blank */
    }
    else if (config->mode & SMS_VDP_MODE_4)
    {
        uint8_t bg_colour;
        bg_colour = tms9928a_state.cram [16 + (tms9928a_state.regs.background_colour & 0x0f)];
        video_background = vdp_to_float [bg_colour & colour_mask];
    }
    else
    {
        video_background = config->palette [tms9928a_state.regs.background_colour & 0x0f];
    }
    /* Note: For now the top/bottom borders just copy the background from the first
     *       and last active lines. Do any games change the value outside of this? */

    /* Top border */
    if (line == 0)
    {
        for (uint32_t border_line = 0; border_line < state.render_start_y; border_line++)
        {
            for (uint32_t x = 0; x < VIDEO_BUFFER_WIDTH; x++)
            {
                frame_buffer [x + border_line * VIDEO_BUFFER_WIDTH] = video_background;
            }
        }
    }

    /* Side borders */
    for (int x = 0; x < VIDEO_BUFFER_WIDTH; x++)
    {
        frame_buffer [x + (state.render_start_y + line) * VIDEO_BUFFER_WIDTH] = video_background;
    }

    if (tms9928a_state.regs.ctrl_0 & SMS_VDP_CTRL_0_MASK_COL_1)
    {
        state.video_blank_left = 8;
    }
    else
    {
        state.video_blank_left = 0;
    }

    /* Bottom border */
    if (line == config->lines_active - 1)
    {
        for (uint32_t border_line = state.render_start_y + config->lines_active; border_line < VIDEO_BUFFER_LINES; border_line++)
        {
            for (uint32_t x = 0; x < VIDEO_BUFFER_WIDTH; x++)
            {
                frame_buffer [x + border_line * VIDEO_BUFFER_WIDTH] = video_background;
            }
        }
    }

    /* Return without rendering patterns if BLANK is enabled */
    if (!(tms9928a_state.regs.ctrl_1 & TMS9928A_CTRL_1_BLANK))
    {
        return;
    }

    if (tms9928a_state.regs.ctrl_0 & SMS_VDP_CTRL_0_MODE_4)
    {
        sms_vdp_render_mode4_background_line (config, line, false);
        sms_vdp_render_mode4_sprites_line (config, line);
        sms_vdp_render_mode4_background_line (config, line, true);
    }
    else if (tms9928a_state.regs.ctrl_0 & TMS9928A_CTRL_0_MODE_2)
    {
        tms9928a_render_mode2_background_line (config, line);
        tms9928a_render_sprites_line (config, line);
    }
}


/*
 * Run one scanline on the VDP.
 */
void sms_vdp_run_one_scanline ()
{
    const TMS9928A_Config *config;
    TMS9928A_Mode mode = sms_vdp_mode_get ();

    switch (mode)
    {
        case TMS9928A_MODE_0:
            config = (state.format == VIDEO_FORMAT_NTSC) ? &Mode0_NTSC : &Mode0_PAL;
            break;

        case TMS9928A_MODE_2: /* Mode 2: 32 × 24 8-byte tiles, sprites enabled, three colour/pattern tables */
            config = (state.format == VIDEO_FORMAT_NTSC) ? &Mode2_NTSC : &Mode2_PAL;
            break;

        case SMS_VDP_MODE_4: /* Mode 4, 192 lines */
        case SMS_VDP_MODE_4_2:
        case SMS_VDP_MODE_4_3:
        case SMS_VDP_MODE_4_3_2_1:
            config = (state.format == VIDEO_FORMAT_NTSC) ? &Mode4_NTSC192 : &Mode4_PAL192;
            break;

        case SMS_VDP_MODE_4_224:
            config = (state.format == VIDEO_FORMAT_NTSC) ? &Mode4_NTSC224 : &Mode4_PAL224;
            break;

        case SMS_VDP_MODE_4_240:
            config = (state.format == VIDEO_FORMAT_NTSC) ? &Mode4_NTSC240 : &Mode4_PAL240;
            break;

        default:
            /* Other modes not implemented. */
            return;
    }

    /* If this is an active line, render it */
    if (tms9928a_state.line < config->lines_active)
    {
        sms_vdp_render_line (config, tms9928a_state.line);
    }

    /* If this the final active line, copy the frame for output to the user */
    /* TODO: This is okay for single-threaded code, but locking may be needed if multi-threading is added */
    if (tms9928a_state.line == config->lines_active - 1)
    {
        pthread_mutex_lock (&video_mutex);

        if (state.console == CONSOLE_MASTER_SYSTEM)
        {
            state.video_start_y = state.render_start_y;
            state.video_height = config->lines_active;
        }

        if (sms_3d_field != SMS_3D_FIELD_NONE)
        {
            sms_vdp_process_3d_field ();
        }
        else
        {
            if (state.console == CONSOLE_GAME_GEAR)
            {
                /* Only keep the LCD area */
                for (uint32_t y = 0; y < VIDEO_BUFFER_LINES; y++)
                {
                    for (uint32_t x = 0; x < VIDEO_BUFFER_WIDTH; x++)
                    {
                        if ((x >= state.video_start_x) && (x < state.video_start_x + state.video_width) &&
                            (y >= state.video_start_y) && (y < state.video_start_y + state.video_height))
                        {
                            continue;
                        }
                        frame_buffer [x + y * VIDEO_BUFFER_WIDTH] = (float_Colour) { .r = 0.0, .g = 0.0, .b = 0.0 };
                    }
                }
            }

            memcpy (state.video_out_data, frame_buffer, sizeof (frame_buffer));
        }

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

    /* Update values for the next line */
    tms9928a_state.line = (tms9928a_state.line + 1) % config->lines_total;

    uint16_t temp_line = tms9928a_state.line;
    for (int range = 0; range < 3; range++)
    {
        if (temp_line < config->v_counter_map [range].last - config->v_counter_map [range].first + 1)
        {
            tms9928a_state.v_counter = temp_line + config->v_counter_map [range].first;
            break;
        }
        else
        {
            temp_line -= config->v_counter_map [range].last - config->v_counter_map [range].first + 1;
        }
    }

    /* Propagate register writes that occurred during this line. */
    tms9928a_state.regs = tms9928a_state.regs_buffer;

    /* Check for frame interrupt */
    if (tms9928a_state.line == config->lines_active + 1)
        tms9928a_state.status |= TMS9928A_STATUS_INT;

    /* Decrement the line interrupt counter during the active display period.
     * Reset outside of the active display period (but not the first line after) */
    if (tms9928a_state.line <= config->lines_active)
        tms9928a_state.line_interrupt_counter--;
    else
        tms9928a_state.line_interrupt_counter = tms9928a_state.regs.line_counter_reset;

    /* Check for line interrupt */
    if (tms9928a_state.line <= config->lines_active && tms9928a_state.line_interrupt_counter == 0xff)
    {
        tms9928a_state.line_interrupt_counter = tms9928a_state.regs.line_counter_reset;
        tms9928a_state.line_interrupt = true;
    }
}
