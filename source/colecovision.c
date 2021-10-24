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

#include "util.h"
#include "snepulator.h"
#include "save_state.h"

#include "gamepad.h"
#include "cpu/z80.h"
#include "video/tms9928a.h"
#include "sound/sn76489.h"

#include "colecovision.h"

extern Snepulator_State state;
extern Snepulator_Gamepad gamepad_1;
extern Snepulator_Gamepad gamepad_2;
extern pthread_mutex_t video_mutex;

static Z80_Context *z80_context = NULL;
static TMS9928A_Context *vdp_context = NULL;

#define COLECOVISION_RAM_SIZE (1 << 10)

static pthread_mutex_t colecovision_state_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct ColecoVision_HW_State_s {
    uint8_t input_mode;
} ColecoVision_HW_State;

static ColecoVision_HW_State hw_state = {
    .input_mode = COLECOVISION_INPUT_MODE_JOYSTICK,
};


/*
 * Declarations.
 */
static uint8_t  colecovision_io_read (uint8_t addr);
static void     colecovision_io_write (uint8_t addr, uint8_t data);
static uint8_t  colecovision_memory_read (uint16_t addr);
static void     colecovision_memory_write (uint16_t addr, uint8_t data);
static void     colecovision_run (uint32_t ms);
static void     colecovision_state_load (const char *filename);
static void     colecovision_state_save (const char *filename);


/*
 * Clean up any console-specific structures.
 */
static void colecovision_cleanup (void)
{
    if (z80_context != NULL)
    {
        free (z80_context);
        z80_context = NULL;
    }

    if (vdp_context != NULL)
    {
        free (vdp_context);
        vdp_context = NULL;
    }
}


/*
 * Process a frame completion by the VDP.
 */
static void colecovision_frame_done (void *ptr)
{
    pthread_mutex_lock (&video_mutex);

    memcpy (state.video_out_data, vdp_context->frame_buffer, sizeof (vdp_context->frame_buffer));
    state.video_start_x = vdp_context->video_start_x;
    state.video_start_y = vdp_context->video_start_y;
    state.video_width   = vdp_context->video_width;
    state.video_height  = vdp_context->video_height;

    pthread_mutex_unlock (&video_mutex);
}


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
            /* Assumes a power-of-two BIOS size */
            return state.bios [(addr) & (state.bios_size - 1)];
        }
    }

    /* 1 KiB RAM (mirrored) */
    if (addr >= 0x6000 && addr <= 0x7fff)
    {
        return state.ram [addr & (COLECOVISION_RAM_SIZE - 1)];
    }

    /* Cartridge slot */
    if (addr >= 0x8000 && addr <= 0xffff)
    {
        if (state.rom != NULL)
        {
            return state.rom [addr & state.rom_mask];
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
        state.ram [addr & (COLECOVISION_RAM_SIZE - 1)] = data;
    }
}


/*
 * Handle ColecoVision I/O reads.
 */
static uint8_t colecovision_io_read (uint8_t addr)
{
    /* tms9928a */
    if (addr >= 0xa0 && addr <= 0xbf)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* tms9928a Data Register */
            return tms9928a_data_read (vdp_context);
        }
        else
        {
            /* tms9928a Status Flags */
            return tms9928a_status_read (vdp_context);
        }
    }

    /* Controller input */
    else if (addr >= 0xe0 && addr <= 0xff)
    {
        if ((addr & 0x02) == 0)
        {
            if (hw_state.input_mode == COLECOVISION_INPUT_MODE_JOYSTICK)
            {
                return (gamepad_1.state [GAMEPAD_DIRECTION_UP]      ? 0 : BIT_0) |
                       (gamepad_1.state [GAMEPAD_DIRECTION_RIGHT]   ? 0 : BIT_1) |
                       (gamepad_1.state [GAMEPAD_DIRECTION_DOWN]    ? 0 : BIT_2) |
                       (gamepad_1.state [GAMEPAD_DIRECTION_LEFT]    ? 0 : BIT_3) |
                       (                                                  BIT_4) |
                       (                                                  BIT_5) |
                       (gamepad_1.state [GAMEPAD_BUTTON_1]          ? 0 : BIT_6);
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

                return key | BIT_4 | BIT_5 | (gamepad_1.state [GAMEPAD_BUTTON_2] ? 0 : BIT_6);
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
 * Handle ColecoVision I/O writes.
 */
static void colecovision_io_write (uint8_t addr, uint8_t data)
{
    /* Set input to keypad-mode */
    if (addr >= 0x80 && addr <= 0x9f)
    {
        hw_state.input_mode = COLECOVISION_INPUT_MODE_KEYPAD;
    }

    /* tms9928a */
    if (addr >= 0xa0 && addr <= 0xbf)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* VDP Data Register */
            tms9928a_data_write (vdp_context, data);
        }
        else
        {
            /* VDP Control Register */
            tms9928a_control_write (vdp_context, data);
        }
    }

    /* Set input to joystick-mode */
    if (addr >= 0xc0 && addr <= 0xdf)
    {
        hw_state.input_mode = COLECOVISION_INPUT_MODE_JOYSTICK;
    }

    /* PSG */
    if (addr >= 0xe0 && addr <= 0xff)
    {
        sn76489_data_write (data);
    }
}


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
 * Returns the ColecoVision clock-rate in Hz.
 */
static uint32_t colecovision_get_clock_rate ()
{
    if (state.format == VIDEO_FORMAT_PAL)
    {
        return COLECOVISION_CLOCK_RATE_PAL;
    }

    return COLECOVISION_CLOCK_RATE_NTSC;
}


/*
 * Emulate the ColecoVision for the specified length of time.
 */
static void colecovision_run (uint32_t ms)
{
    /* TODO: Make these calculations common */
    static uint64_t millicycles = 0;
    uint64_t lines;

    pthread_mutex_lock (&colecovision_state_mutex);

    millicycles += (uint64_t) ms * colecovision_get_clock_rate ();
    lines = millicycles / 228000;
    millicycles -= lines * 228000;

    while (lines--)
    {
        assert (lines >= 0);

        /* 228 CPU cycles per scanline */
        z80_run_cycles (z80_context, 228 + state.overclock);
        psg_run_cycles (228);
        tms9928a_run_one_scanline (vdp_context);
    }

    pthread_mutex_unlock (&colecovision_state_mutex);
}


/*
 * Maskable interrupt line is not used.
 */
static bool colecovision_get_int (void *ptr)
{
    return false;
}


/*
 * Returns true if there is a non-maskable interrupt.
 */
static bool colecovision_get_nmi (void *ptr)
{
    return tms9928a_get_interrupt (vdp_context);
}


/*
 * Export ColecoVision state to a file.
 */
static void colecovision_state_save (const char *filename)
{
    pthread_mutex_lock (&colecovision_state_mutex);

    /* Begin creating a new save state. */
    save_state_begin (CONSOLE_ID_COLECOVISION);

    save_state_section_add (SECTION_ID_COLECOVISION_HW, 1, sizeof (hw_state), &hw_state);

    z80_state_save (z80_context);
    save_state_section_add (SECTION_ID_RAM, 1, COLECOVISION_RAM_SIZE, state.ram);

    tms9928a_state_save (vdp_context);
    save_state_section_add (SECTION_ID_VRAM, 1, TMS9928A_VRAM_SIZE, vdp_context->vram);

    sn76489_state_save ();

    pthread_mutex_unlock (&colecovision_state_mutex);
    save_state_write (filename);
}


/*
 * Import ColecoVision state from a file.
 */
static void colecovision_state_load (const char *filename)
{
    const char *console_id;
    uint32_t sections_loaded;

    if (load_state_begin (filename, &console_id, &sections_loaded) == -1)
    {
        return;
    }

    pthread_mutex_lock (&colecovision_state_mutex);


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
            z80_state_load (z80_context, version, size, data);
        }
        else if (!strncmp (section_id, SECTION_ID_RAM, 4))
        {
            if (size == COLECOVISION_RAM_SIZE)
            {
                memcpy (state.ram, data, COLECOVISION_RAM_SIZE);
            }
            else
            {
                snepulator_error ("Error", "Save-state contains incorrect RAM size");
            }
        }
        else if (!strncmp (section_id, SECTION_ID_VDP, 4))
        {
            tms9928a_state_load (vdp_context, version, size, data);
        }
        else if (!strncmp (section_id, SECTION_ID_VRAM, 4))
        {
            if (size == TMS9928A_VRAM_SIZE)
            {
                memcpy (vdp_context->vram, data, TMS9928A_VRAM_SIZE);
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

    pthread_mutex_unlock (&colecovision_state_mutex);

    load_state_end ();
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
        snepulator_error ("Error", "Unable to allocate Colecovision RAM.");
    }

    /* Load BIOS */
    if (state.colecovision_bios_filename)
    {
        if (snepulator_load_rom (&state.bios, &state.bios_size, state.colecovision_bios_filename) == -1)
        {
            return;
        }
        fprintf (stdout, "%d KiB BIOS %s loaded.\n", state.bios_size >> 10, state.colecovision_bios_filename);
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
    z80_context = z80_init (NULL,
                            colecovision_memory_read, colecovision_memory_write,
                            colecovision_io_read, colecovision_io_write,
                            colecovision_get_int, colecovision_get_nmi);

    vdp_context = tms9928a_init (NULL, colecovision_frame_done);
    vdp_context->render_start_x = VIDEO_SIDE_BORDER;
    vdp_context->render_start_y = (VIDEO_BUFFER_LINES - 192) / 2;
    vdp_context->video_start_x = vdp_context->render_start_x;
    vdp_context->video_start_y = vdp_context->render_start_y;
    vdp_context->remove_sprite_limit = state.remove_sprite_limit;

    sn76489_init ();

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
    state.run_callback = colecovision_run;
    state.state_save = colecovision_state_save;
    state.state_load = colecovision_state_load;

    /* Begin emulation */
    state.run = RUN_STATE_RUNNING;
}
