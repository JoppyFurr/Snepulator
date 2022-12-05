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

#include "snepulator_types.h"
#include "snepulator.h"
#include "util.h"
#include "save_state.h"

#include "gamepad.h"
#include "cpu/z80.h"
#include "video/tms9928a.h"
#include "sound/sn76489.h"

#include "sg-1000.h"

extern Snepulator_State state;
extern Snepulator_Gamepad gamepad [3];
extern pthread_mutex_t video_mutex;

static pthread_mutex_t sg_1000_state_mutex = PTHREAD_MUTEX_INITIALIZER;


/*
 * Declarations.
 */
static uint8_t  sg_1000_io_read (void *context_ptr, uint8_t addr);
static void     sg_1000_io_write (void *context_ptr, uint8_t addr, uint8_t data);
static uint8_t  sg_1000_memory_read (void *context_ptr, uint16_t addr);
static void     sg_1000_memory_write (void *context_ptr, uint16_t addr, uint8_t data);
static void     sg_1000_run (void *context_ptr, uint32_t ms);
static void     sg_1000_state_load (void *context_ptr, const char *filename);
static void     sg_1000_state_save (void *context_ptr, const char *filename);
static void     sg_1000_update_settings (void *context_ptr);


/*
 * Callback to supply SDL with audio frames.
 */
static void sg_1000_audio_callback (void *userdata, uint8_t *stream, int len)
{
    if (state.run == RUN_STATE_RUNNING)
    {
        /* Assuming little-endian host */
        sn76489_get_samples ((int16_t *)stream, len / 2);
    }
    else
    {
        memset (stream, 0, len);
    }
}


/*
 * Clean up any console-specific structures.
 */
static void sg_1000_cleanup (void *context_ptr)
{
    SG_1000_Context *context = (SG_1000_Context *) context_ptr;

    if (context->z80_context != NULL)
    {
        free (context->z80_context);
        context->z80_context = NULL;
    }

    if (context->vdp_context != NULL)
    {
        free (context->vdp_context);
        context->vdp_context = NULL;
    }

    if (context->rom != NULL)
    {
        free (context->rom);
        context->rom = NULL;
    }
}


/*
 * Process a frame completion by the VDP.
 */
static void sg_1000_frame_done (void *context_ptr)
{
    SG_1000_Context *context = (SG_1000_Context *) context_ptr;
    TMS9928A_Context *vdp_context = context->vdp_context;

    pthread_mutex_lock (&video_mutex);

    memcpy (state.video_out_data, vdp_context->frame_buffer, sizeof (vdp_context->frame_buffer));
    state.video_start_x = vdp_context->video_start_x;
    state.video_start_y = vdp_context->video_start_y;
    state.video_width   = vdp_context->video_width;
    state.video_height  = vdp_context->video_height;

    pthread_mutex_unlock (&video_mutex);
}


/*
 * Returns the SG-1000 clock-rate in Hz.
 */
static uint32_t sg_1000_get_clock_rate (void *context_ptr)
{
    SG_1000_Context *context = (SG_1000_Context *) context_ptr;

    if (context->format == VIDEO_FORMAT_PAL)
    {
        return SG_1000_CLOCK_RATE_PAL;
    }

    return SG_1000_CLOCK_RATE_NTSC;
}


/*
 * Returns true if there is an interrupt.
 */
static bool sg_1000_get_int (void *context_ptr)
{
    SG_1000_Context *context = (SG_1000_Context *) context_ptr;

    return tms9928a_get_interrupt (context->vdp_context);
}


/*
 * Returns true if there is a non-maskable interrupt.
 */
static bool sg_1000_get_nmi (void *context_ptr)
{
    return !! gamepad [1].state [GAMEPAD_BUTTON_START];
}


/*
 * Returns a pointer to the rom hash.
 */
static uint8_t *sg_1000_get_rom_hash (void *context_ptr)
{
    SG_1000_Context *context = (SG_1000_Context *) context_ptr;

    return context->rom_hash;
}


/*
 * Reset the SG-1000 and load a new cartridge ROM.
 */
SG_1000_Context *sg_1000_init (void)
{
    SG_1000_Context *context;
    Z80_Context *z80_context;
    TMS9928A_Context *vdp_context;

    context = calloc (1, sizeof (SG_1000_Context));
    if (context == NULL)
    {
        snepulator_error ("Error", "Unable to allocate memory for SG_1000_Context");
        return NULL;
    }

    /* Initialise CPU */
    z80_context = z80_init (context,
                            sg_1000_memory_read, sg_1000_memory_write,
                            sg_1000_io_read, sg_1000_io_write,
                            sg_1000_get_int, sg_1000_get_nmi);
    context->z80_context = z80_context;

    /* Initialize VDP */
    vdp_context = tms9928a_init (context, sg_1000_frame_done);
    vdp_context->render_start_x = VIDEO_SIDE_BORDER;
    vdp_context->render_start_y = (VIDEO_BUFFER_LINES - 192) / 2;
    vdp_context->video_start_x  = vdp_context->render_start_x;
    vdp_context->video_start_y  = vdp_context->render_start_y;
    context->vdp_context = vdp_context;

    /* Initialise PSG */
    sn76489_init ();

    /* Pull in the settings */
    sg_1000_update_settings (context);

    /* Reset the mapper */
    context->hw_state.mapper_bank [0] = 0;
    context->hw_state.mapper_bank [1] = 1;
    context->hw_state.mapper_bank [2] = 2;
    context->sram_used = false;

    /* Load ROM cart */
    if (state.cart_filename)
    {
        if (util_load_rom (&context->rom, &context->rom_size, state.cart_filename) == -1)
        {
            sg_1000_cleanup (context);
            return NULL;
        }
        fprintf (stdout, "%d KiB ROM %s loaded.\n", context->rom_size >> 10, state.cart_filename);
        context->rom_mask = util_round_up (context->rom_size) - 1;
        util_hash_rom (context->rom, context->rom_size, context->rom_hash);
    }

    /* Initial video parameters */
    state.video_start_x    = vdp_context->video_start_x;
    state.video_start_y    = vdp_context->video_start_y;
    state.video_width      = vdp_context->video_width;
    state.video_height     = vdp_context->video_height;
    state.video_has_border = true;

    /* Hook up the callbacks */
    state.audio_callback = sg_1000_audio_callback;
    state.cleanup = sg_1000_cleanup;
    state.get_clock_rate = sg_1000_get_clock_rate;
    state.get_rom_hash = sg_1000_get_rom_hash;
    state.run_callback = sg_1000_run;
    state.state_load = sg_1000_state_load;
    state.state_save = sg_1000_state_save;
    state.update_settings = sg_1000_update_settings;

    /* Begin emulation */
    state.run = RUN_STATE_RUNNING;

    return context;
}


/*
 * Handle SG-1000 I/O reads.
 */
static uint8_t sg_1000_io_read (void *context_ptr, uint8_t addr)
{
    SG_1000_Context *context = (SG_1000_Context *) context_ptr;

    /* tms9928a */
    if (addr >= 0x80 && addr <= 0xbf)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* tms9928a Data Register */
            return tms9928a_data_read (context->vdp_context);
        }
        else
        {
            /* tms9928a Status Flags */
            return tms9928a_status_read (context->vdp_context);
        }
    }

    /* A pressed button returns zero */
    else if (addr >= 0xc0 && addr <= 0xff)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* I/O Port A/B */
            return (gamepad [1].state [GAMEPAD_DIRECTION_UP]    ? 0 : BIT_0) |
                   (gamepad [1].state [GAMEPAD_DIRECTION_DOWN]  ? 0 : BIT_1) |
                   (gamepad [1].state [GAMEPAD_DIRECTION_LEFT]  ? 0 : BIT_2) |
                   (gamepad [1].state [GAMEPAD_DIRECTION_RIGHT] ? 0 : BIT_3) |
                   (gamepad [1].state [GAMEPAD_BUTTON_1]        ? 0 : BIT_4) |
                   (gamepad [1].state [GAMEPAD_BUTTON_2]        ? 0 : BIT_5) |
                   (gamepad [2].state [GAMEPAD_DIRECTION_UP]    ? 0 : BIT_6) |
                   (gamepad [2].state [GAMEPAD_DIRECTION_DOWN]  ? 0 : BIT_7);
        }
        else
        {
            /* I/O Port B/misc */
            return (gamepad [2].state [GAMEPAD_DIRECTION_LEFT]  ? 0 : BIT_0) |
                   (gamepad [2].state [GAMEPAD_DIRECTION_RIGHT] ? 0 : BIT_1) |
                   (gamepad [2].state [GAMEPAD_BUTTON_1]        ? 0 : BIT_2) |
                   (gamepad [2].state [GAMEPAD_BUTTON_2]        ? 0 : BIT_3) |
                   (                                                  BIT_4);
        }
    }

    /* DEFAULT */
    return 0xff;
}


/*
 * Handle SG-1000 I/O writes.
 */
static void sg_1000_io_write (void *context_ptr, uint8_t addr, uint8_t data)
{
    SG_1000_Context *context = (SG_1000_Context *) context_ptr;

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
            tms9928a_data_write (context->vdp_context, data);
        }
        else
        {
            /* VDP Control Register */
            tms9928a_control_write (context->vdp_context, data);
        }
    }
}


/*
 * Handle SG-1000 memory reads.
 */
static uint8_t sg_1000_memory_read (void *context_ptr, uint16_t addr)
{
    SG_1000_Context *context = (SG_1000_Context *) context_ptr;

    /* Cartridge slot */
    if (addr >= 0x0000 && addr <= 0xbfff && addr < context->rom_size)
    {
        uint8_t slot = (addr >> 14);
        uint32_t bank_base = context->hw_state.mapper_bank [slot] * ((uint32_t) 16 << 10);
        uint16_t offset    = addr & 0x3fff;

        return context->rom [(bank_base + offset) & context->rom_mask];
    }

    /* Up to 8 KiB of on-cartridge sram */
    if (addr >= 0x8000 && addr <= 0xbfff)
    {
        return context->sram [addr & (SG_1000_SRAM_SIZE - 1)];
    }

    /* 1 KiB RAM (mirrored) */
    if (addr >= 0xc000 && addr <= 0xffff)
    {
        return context->ram [addr & (SG_1000_RAM_SIZE - 1)];
    }

    return 0xff;
}


/*
 * Handle SG-1000 memory writes.
 */
static void sg_1000_memory_write (void *context_ptr, uint16_t addr, uint8_t data)
{
    SG_1000_Context *context = (SG_1000_Context *) context_ptr;

    /* Sega Mapper */
    if (addr == 0xffff)
    {
        context->hw_state.mapper_bank [2] = data & 0x3f;
    }

    /* Up to 8 KiB of on-cartridge sram */
    if (addr >= 0x8000 && addr <= 0xbfff)
    {
        context->sram [addr & (SG_1000_SRAM_SIZE - 1)] = data;
        context->sram_used = true;
    }

    /* 1 KiB RAM (mirrored) */
    if (addr >= 0xc000 && addr <= 0xffff)
    {
        context->ram [addr & (SG_1000_RAM_SIZE - 1)] = data;
    }
}


/*
 * Emulate the SG-1000 for the specified length of time.
 */
static void sg_1000_run (void *context_ptr, uint32_t ms)
{
    SG_1000_Context *context = (SG_1000_Context *) context_ptr;
    uint64_t lines;

    pthread_mutex_lock (&sg_1000_state_mutex);

    /* Convert the time into lines to run, storing the remainder as millicycles. */
    context->millicycles += (uint64_t) ms * sg_1000_get_clock_rate (context);
    lines = context->millicycles / 228000;
    context->millicycles -= lines * 228000;

    while (lines--)
    {
        assert (lines >= 0);

        /* 228 CPU cycles per scanline */
        z80_run_cycles (context->z80_context, 228 + context->overclock);
        psg_run_cycles (228);
        tms9928a_run_one_scanline (context->vdp_context);
    }

    pthread_mutex_unlock (&sg_1000_state_mutex);
}


/*
 * Import SG-1000 state from a file.
 */
static void sg_1000_state_load (void *context_ptr, const char *filename)
{
    SG_1000_Context *context = (SG_1000_Context *) context_ptr;

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

    context->sram_used = false;

    for (uint32_t i = 0; i < sections_loaded; i++)
    {
        const char *section_id;
        uint32_t version;
        uint32_t size;
        uint8_t *data;
        load_state_section (&section_id, &version, &size, (void *) &data);

        if (!strncmp (section_id, SECTION_ID_SG_1000_HW, 4))
        {
            if (size == sizeof (SG_1000_HW_State))
            {
                memcpy (&context->hw_state, data, sizeof (SG_1000_HW_State));
            }
            else
            {
                snepulator_error ("Error", "Save-state contains incorrect hw_state size");
            }
        }
        else if (!strncmp (section_id, SECTION_ID_Z80, 4))
        {
            z80_state_load (context->z80_context, version, size, data);
        }
        else if (!strncmp (section_id, SECTION_ID_RAM, 4))
        {
            if (size == SG_1000_RAM_SIZE)
            {
                memcpy (context->ram, data, SG_1000_RAM_SIZE);
            }
            else
            {
                snepulator_error ("Error", "Save-state contains incorrect RAM size");
            }
        }
        else if (!strncmp (section_id, SECTION_ID_SRAM, 4))
        {
            context->sram_used = true;
            if (size == SG_1000_SRAM_SIZE)
            {
                memcpy (context->sram, data, SG_1000_SRAM_SIZE);
            }
            else
            {
                snepulator_error ("Error", "Save-state contains incorrect SRAM size");
            }
        }
        else if (!strncmp (section_id, SECTION_ID_VDP, 4))
        {
            tms9928a_state_load (context->vdp_context, version, size, data);
        }
        else if (!strncmp (section_id, SECTION_ID_VRAM, 4))
        {
            if (size == TMS9928A_VRAM_SIZE)
            {
                memcpy (context->vdp_context->vram, data, TMS9928A_VRAM_SIZE);
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
 * Export SG-1000 state to a file.
 */
static void sg_1000_state_save (void *context_ptr, const char *filename)
{
    SG_1000_Context *context = (SG_1000_Context *) context_ptr;

    pthread_mutex_lock (&sg_1000_state_mutex);

    /* Begin creating a new save state. */
    save_state_begin (CONSOLE_ID_SG_1000);

    save_state_section_add (SECTION_ID_SG_1000_HW, 1, sizeof (SG_1000_HW_State), &context->hw_state);

    z80_state_save (context->z80_context);
    save_state_section_add (SECTION_ID_RAM, 1, SG_1000_RAM_SIZE, context->ram);
    if (context->sram_used)
    {
        save_state_section_add (SECTION_ID_SRAM, 1, SG_1000_SRAM_SIZE, context->sram);
    }

    tms9928a_state_save (context->vdp_context);
    save_state_section_add (SECTION_ID_VRAM, 1, TMS9928A_VRAM_SIZE, context->vdp_context->vram);

    sn76489_state_save ();

    pthread_mutex_unlock (&sg_1000_state_mutex);

    save_state_write (filename);
}


/*
 * Propagate user settings into the console context.
 */
static void sg_1000_update_settings (void *context_ptr)
{
    SG_1000_Context *context = (SG_1000_Context *) context_ptr;

    /* Update console */
    context->overclock = state.overclock;

    if (state.format_auto)
    {
        state.format = VIDEO_FORMAT_NTSC;
        context->format = VIDEO_FORMAT_NTSC;
    }
    else
    {
        context->format = state.format;
    }

    /* Update VDP */
    context->vdp_context->format              = context->format;
    context->vdp_context->remove_sprite_limit = state.remove_sprite_limit;
    context->vdp_context->disable_blanking    = state.disable_blanking;
    if (state.override_tms_palette == NULL)
    {
        context->vdp_context->palette = tms9928a_palette;
    }
    else
    {
        context->vdp_context->palette = state.override_tms_palette;
    }
}
