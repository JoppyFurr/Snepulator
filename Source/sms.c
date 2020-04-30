#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "util.h"
#include "snepulator.h"
extern Snepulator snepulator;

#include "sms.h"
#include "cpu/z80.h"
extern Z80_Regs z80_regs;

#include "video/tms9918a.h"
#include "video/sega_vdp.h"
#include "sound/sn79489.h"

/* Console state */
uint8_t *bios = NULL;
uint8_t *cart = NULL;
static uint32_t bios_size = 0;
static uint32_t cart_size = 0;
SMS_Region region = REGION_WORLD;
SMS_Framerate framerate = FRAMERATE_NTSC;

uint8_t ram[8 << 10];
uint8_t memory_control = 0x00;
uint8_t io_control = 0x00;

/* Sega Mapper */
uint8_t mapper_bank[3] = { 0x00, 0x01, 0x02 };

/* Gamepads */
SMS_Gamepad gamepad_1;
SMS_Gamepad gamepad_2;
bool pause_button;

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


/*
 * Handle SMS memory reads.
 */
static uint8_t sms_memory_read (uint16_t addr)
{
    /* Cartridge, card, BIOS, expansion slot */
    if (addr >= 0x0000 && addr <= 0xbfff)
    {
        uint8_t  slot = (addr >> 14);
        uint32_t bank_base = mapper_bank[slot] * ((uint32_t) 16 << 10);
        uint16_t offset    = addr & 0x3fff;

        /* The first 1 KiB of slot 0 is not affected by mapping */
        if (slot == 0 && offset < (1 << 10))
            bank_base = 0;

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


/*
 * Handle SMS memory writes.
 */
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
        mapper_bank[0] = data & 0x3f;
    }
    else if (addr == 0xfffe)
    {
        mapper_bank[1] = data & 0x3f;
    }
    else if (addr == 0xffff)
    {
        mapper_bank[2] = data & 0x3f;
    }

    /* CodeMasters Mapper */
    /* TODO: There are differences from the Sega mapper. Do any games rely on them?
     *  1. Initial banks are different (0, 1, 0) instead of (0, 1, 2).
     *  2. The first 1KB is not protected. */
    if (addr == 0x0000)
    {
        mapper_bank[0] = data & 0x3f;
    }
    if (addr == 0x4000)
    {
        mapper_bank[1] = data & 0x3f;
    }
    else if (addr == 0x8000)
    {
        mapper_bank[2] = data & 0x3f;
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


/*
 * Handle SMS I/O reads.
 */
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
            return (gamepad_1.up        ? 0 : BIT_0) |
                   (gamepad_1.down      ? 0 : BIT_1) |
                   (gamepad_1.left      ? 0 : BIT_2) |
                   (gamepad_1.right     ? 0 : BIT_3) |
                   (gamepad_1.button_1  ? 0 : BIT_4) |
                   (gamepad_1.button_2  ? 0 : BIT_5) |
                   (gamepad_2.up        ? 0 : BIT_6) |
                   (gamepad_2.down      ? 0 : BIT_7);
        }
        else
        {
            bool port_1_th = false;
            bool port_2_th = false;

            if (region == REGION_WORLD)
            {
                if ((io_control & SMS_IO_TH_A_DIRECTION) == 0)
                    port_1_th = io_control & SMS_IO_TH_A_LEVEL;

                if ((io_control & SMS_IO_TH_B_DIRECTION) == 0)
                    port_2_th = io_control & SMS_IO_TH_B_LEVEL;
            }

            /* I/O Port B/misc */
            return (gamepad_2.left      ? 0 : BIT_0) |
                   (gamepad_2.right     ? 0 : BIT_1) |
                   (gamepad_2.button_1  ? 0 : BIT_2) |
                   (gamepad_2.button_2  ? 0 : BIT_3) |
                   (/* TODO: RESET */ 0 ? 0 : BIT_4) |
                   (port_1_th           ? BIT_6 : 0) |
                   (port_2_th           ? BIT_7 : 0);
        }
    }

    /* DEFAULT */
    return 0xff;
}


/*
 * Handle SMS I/O writes.
 */
static void sms_io_write (uint8_t addr, uint8_t data)
{
    if (addr >= 0x00 && addr <= 0x3f)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* Memory Control Register */
            memory_control = data;
        }
        else
        {
            /* I/O Control Register */
            io_control = data;
        }

    }

    /* PSG */
    else if (addr >= 0x40 && addr <= 0x7f)
    {
        sn79489_data_write (data);
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


/*
 * Returns true if there is a non-maskable interrupt.
 */
bool sms_nmi_check ()
{
    static bool pause_button_previous = false;
    bool ret = false;

    if (pause_button_previous == false && pause_button == true)
    {
        ret = true;
    }

    pause_button_previous = pause_button;

    return ret;
}


/*
 * Reads a ROM image from file into memory.
 */
int32_t sms_load_rom (uint8_t **buffer, uint32_t *buffer_size, char *filename)
{
    uint32_t bytes_read = 0;
    uint32_t file_size = 0;
    uint32_t skip = 0;

    /* Open ROM file */
    FILE *rom_file = fopen (filename, "rb");
    if (!rom_file)
    {
        perror ("Error: Unable to open ROM");
        return -1;
    }

    /* Get ROM size */
    fseek (rom_file, 0, SEEK_END);
    file_size = ftell (rom_file);

    /* Some roms seem to have an extra header at the start. Skip this. */
    skip = file_size & 0x3ff;
    fseek (rom_file, skip, SEEK_SET);
    *buffer_size = file_size - skip;

    /* Allocate memory */
    *buffer = (uint8_t *) malloc (*buffer_size);
    if (!*buffer)
    {
        perror ("Error: Unable to allocate memory for ROM.\n");
        return -1;
    }

    /* Copy to memory */
    while (bytes_read < *buffer_size)
    {
        bytes_read += fread (*buffer + bytes_read, 1, *buffer_size - bytes_read, rom_file);
    }

    fclose (rom_file);

    return EXIT_SUCCESS;
}


/*
 * Callback to supply SDL with audio frames.
 */
void sms_audio_callback (void *userdata, uint8_t *stream, int len)
{
    /* Assuming little-endian host */
    if (snepulator.running)
        sn79489_get_samples ((int16_t *)stream, len / 2);
    else
        memset (stream, 0, len);
}


/*
 * Reset the SMS and load a new BIOS and/or cartridge ROM.
 */
void sms_init (char *bios_filename, char *cart_filename)
{
    /* Free the previous ROMs */
    if (bios != NULL)
    {
        free (bios);
        bios = NULL;
    }
    if (cart != NULL)
    {
        free (cart);
        cart = NULL;
    }

    /* Load BIOS */
    if (bios_filename)
    {
        if (sms_load_rom (&bios, &bios_size, bios_filename) == -1)
        {
            snepulator.abort = true;
        }
        fprintf (stdout, "%d KiB BIOS %s loaded.\n", bios_size >> 10, bios_filename);
    }

    /* Load cart */
    if (cart_filename)
    {
        if (sms_load_rom (&cart, &cart_size, cart_filename) == -1)
        {
            snepulator.abort = true;
        }
        fprintf (stdout, "%d KiB cart %s loaded.\n", cart_size >> 10, cart_filename);
    }

    /* Initialise CPU and VDP */
    z80_init (sms_memory_read, sms_memory_write, sms_io_read, sms_io_write);
    vdp_init ();
    sn79489_init ();

    /* Initialize input */
    memset (&gamepad_1, 0, sizeof (gamepad_1));
    memset (&gamepad_2, 0, sizeof (gamepad_2));
    pause_button = false;

    /* Minimal alternative to the BIOS */
    if (!bios_filename)
    {
        z80_regs.im = 1;
        memory_control |= SMS_MEMORY_CTRL_BIOS_DISABLE;

        /* Leave the VDP in Mode4 */
        vdp_control_write (SMS_VDP_CTRL_0_MODE_4);
        vdp_control_write (TMS9918A_CODE_REG_WRITE | 0x00);
    }
}


/*
 * Returns the SMS clock-rate in Hz.
 */
uint32_t sms_get_clock_rate ()
{
    if (framerate == FRAMERATE_NTSC)
    {
        return SMS_CLOCK_RATE_NTSC;
    }
    else if (framerate == FRAMERATE_PAL)
    {
        return SMS_CLOCK_RATE_PAL;
    }

    fprintf (stderr, "Error: Framerate is neither NTSC or PAL.\n");
    return SMS_CLOCK_RATE_NTSC;
}


/*
 * Emulate the SMS for the specified length of time.
 */
void sms_run (double ms)
{
    int lines = (ms * sms_get_clock_rate () / 228.0) / 1000.0;

    while (lines--)
    {
        assert (lines >= 0);

        /* 228 CPU cycles per scanline */
        z80_run_cycles (228);
        psg_run_cycles (228);
        vdp_run_one_scanline ();
    }
}
