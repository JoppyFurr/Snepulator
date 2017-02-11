#include <stdio.h>
#include <stdlib.h>
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

void vdp_init (void)
{
    /* TODO: Are there any nonzero default values? */
    memset (&vdp_regs, 0, sizeof(vdp_regs));
    memset (vram,      0, sizeof(vram));
    memset (cram,      0, sizeof(cram));
}

void vdp_dump (void)
{
    /* Registers */
    fprintf (stdout, "Registers:\n");
    fprintf (stdout, "mode_ctrl_1: %02x.\n", vdp_regs.mode_ctrl_1);
    fprintf (stdout, "mode_ctrl_2: %02x.\n", vdp_regs.mode_ctrl_2);
    fprintf (stdout, "name_table_addr: %02x.\n", vdp_regs.name_table_addr);
    fprintf (stdout, "background_colour: %02x.\n", vdp_regs.background_colour);

    /* VRAM */

    /* CRAM */
    fprintf (stdout, "CRAM:\n");
    for (uint8_t i = 0; i < sizeof(cram); i++)
    {
        fprintf (stdout, "%02x", cram[i]);
        if ((i & 0x07) == 0x07)
        {
            fprintf (stdout, "\n");
        }
        else
        {
            fprintf (stdout, " ");
        }
    }
    fprintf (stdout, "\n");
}

uint32_t vdp_access (uint8_t value, Vdp_Port port, Vdp_Operation operation)
{
    static bool first_byte_received = false;

    switch (operation)
    {
        case VDP_OPERATION_READ:
            switch (port)
            {
                case VDP_PORT_V_COUNTER:
                    /* TODO */
                    break;
                case VDP_PORT_H_COUNTER:
                    /* TODO */
                    break;
                case VDP_PORT_DATA:
                    /* TODO */
                    first_byte_received = false;
                    break;
                case VDP_PORT_CONTROL:
                    /* TODO */
                    first_byte_received = false;
                    break;
            }
            break;

        case VDP_OPERATION_WRITE:
            switch (port)
            {
                case VDP_PORT_DATA:
                        first_byte_received = false;
                        switch (vdp_regs.code)
                        {
                            case VDP_CODE_VRAM_READ:
                                /* TODO */
                                break;
                            case VDP_CODE_VRAM_WRITE:
                                vram[vdp_regs.address] = value;
                                break;
                            case VDP_CODE_REG_WRITE:
                                /* TODO */
                                break;
                            case VDP_CODE_CRAM_WRITE:
                                cram[vdp_regs.address & 0x1f] = value;
                                break;
                            default:
                                break;
                        }
                        vdp_regs.address = (vdp_regs.address + 1) & 0x3fff;
                    break;
                case VDP_PORT_CONTROL:
                    if (!first_byte_received)
                    {
                        first_byte_received = true;
                        vdp_regs.address = (vdp_regs.address & 0xff00) | (uint16_t) value << 0;
                    }
                    else
                    {
                        first_byte_received = false;
                        vdp_regs.address = (vdp_regs.address & 0x00ff) | (uint16_t) value << 8;
                        vdp_regs.code = value >> 6;

                        switch (vdp_regs.code)
                        {
                            case VDP_CODE_VRAM_READ:
                                /* TODO */
                                break;
                            case VDP_CODE_REG_WRITE:
                                if ((value & 0x0f) <= 10)
                                    ((uint8_t *) &vdp_regs) [value & 0x0f] = vdp_regs.address & 0x00ff;
                                break;
                            default:
                                break;
                        }
                    }
                    break;
            }
            break;
    }

    return 0;
}

void vdp_render_pattern (Vdp_Pattern *pattern, Vdp_Palette palette, uint32_t x, uint32_t y)
{
    for (uint32_t pattern_x = 0; pattern_x < 8; pattern_x++)
    {
        for (uint32_t pattern_y = 0; pattern_y < 8; pattern_y++)
        {
            uint8_t pixel = (cram + (palette == VDP_PALETTE_SPRITE ? 16 : 0))
                            [ pattern->data[pattern_y * 4 + 0 ] & (8 >> pattern_x) ? (1 << 0) : 0 |
                              pattern->data[pattern_y * 4 + 1 ] & (8 >> pattern_x) ? (2 << 0) : 0 |
                              pattern->data[pattern_y * 4 + 2 ] & (8 >> pattern_x) ? (3 << 0) : 0 |
                              pattern->data[pattern_y * 4 + 3 ] & (8 >> pattern_x) ? (4 << 0) : 0 ];

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
                uint16_t *name_table_base = (uint16_t *)
                                                (vram + (((vdp_regs.name_table_addr & (1 << 3)) ? (1 << 13) : 0) |
                                                         ((vdp_regs.name_table_addr & (1 << 2)) ? (1 << 12) : 0) |
                                                         ((vdp_regs.name_table_addr & (1 << 1)) ? (1 << 11) : 0)));
                for (uint32_t tile_x = 0; tile_x < 32; tile_x++)
                {
                    for (uint32_t tile_y = 0; tile_y < 28; tile_y++)
                    {
                        if (tile_y >= 24) /* TODO: Do this properly once scrolling is implemented */
                            continue;

                        uint16_t tile = name_table_base[tile_x + 32 * tile_y];
                        Vdp_Pattern pattern = ((Vdp_Pattern *)vram)[tile * 0x01ff];
                        Vdp_Palette palette = tile & (1 << 11) ? VDP_PALETTE_SPRITE : VDP_PALETTE_BACKGROUND;
                        /* TODO: Flip flags */
                        /* TODO: Priority flag */
                        vdp_render_pattern (&pattern, palette, VDP_OVERSCAN_X + 8 * tile_x,
                                                               VDP_OVERSCAN_Y + 8 * tile_y);

                    }
                }
            }
            break;

        /* TMS9918 mode */
        default:
            fprintf (stderr, "TMS9918 modes not implemented.\n");
            break;
    }
}
