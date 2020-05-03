/*
 * ColecoVision implementation
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
#include "sound/sn76489.h"

#include "colecovision.h"

extern Snepulator_State state;

#define COLECOVISION_RAM_SIZE (1 << 10)

/*
 * Handle ColecoVision memory reads.
 */
static uint8_t colecovision_memory_read (uint16_t addr)
{
    /* BIOS */
    if (addr >= 0x0000 && addr <= 0x1fff)
    {
        if (state.bios != NULL)
        {
            return state.bios[(addr) & (state.bios_size - 1)];
        }
    }

    /* 1 KiB RAM (mirrored) */
    if (addr >= 0x6000 && addr <= 0x7fff)
    {
        return state.ram[addr & (COLECOVISION_RAM_SIZE - 1)];
    }

    /* Cartridge slot */
    if (addr >= 0x8000 && addr <= 0xffff)
    {
        if (state.rom != NULL)
        {
            return state.rom[addr & (state.rom_size - 1)];
        }
    }

    return 0xff;
}


/*
 * Handle ColecoVision memory writes.
 */
static void colecovision_memory_write (uint16_t addr, uint8_t data)
{
    /* 1 KiB RAM (mirrored) */
    if (addr >= 0x6000 && addr <= 0x7fff)
    {
        state.ram[addr & (COLECOVISION_RAM_SIZE - 1)] = data;
    }
}


/*
 * Handle ColecoVision I/O reads.
 */
static uint8_t colecovision_io_read (uint8_t addr)
{
    /* tms9918a */
    if (addr >= 0xa0 && addr <= 0xbf)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* tms9918a Data Register */
            return tms9918a_data_read ();
        }
        else
        {
            /* tms9918a Status Flags */
            return tms9918a_status_read ();
        }
    }

    /* Controller input */
    else if (addr >= 0xe0 && addr <= 0xff)
    {
        /* TODO */
    }

    /* DEFAULT */
    return 0xff;
}


/*
 * Handle ColecoVision I/O writes.
 */
static void colecovision_io_write (uint8_t addr, uint8_t data)
{
    /* TODO: 0x80 - 0x9f: Set input to keypad-mode */

    /* tms9918a */
    if (addr >= 0xa0 && addr <= 0xbf)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* VDP Data Register */
            tms9918a_data_write (data);
        }
        else
        {
            /* VDP Control Register */
            tms9918a_control_write (data);
        }
    }

    /* TODO: 0xc0 - 0xdf: Set input to joystick-mode */

    /* PSG */
    if (addr >= 0xe0 && addr <= 0xff)
    {
        sn76489_data_write (data);
    }
}


/* TODO: Make this common */
/*
 * Reads a ROM image from file into memory.
 */
int32_t colecovision_load_rom (uint8_t **buffer, uint32_t *buffer_size, char *filename)
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
static void colecovision_audio_callback (void *userdata, uint8_t *stream, int len)
{
    /* Assuming little-endian host */
    if (state.running)
        sn76489_get_samples ((int16_t *)stream, len / 2);
    else
        memset (stream, 0, len);
}


/*
 * Returns the ColecoVision clock-rate in Hz.
 */
static uint32_t colecovision_get_clock_rate ()
{
    if (state.system == VIDEO_SYSTEM_PAL)
    {
        return COLECOVISION_CLOCK_RATE_PAL;
    }

    return COLECOVISION_CLOCK_RATE_NTSC;
}


/*
 * Emulate the ColecoVision for the specified length of time.
 */
static void colecovision_run (double ms)
{
    int lines = (ms * colecovision_get_clock_rate () / 228.0) / 1000.0;

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
 * Reset the ColecoVision and load a new cartridge ROM.
 */
void colecovision_init (void)
{
    /* Create RAM */
    state.ram = calloc (COLECOVISION_RAM_SIZE, 1);
    if (state.ram == NULL)
    {
        fprintf (stderr, "Error: Unable to allocate memory.");
    }

    /* Load BIOS */
    if (state.colecovision_bios_filename)
    {
        /* TODO: Common rom loading function */
        if (colecovision_load_rom (&state.bios, &state.bios_size, state.colecovision_bios_filename) == -1)
        {
            state.abort = true;
        }
        fprintf (stdout, "%d KiB BIOS %s loaded.\n", state.bios_size >> 10, state.colecovision_bios_filename);
    }

    /* Load ROM cart */
    if (state.cart_filename)
    {
        if (colecovision_load_rom (&state.rom, &state.rom_size, state.cart_filename) == -1)
        {
            state.abort = true;
        }
        fprintf (stdout, "%d KiB ROM %s loaded.\n", state.rom_size >> 10, state.cart_filename);
    }

    /* Initialise hardware */
    z80_init (colecovision_memory_read, colecovision_memory_write, colecovision_io_read, colecovision_io_write);
    tms9918a_init ();
    sn76489_init ();

    /* Initialize input */
    memset (&state.gamepad_1, 0, sizeof (state.gamepad_1));
    memset (&state.gamepad_2, 0, sizeof (state.gamepad_2));
    state.pause_button = false;

    /* Hook up the audio callback */
    state.audio_callback = colecovision_audio_callback;
    state.get_clock_rate = colecovision_get_clock_rate;
    state.get_int = NULL;
    state.get_nmi = tms9918a_get_interrupt;
    state.run = colecovision_run;

    /* Begin emulation */
    state.ready = true;
    state.running = true;
}
