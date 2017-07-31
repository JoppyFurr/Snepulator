#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include "../snepulator.h"
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
static uint8_t v_counter = 0; /* Should this be added to the register struct? */
static uint8_t cram [VDP_CRAM_SIZE];
static uint8_t vram [VDP_VRAM_SIZE];

/* TODO: Make this line accurate */
/* TODO: Add interrupts */

void vdp_init (void)
{
    /* TODO: Are there any nonzero default values? */
    memset (&vdp_regs, 0, sizeof(vdp_regs));
    memset (vram,      0, sizeof(vram));
    memset (cram,      0, sizeof(cram));
}

static bool first_byte_received = false;

uint8_t vdp_data_read ()
{
    first_byte_received = false;

    fprintf (stdout,"[DEBUG(vdp)]: vdp_data_read () not implemented.\n");

    /* DEFAULT */
    return 0xff;
}

void vdp_data_write (uint8_t value)
{
    first_byte_received = false;

    switch (vdp_regs.code)
    {
        case VDP_CODE_VRAM_READ:
        case VDP_CODE_VRAM_WRITE:
            vram[vdp_regs.address] = value;
            break;

        case VDP_CODE_REG_WRITE:
            fprintf (stdout, "[DEBUG(vdp)]: vdp_data_write: REG_WRITE not implemented.\n");
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

/* 192 - Active display
 *  48 - Bottom border
 *   3 - Bottom blanking
 *   3 - Vertical blanking
 *  13 - Top blanking
 *  54 - Top border
 *
 *  0x00 -> 0xf2, 0xba -> 0xff
 */

#define SMS_CLOCK_RATE_PAL  3546895
#define SMS_CLOCK_PER_FRAME_PAL (SMS_CLOCK_RATE_PAL / 50)
#define SMS_CLOCK_PER_LINE_PAL (SMS_CLOCK_RATE_PAL / (50 * 313))

/* TODO: For now, assuming PAL (313 scanlines) */
void vdp_clock_update (uint64_t cycles)
{
    static uint64_t previous_cycles = 0;
    static uint64_t cycles_unprocessed = 0;
    static uint16_t v_counter_16 = 0;

    cycles_unprocessed += (cycles - previous_cycles);
    previous_cycles = cycles;

    while (cycles_unprocessed > SMS_CLOCK_PER_LINE_PAL)
    {
        /* Check for frame interrupt */
        if (v_counter_16 == 193)
            vdp_regs.status |= VDP_STATUS_INT;

        /* Decrement the line interrupt counter during the active display period.
         * Reset outside of the active display period (but not the first line after) */
        if (v_counter_16 <= 192)
            vdp_regs.line_interrupt_counter--;
        else
            vdp_regs.line_interrupt_counter = vdp_regs.line_counter;

        /* On underflow, we reset the line interrupt counter and set the pending flag */
        if (v_counter_16 <= 192 && vdp_regs.line_interrupt_counter == 0xff)
        {
            vdp_regs.line_interrupt_counter = vdp_regs.line_counter;
            vdp_regs.line_interrupt = true;
        }

        /* Update values for the next line */
        v_counter_16 = (v_counter_16 + 1) % 313;
        v_counter = (v_counter_16 <= 0xf2) ? v_counter_16 : (v_counter_16 - 0xf3) + 0xba;
        cycles_unprocessed -= SMS_CLOCK_PER_LINE_PAL;
    }
}

uint8_t vdp_get_v_counter (void)
{
    return v_counter;
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

#define VDP_STRIDE_X (3)
#define VDP_STRIDE_Y (256 * 3)

typedef struct Point2D_s {
    int32_t x;
    int32_t y;
} Point2D;

void vdp_render_pattern (Vdp_Pattern *pattern_base, Vdp_Palette palette, Point2D offset, bool h_flip, bool v_flip, bool transparency)
{
    for (uint32_t y = 0; y < 8; y++)
    {
        char *line_base;

        if (v_flip)
            line_base = (char *)(&pattern_base->data[(7 - y) * 4]);
        else
            line_base = (char *)(&pattern_base->data[y * 4]);

        for (uint32_t x = 0; x < 8; x++)
        {
            /* Don't draw texture pixels that fall outside of the screen */
            if (x + offset.x >= 256 || y + offset.y >= 192)
                continue;

            /* TODO: This seems to be chopping off a little too much */
            /* TODO: To make this blend in better, should it use the darkened version? */
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

            snepulator.sms_vdp_texture_data [(offset.x + x) * VDP_STRIDE_X + (offset.y + y) * VDP_STRIDE_Y + 0] = VDP_TO_RED   (pixel) / 256.0f;
            snepulator.sms_vdp_texture_data [(offset.x + x) * VDP_STRIDE_X + (offset.y + y) * VDP_STRIDE_Y + 1] = VDP_TO_GREEN (pixel) / 256.0f;
            snepulator.sms_vdp_texture_data [(offset.x + x) * VDP_STRIDE_X + (offset.y + y) * VDP_STRIDE_Y + 2] = VDP_TO_BLUE  (pixel) / 256.0f;
        }
    }
}

#define VDP_PATTERN_HORIZONTAL_FLIP     BIT_9
#define VDP_PATTERN_VERTICAL_FLIP       BIT_10

/* TODO: Confirm left-edge behaviour or a real Master System */
void vdp_render_background (bool priority)
{
    /* TODO: If using more than 192 lines, only two bits should be used here */
    uint16_t name_table_base = (((uint16_t) vdp_regs.name_table_addr) << 10) & 0x3800;

    uint8_t start_column = 32 - ((vdp_regs.background_x_scroll & 0xf8) >> 3);
    uint8_t fine_scroll_x = vdp_regs.background_x_scroll & 0x07;
    uint8_t start_row = (((vdp_regs.background_y_scroll % 224) & 0xf8) >> 3);
    uint8_t fine_scroll_y = (vdp_regs.background_y_scroll % 224) & 0x07;
    Point2D position;

    /* TODO: Implement VDP_MODE_CTRL_1_HLOCK_24_31 */
    /* TODO: Implement VDP_MODE_CTRL_1_VLOCK_0_1   */
    /* TODO: The vertical scroll value should only take affect between active frames, not mid-frame */
    /* TODO: If fine-scroll of Y is active, what happens in the first/last few lines of the display? */
    for (uint32_t tile_y = 0; tile_y < 24; tile_y++)
    {
        for (uint32_t tile_x = 0; tile_x < 32; tile_x++)
        {
            uint16_t tile_address = name_table_base | (((tile_y + start_row) % 28) << 6) | ((tile_x + start_column) % 32 << 1);

            uint16_t tile = ((uint16_t)(vram [tile_address])) +
                            (((uint16_t)(vram [tile_address + 1])) << 8);

            /* If we are rendering the "priority" layer, skip any non-priority tiles */
            if (priority && !(tile & 0x1000))
                continue;

            bool h_flip = tile & VDP_PATTERN_HORIZONTAL_FLIP;
            bool v_flip = tile & VDP_PATTERN_VERTICAL_FLIP;

            Vdp_Pattern *pattern = (Vdp_Pattern *) &vram[(tile & 0x1ff) * sizeof(Vdp_Pattern)];

            Vdp_Palette palette = (tile & (1 << 11)) ? VDP_PALETTE_SPRITE : VDP_PALETTE_BACKGROUND;

            position.x = 8 * tile_x + fine_scroll_x;
            position.y = 8 * tile_y - fine_scroll_y;
            vdp_render_pattern (pattern, palette, position, h_flip, v_flip, false | priority);

        }
    }
}

/* TODO: Limit of eight sprites per line / set sprite-overflow flag */
/* TODO: Collision detection */
/* TODO: VDP Flag BIT_3 of ctrl_1 subtracts 8 from x position of sprites */
/* TODO: Pixel-doubling */
/* TODO: Draw order: the first entry in the line's sprite buffer with a non-transparent pixel is the one that sticks */
/* TODO: Actually render as-per the clock cycles. May need to double-buffer to prevent tearing */
void vdp_render_sprites (void)
{
    uint16_t sprite_attribute_table_base = (((uint16_t) vdp_regs.sprite_attr_table_addr) << 7) & 0x3f00;
    uint16_t sprite_pattern_offset = (vdp_regs.sprite_pattern_generator_addr & 0x04) ? 256 : 0;
    Vdp_Pattern *pattern;
    Point2D position;

    for (int i = 0; i < 64; i++)
    {
        uint8_t y = vram [sprite_attribute_table_base + i];
        uint8_t x = vram [sprite_attribute_table_base + 0x80 + i * 2];
        uint8_t pattern_index = vram [sprite_attribute_table_base + 0x80 + i * 2 + 1];

        /* Break if there are no more sprites */
        if (y == 0xd0)
            break;

        position.x = x;
        position.y = y + 1;

        if (vdp_regs.mode_ctrl_2 & VDP_MODE_CTRL_2_SPRITE_TALL)
            pattern_index &= 0xfe;

        pattern = (Vdp_Pattern *) &vram [(sprite_pattern_offset + pattern_index) * sizeof (Vdp_Pattern)];
        vdp_render_pattern (pattern, VDP_PALETTE_SPRITE, position, false, false, true);

        if (vdp_regs.mode_ctrl_2 & VDP_MODE_CTRL_2_SPRITE_TALL)
        {
            position.y += 8;
            pattern = (Vdp_Pattern *) &vram [(sprite_pattern_offset + pattern_index + 1) * sizeof (Vdp_Pattern)];
            vdp_render_pattern (pattern, VDP_PALETTE_SPRITE, position, false, false, true);
        }
    }
}

/* Note: For now, we will assume 192 lines, as this is what the vast majority of
 *       SMS games actually use */
void vdp_render (void)
{
    /* Check if the display is blanked */
    if (!(vdp_regs.mode_ctrl_2 & VDP_MODE_CTRL_2_BLANK))
    {
        snepulator.video_background [0] = 0.0f;
        snepulator.video_background [1] = 0.0f;
        snepulator.video_background [2] = 0.0f;
        for (int y = 0; y < 192; y++)
            for (int x = 0; x < 256; x++)
            {
                    snepulator.sms_vdp_texture_data [x * VDP_STRIDE_X + y * VDP_STRIDE_Y + 0] = 0.0f;
                    snepulator.sms_vdp_texture_data [x * VDP_STRIDE_X + y * VDP_STRIDE_Y + 1] = 0.0f;
                    snepulator.sms_vdp_texture_data [x * VDP_STRIDE_X + y * VDP_STRIDE_Y + 2] = 0.0f;
            }
        return;
    }

    /* Background / overscan - Is this specific to mode 4? */
    uint8_t bg_colour;
    bg_colour = cram [16 + (vdp_regs.background_colour & 0x0f)];
    snepulator.video_background [0] = VDP_TO_RED (bg_colour) / 256.0f;
    snepulator.video_background [1] = VDP_TO_GREEN (bg_colour) / 256.0f;
    snepulator.video_background [2] = VDP_TO_BLUE (bg_colour) / 256.0f;

    switch (vdp_regs.mode_ctrl_1 & VDP_MODE_CTRL_1_MODE_4)
    {
        /* Mode 4 */
        case VDP_MODE_CTRL_1_MODE_4:
            for (int y = 0; y < 192; y++)
                for (int x = 0; x < 256; x++)
                {
                        snepulator.sms_vdp_texture_data [x * VDP_STRIDE_X + y * VDP_STRIDE_Y + 0] = snepulator.video_background[0];
                        snepulator.sms_vdp_texture_data [x * VDP_STRIDE_X + y * VDP_STRIDE_Y + 1] = snepulator.video_background[1];
                        snepulator.sms_vdp_texture_data [x * VDP_STRIDE_X + y * VDP_STRIDE_Y + 2] = snepulator.video_background[2];
                }
            vdp_render_background (false);
            vdp_render_sprites ();
            vdp_render_background (true);
            break;

        /* TMS9918 mode */
        default:
            /* fprintf (stderr, "TMS9918 modes not implemented.\n"); */
            break;
    }
}
