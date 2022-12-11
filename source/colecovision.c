/*
 * ColecoVision implementation
 */

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "snepulator_types.h"
#include "snepulator.h"
#include "util.h"
#include "save_state.h"

#include "gamepad.h"
#include "cpu/z80.h"
#include "video/tms9928a.h"
#include "sound/sn76489.h"

#include "colecovision.h"

extern Snepulator_State state;
extern Snepulator_Gamepad gamepad [3];
extern pthread_mutex_t video_mutex;


/*
 * Declarations.
 */
static uint8_t  colecovision_io_read (void *context_ptr, uint8_t addr);
static void     colecovision_io_write (void *context_ptr, uint8_t addr, uint8_t data);
static uint8_t  colecovision_memory_read (void *context_ptr, uint16_t addr);
static void     colecovision_memory_write (void *context_ptr, uint16_t addr, uint8_t data);
static void     colecovision_run (void *context_ptr, uint32_t ms);
static void     colecovision_state_load (void *context_ptr, const char *filename);
static void     colecovision_state_save (void *context_ptr, const char *filename);
static void     colecovision_update_settings (void *context_ptr);


/*
 * Callback to supply SDL with audio frames.
 */
static void colecovision_audio_callback (void *userdata, uint8_t *stream, int len)
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
static void colecovision_cleanup (void *context_ptr)
{
    ColecoVision_Context *context = (ColecoVision_Context *) context_ptr;

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

    if (context->bios != NULL)
    {
        free (context->bios);
        context->bios = NULL;
    }
}


/*
 * Process a frame completion by the VDP.
 */
static void colecovision_frame_done (void *context_ptr)
{
    ColecoVision_Context *context = (ColecoVision_Context *) context_ptr;
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
 * Returns the ColecoVision clock-rate in Hz.
 */
static uint32_t colecovision_get_clock_rate (void *context_ptr)
{
    ColecoVision_Context *context = (ColecoVision_Context *) context_ptr;

    if (context->format == VIDEO_FORMAT_PAL)
    {
        return COLECOVISION_CLOCK_RATE_PAL;
    }

    return COLECOVISION_CLOCK_RATE_NTSC;
}


/*
 * Maskable interrupt line is not used.
 */
static bool colecovision_get_int (void *context_ptr)
{
    return false;
}


/*
 * Returns true if there is a non-maskable interrupt.
 */
static bool colecovision_get_nmi (void *context_ptr)
{
    ColecoVision_Context *context = (ColecoVision_Context *) context_ptr;

    return tms9928a_get_interrupt (context->vdp_context);
}


/*
 * Returns a pointer to the rom hash.
 */
static uint8_t *colecovision_get_rom_hash (void *context_ptr)
{
    ColecoVision_Context *context = (ColecoVision_Context *) context_ptr;

    return context->rom_hash;
}


/*
 * Reset the ColecoVision and load a new cartridge ROM.
 */
ColecoVision_Context *colecovision_init (void)
{
    ColecoVision_Context *context;
    Z80_Context *z80_context;
    TMS9928A_Context *vdp_context;

    context = calloc (1, sizeof (ColecoVision_Context));
    if (context == NULL)
    {
        snepulator_error ("Error", "Unable to allocate memory for ColecoVision_Context");
        return NULL;
    }

    /* Initialise CPU */
    z80_context = z80_init (context,
                            colecovision_memory_read, colecovision_memory_write,
                            colecovision_io_read, colecovision_io_write,
                            colecovision_get_int, colecovision_get_nmi);
    context->z80_context = z80_context;

    /* Initialise VDP */
    vdp_context = tms9928a_init (context, colecovision_frame_done);
    vdp_context->render_start_x      = VIDEO_SIDE_BORDER;
    vdp_context->render_start_y      = (VIDEO_BUFFER_LINES - 192) / 2;
    vdp_context->video_start_x       = vdp_context->render_start_x;
    vdp_context->video_start_y       = vdp_context->render_start_y;
    context->vdp_context = vdp_context;

    /* Initialise PSG */
    sn76489_init ();

    /* Pull in the settings */
    colecovision_update_settings (context);

    /* Defaults */
    context->hw_state.input_mode = COLECOVISION_INPUT_MODE_JOYSTICK;

    /* Load BIOS */
    if (state.colecovision_bios_filename)
    {
        if (util_load_rom (&context->bios, &context->bios_size, state.colecovision_bios_filename) == -1)
        {
            colecovision_cleanup (context);
            return NULL;
        }
        fprintf (stdout, "%d KiB BIOS %s loaded.\n", context->bios_size >> 10, state.colecovision_bios_filename);
        context->bios_mask = util_round_up (context->bios_size) - 1;
    }

    /* Load ROM cart */
    if (state.cart_filename)
    {
        if (util_load_rom (&context->rom, &context->rom_size, state.cart_filename) == -1)
        {
            colecovision_cleanup (context);
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
    state.audio_callback = colecovision_audio_callback;
    state.cleanup = colecovision_cleanup;
    state.get_clock_rate = colecovision_get_clock_rate;
    state.get_rom_hash = colecovision_get_rom_hash;
    state.run_callback = colecovision_run;
    state.state_load = colecovision_state_load;
    state.state_save = colecovision_state_save;
    state.update_settings = colecovision_update_settings;

    /* Begin emulation */
    state.run = RUN_STATE_RUNNING;

    return context;
}


/*
 * Handle ColecoVision I/O reads.
 */
static uint8_t colecovision_io_read (void *context_ptr, uint8_t addr)
{
    ColecoVision_Context *context = (ColecoVision_Context *) context_ptr;

    /* tms9928a */
    if (addr >= 0xa0 && addr <= 0xbf)
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

    /* Controller input */
    else if (addr >= 0xe0 && addr <= 0xff)
    {
        if ((addr & 0x02) == 0)
        {
            if (context->hw_state.input_mode == COLECOVISION_INPUT_MODE_JOYSTICK)
            {
                return (gamepad [1].state [GAMEPAD_DIRECTION_UP]    ? 0 : BIT_0) |
                       (gamepad [1].state [GAMEPAD_DIRECTION_RIGHT] ? 0 : BIT_1) |
                       (gamepad [1].state [GAMEPAD_DIRECTION_DOWN]  ? 0 : BIT_2) |
                       (gamepad [1].state [GAMEPAD_DIRECTION_LEFT]  ? 0 : BIT_3) |
                       (                                                  BIT_4) |
                       (                                                  BIT_5) |
                       (gamepad [1].state [GAMEPAD_BUTTON_1]        ? 0 : BIT_6);
            }
            else
            {
                /* For now, just use the computer keyboard. */
                uint8_t const *keyboard_state = SDL_GetKeyboardState (NULL);
                uint8_t key;

                if ((keyboard_state [SDL_SCANCODE_8] && (keyboard_state [SDL_SCANCODE_LSHIFT] || keyboard_state [SDL_SCANCODE_RSHIFT])) ||
                    keyboard_state [SDL_SCANCODE_KP_MULTIPLY])
                {
                    key = 0x09;
                }
                else if ((keyboard_state [SDL_SCANCODE_3] && (keyboard_state [SDL_SCANCODE_LSHIFT]|| keyboard_state [SDL_SCANCODE_RSHIFT])) ||
                    keyboard_state [SDL_SCANCODE_KP_HASH])
                {
                    key = 0x06;
                }
                else if (keyboard_state [SDL_SCANCODE_1] || keyboard_state [SDL_SCANCODE_KP_1])
                {
                    key = 0x0d;
                }
                else if (keyboard_state [SDL_SCANCODE_2] || keyboard_state [SDL_SCANCODE_KP_2])
                {
                    key = 0x07;
                }
                else if (keyboard_state [SDL_SCANCODE_3] || keyboard_state [SDL_SCANCODE_KP_3])
                {
                    key = 0x0c;
                }
                else if (keyboard_state [SDL_SCANCODE_4] || keyboard_state [SDL_SCANCODE_KP_4])
                {
                    key = 0x02;
                }
                else if (keyboard_state [SDL_SCANCODE_5] || keyboard_state [SDL_SCANCODE_KP_5])
                {
                    key = 0x03;
                }
                else if (keyboard_state [SDL_SCANCODE_6] || keyboard_state [SDL_SCANCODE_KP_6])
                {
                    key = 0x0e;
                }
                else if (keyboard_state [SDL_SCANCODE_7] || keyboard_state [SDL_SCANCODE_KP_7])
                {
                    key = 0x05;
                }
                else if (keyboard_state [SDL_SCANCODE_8] || keyboard_state [SDL_SCANCODE_KP_8])
                {
                    key = 0x01;
                }
                else if (keyboard_state [SDL_SCANCODE_9] || keyboard_state [SDL_SCANCODE_KP_9])
                {
                    key = 0x0b;
                }
                else if (keyboard_state [SDL_SCANCODE_0] || keyboard_state [SDL_SCANCODE_KP_0])
                {
                    key = 0x0a;
                }
                else
                {
                    key = 0x0f;
                }

                return key | BIT_4 | BIT_5 | (gamepad [1].state [GAMEPAD_BUTTON_2] ? 0 : BIT_6);
            }
        }
        else
        {
            /* Player 2 not implemented */
        }
    }

    /* DEFAULT */
    return 0xff;
}


/*
 * Handle ColecoVision memory reads.
 */
static uint8_t colecovision_memory_read (void *context_ptr, uint16_t addr)
{
    ColecoVision_Context *context = (ColecoVision_Context *) context_ptr;

    /* BIOS */
    if (addr >= 0x0000 && addr <= 0x1fff)
    {
        if (context->bios != NULL)
        {
            return context->bios [(addr) & context->bios_mask];
        }
    }

    /* 1 KiB RAM (mirrored) */
    if (addr >= 0x6000 && addr <= 0x7fff)
    {
        return context->ram [addr & (COLECOVISION_RAM_SIZE - 1)];
    }

    /* Cartridge slot */
    if (addr >= 0x8000 && addr <= 0xffff)
    {
        if (context->rom != NULL)
        {
            return context->rom [addr & context->rom_mask];
        }
    }

    return 0xff;
}


/*
 * Handle ColecoVision memory writes.
 */
static void colecovision_memory_write (void *context_ptr, uint16_t addr, uint8_t data)
{
    ColecoVision_Context *context = (ColecoVision_Context *) context_ptr;

    /* 1 KiB RAM (mirrored) */
    if (addr >= 0x6000 && addr <= 0x7fff)
    {
        context->ram [addr & (COLECOVISION_RAM_SIZE - 1)] = data;
    }
}


/*
 * Handle ColecoVision I/O writes.
 */
static void colecovision_io_write (void *context_ptr, uint8_t addr, uint8_t data)
{
    ColecoVision_Context *context = (ColecoVision_Context *) context_ptr;

    /* Set input to keypad-mode */
    if (addr >= 0x80 && addr <= 0x9f)
    {
        context->hw_state.input_mode = COLECOVISION_INPUT_MODE_KEYPAD;
    }

    /* tms9928a */
    if (addr >= 0xa0 && addr <= 0xbf)
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

    /* Set input to joystick-mode */
    if (addr >= 0xc0 && addr <= 0xdf)
    {
        context->hw_state.input_mode = COLECOVISION_INPUT_MODE_JOYSTICK;
    }

    /* PSG */
    if (addr >= 0xe0 && addr <= 0xff)
    {
        sn76489_data_write (data);
    }
}


/*
 * Emulate the ColecoVision for the specified length of time.
 * Called with the run_mutex held.
 */
static void colecovision_run (void *context_ptr, uint32_t ms)
{
    ColecoVision_Context *context = (ColecoVision_Context *) context_ptr;
    uint64_t lines;

    /* Convert the time into lines to run, storing the remainder as millicycles. */
    context->millicycles += (uint64_t) ms * colecovision_get_clock_rate (context);
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
}


/*
 * Import ColecoVision state from a file.
 * Called with the run_mutex held.
 */
static void colecovision_state_load (void *context_ptr, const char *filename)
{
    ColecoVision_Context *context = (ColecoVision_Context *) context_ptr;

    const char *console_id;
    uint32_t sections_loaded;

    if (load_state_begin (filename, &console_id, &sections_loaded) == -1)
    {
        return;
    }

    if (!strncmp (console_id, CONSOLE_ID_COLECOVISION, 4))
    {
        state.console = CONSOLE_COLECOVISION;
    }
    else
    {
        return;
    }

    for (uint32_t i = 0; i < sections_loaded; i++)
    {
        const char *section_id;
        uint32_t version;
        uint32_t size;
        uint8_t *data;
        load_state_section (&section_id, &version, &size, (void *) &data);

        if (!strncmp (section_id, SECTION_ID_COLECOVISION_HW, 4))
        {
            if (size == sizeof (ColecoVision_HW_State))
            {
                memcpy (&context->hw_state, data, sizeof (ColecoVision_HW_State));
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
            if (size == COLECOVISION_RAM_SIZE)
            {
                memcpy (context->ram, data, COLECOVISION_RAM_SIZE);
            }
            else
            {
                snepulator_error ("Error", "Save-state contains incorrect RAM size");
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

    load_state_end ();
}


/*
 * Export ColecoVision state to a file.
 * Called with the run_mutex held.
 */
static void colecovision_state_save (void *context_ptr, const char *filename)
{
    ColecoVision_Context *context = (ColecoVision_Context *) context_ptr;

    /* Begin creating a new save state. */
    save_state_begin (CONSOLE_ID_COLECOVISION);

    save_state_section_add (SECTION_ID_COLECOVISION_HW, 1, sizeof (ColecoVision_HW_State), &context->hw_state);

    z80_state_save (context->z80_context);
    save_state_section_add (SECTION_ID_RAM, 1, COLECOVISION_RAM_SIZE, context->ram);

    tms9928a_state_save (context->vdp_context);
    save_state_section_add (SECTION_ID_VRAM, 1, TMS9928A_VRAM_SIZE, context->vdp_context->vram);

    sn76489_state_save ();

    save_state_write (filename);
}


/*
 * Propagate user settings into the console context.
 */
static void colecovision_update_settings (void *context_ptr)
{
    ColecoVision_Context *context = (ColecoVision_Context *) context_ptr;

    /* Update console */
    context->overclock           = state.overclock;

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
