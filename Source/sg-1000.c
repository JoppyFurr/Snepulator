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

#include "gamepad.h"
#include "cpu/z80.h"
#include "video/tms9918a.h"
#include "sound/sn76489.h"

#include "sg-1000.h"

extern Snepulator_State state;
extern Snepulator_Gamepad gamepad_1;
extern Snepulator_Gamepad gamepad_2;

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
        return state.ram[addr & (SG_1000_RAM_SIZE - 1)];
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
        state.ram[addr & (SG_1000_RAM_SIZE - 1)] = data;
    }
}


/*
 * Handle SG-1000 I/O reads.
 */
static uint8_t sg_1000_io_read (uint8_t addr)
{
    /* tms9918a */
    if (addr >= 0x80 && addr <= 0xbf)
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

    /* A pressed button returns zero */
    else if (addr >= 0xc0 && addr <= 0xff)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* I/O Port A/B */
            return (gamepad_1.state [GAMEPAD_DIRECTION_UP]      ? 0 : BIT_0) |
                   (gamepad_1.state [GAMEPAD_DIRECTION_DOWN]    ? 0 : BIT_1) |
                   (gamepad_1.state [GAMEPAD_DIRECTION_LEFT]    ? 0 : BIT_2) |
                   (gamepad_1.state [GAMEPAD_DIRECTION_RIGHT]   ? 0 : BIT_3) |
                   (gamepad_1.state [GAMEPAD_BUTTON_1]          ? 0 : BIT_4) |
                   (gamepad_1.state [GAMEPAD_BUTTON_2]          ? 0 : BIT_5) |
                   (gamepad_2.state [GAMEPAD_DIRECTION_UP]      ? 0 : BIT_6) |
                   (gamepad_2.state [GAMEPAD_DIRECTION_DOWN]    ? 0 : BIT_7);
        }
        else
        {
            /* I/O Port B/misc */
            return (gamepad_2.state [GAMEPAD_DIRECTION_LEFT]    ? 0 : BIT_0) |
                   (gamepad_2.state [GAMEPAD_DIRECTION_RIGHT]   ? 0 : BIT_1) |
                   (gamepad_2.state [GAMEPAD_BUTTON_1]          ? 0 : BIT_2) |
                   (gamepad_2.state [GAMEPAD_BUTTON_2]          ? 0 : BIT_3) |
                   (                                                  BIT_4);
        }
    }

    /* DEFAULT */
    return 0xff;
}


/*
 * Handle SG-1000 I/O writes.
 */
static void sg_1000_io_write (uint8_t addr, uint8_t data)
{
    /* PSG */
    if (addr >= 0x40 && addr <= 0x7f)
    {
        sn76489_data_write (data);
    }

    /* VDP */
    else if (addr >= 0x80 && addr <= 0xbf)
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
}


/*
 * Returns true if there is a non-maskable interrupt.
 */
static bool sg_1000_get_nmi ()
{
    static bool pause_button_previous = false;
    bool ret = false;

    if (pause_button_previous == false && gamepad_1.state [GAMEPAD_BUTTON_START] == true)
    {
        ret = true;
    }

    pause_button_previous = gamepad_1.state [GAMEPAD_BUTTON_START];

    return ret;
}


/*
 * Callback to supply SDL with audio frames.
 */
static void sg_1000_audio_callback (void *userdata, uint8_t *stream, int len)
{
    /* Assuming little-endian host */
    if (state.running)
        sn76489_get_samples ((int16_t *)stream, len / 2);
    else
        memset (stream, 0, len);
}


/*
 * Returns the SG-1000 clock-rate in Hz.
 */
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
static void sg_1000_run (uint32_t ms)
{
    /* TODO: Make these calculations common */
    static uint64_t millicycles = 0;
    uint64_t lines;

    millicycles += (uint64_t) ms * sg_1000_get_clock_rate();
    lines = millicycles / 228000;
    millicycles -= lines * 228000;

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
void sg_1000_init (void)
{
    /* Reset the mapper */
    mapper_bank[0] = 0;
    mapper_bank[1] = 1;
    mapper_bank[2] = 2;

    /* Create RAM */
    state.ram = calloc (SG_1000_RAM_SIZE, 1);
    if (state.ram == NULL)
    {
        snepulator_error ("Error", "Unable to allocate SG-1000 RAM.");
        return;
    }

    /* Load ROM cart */
    if (state.cart_filename)
    {
        if (snepulator_load_rom (&state.rom, &state.rom_size, state.cart_filename) == -1)
        {
            return;
        }
        fprintf (stdout, "%d KiB ROM %s loaded.\n", state.rom_size >> 10, state.cart_filename);
    }

    /* Initialise hardware */
    z80_init (sg_1000_memory_read, sg_1000_memory_write, sg_1000_io_read, sg_1000_io_write);
    tms9918a_init ();
    sn76489_init ();

    /* Hook up the audio callback */
    state.audio_callback = sg_1000_audio_callback;
    state.get_clock_rate = sg_1000_get_clock_rate;
    state.get_int = tms9918a_get_interrupt;
    state.get_nmi = sg_1000_get_nmi;
    state.run = sg_1000_run;

    /* Begin emulation */
    state.ready = true;
    state.running = true;
}
