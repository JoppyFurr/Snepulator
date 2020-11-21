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
#include "database/sms_db.h"

#include "gamepad.h"
#include "cpu/z80.h"
#include "video/tms9928a.h"
#include "video/sms_vdp.h"
#include "sound/sn76489.h"
#include "sms.h"

extern Snepulator_State state;
extern Snepulator_Gamepad gamepad_1;
extern Snepulator_Gamepad gamepad_2;
extern Z80_Regs z80_regs;

#define SMS_RAM_SIZE (8 << 10)
#define SMS_SRAM_SIZE (8 << 10)

/* Console state */
uint8_t memory_control = 0x00;
uint8_t io_control = 0x00;
bool export_paddle = false;
SMS_3D_Field sms_3d_field = SMS_3D_FIELD_NONE;

/* Cartridge Mapper */
static SMS_Mapper mapper = SMS_MAPPER_UNKNOWN;
static uint8_t mapper_bank [3] = { 0x00, 0x01, 0x02 };
static bool sram_enable = false;
static bool sram_modified = false;

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
        {
            bank_base = 0;
        }

        /* BIOS */
        if (state.bios != NULL && !(memory_control & SMS_MEMORY_CTRL_BIOS_DISABLE))
        {
            /* Assumes a power-of-two BIOS size */
            return state.bios [(bank_base + offset) & (state.bios_size - 1)];
        }

        /* On-cartridge SRAM */
        if (sram_enable && slot == 2)
        {
            return state.sram [offset & (SMS_SRAM_SIZE - 1)];
        }

        /* Cartridge ROM */
        if (state.rom != NULL && !(memory_control & SMS_MEMORY_CTRL_CART_DISABLE))
        {
            return state.rom [(bank_base + offset) & state.rom_mask];
        }
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
        if (data & 0x01)
        {
            sms_3d_field = SMS_3D_FIELD_LEFT;
        }
        else
        {
            sms_3d_field = SMS_3D_FIELD_RIGHT;
        }
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
            sram_enable = (data & BIT_3) ? true : false;

            if (data & (BIT_0 | BIT_1))
            {
                snepulator_error ("Error", "Bank shifting not implemented.");
            }
            if (data & (BIT_2 | BIT_4))
            {
                snepulator_error ("Error", "SRAM bank not implemented.");
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
                snepulator_error ("Error", "Codemasters SRAM not implemented.");
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

    /* On-cartridge SRAM */
    if (sram_enable && addr >= 0x8000 && addr <= 0xbfff)
    {
        state.sram [addr & (SMS_SRAM_SIZE - 1)] = data;
        sram_modified = true;
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
            uint8_t port_value = 0;

            if (gamepad_1.type == GAMEPAD_TYPE_SMS_PADDLE)
            {
                static uint8_t paddle_clock = 0;

                /* The "export paddle" uses the TH pin for a clock signal */
                if (export_paddle)
                {
                    if ((io_control & SMS_IO_TH_A_DIRECTION) == 0 && (io_control & SMS_IO_TH_A_LEVEL))
                    {
                        paddle_clock = 1;
                    }
                    else
                    {
                        paddle_clock = 0;
                    }
                }
                /* The Japanese paddle has an internal 8 kHz clock */
                else
                {
                    paddle_clock ^= 0x01;
                }

                if ((paddle_clock & 0x01) == 0x00)
                {
                    port_value = (gamepad_1.paddle_position & 0x0f) |
                                 ((gamepad_1.state [GAMEPAD_BUTTON_1] || gamepad_1.state [GAMEPAD_BUTTON_2]) ? 0 : BIT_4);
                }
                else
                {
                    port_value = (gamepad_1.paddle_position >> 0x04) |
                                 ((gamepad_1.state [GAMEPAD_BUTTON_1] || gamepad_1.state [GAMEPAD_BUTTON_2]) ? 0 : BIT_4) | BIT_5;
                }
            }
            else
            {
                port_value = (gamepad_1.state [GAMEPAD_DIRECTION_UP]        ? 0 : BIT_0) |
                             (gamepad_1.state [GAMEPAD_DIRECTION_DOWN]      ? 0 : BIT_1) |
                             (gamepad_1.state [GAMEPAD_DIRECTION_LEFT]      ? 0 : BIT_2) |
                             (gamepad_1.state [GAMEPAD_DIRECTION_RIGHT]     ? 0 : BIT_3) |
                             (gamepad_1.state [GAMEPAD_BUTTON_1]            ? 0 : BIT_4) |
                             (gamepad_1.state [GAMEPAD_BUTTON_2]            ? 0 : BIT_5);
            }

            return port_value | (gamepad_2.state [GAMEPAD_DIRECTION_UP]     ? 0 : BIT_6) |
                                (gamepad_2.state [GAMEPAD_DIRECTION_DOWN]   ? 0 : BIT_7);
        }
        else
        {
            bool port_1_th = false;
            bool port_2_th = false;

            if (state.region == REGION_WORLD)
            {
                if ((io_control & SMS_IO_TH_A_DIRECTION) == 0)
                {
                    port_1_th = io_control & SMS_IO_TH_A_LEVEL;

                    if (gamepad_1.type == GAMEPAD_TYPE_SMS_PADDLE)
                    {
                        export_paddle = true;
                    }

                }

                if ((io_control & SMS_IO_TH_B_DIRECTION) == 0)
                {
                    port_2_th = io_control & SMS_IO_TH_B_LEVEL;
                }
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
    return !! gamepad_1.state [GAMEPAD_BUTTON_START];
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

    if (gamepad_1.type == GAMEPAD_TYPE_SMS_PADDLE)
    {
        gamepad_paddle_tick (ms);
    }

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
 * Generate the SRAM backup path.
 *
 * This string needs to be freed.
 */
static char *sram_path (void)
{
    static char *path;
    char *dir;
    int len;

    if (snepulator_sram_directory (&dir) == -1)
    {
        return NULL;
    }

    /* Get the path length */
    len = snprintf (NULL, 0, "%s/000000000000000000000000.sram", dir) + 1;

    /* Create the string */
    path = calloc (len, 1);
    snprintf (path, len, "%s/%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x.sram", dir,
            state.rom_hash [ 0], state.rom_hash [ 1], state.rom_hash [ 2], state.rom_hash [ 3],
            state.rom_hash [ 4], state.rom_hash [ 5], state.rom_hash [ 6], state.rom_hash [ 7],
            state.rom_hash [ 8], state.rom_hash [ 9], state.rom_hash [10], state.rom_hash [11]);

    return path;
}


/*
 * Backup the on-cartridge SRAM.
 */
static void sms_sync (void)
{
    if (sram_modified)
    {
        uint32_t bytes_written = 0;
        char *path = sram_path ();
        FILE *sram_file = fopen (path, "wb");

        if (sram_file != NULL)
        {
            while (bytes_written < SMS_SRAM_SIZE)
            {
                bytes_written += fwrite (state.sram + bytes_written, 1, SMS_SRAM_SIZE - bytes_written, sram_file);
            }

            fclose (sram_file);
        }

        free (path);
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
    sram_enable = false;
    sram_modified = false;

    /* Create RAM */
    state.ram = calloc (SMS_RAM_SIZE, 1);
    if (state.ram == NULL)
    {
        snepulator_error ("Error", "Unable to allocate SMS RAM.");
        return;
    }

    /* Create Cartridge SRAM */
    state.sram = calloc (SMS_SRAM_SIZE, 1);
    if (state.sram == NULL)
    {
        snepulator_error ("Error", "Unable to allocate SMS Cartridge RAM.");
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

        state.rom_mask = round_up (state.rom_size) - 1;
        state.rom_hints = sms_db_get_hints (state.rom_hash);
    }

    /* Automatic video format */
    if (state.format_auto && (state.rom_hints & SMS_HINT_PAL_ONLY))
    {
        state.format = VIDEO_FORMAT_PAL;
    }

    /* Automatic controller type */
    if (gamepad_1.type_auto)
    {
        if (state.rom_hints & SMS_HINT_PADDLE_ONLY)
        {
            gamepad_1.type = GAMEPAD_TYPE_SMS_PADDLE;
        }
        else
        {
            gamepad_1.type = GAMEPAD_TYPE_SMS;
        }
    }

    /* Load SRAM if it exists */
    char *_sram_path = sram_path ();
    FILE *sram_file = fopen (_sram_path, "rb");
    if (sram_file != NULL)
    {
        uint32_t bytes_read = 0;

        while (bytes_read < SMS_SRAM_SIZE)
        {
            bytes_read += fread (state.sram + bytes_read, 1, SMS_SRAM_SIZE - bytes_read, sram_file);
        }

        fclose (sram_file);
    }
    free (_sram_path);

    export_paddle = false;
    sms_3d_field = SMS_3D_FIELD_NONE;

    /* Initialise CPU and VDP */
    z80_init (sms_memory_read, sms_memory_write, sms_io_read, sms_io_write);
    sms_vdp_init ();
    sn76489_init ();

    /* Hook up callbacks */
    state.audio_callback = sms_audio_callback;
    state.get_clock_rate = sms_get_clock_rate;
    state.get_int = sms_vdp_get_interrupt;
    state.get_nmi = sms_get_nmi;
    state.sync = sms_sync;
    state.run = sms_run;

    /* Begin emulation */
    state.ready = true;
    state.running = true;

    /* Minimal alternative to the BIOS */
    if (!state.sms_bios_filename)
    {
        /* Z80 interrupt mode and stack pointer */
        z80_regs.im = 1;
        z80_regs.sp = 0xdff0;

        memory_control |= SMS_MEMORY_CTRL_BIOS_DISABLE;

        /* Leave the VDP in Mode4 */
        sms_vdp_control_write (SMS_VDP_CTRL_0_MODE_4);
        sms_vdp_control_write (TMS9928A_CODE_REG_WRITE | 0x00);

        /* Line counter starts at 0xff */
        sms_vdp_control_write (0xff);
        sms_vdp_control_write (TMS9928A_CODE_REG_WRITE | 0x0a);
    }
}
