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

/* Structs */
typedef struct Vdp_Regs_t {
    uint8_t mode_ctrl_1;
    uint8_t mode_ctrl_2;
    uint8_t name_table_addr;
    uint8_t colour_table_addr; /* Unused? */
    uint8_t background_pattern_addr; /* Unused? */
    uint8_t sprite_attr_table_addr;
    uint8_t sprite_pattern_table_addr;
    uint8_t background_colour;
    uint8_t background_x_scroll;
    uint8_t background_y_scroll;
    uint8_t line_counter;
    uint8_t code;
    uint16_t address;
} Vdp_Regs;

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
    fprintf (stdout, "background_colour: %02x.\n", vdp_regs.background_colour);

    /* VRAM */

    /* CRAM */
    fprintf (stdout, "CRAM:\n");
    for (int i = 0; i < sizeof(cram); i++)
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
                                if (value & 0x0f <= 10)
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

void vdp_render (void)
{
    uint8_t c;
    /* Background / overscan */
    c = cram [16 + (vdp_regs.background_colour & 0x0f)];
    SDL_SetRenderDrawColor (renderer, VDP_TO_RED (c), VDP_TO_GREEN (c), VDP_TO_BLUE (c), 255);
    SDL_RenderClear (renderer);
}
