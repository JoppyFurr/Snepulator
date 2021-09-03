/*
 * Sega SG-1000 implementation.
 */

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "snepulator.h"
#include "save_state.h"

#include "gamepad.h"
#include "cpu/z80.h"
#include "video/tms9928a.h"
#include "sound/sn76489.h"

#include "sg-1000.h"

extern Snepulator_State state;
extern Snepulator_Gamepad gamepad_1;
extern Snepulator_Gamepad gamepad_2;

#define SG_1000_RAM_SIZE (1 << 10)
#define SG_1000_SRAM_SIZE (8 << 10)

static pthread_mutex_t sg_1000_state_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Console hardware state */
typedef struct SG_1000_HW_State_s {
    uint8_t mapper;
    uint8_t mapper_bank [3];
} SG_1000_HW_State;

static SG_1000_HW_State hw_state = {
    .mapper_bank = { 0x00, 0x01, 0x02 },
};

static bool sram_used = false;


/*
 * Handle SG-1000 memory reads.
 */
static uint8_t sg_1000_memory_read (uint16_t addr)
{
    /* Cartridge slot */
    if (addr >= 0x0000 && addr <= 0xbfff && addr < state.rom_size)
    {
        uint8_t slot = (addr >> 14);
        uint32_t bank_base = hw_state.mapper_bank [slot] * ((uint32_t) 16 << 10);
        uint16_t offset    = addr & 0x3fff;

        return state.rom [(bank_base + offset) & state.rom_mask];
    }

    /* Up to 8 KiB of on-cartridge sram */
    if (addr >= 0x8000 && addr <= 0xbfff)
    {
        return state.sram [addr & (SG_1000_SRAM_SIZE - 1)];
    }

    /* 1 KiB RAM (mirrored) */
    if (addr >= 0xc000 && addr <= 0xffff)
    {
        return state.ram [addr & (SG_1000_RAM_SIZE - 1)];
    }

    return 0xff;
}


/*
 * Handle SG-1000 memory writes.
 */
static void sg_1000_memory_write (uint16_t addr, uint8_t data)
{
    /* Sega Mapper */
    if (addr == 0xffff)
    {
        hw_state.mapper_bank [2] = data & 0x3f;
    }

    /* Up to 8 KiB of on-cartridge sram */
    if (addr >= 0x8000 && addr <= 0xbfff)
    {
        state.sram [addr & (SG_1000_SRAM_SIZE - 1)] = data;
        sram_used = true;
    }

    /* 1 KiB RAM (mirrored) */
    if (addr >= 0xc000 && addr <= 0xffff)
    {
        state.ram [addr & (SG_1000_RAM_SIZE - 1)] = data;
    }
}


/*
 * Handle SG-1000 I/O reads.
 */
static uint8_t sg_1000_io_read (uint8_t addr)
{
    /* tms9928a */
    if (addr >= 0x80 && addr <= 0xbf)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* tms9928a Data Register */
            return tms9928a_data_read ();
        }
        else
        {
            /* tms9928a Status Flags */
            return tms9928a_status_read ();
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
            tms9928a_data_write (data);
        }
        else
        {
            /* VDP Control Register */
            tms9928a_control_write (data);
        }
    }
}


/*
 * Returns true if there is a non-maskable interrupt.
 */
static bool sg_1000_get_nmi ()
{
    return !! gamepad_1.state [GAMEPAD_BUTTON_START];
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
    if (state.format == VIDEO_FORMAT_PAL)
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

    pthread_mutex_lock (&sg_1000_state_mutex);

    millicycles += (uint64_t) ms * sg_1000_get_clock_rate ();
    lines = millicycles / 228000;
    millicycles -= lines * 228000;

    while (lines--)
    {
        assert (lines >= 0);

        /* 228 CPU cycles per scanline */
        z80_run_cycles (228 + state.overclock);
        psg_run_cycles (228);
        tms9928a_run_one_scanline ();
    }

    pthread_mutex_unlock (&sg_1000_state_mutex);
}


/*
 * Export SG-1000 state to a file.
 */
static void sg_1000_state_save (const char *filename)
{
    pthread_mutex_lock (&sg_1000_state_mutex);

    /* Begin creating a new save state. */
    save_state_begin (CONSOLE_ID_SG_1000);

    save_state_section_add (SECTION_ID_SG_1000_HW, 1, sizeof (hw_state), &hw_state);

    z80_state_save ();
    save_state_section_add (SECTION_ID_RAM, 1, SG_1000_RAM_SIZE, state.ram);
    if (sram_used)
    {
        save_state_section_add (SECTION_ID_SRAM, 1, SG_1000_SRAM_SIZE, state.sram);
    }

    tms9928a_state_save ();
    save_state_section_add (SECTION_ID_VRAM, 1, TMS9928A_VRAM_SIZE, state.vram);

    sn76489_state_save ();

    pthread_mutex_unlock (&sg_1000_state_mutex);

    save_state_write (filename);
}


/*
 * Import SG-1000 state from a file.
 */
static void sg_1000_state_load (const char *filename)
{
    const char *console_id;
    uint32_t sections_loaded;

    if (load_state_begin (filename, &console_id, &sections_loaded) == -1)
    {
        return;
    }

    pthread_mutex_lock (&sg_1000_state_mutex);

    if (!strncmp (console_id, CONSOLE_ID_SG_1000, 4))
    {
        state.console = CONSOLE_SG_1000;
    }
    else
    {
        return;
    }

    sram_used = false;

    for (uint32_t i = 0; i < sections_loaded; i++)
    {
        const char *section_id;
        uint32_t version;
        uint32_t size;
        uint8_t *data;
        load_state_section (&section_id, &version, &size, (void *) &data);

        if (!strncmp (section_id, SECTION_ID_SG_1000_HW, 4))
        {
            if (size == sizeof (hw_state))
            {
                memcpy (&hw_state, data, sizeof (hw_state));
            }
            else
            {
                snepulator_error ("Error", "Save-state contains incorrect hw_state size");
            }
        }
        else if (!strncmp (section_id, SECTION_ID_Z80, 4))
        {
            z80_state_load (version, size, data);
        }
        else if (!strncmp (section_id, SECTION_ID_RAM, 4))
        {
            if (size == SG_1000_RAM_SIZE)
            {
                memcpy (state.ram, data, SG_1000_RAM_SIZE);
            }
            else
            {
                snepulator_error ("Error", "Save-state contains incorrect RAM size");
            }
        }
        else if (!strncmp (section_id, SECTION_ID_SRAM, 4))
        {
            sram_used = true;
            if (size == SG_1000_SRAM_SIZE)
            {
                memcpy (state.sram, data, SG_1000_SRAM_SIZE);
            }
            else
            {
                snepulator_error ("Error", "Save-state contains incorrect SRAM size");
            }
        }
        else if (!strncmp (section_id, SECTION_ID_VDP, 4))
        {
            tms9928a_state_load (version, size, data);
        }
        else if (!strncmp (section_id, SECTION_ID_VRAM, 4))
        {
            if (size == TMS9928A_VRAM_SIZE)
            {
                memcpy (state.vram, data, TMS9928A_VRAM_SIZE);
            }
            else
            {
                snepulator_error ("Error", "Save-state contains incorrect VRAM size");
            }
        }
        else if (!strncmp (section_id, SECTION_ID_PSG, 4))
        {
            sn76489_state_load (version, size, data);
        }
        else
        {
            printf ("Unknown Section: section_id=%s, version=%u, size=%u, data=%p.\n", section_id, version, size, data);
        }
    }

    pthread_mutex_unlock (&sg_1000_state_mutex);

    load_state_end ();
}


/*
 * Reset the SG-1000 and load a new cartridge ROM.
 */
void sg_1000_init (void)
{
    /* Reset the mapper */
    hw_state.mapper_bank [0] = 0;
    hw_state.mapper_bank [1] = 1;
    hw_state.mapper_bank [2] = 2;
    sram_used = false;

    /* Create RAM */
    state.ram = calloc (SG_1000_RAM_SIZE, 1);
    if (state.ram == NULL)
    {
        snepulator_error ("Error", "Unable to allocate SG-1000 RAM.");
        return;
    }

    /* Create Cartridge SRAM */
    state.sram = calloc (SG_1000_SRAM_SIZE, 1);
    if (state.sram == NULL)
    {
        snepulator_error ("Error", "Unable to allocate SG-1000 Cartridge RAM.");
        return;
    }

    /* Create VRAM */
    state.vram = calloc (TMS9928A_VRAM_SIZE, 1);
    if (state.vram == NULL)
    {
        snepulator_error ("Error", "Unable to allocate SG-1000 VRAM.");
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

        state.rom_mask = round_up (state.rom_size) - 1;
    }

    /* Initialise hardware */
    z80_init (sg_1000_memory_read, sg_1000_memory_write, sg_1000_io_read, sg_1000_io_write);
    tms9928a_init ();
    sn76489_init ();

    /* Hook up the callbacks */
    state.audio_callback = sg_1000_audio_callback;
    state.get_clock_rate = sg_1000_get_clock_rate;
    state.get_int = tms9928a_get_interrupt;
    state.get_nmi = sg_1000_get_nmi;
    state.run = sg_1000_run;
    state.state_save = sg_1000_state_save;
    state.state_load = sg_1000_state_load;

    /* Video parameters */
    state.render_start_x = VIDEO_SIDE_BORDER;
    state.render_start_y = (VIDEO_BUFFER_LINES - 192) / 2;
    state.video_width = 256;
    state.video_height = 192;
    state.video_start_x = state.render_start_x;
    state.video_start_y = state.render_start_y;
    state.video_has_border = true;

    /* Begin emulation */
    state.ready = true;
    state.running = true;
}
