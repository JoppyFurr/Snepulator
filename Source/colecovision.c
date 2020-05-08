/*
 * ColecoVision implementation
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "util.h"
#include "snepulator.h"

#include "cpu/z80.h"
#include "video/tms9918a.h"
#include "sound/sn76489.h"

#include "colecovision.h"

extern Snepulator_State state;

#define COLECOVISION_RAM_SIZE (1 << 10)


static ColecoVision_Input_Mode colecovision_input_mode = COLECOVISION_INPUT_MODE_JOYSTICK;

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
        if (addr & 0x02)
        {
            if (colecovision_input_mode == COLECOVISION_INPUT_MODE_JOYSTICK)
            {
                return (state.gamepad_1.up        ? 0 : BIT_0) |
                       (state.gamepad_1.right     ? 0 : BIT_1) |
                       (state.gamepad_1.down      ? 0 : BIT_2) |
                       (state.gamepad_1.left      ? 0 : BIT_3) |
                       (                                BIT_4) |
                       (                                BIT_5) |
                       (state.gamepad_1.button_1  ? 0 : BIT_6);
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

                return key | BIT_4 | BIT_5 | (state.gamepad_1.button_2  ? 0 : BIT_6);
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
        colecovision_input_mode = COLECOVISION_INPUT_MODE_KEYPAD;
    }

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

    /* Set input to joystick-mode */
    if (addr >= 0xc0 && addr <= 0xdf)
    {
        colecovision_input_mode = COLECOVISION_INPUT_MODE_JOYSTICK;
    }

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
 * Maskable interrupt line is not used.
 */
static bool colecovision_get_int (void)
{
    return false;
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
    state.get_int = colecovision_get_int;
    state.get_nmi = tms9918a_get_interrupt;
    state.run = colecovision_run;

    /* Begin emulation */
    state.ready = true;
    state.running = true;
}
