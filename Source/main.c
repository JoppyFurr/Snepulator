#include <stdio.h>
#include <stdlib.h>

#include "SDL2/SDL.h"

#include "gpu/sega_vdp.h"

/* Global state */
SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;

/* TODO: Eventually move Master System code to its own file */
static void sms_memory_write (uint16_t addr, uint8_t data)
{
    /* No early breaks - Register writes also affect RAM */

    /* 3D glasses */
    if (addr >= 0xfff8 && addr <= 0xfffb)
    {
    }

    /* Mapping (Sega) */
    if (addr >= 0xfffc && addr <= 0xffff)
    {
    }

    /* Mapping (CodeMasters) */
    if (addr == 0x8000)
    {
    }

    /* Cartridge, card, BIOS, expansion slot */
    if (addr >= 0x0000 && addr <= 0xbfff)
    {
    }

    /* RAM + mirror */
    if (addr >= 0xc000 && addr <= 0xffff)
    {
    }
}

static uint8_t sms_memory_read (uint16_t addr)
{
    /* Cartridge, card, BIOS, expansion slot */
    if (addr >= 0x0000 && addr <= 0xbfff)
    {
    }

    /* 8 KiB RAM + mirror */
    if (addr >= 0xc000 && addr <= 0xffff)
    {
    }
}

static void sms_io_write (uint16_t addr, uint8_t data)
{
    addr &= 0x00ff; /* The upper 8 bits are ignored */

    if (addr >= 0x00 && addr <= 0x3f)
    {
        if (addr & 0x01 == 0x00)
        {
            /* Memory Control Register */
        }
        else
        {
            /* I/O Control Register */
        }

    }

    /* PSG */
    else if (addr >= 0x40 && addr <= 0x7f)
    {
        /* Not implemented */
    }


    /* VDP */
    else if (addr >= 0x80 && addr <= 0xbf)
    {
        if (addr & 0x01 == 0x00)
        {
            /* VDP Data Register */
            vdp_access (data, VDP_PORT_DATA, VDP_OPERATION_WRITE);
        }
        else
        {
            /* VDP Control Register */
            vdp_access (data, VDP_PORT_CONTROL, VDP_OPERATION_WRITE);
        }
    }
}

static uint8_t sms_io_read (uint16_t addr)
{
    addr &= 0x00ff; /* The upper 8 bits are ignored */

    if (addr >= 0x00 && addr <= 0x3f)
    {
        /* Returns the last byte of the instruction that read the port */
    }

    else if (addr >= 0x40 && addr <= 0x7f)
    {
        if (addr & 0x01 == 0x00)
        {
            /* V Counter */
        }
        else
        {
            /* H Counter */
        }
    }


    /* VDP */
    else if (addr >= 0x80 && addr <= 0xbf)
    {
        if (addr & 0x01 == 0x00)
        {
            /* VDP Data Register */
        }
        else
        {
            /* VDP Status Flags */
        }
    }

    else if (addr >= 0xc0 && addr <= 0xff)
    {
        if (addr & 0x01 == 0x00)
        {
            /* I/O Port A/B */
        }
        else
        {
            /* I/O Port B/misc */
        }
    }
}

int main (int argc, char **argv)
{
    printf ("Snepulator.\n");
    printf ("Built on " BUILD_DATE ".\n");

    /* Initialize SDL */
    if (SDL_Init (SDL_INIT_EVERYTHING) == -1)
    {
        fprintf (stderr, "Error: SDL_Init failed.\n");
        return EXIT_FAILURE;
    }

    /* Create a window */
    /* For now, lets assume Master System resolution.
     * 256 × 192, with 16 pixels for left/right border, and 32 pixels for top/bottom border */
    SDL_CreateWindowAndRenderer (256 + VDP_OVERSCAN_X * 2, 192 + VDP_OVERSCAN_Y * 2, 0, &window, &renderer);
    if (window == NULL || renderer == NULL)
    {
        fprintf (stderr, "Error: SDL_CreateWindowAndRenderer failed.\n");
        SDL_Quit ();
        return EXIT_FAILURE;
    }

    /* Blank the screen */
    SDL_SetRenderDrawColor (renderer, 0, 0, 0, 255);
    SDL_RenderClear (renderer);
    SDL_RenderPresent (renderer);

    vdp_init ();

    /* TEST */

        /* Mode 4 */
        vdp_access (0x04, VDP_PORT_CONTROL, VDP_OPERATION_WRITE); /* data = Mode 4 */
        vdp_access (0x80, VDP_PORT_CONTROL, VDP_OPERATION_WRITE); /* mode_ctrl_1 */

        /* Dark green for background */
        vdp_access (0x10, VDP_PORT_CONTROL, VDP_OPERATION_WRITE); /* Address = 0x10 */
        vdp_access (0xc0, VDP_PORT_CONTROL, VDP_OPERATION_WRITE); /* Target = cram */
        vdp_access (0x04, VDP_PORT_DATA,    VDP_OPERATION_WRITE); /* Dark green */

        /* Select the background to be sprite colour 0 */
        vdp_access (0x00, VDP_PORT_CONTROL, VDP_OPERATION_WRITE); /* data = 0x00 */
        vdp_access (0x87, VDP_PORT_CONTROL, VDP_OPERATION_WRITE); /* Write to background register */

    vdp_dump ();


    /* Loop until the window is closed */
    for (;;)
    {
        SDL_Event event;

        while (SDL_PollEvent (&event))
        {
            if (event.type == SDL_QUIT)
            {
                goto snepulator_close;
            }
        }

        vdp_render ();
        SDL_RenderPresent (renderer);

        SDL_Delay (16);
    }

snepulator_close:

    SDL_DestroyRenderer (renderer);
    SDL_DestroyWindow (window);
    SDL_Quit ();

    return EXIT_SUCCESS;
}
