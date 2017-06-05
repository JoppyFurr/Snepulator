#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "SDL2/SDL.h"

#include "sega_vdp.h"

/* Constants */
#define VDP_CRAM_SIZE (32)
#define VDP_VRAM_SIZE (16 << 10)

/* Macros */
#define VDP_TO_RED(C)   ((0xff / 3) * ((C & 0x03) >> 0))
#define VDP_TO_GREEN(C) ((0xff / 3) * ((C & 0x0c) >> 2))
#define VDP_TO_BLUE(C)  ((0xff / 3) * ((C & 0x30) >> 4))

/* Externs */
extern SDL_Window *window;

/* VDP State */
static Vdp_Regs vdp_regs;
static uint8_t v_counter = 0;
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

void vdp_dump (void)
{
#if 0
    /* Registers */
    fprintf (stdout, "Registers:\n");
    fprintf (stdout, "mode_ctrl_1:     %02x.\n", vdp_regs.mode_ctrl_1);
    fprintf (stdout, "mode_ctrl_2:     %02x.\n", vdp_regs.mode_ctrl_2);
    fprintf (stdout, "name_table_addr: %02x.\n", vdp_regs.name_table_addr);
    fprintf (stdout, "bg_colour:       %02x.\n", vdp_regs.background_colour);
    fprintf (stdout, "\n");
#endif

#if 0
    /* CRAM */
    fprintf (stdout, "CRAM:\n");
    for (uint16_t i = 0; i < sizeof(cram); i++)
    {
        if ((i & 0x0f) == 0x00)
        {
            fprintf (stdout, "0x%04x:", i);
        }

        fprintf (stdout, " %02x", cram[i]);

        if ((i & 0x0f) == 0x0f)
        {
            fprintf (stdout, "\n");
        }
    }
    fprintf (stdout, "\n");
#endif

#if 0
    /* VRAM */
    fprintf (stdout, "VRAM:\n");
    for (uint16_t i = 0; i < sizeof(vram); i++)
    {
        if ((i & 0x0f) == 0x00)
        {
            if (memcmp (&vram[i], "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16) == 0)
            {
                i += 15;
                continue;
            }
            fprintf (stdout, "0x%04x: ", i);
        }

        fprintf (stdout, "%02x", vram[i]);

        if ((i & 0x0f) == 0x0f)
        {
            fprintf (stdout, "\n");
        }
        else
        {
            fprintf (stdout, " ");
        }
    }
    fprintf (stdout, "\n");
#endif

#if 0
    /* Name Table */
    fprintf (stdout, "Name Table:\n");
    uint16_t name_table_base = (((uint16_t) vdp_regs.name_table_addr) << 10) & 0x3800;


    for (uint32_t tile_y = 0; tile_y < 28; tile_y++)
    {
        fprintf (stdout, "Row %02d:", tile_y);
        for (uint32_t tile_x = 0; tile_x < 32; tile_x++)
        {
            uint16_t tile_address = name_table_base | (tile_y << 6) | (tile_x << 1);

            uint16_t tile = (uint16_t)(vram [tile_address]) +
                            (((uint16_t)(vram [tile_address + 1])) << 8);

            fprintf (stdout, " %04x", tile);

        }
        fprintf (stdout, "\n");
    }
#endif

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
    vdp_regs.line_interrupt = false;

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

                    if ((value & 0x0f) == 0x01 && ((vdp_regs.address & 0x00ff) & (1 << 5)))
                    {
                        fprintf (stdout, "[DEBUG(vdp)]: Frame interrupts enabled.\n");
                    }
                    else if ((value & 0x0f) == 0x00 && ((vdp_regs.address & 0x00ff) & (1 << 4)))
                    {
                        fprintf (stdout, "[DEBUG(vdp)]: Line interrupts enabled.\n");
                    }
                }
                break;
            case VDP_CODE_CRAM_WRITE:
                break;
            default:
                break;
        }
    }
}

extern float sms_vdp_texture_data [256 * 256 * 3];
extern float sms_vdp_background [3];
#define VDP_STRIDE_X (3)
#define VDP_STRIDE_Y (256 * 3)

void vdp_render_pattern (Vdp_Pattern *pattern_base, Vdp_Palette palette, uint32_t x, uint32_t y)
{
    for (uint32_t pattern_y = 0; pattern_y < 8; pattern_y++)
    {
        char *line_base = (char *)(&pattern_base->data[pattern_y * 4]);
        for (uint32_t pattern_x = 0; pattern_x < 8; pattern_x++)
        {
            uint8_t bit0 = (line_base[0] & (0x80 >> pattern_x)) ? 0x01 : 0x00;
            uint8_t bit1 = (line_base[1] & (0x80 >> pattern_x)) ? 0x02 : 0x00;
            uint8_t bit2 = (line_base[2] & (0x80 >> pattern_x)) ? 0x04 : 0x00;
            uint8_t bit3 = (line_base[3] & (0x80 >> pattern_x)) ? 0x08 : 0x00;

            /* TODO: Support the sprite palette */
            uint8_t pixel = cram[((palette == VDP_PALETTE_SPRITE) ? 16 : 0) + (bit0 | bit1 | bit2 | bit3)];

            sms_vdp_texture_data [(x + pattern_x) * VDP_STRIDE_X + (y + pattern_y) * VDP_STRIDE_Y + 0] = VDP_TO_RED (pixel) / 256.0f;
            sms_vdp_texture_data [(x + pattern_x) * VDP_STRIDE_X + (y + pattern_y) * VDP_STRIDE_Y + 1] = VDP_TO_GREEN (pixel) / 256.0f;
            sms_vdp_texture_data [(x + pattern_x) * VDP_STRIDE_X + (y + pattern_y) * VDP_STRIDE_Y + 2] = VDP_TO_BLUE (pixel) / 256.0f;
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

/* TODO: Consider skipped values */
void vdp_clock_update (uint64_t cycles)
{
    uint16_t v_counter_16 = (cycles % SMS_CLOCK_PER_FRAME_PAL) / 227; /* Dividing by 227 roughly maps from 0 -> 312,
                                                                         one value per scanline */
    uint8_t previous_v_counter = v_counter;

    if (v_counter_16 <= 0xf2)
        v_counter = v_counter_16;
    else
        v_counter = v_counter_16 - 0x39;

    /* Line interrupt */
    if (v_counter > 192)
        vdp_regs.line_interrupt_counter = vdp_regs.line_counter;
    else if (v_counter != previous_v_counter)
    {
        vdp_regs.line_interrupt_counter--;
        if (vdp_regs.line_interrupt_counter == 0xff)
            vdp_regs.line_interrupt = true;
    }

    /* Frame interrupt */
    static int frame = 0;
    if (v_counter != previous_v_counter)
    {
        if (v_counter_16 == 0xc1) /* TODO: This constant is only for 192-line mode */
        {
            vdp_regs.status |= VDP_STATUS_INT;
            frame++;
        }
    }

}

uint8_t vdp_get_v_counter (void)
{
    return v_counter;
}

/* TODO */
bool vdp_get_interrupt (void)
{
    /* Frame interrupt */
    if ((vdp_regs.mode_ctrl_2 & (1 << 5)) && (vdp_regs.status & VDP_STATUS_INT))
    {
        printf ("[DEBUG]: Frame interrupt sent.\n");
        return true;
    }

    /* Line interrupt */
    if ((vdp_regs.mode_ctrl_1 & (1 << 4)) && vdp_regs.line_interrupt)
    {
        printf ("[DEBUG]: Line interrupt sent.\n");
        return true;
    }

    return false;
}

void vdp_render (void)
{
    /* Background / overscan - Is this specific to mode 4? */
    uint8_t bg_colour;
    bg_colour = cram [16 + (vdp_regs.background_colour & 0x0f)];
    sms_vdp_background [0] = VDP_TO_RED (bg_colour) / 256.0f;
    sms_vdp_background [1] = VDP_TO_GREEN (bg_colour) / 256.0f;
    sms_vdp_background [2] = VDP_TO_BLUE (bg_colour) / 256.0f;

    switch (vdp_regs.mode_ctrl_1 & VDP_MODE_CTRL_1_MODE_4)
    {
        /* Mode 4 */
        case VDP_MODE_CTRL_1_MODE_4:
            /* Draw order:
             *  Non-priority background
             *    Patterns are used for both sprites and the background. Each pattern is 8x8 pixels.
             *    There is enough VRAM for 512 patterns, but some is used for tables. Each pattern
             *    uses 32 bytes. Four bytes per row. Each byte being one bit for each pixel.
             *    While not documented, I assume the patterns just start at zero?
             *    The name table  is a 2D matrix of 16-bit words selecting a pattern and palette from VRAM.
             *  sprites
             *  priority background */

            /* Background */
            /* By default the background is made out of 32×28 tiles */
            /* For other resolutions this can be 32×32 tiles */

            /* For other resolutions we use only two bits */
            {
                uint16_t name_table_base = (((uint16_t) vdp_regs.name_table_addr) << 10) & 0x3800;

                for (uint32_t tile_y = 0; tile_y < 28; tile_y++)
                {
                    if (tile_y >= 24) /* TODO: Do this properly once scrolling is implemented */
                        continue;

                    for (uint32_t tile_x = 0; tile_x < 32; tile_x++)
                    {

                        uint16_t tile_address = name_table_base | (tile_y << 6) | (tile_x << 1);

                        uint16_t tile = ((uint16_t)(vram [tile_address])) +
                                        (((uint16_t)(vram [tile_address + 1])) << 8);

                        Vdp_Pattern *pattern = (Vdp_Pattern *) &vram[(tile & 0x1ff) * sizeof(Vdp_Pattern)];

                        Vdp_Palette palette = (tile & (1 << 11)) ? VDP_PALETTE_SPRITE : VDP_PALETTE_BACKGROUND;

                        /* TODO: Flip flags */
                        /* TODO: Priority flag */
                        vdp_render_pattern (pattern, palette, 8 * tile_x, 8 * tile_y);

                    }
                }
            }
            break;

        /* TMS9918 mode */
        default:
            /* fprintf (stderr, "TMS9918 modes not implemented.\n"); */
            break;
    }
}
