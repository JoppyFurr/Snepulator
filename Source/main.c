#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "SDL2/SDL.h"

#include "gpu/sega_vdp.h"
#include "cpu/z80.h"

/* Global state */
SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;

/* This should also be moved somewhere Master System specific */
uint8_t bios[128 << 10];
uint8_t ram[8 << 10];
uint8_t memory_control = 0x00;
uint8_t io_control = 0x00;

/* Sega Mapper */
uint8_t mapper_bank[3] = { 0x00, 0x01, 0x02 };

/* 0: Output
 * 1: Input */
#define SMS_IO_TR_A_DIRECTION (1 << 0)
#define SMS_IO_TH_A_DIRECTION (1 << 1)
#define SMS_IO_TR_B_DIRECTION (1 << 2)
#define SMS_IO_TH_B_DIRECTION (1 << 3)
/* 0: Low
 * 1: High */
#define SMS_IO_TR_A_LEVEL (1 << 4)
#define SMS_IO_TH_A_LEVEL (1 << 5)
#define SMS_IO_TR_B_LEVEL (1 << 6)
#define SMS_IO_TH_B_LEVEL (1 << 7)

/* TODO: Eventually move Master System code to its own file */
static void sms_memory_write (uint16_t addr, uint8_t data)
{
    /* No early breaks - Register writes also affect RAM */

    /* 3D glasses */
    if (addr >= 0xfff8 && addr <= 0xfffb)
    {
    }

    /* Sega Mapper */
    if (addr == 0xfffc)
    {
        fprintf (stderr, "Error: Sega Memory Mapper register 0xfffc not implemented.\n");
    }
    else if (addr == 0xfffd)
    {
        fprintf (stdout, "[DEBUG]: MAPPER[0] set to %02x.\n", data);
        mapper_bank[0] = data;
    }
    else if (addr == 0xfffe)
    {
        fprintf (stdout, "[DEBUG]: MAPPER[1] set to %02x.\n", data);
        mapper_bank[1] = data;
    }
    else if (addr == 0xffff)
    {
        fprintf (stdout, "[DEBUG]: MAPPER[2] set to %02x.\n", data);
        mapper_bank[2] = data;
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
        ram[(addr - 0xc000) & ((8 << 10) - 1)] = data;
    }
}

static uint8_t sms_memory_read (uint16_t addr)
{
    /* Cartridge, card, BIOS, expansion slot */
    if (addr >= 0x0000 && addr <= 0xbfff)
    {
        uint32_t bank_base = mapper_bank[(addr >> 14)] * ((uint32_t)16 << 10);
        uint16_t offset    = addr & 0x3fff;
        if ((memory_control & 0x08) == 0x00)
            return bios[bank_base + offset];
    }

    /* 8 KiB RAM + mirror */
    if (addr >= 0xc000 && addr <= 0xffff)
    {
        return ram[(addr - 0xc000) & ((8 << 10) - 1)];
    }

    return 0xff;
}

extern Z80_Regs z80_regs;

static void sms_io_write (uint8_t addr, uint8_t data)
{
    if (addr >= 0x00 && addr <= 0x3f)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* Memory Control Register */
            memory_control = data;
            fprintf (stderr, "[DEBUG(sms)]: Memory Control Register <- %02x:\n", memory_control);
            fprintf (stderr, "              -> PC is %04x.\n", z80_regs.pc);
            fprintf (stderr, "              -> Cartridge is %s.\n", (memory_control & 0x40) ? "DISABLED" : "ENABLED");
            fprintf (stderr, "              -> Work RAM is  %s.\n", (memory_control & 0x10) ? "DISABLED" : "ENABLED");
            fprintf (stderr, "              -> BIOS ROM is  %s.\n", (memory_control & 0x08) ? "DISABLED" : "ENABLED");
            fprintf (stderr, "              -> I/O Chip is  %s.\n", (memory_control & 0x04) ? "DISABLED" : "ENABLED");
        }
        else
        {
            /* I/O Control Register */
            io_control = data;
            fprintf (stderr, "[DEBUG(sms)] I/O control register not implemented.\n");
        }

    }

    /* PSG */
    else if (addr >= 0x40 && addr <= 0x7f)
    {
        /* Not implemented */
        fprintf (stderr, "Error: PSG not implemented.\n");
    }


    /* VDP */
    else if (addr >= 0x80 && addr <= 0xbf)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* VDP Data Register */
            vdp_data_write (data);
        }
        else
        {
            /* VDP Control Register */
            vdp_control_write (data);
        }
    }
}

static uint8_t sms_io_read (uint8_t addr)
{
    if ((memory_control & 0x04) && addr >= 0xC0 && addr <= 0xff)
    {
        /* SMS2/GG return 0xff */
        return 0xff;
    }

    if (addr >= 0x00 && addr <= 0x3f)
    {
        /* SMS2/GG return 0xff */
        return 0xff;
    }

    else if (addr >= 0x40 && addr <= 0x7f)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* V Counter */
            fprintf (stderr, "Error: V Counter not implemented.\n");
        }
        else
        {
            /* H Counter */
            fprintf (stderr, "Error: H Counter not implemented.\n");
        }
    }


    /* VDP */
    else if (addr >= 0x80 && addr <= 0xbf)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* VDP Data Register */
            return vdp_data_read ();
        }
        else
        {
            /* VDP Status Flags */
            return vdp_status_read ();
        }
    }

    else if (addr >= 0xc0 && addr <= 0xff)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* I/O Port A/B */
            fprintf (stderr, "Error: I/O Port A/B not implemented.\n");
        }
        else
        {
            /* I/O Port B/misc */
            fprintf (stderr, "Error: I/O Port B/misc not implemented.\n");
        }
    }
}

/* For now, load the Alex Kidd bios */
int32_t sms_load_bios ()
{
    FILE *bios_file = fopen ("../alex_kidd_bios.sms", "rb");
    uint32_t bytes_read = 0;
    if (!bios_file)
    {
        fprintf (stderr, "Error: Unable to load bios.\n");
        return EXIT_FAILURE;
    }

    while (bytes_read < (128 << 10))
    {
        bytes_read += fread (bios + bytes_read, 1, (128 << 10) - bytes_read, bios_file);
    }

    fclose (bios_file);

    fprintf (stdout, "BIOS loaded.\n");
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

    if(sms_load_bios () == EXIT_FAILURE)
    {
        goto snepulator_close;
    }

    z80_reset ();

    if (z80_run (sms_memory_read, sms_memory_write, sms_io_read, sms_io_write) == EXIT_FAILURE)
    {
        vdp_dump ();
        /* goto snepulator_close; */
    }

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

        SDL_Delay (1000);
        /* SDL_Delay (16); */
    }

snepulator_close:

    SDL_DestroyRenderer (renderer);
    SDL_DestroyWindow (window);
    SDL_Quit ();

    return EXIT_SUCCESS;
}
