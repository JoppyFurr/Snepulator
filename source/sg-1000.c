/*
 * Snepulator
 * Sega SG-1000 implementation.
 */

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snepulator_types.h"
#include "snepulator.h"
#include "util.h"
#include "database/sg_db.h"
#include "save_state.h"

#include "gamepad.h"
#include "cpu/z80.h"
#include "video/tms9928a.h"
#include "sound/band_limit.h"
#include "sound/sn76489.h"

#include "sg-1000.h"

extern Snepulator_State state;
extern Snepulator_Gamepad gamepad [3];
extern pthread_mutex_t video_mutex;


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
 * Callback to supply audio frames.
 */
static void sg_1000_audio_callback (void *context_ptr, int16_t *stream, uint32_t count)
{
    SG_1000_Context *context = (SG_1000_Context *) context_ptr;

    /* Assuming little-endian host */
    sn76489_get_samples (context->psg_context, stream, count);
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

    if (context->psg_context != NULL)
    {
        if (context->psg_context->bandlimit_context_l)
        {
            free (context->psg_context->bandlimit_context_l);
        }
        if (context->psg_context->bandlimit_context_r)
        {
            free (context->psg_context->bandlimit_context_r);
        }

        free (context->psg_context);
        context->psg_context = NULL;
    }

    if (context->rom != NULL)
    {
        free (context->rom);
        context->rom = NULL;
    }
}


/*
 * Display diagnostic information.
 */
#ifdef DEVELOPER_BUILD
static void sg_1000_diagnostics_show (void)
{
    SG_1000_Context *context = state.console_context;
    Z80_Context *z80_context = context->z80_context;
    TMS9928A_Context *vdp_context = context->vdp_context;

    state.diagnostics_print ("SG-1000");

    state.diagnostics_print ("---");
    state.diagnostics_print ("CPU");
    state.diagnostics_print ("PC : %04x    SP : %04x", z80_context->state.pc, z80_context->state.sp);
    state.diagnostics_print ("AF : %04x    BC : %04x", z80_context->state.af, z80_context->state.bc);
    state.diagnostics_print ("DE : %04x    HL : %04x", z80_context->state.de, z80_context->state.hl);
    state.diagnostics_print ("IX : %04x    IY : %04x", z80_context->state.ix, z80_context->state.iy);
    state.diagnostics_print ("IM :  %3u    IFF: %d,%d", z80_context->state.im, z80_context->state.iff1, z80_context->state.iff2);

    state.diagnostics_print ("---");
    state.diagnostics_print ("Video");
    state.diagnostics_print ("Mode : %s", tms9928a_mode_name_get (tms9928a_get_mode (context->vdp_context)));
    state.diagnostics_print ("Frame interrupts : %s", vdp_context->state.regs.ctrl_1_frame_int_en ? "Enabled" : "Disabled");
}
#endif


/*
 * Process a frame completion by the VDP.
 */
static void sg_1000_frame_done (void *context_ptr)
{
    SG_1000_Context *context = (SG_1000_Context *) context_ptr;
    TMS9928A_Context *vdp_context = context->vdp_context;

    snepulator_frame_done (vdp_context->frame_buffer);

    /* TODO: Make these parameters */
    state.video_start_x = vdp_context->video_start_x;
    state.video_start_y = vdp_context->video_start_y;
    state.video_width   = vdp_context->video_width;
    state.video_height  = vdp_context->video_height;
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
    SN76489_Context *psg_context;

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

    /* Initialise VDP */
    vdp_context = tms9928a_init (context, sg_1000_frame_done);
    vdp_context->render_start_x = VIDEO_SIDE_BORDER;
    vdp_context->render_start_y = (VIDEO_BUFFER_LINES - 192) / 2;
    vdp_context->video_start_x  = vdp_context->render_start_x;
    vdp_context->video_start_y  = vdp_context->render_start_y;
    context->vdp_context = vdp_context;

    /* Initialise PSG */
    psg_context = sn76489_init ();
    context->psg_context = psg_context;

    /* Pull in the settings */
    sg_1000_update_settings (context);

    /* Reset the mapper */
    context->hw_state.mapper = SG_MAPPER_UNKNOWN;
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
        context->rom_hints = sg_db_get_hints (context->rom_hash);

        if (context->rom_hints & SG_HINT_MAPPER_GRAPHIC_BOARD)
        {
            context->hw_state.mapper = SG_MAPPER_GRAPHIC_BOARD;
        }
        else if (context->rom_size <= SIZE_48K)
        {
            context->hw_state.mapper = SG_MAPPER_NONE;
        }
    }

    /* Initial video parameters */
    state.video_start_x    = vdp_context->video_start_x;
    state.video_start_y    = vdp_context->video_start_y;
    state.video_width      = vdp_context->video_width;
    state.video_height     = vdp_context->video_height;

    /* Hook up the callbacks */
    state.audio_callback = sg_1000_audio_callback;
    state.cleanup = sg_1000_cleanup;
    state.get_rom_hash = sg_1000_get_rom_hash;
    state.run_callback = sg_1000_run;
    state.state_load = sg_1000_state_load;
    state.state_save = sg_1000_state_save;
    state.update_settings = sg_1000_update_settings;
#ifdef DEVELOPER_BUILD
    state.diagnostics_show = sg_1000_diagnostics_show;
#endif

    /* Switch to Interrupt Mode 1
     * This doesn't happen on a real SG-1000, normally it needs
     * to be set by the game. However, some homebrew may only be
     * tested on a Master System, which has a BIOS that sets IM 1. */
    z80_context->state.im = 1;

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
                   ( /* Cartridge Pin B11 */                          BIT_4) |
                   ( /* SC-3000 Serial Printer FAULT */               BIT_5) |
                   ( /* SC-3000 Serial Printer BUSY */                BIT_6) |
                   ( /* SC-3000 Cassette Input */                     BIT_7);
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
        sn76489_data_write (context->psg_context, data);
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

    /* Graphic Board */
    if (context->hw_state.mapper == SG_MAPPER_GRAPHIC_BOARD)
    {
        if (addr == 0x8000)
        {
            /* Bit 0: Button pressed
             * Bit 7: Busy */
            return state.cursor_button ? 0x00 : 0x01;
        }
        else if (addr == 0xa000)
        {
            /* Position - Make use of light-phaser cursor support */
            if (state.cursor_x < 2 || state.cursor_x > 254 ||
                state.cursor_y < 0 || state.cursor_y > 192)
            {
                /* Pen not on board */
                return 0x00;
            }
            else if (context->graphic_board_axis)
            {
                /* X position */
                return state.cursor_x - 2;
            }
            else
            {
                /* Y position */
                return state.cursor_y + 28;
            }
        }
    }

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

    /* Graphic Board */
    if (context->hw_state.mapper == SG_MAPPER_GRAPHIC_BOARD && addr == 0x6000)
    {
        context->graphic_board_axis = data & 0x01;
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
 * Emulate the SG-1000 for the specified number of clock cycles.
 * Called with the run_mutex held.
 */
static void sg_1000_run (void *context_ptr, uint32_t cycles)
{
    SG_1000_Context *context = (SG_1000_Context *) context_ptr;
    uint32_t lines;

    /* Convert cycles into lines to run, storing the remainder for next time. */
    context->pending_cycles += cycles;
    lines = context->pending_cycles / 228;
    context->pending_cycles -= lines * 228;

    while (lines--)
    {
        /* 228 CPU cycles per scanline */
        z80_run_cycles (context->z80_context, 228 + context->overclock);
        sn76489_run_cycles (context->psg_context, state.clock_rate, 228);
        tms9928a_run_one_scanline (context->vdp_context);
    }
}


/*
 * Import SG-1000 state from a file.
 * Called with the run_mutex held.
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
            sn76489_state_load (context->psg_context, version, size, data);
        }
        else
        {
            printf ("Unknown Section: section_id=%s, version=%u, size=%u, data=%p.\n", section_id, version, size, data);
        }
    }

    load_state_end ();
}


/*
 * Export SG-1000 state to a file.
 * Called with the run_mutex held.
 */
static void sg_1000_state_save (void *context_ptr, const char *filename)
{
    SG_1000_Context *context = (SG_1000_Context *) context_ptr;

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

    sn76489_state_save (context->psg_context);

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

    /* Update clock rate */
    state.clock_rate = (context->format == VIDEO_FORMAT_PAL) ? PAL_COLOURBURST_4_5_FREQ : NTSC_COLOURBURST_FREQ;

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
