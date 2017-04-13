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
extern SDL_Renderer *renderer;

/* VDP State */
static Vdp_Regs vdp_regs;
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
    first_byte_received = false;

    fprintf (stdout,"[DEBUG(vdp)]: vdp_status_read () not implemented.\n");
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

            SDL_SetRenderDrawColor (renderer, VDP_TO_RED (pixel),
                                              VDP_TO_GREEN (pixel),
                                              VDP_TO_BLUE (pixel), 255);

            SDL_RenderDrawPoint (renderer, x + pattern_x, y + pattern_y);
        }
    }
}

void vdp_render (void)
{
    uint8_t pixel;
    /* Background / overscan - Is this specific to mode 4? */
    pixel = cram [16 + (vdp_regs.background_colour & 0x0f)];
    SDL_SetRenderDrawColor (renderer, VDP_TO_RED (pixel), VDP_TO_GREEN (pixel), VDP_TO_BLUE (pixel), 255);
    SDL_RenderClear (renderer);

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
                        vdp_render_pattern (pattern, palette, VDP_OVERSCAN_X + 8 * tile_x,
                                                               VDP_OVERSCAN_Y + 8 * tile_y);

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
