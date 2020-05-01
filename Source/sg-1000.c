/*
 * Sega SG-1000 implementation.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "snepulator.h"

#include "cpu/z80.h"
#include "video/tms9918a.h"
#include "video/sms_vdp.h"
#include "sound/sn79489.h"

extern Snepulator_State state;

#define SG_1000_RAM_SIZE (1 << 10)

/* Sega Mapper */
static uint8_t mapper_bank[3] = { 0x00, 0x01, 0x02 };


/*
 * Handle SG-1000 memory reads.
 */
static uint8_t sg_1000_memory_read (uint16_t addr)
{
    /* Cartridge slot */
    if (addr >= 0x0000 && addr <= 0xbfff)
    {
        uint8_t  slot = (addr >> 14);
        uint32_t bank_base = mapper_bank[slot] * ((uint32_t) 16 << 10);
        uint16_t offset    = addr & 0x3fff;

        /* The first 1 KiB of slot 0 is not affected by mapping */
        if (slot == 0 && offset < (1 << 10))
            bank_base = 0;

        if (state.rom != NULL)
        {
            return state.rom[(bank_base + offset) & (state.rom_size - 1)];
        }
    }

    /* 1 KiB RAM (mirrored) */
    if (addr >= 0xc000 && addr <= 0xffff)
    {
        return state.ram[(addr - 0xc000) & (SG_1000_RAM_SIZE - 1)];
    }

    return 0xff;
}


/*
 * Handle SG-1000 memory writes.
 */
static void sg_1000_memory_write (uint16_t addr, uint8_t data)
{
    /* No early breaks - Register writes also affect RAM */

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

    /* 1 KiB RAM (mirrored) */
    if (addr >= 0xc000 && addr <= 0xffff)
    {
        state.ram[(addr - 0xc000) & (SG_1000_RAM_SIZE - 1)] = data;
    }
}


/*
 * Handle SMS I/O reads.
 */
static uint8_t sg_1000_io_read (uint8_t addr)
{
    /* VDP */
    if (addr >= 0x80 && addr <= 0xbf)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* VDP Data Register */
            return sms_vdp_data_read ();
        }
        else
        {
            /* VDP Status Flags */
            return sms_vdp_status_read ();
        }
    }

    /* A pressed button returns zero */
    else if (addr >= 0xc0 && addr <= 0xff)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* I/O Port A/B */
            return (state.gamepad_1.up        ? 0 : BIT_0) |
                   (state.gamepad_1.down      ? 0 : BIT_1) |
                   (state.gamepad_1.left      ? 0 : BIT_2) |
                   (state.gamepad_1.right     ? 0 : BIT_3) |
                   (state.gamepad_1.button_1  ? 0 : BIT_4) |
                   (state.gamepad_1.button_2  ? 0 : BIT_5) |
                   (state.gamepad_2.up        ? 0 : BIT_6) |
                   (state.gamepad_2.down      ? 0 : BIT_7);
        }
        else
        {
            /* I/O Port B/misc */
            return (state.gamepad_2.left      ? 0 : BIT_0) |
                   (state.gamepad_2.right     ? 0 : BIT_1) |
                   (state.gamepad_2.button_1  ? 0 : BIT_2) |
                   (state.gamepad_2.button_2  ? 0 : BIT_3) |
                   (                          BIT_4);
        }
    }

    /* DEFAULT */
    return 0xff;
}


/*
 * Handle SMS I/O writes.
 */
static void sg_1000_io_write (uint8_t addr, uint8_t data)
{
    /* PSG */
    if (addr >= 0x40 && addr <= 0x7f)
    {
        sn79489_data_write (data);
    }

    /* VDP */
    else if (addr >= 0x80 && addr <= 0xbf)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* VDP Data Register */
            sms_vdp_data_write (data);
        }
        else
        {
            /* VDP Control Register */
            sms_vdp_control_write (data);
        }
    }
}


/*
 * Returns true if there is a non-maskable interrupt.
 */
bool sg_1000_nmi_check ()
{
    static bool pause_button_previous = false;
    bool ret = false;

    if (pause_button_previous == false && state.pause_button == true)
    {
        ret = true;
    }

    pause_button_previous = state.pause_button;

    return ret;
}


/* TODO: This could be made common */
/*
 * Reads a ROM image from file into memory.
 */
int32_t sg_1000_load_rom (uint8_t **buffer, uint32_t *buffer_size, char *filename)
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
static void sg_1000_audio_callback (void *userdata, uint8_t *stream, int len)
{
    /* Assuming little-endian host */
    if (state.running)
        sn79489_get_samples ((int16_t *)stream, len / 2);
    else
        memset (stream, 0, len);
}


/*
 * Returns the SG-1000 clock-rate in Hz.
 * TODO: Confirm values.
 * TODO: Rename "Frame rate" to "Video system"
 */
#define SG_1000_CLOCK_RATE_PAL  3546895
#define SG_1000_CLOCK_RATE_NTSC 3579545
static uint32_t sg_1000_get_clock_rate ()
{
    if (state.system == VIDEO_SYSTEM_PAL)
    {
        return SG_1000_CLOCK_RATE_PAL;
    }

    return SG_1000_CLOCK_RATE_NTSC;
}


/*
 * Emulate the SG-1000 for the specified length of time.
 */
static void sg_1000_run (double ms)
{
    int lines = (ms * sg_1000_get_clock_rate () / 228.0) / 1000.0;

    while (lines--)
    {
        assert (lines >= 0);

        /* 228 CPU cycles per scanline */
        z80_run_cycles (228);
        psg_run_cycles (228);
        tms9918a_run_one_scanline ();
    }
}


/*
 * Reset the SG-1000 and load a new cartridge ROM.
 */
void sg_1000_init (char *bios_filename, char *cart_filename)
{
    snepulator_reset ();

    /* Reset the mapper */
    mapper_bank[0] = 0;
    mapper_bank[1] = 1;
    mapper_bank[2] = 2;

    /* Create RAM */
    state.ram = calloc (SG_1000_RAM_SIZE, 1);
    if (state.ram == NULL)
    {
        fprintf (stderr, "Error: Unable to allocate memory.");
    }

    /* Load ROM cart */
    if (cart_filename)
    {
        if (sg_1000_load_rom (&state.rom, &state.rom_size, cart_filename) == -1)
        {
            state.abort = true;
        }
        fprintf (stdout, "%d KiB ROM %s loaded.\n", state.rom_size >> 10, cart_filename);
    }

    /* Initialise hardware */
    z80_init (sg_1000_memory_read, sg_1000_memory_write, sg_1000_io_read, sg_1000_io_write);
    tms9918a_init ();
    sn79489_init ();

    /* Initialize input */
    memset (&state.gamepad_1, 0, sizeof (state.gamepad_1));
    memset (&state.gamepad_2, 0, sizeof (state.gamepad_2));
    state.pause_button = false;

    /* Hook up the audio callback */
    state.audio_callback = sg_1000_audio_callback;
    state.get_clock_rate = sg_1000_get_clock_rate;
    state.run = sg_1000_run;
}
