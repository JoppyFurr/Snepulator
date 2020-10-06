/*
 * Sega Master System implementation.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "util.h"
#include "snepulator.h"

#include "gamepad.h"
#include "cpu/z80.h"
#include "video/tms9918a.h"
#include "video/sms_vdp.h"
#include "sound/sn76489.h"
#include "sms.h"

extern Snepulator_State state;
extern Snepulator_Gamepad gamepad_1;
extern Snepulator_Gamepad gamepad_2;
extern Z80_Regs z80_regs;

#define SMS_RAM_SIZE (8 << 10)

/* Console state */
uint8_t memory_control = 0x00;
uint8_t io_control = 0x00;

/* Cartridge Mapper */
static SMS_Mapper mapper = SMS_MAPPER_UNKNOWN;
static uint8_t mapper_bank [3] = { 0x00, 0x01, 0x02 };

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
        uint8_t slot = (addr >> 14);
        uint32_t bank_base = mapper_bank [slot] * ((uint32_t) 16 << 10);
        uint16_t offset    = addr & 0x3fff;

        /* The first 1 KiB of slot 0 is not affected by mapping */
        if (slot == 0 && offset < (1 << 10))
            bank_base = 0;

        if (state.bios != NULL && !(memory_control & SMS_MEMORY_CTRL_BIOS_DISABLE))
            return state.bios [(bank_base + offset) & (state.bios_size - 1)];

        if (state.rom != NULL && !(memory_control & SMS_MEMORY_CTRL_CART_DISABLE))
            return state.rom [(bank_base + offset) & (state.rom_size - 1)];
    }

    /* 8 KiB RAM + mirror */
    if (addr >= 0xc000 && addr <= 0xffff)
    {
        return state.ram [addr & (SMS_RAM_SIZE - 1)];
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

    if (mapper == SMS_MAPPER_UNKNOWN)
    {
        if (addr == 0xfffc || addr == 0xfffd ||
            addr == 0xfffe || addr == 0xffff)
        {
            mapper = SMS_MAPPER_SEGA;
        }
        else if (addr == 0x4000 || addr == 0x8000)
        {
            mapper = SMS_MAPPER_CODEMASTERS;
        }
        else if (addr == 0xa000)
        {
            mapper = SMS_MAPPER_KOREAN;
        }
    }

    if (mapper == SMS_MAPPER_SEGA)
    {
        if (addr == 0xfffc)
        {
            if (data & (BIT_0 | BIT_1))
            {
                snepulator_error ("Error", "Bank shifting not implemented.");
            }

            if (data & (BIT_2 | BIT_3 | BIT_4))
            {
                snepulator_error ("Error", "Cartridge RAM not implemented.");
            }
        }
        else if (addr == 0xfffd)
        {
            mapper_bank [0] = data & 0x3f;
        }
        else if (addr == 0xfffe)
        {
            mapper_bank [1] = data & 0x3f;
        }
        else if (addr == 0xffff)
        {
            mapper_bank [2] = data & 0x3f;
        }
    }

    if (mapper == SMS_MAPPER_CODEMASTERS)
    {
        /* TODO: There are differences from the Sega mapper. Do any games rely on them?
         *  1. Initial banks are different (0, 1, 0) instead of (0, 1, 2).
         *  2. The first 1KB is not protected.
         */
        if (addr == 0x0000)
        {
            mapper_bank [0] = data & 0x3f;
        }
        else if (addr == 0x4000)
        {
            mapper_bank [1] = data & 0x3f;
        }
        else if (addr == 0x8000)
        {
            mapper_bank [2] = data & 0x3f;

            if (data & BIT_7)
            {
                snepulator_error ("Error", "Cartridge RAM not implemented.");
            }
        }
    }

    if (mapper == SMS_MAPPER_KOREAN)
    {
        if (addr == 0xa000)
        {
            mapper_bank [2] = data & 0x3f;
        }
    }

    /* Cartridge, card, BIOS, expansion slot */
    if (addr >= 0x0000 && addr <= 0xbfff)
    {
    }

    /* RAM + mirror */
    if (addr >= 0xc000 && addr <= 0xffff)
    {
        state.ram [addr & (SMS_RAM_SIZE - 1)] = data;
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
            return sms_vdp_get_v_counter ();
        }
        else
        {
            /* H Counter */
            fprintf (stderr, "Warning: H Counter not implemented.\n");
        }
    }


    /* VDP */
    else if (addr >= 0x80 && addr <= 0xbf)
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
            bool port_1_th = false;
            bool port_2_th = false;

            if (state.region == REGION_WORLD)
            {
                if ((io_control & SMS_IO_TH_A_DIRECTION) == 0)
                    port_1_th = io_control & SMS_IO_TH_A_LEVEL;

                if ((io_control & SMS_IO_TH_B_DIRECTION) == 0)
                    port_2_th = io_control & SMS_IO_TH_B_LEVEL;
            }

            /* I/O Port B/misc */
            return (gamepad_2.state [GAMEPAD_DIRECTION_LEFT]    ? 0 : BIT_0) |
                   (gamepad_2.state [GAMEPAD_DIRECTION_RIGHT]   ? 0 : BIT_1) |
                   (gamepad_2.state [GAMEPAD_BUTTON_1]          ? 0 : BIT_2) |
                   (gamepad_2.state [GAMEPAD_BUTTON_2]          ? 0 : BIT_3) |
                   (/* TODO: RESET */                         0 ? 0 : BIT_4) |
                   (port_1_th                                   ? BIT_6 : 0) |
                   (port_2_th                                   ? BIT_7 : 0);
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
        sn76489_data_write (data);
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
static bool sms_get_nmi ()
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
static void sms_audio_callback (void *userdata, uint8_t *stream, int len)
{
    /* Assuming little-endian host */
    if (state.running)
        sn76489_get_samples ((int16_t *)stream, len / 2);
    else
        memset (stream, 0, len);
}


/*
 * Returns the SMS clock-rate in Hz.
 */
static uint32_t sms_get_clock_rate ()
{
    if (state.format == VIDEO_FORMAT_PAL)
    {
        return SMS_CLOCK_RATE_PAL;
    }

    return SMS_CLOCK_RATE_NTSC;
}


/*
 * Emulate the SMS for the specified length of time.
 *
 * TODO: Something better than millicycles.
 */
static void sms_run (uint32_t ms)
{
    static uint64_t millicycles = 0;
    uint64_t lines;

    millicycles += (uint64_t) ms * sms_get_clock_rate ();
    lines = millicycles / 228000;
    millicycles -= lines * 228000;

    while (lines--)
    {
        assert (lines >= 0);

        /* 228 CPU cycles per scanline */
        z80_run_cycles (228);
        psg_run_cycles (228);
        sms_vdp_run_one_scanline ();
    }
}


/*
 * Reset the SMS and load a new BIOS and/or cartridge ROM.
 */
void sms_init (void)
{
    /* Reset the mapper */
    mapper = SMS_MAPPER_UNKNOWN;
    mapper_bank [0] = 0;
    mapper_bank [1] = 1;
    mapper_bank [2] = 2;

    /* Create RAM */
    state.ram = calloc (SMS_RAM_SIZE, 1);
    if (state.ram == NULL)
    {
        snepulator_error ("Error", "Unable to allocate SMS RAM.");
        return;
    }

    /* Load BIOS */
    if (state.sms_bios_filename)
    {
        if (snepulator_load_rom (&state.bios, &state.bios_size, state.sms_bios_filename) == -1)
        {
            return;
        }
        fprintf (stdout, "%d KiB BIOS %s loaded.\n", state.bios_size >> 10, state.sms_bios_filename);
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

    /* Initialise CPU and VDP */
    z80_init (sms_memory_read, sms_memory_write, sms_io_read, sms_io_write);
    sms_vdp_init ();
    sn76489_init ();

    /* Hook up callbacks */
    state.audio_callback = sms_audio_callback;
    state.get_clock_rate = sms_get_clock_rate;
    state.get_int = sms_vdp_get_interrupt;
    state.get_nmi = sms_get_nmi;
    state.run = sms_run;

    /* Begin emulation */
    state.ready = true;
    state.running = true;

    /* Minimal alternative to the BIOS */
    if (!state.sms_bios_filename)
    {
        z80_regs.im = 1;
        memory_control |= SMS_MEMORY_CTRL_BIOS_DISABLE;

        /* Leave the VDP in Mode4 */
        sms_vdp_control_write (SMS_VDP_CTRL_0_MODE_4);
        sms_vdp_control_write (TMS9918A_CODE_REG_WRITE | 0x00);
    }
}
