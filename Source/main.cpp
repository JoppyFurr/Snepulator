#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include "SDL2/SDL.h"

extern "C" {
#include "gpu/sega_vdp.h"
#include "cpu/z80.h"
}

/* Global state */
SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;

/* This should also be moved somewhere Master System specific */
uint8_t *bios = NULL;
uint8_t *cart = NULL;
static uint32_t bios_size = 0;
static uint32_t cart_size = 0;

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

#define SMS_MEMORY_CTRL_BIOS_DISABLE 0x08
#define SMS_MEMORY_CTRL_CART_DISABLE 0x40
#define SMS_MEMORY_CTRL_IO_DISABLE   0x04

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
        /* fprintf (stderr, "Error: Sega Memory Mapper register 0xfffc not implemented.\n"); */
    }
    else if (addr == 0xfffd)
    {
        /* fprintf (stdout, "[DEBUG]: MAPPER[0] set to %02x.\n", data & 0x07); */
        mapper_bank[0] = data & 0x1f;
    }
    else if (addr == 0xfffe)
    {
        /* fprintf (stdout, "[DEBUG]: MAPPER[1] set to %02x.\n", data & 0x07); */
        mapper_bank[1] = data & 0x1f;
    }
    else if (addr == 0xffff)
    {
        /* fprintf (stdout, "[DEBUG]: MAPPER[2] set to %02x.\n", data & 0x07); */
        mapper_bank[2] = data & 0x1f;
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

/* TODO: Currently assuming a power-of-two size for the ROM */

static uint8_t sms_memory_read (uint16_t addr)
{
    /* Cartridge, card, BIOS, expansion slot */
    if (addr >= 0x0000 && addr <= 0xbfff)
    {
        uint32_t bank_base = mapper_bank[(addr >> 14)] * ((uint32_t)16 << 10);
        uint16_t offset    = addr & 0x3fff;

        if (bios && !(memory_control & SMS_MEMORY_CTRL_BIOS_DISABLE))
            return bios[(bank_base + offset) & (bios_size - 1)];

        if (cart && !(memory_control & SMS_MEMORY_CTRL_CART_DISABLE))
            return cart[(bank_base + offset) & (cart_size - 1)];
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

    /* Minimal SDSC Debug Console */
    if (addr == 0xfd && (memory_control & 0x04))
    {
        fprintf (stdout, "%c", data);
        fflush (stdout);
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
            return vdp_get_v_counter ();
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

    /* A pressed button returns zero */
    else if (addr >= 0xc0 && addr <= 0xff)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* I/O Port A/B */
            return 0xff;
        }
        else
        {
            /* I/O Port B/misc */
            return 0xff;
        }
    }

    /* DEFAULT */
    return 0xff;
}

int32_t sms_load_rom (uint8_t **buffer, uint32_t *filesize, char *filename)
{
    uint32_t bytes_read = 0;

    /* Open ROM file */
    FILE *rom_file = fopen (filename, "rb");
    if (!rom_file)
    {
        perror ("Error: Unable to open ROM");
        return -1;
    }

    /* Get ROM size */
    fseek(rom_file, 0, SEEK_END);
    *filesize = ftell(rom_file);
    fseek(rom_file, 0, SEEK_SET);

    /* Allocate memory */
    *buffer = (uint8_t *) malloc (*filesize);
    if (!*buffer)
    {
        perror ("Error: Unable to allocate memory for ROM.\n");
        return -1;
    }

    /* Copy to memory */
    while (bytes_read < *filesize)
    {
        bytes_read += fread (*buffer + bytes_read, 1, *filesize - bytes_read, rom_file);
    }

    fclose (rom_file);

    return EXIT_SUCCESS;
}

bool _abort_ = false;

/* TODO: Move these somewhere SMS-specific */
extern const char *z80_instruction_name[256];
extern const char *z80_instruction_name_extended[256];
extern const char *z80_instruction_name_bits[256];
extern const char *z80_instruction_name_ix[256];
extern void vdp_clock_update (uint64_t cycles);
extern bool vdp_get_interrupt (void);
extern uint8_t instructions_before_interrupts;
extern uint64_t z80_cycle;
#define SMS_CLOCK_RATE_PAL  3546895
#define SMS_CLOCK_RATE_NTSC 3579545

int main (int argc, char **argv)
{
    char *bios_filename = NULL;
    char *cart_filename = NULL;

    printf ("Snepulator.\n");
    printf ("Built on " BUILD_DATE ".\n");

    /* Parse all CLI arguments */
    while (*(++argv))
    {
        if (!strcmp ("-b", *argv))
        {
            /* BIOS to load */
            bios_filename = *(++argv);
        }
        else if (!strcmp ("-r", *argv))
        {
            /* ROM to load */
            cart_filename = *(++argv);
        }
        else
        {
            /* Display usage */
            fprintf (stdout, "Usage: Snepulator [-b bios.sms] [-r rom.sms]\n");
            return EXIT_FAILURE;
        }
    }

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

    /* Load BIOS */
    if (sms_load_rom (&bios, &bios_size, bios_filename) == -1)
    {
        _abort_ = true;
    }
    fprintf (stdout, "%d KiB BIOS %s loaded.\n", bios_size >> 10, bios_filename);

    /* Load cart */
    if (cart_filename)
    {
        if (sms_load_rom (&cart, &cart_size, cart_filename) == -1)
        {
            _abort_ = true;
        }
        fprintf (stdout, "%d KiB cart %s loaded.\n", cart_size >> 10, cart_filename);
    }

    z80_init (sms_memory_read, sms_memory_write, sms_io_read, sms_io_write);
    vdp_init ();

    /* Master System loop */
    uint64_t next_frame_cycle = 0;
    uint64_t frame_number = 0;
    while (!_abort_)
    {
        /* TIMING DEBUG */
        uint64_t previous_cycle_count = z80_cycle;
        uint8_t debug_instruction_0 = sms_memory_read (z80_regs.pc + 0);
        uint8_t debug_instruction_1 = sms_memory_read (z80_regs.pc + 1);
        uint8_t debug_instruction_2 = sms_memory_read (z80_regs.pc + 2);
        uint8_t debug_instruction_3 = sms_memory_read (z80_regs.pc + 3);
        z80_instruction ();
        if (z80_cycle == previous_cycle_count)
        {
            fprintf (stderr, "Instruction %x %x %x %x took no time\n",
                     debug_instruction_0,
                     debug_instruction_1,
                     debug_instruction_2,
                     debug_instruction_3);

            if (debug_instruction_0 == 0xcb)
                fprintf (stderr, "DECODE %s %s %x %x took no time\n",
                         z80_instruction_name[debug_instruction_0],
                         z80_instruction_name_bits[debug_instruction_1],
                         debug_instruction_2,
                         debug_instruction_3);
            else if (debug_instruction_0 == 0xed)
                fprintf (stderr, "DECODE %s %s %x %x took no time\n",
                         z80_instruction_name[debug_instruction_0],
                         z80_instruction_name_extended[debug_instruction_1],
                         debug_instruction_2,
                         debug_instruction_3);
            else if ((debug_instruction_0 == 0xdd || debug_instruction_0 == 0xfd) && debug_instruction_1 == 0xcb)
                fprintf (stderr, "DECODE %s %s %s took no time\n",
                         z80_instruction_name[debug_instruction_0],
                         z80_instruction_name_ix[debug_instruction_1],
                         z80_instruction_name_bits[debug_instruction_2]);
            else if (debug_instruction_0 == 0xdd || debug_instruction_0 == 0xfd)
                    fprintf (stderr, "DECODE %s %s %x %x took no time\n",
                             z80_instruction_name[debug_instruction_0],
                             z80_instruction_name_ix[debug_instruction_1],
                             debug_instruction_2,
                             debug_instruction_3);
            else
                fprintf (stderr, "DECODE %s %x %x %x took no time\n",
                         z80_instruction_name[debug_instruction_0],
                         debug_instruction_1,
                         debug_instruction_2,
                         debug_instruction_3);
            _abort_ = true;
        }
        /* END TIMING DEBUG */

        /* Time has passed, update the VDP state */
        vdp_clock_update (z80_cycle);

        /* Check for interrupts */
        if (instructions_before_interrupts)
            instructions_before_interrupts--;

        /* TODO: Interrupt handling should live in the z80 file */
        if (!instructions_before_interrupts && z80_regs.iff1 && vdp_get_interrupt ())
        {
            z80_regs.iff1 = false;
            z80_regs.iff2 = false;

            switch (z80_regs.im)
            {
                /* TODO: Cycle count? */
                case 1:
                    /* RST 0x38 */
#if 0
                    printf ("INTERRUPT: RST 0x38  <--\n");
#endif
                    sms_memory_write (--z80_regs.sp, z80_regs.pc_h);
                    sms_memory_write (--z80_regs.sp, z80_regs.pc_l);
                    z80_regs.pc = 0x38;
                    break;
                default:
                    fprintf (stderr, "Unknown interrupt mode %d.\n", z80_regs.im);
                    _abort_ = true;
            }
        }

        /* TODO: Rather than SMS cycles, we should update the display based on host VSYNC */
        if (z80_cycle >= next_frame_cycle)
        {
            /* Check input */
            SDL_Event event;

            while (SDL_PollEvent (&event))
            {
                if (event.type == SDL_QUIT)
                {
                    _abort_ = true;
                }
            }

            frame_number++;

            vdp_render ();
            SDL_RenderPresent (renderer);
            SDL_Delay (10);

            if ((frame_number % 50) == 0)
            {
                printf ("-- %02" PRId64 " seconds have passed --\n", frame_number / 50);
            }

            next_frame_cycle = z80_cycle + (SMS_CLOCK_RATE_PAL / 50);
        }

        if (_abort_)
        {
            fprintf (stderr, "[DEBUG]: _abort_ set. Terminating emulation.\n");
            return EXIT_FAILURE;
        }
    }

    fprintf (stdout, "EMULATION ENDED.\n");

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
