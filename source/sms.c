/*
 * Sega Master System implementation.
 */

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "util.h"
#include "snepulator.h"
#include "database/sms_db.h"
#include "save_state.h"

#include "gamepad.h"
#include "cpu/z80.h"
#include "video/tms9928a.h"
#include "video/sms_vdp.h"
#include "sound/sn76489.h"
#include "sms.h"

extern Snepulator_State state;
extern Snepulator_Gamepad gamepad_1;
extern Snepulator_Gamepad gamepad_2;
extern SN76489_State sn76489_state;
extern pthread_mutex_t video_mutex;

static Z80_Context *z80_context = NULL;
static TMS9928A_Context *vdp_context = NULL;


#define SMS_RAM_SIZE SIZE_8K
#define SMS_SRAM_SIZE SIZE_8K

/* 0: Output, 1: Input */
#define SMS_IO_TR_A_DIRECTION (1 << 0)
#define SMS_IO_TH_A_DIRECTION (1 << 1)
#define SMS_IO_TR_B_DIRECTION (1 << 2)
#define SMS_IO_TH_B_DIRECTION (1 << 3)

/* 0: Low, 1: High */
#define SMS_IO_TR_A_LEVEL (1 << 4)
#define SMS_IO_TH_A_LEVEL (1 << 5)
#define SMS_IO_TR_B_LEVEL (1 << 6)
#define SMS_IO_TH_B_LEVEL (1 << 7)

#define SMS_MEMORY_CTRL_BIOS_DISABLE 0x08
#define SMS_MEMORY_CTRL_CART_DISABLE 0x40
#define SMS_MEMORY_CTRL_IO_DISABLE   0x04

static pthread_mutex_t sms_state_mutex = PTHREAD_MUTEX_INITIALIZER;


/* Console hardware state */
typedef struct SMS_HW_State_s {
    uint8_t memory_control;
    uint8_t io_control;
    uint8_t mapper;
    uint8_t mapper_bank [3];
    bool sram_enable;
} SMS_HW_State;

static SMS_HW_State hw_state = {
    .memory_control = 0x00,
    .io_control = 0x00,
    .mapper = SMS_MAPPER_UNKNOWN,
    .mapper_bank = { 0x00, 0x01, 0x02 },
    .sram_enable = false
};

static bool export_paddle = false;
SMS_3D_Field sms_3d_field = SMS_3D_FIELD_NONE;
static bool sram_used = false;


/*
 * Declarations.
 */
static uint8_t  sms_io_read (uint8_t addr);
static void     sms_io_write (uint8_t addr, uint8_t data);
static uint8_t  sms_memory_read (uint16_t addr);
static void     sms_memory_write (uint16_t addr, uint8_t data);
static void     sms_process_3d_field (TMS9928A_Context *context);
static void     sms_run (uint32_t ms);
static void     sms_state_load (const char *filename);
static void     sms_state_save (const char *filename);
static void     sms_sync (void);

/*
 * Callback to supply SDL with audio frames.
 */
static void sms_audio_callback (void *userdata, uint8_t *stream, int len)
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
static void sms_cleanup (void)
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
static void sms_frame_done (void *ptr)
{
    if (state.console == CONSOLE_GAME_GEAR)
    {
        /* Only keep the LCD area */
        for (uint32_t y = 0; y < VIDEO_BUFFER_LINES; y++)
        {
            for (uint32_t x = 0; x < VIDEO_BUFFER_WIDTH; x++)
            {
                if ((x >= vdp_context->video_start_x) && (x < vdp_context->video_start_x + vdp_context->video_width) &&
                    (y >= vdp_context->video_start_y) && (y < vdp_context->video_start_y + vdp_context->video_height))
                {
                    continue;
                }
                vdp_context->frame_buffer [x + y * VIDEO_BUFFER_WIDTH] = (float_Colour) { .r = 0.0, .g = 0.0, .b = 0.0 };
            }
        }
    }

    pthread_mutex_lock (&video_mutex);

    if (sms_3d_field != SMS_3D_FIELD_NONE)
    {
        sms_process_3d_field (vdp_context);
    }
    else
    {
        memcpy (state.video_out_data, vdp_context->frame_buffer, sizeof (vdp_context->frame_buffer));
    }

    state.video_start_x     = vdp_context->video_start_x;
    state.video_start_y     = vdp_context->video_start_y;
    state.video_width       = vdp_context->video_width;
    state.video_height      = vdp_context->video_height;
    state.video_blank_left  = vdp_context->video_blank_left;

    pthread_mutex_unlock (&video_mutex);
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
 * Returns true if there is an interrupt.
 */
static bool sms_get_int (void *ptr)
{
    return sms_vdp_get_interrupt (vdp_context);
}


/*
 * Returns true if there is a non-maskable interrupt.
 */
static bool sms_get_nmi (void *ptr)
{
    if (state.console == CONSOLE_MASTER_SYSTEM)
    {
        return !! gamepad_1.state [GAMEPAD_BUTTON_START];
    }

    return false;
}


/*
 * Reset the SMS and load a new BIOS and/or cartridge ROM.
 */
void sms_init (void)
{
    /* Reset the mapper */
    hw_state.mapper = SMS_MAPPER_UNKNOWN;
    hw_state.mapper_bank [0] = 0;
    hw_state.mapper_bank [1] = 1;
    hw_state.mapper_bank [2] = 2;
    hw_state.sram_enable = false;
    sram_used = false;

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
    if (state.console == CONSOLE_MASTER_SYSTEM && state.sms_bios_filename)
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
        else if (state.rom_hints & SMS_HINT_LIGHT_PHASER)
        {
            gamepad_1.type = GAMEPAD_TYPE_SMS_PHASER;
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
    z80_context = z80_init (NULL,
                            sms_memory_read, sms_memory_write,
                            sms_io_read, sms_io_write,
                            sms_get_int, sms_get_nmi);

    vdp_context = sms_vdp_init (NULL, sms_frame_done, state.console);
    vdp_context->render_start_x = VIDEO_SIDE_BORDER;
    vdp_context->render_start_y = (VIDEO_BUFFER_LINES - 192) / 2;
    vdp_context->remove_sprite_limit = state.remove_sprite_limit; /* TODO: Update the value when the setting is changed. */
    vdp_context->disable_blanking = state.disable_blanking;

    if (state.console == CONSOLE_GAME_GEAR)
    {
        vdp_context->video_start_x = vdp_context->render_start_x + 48;
        vdp_context->video_start_y = vdp_context->render_start_y + 24;
    }
    else
    {
        vdp_context->video_start_x = vdp_context->render_start_x;
        vdp_context->video_start_y = vdp_context->render_start_y;
    }

    if (state.rom_hints & SMS_HINT_SMS1_VDP)
    {
        vdp_context->sms1_vdp_hint = true;
    }

    sn76489_init ();

    /* Initial video parameters */
    state.video_start_x = vdp_context->video_start_x;
    state.video_start_y = vdp_context->video_start_y;
    state.video_width   = vdp_context->video_width;
    state.video_height  = vdp_context->video_height;
    if (state.console == CONSOLE_GAME_GEAR)
    {
        state.video_has_border = false;
    }
    else
    {
        state.video_has_border = true;
    }

    /* Hook up callbacks */
    state.audio_callback = sms_audio_callback;
    state.cleanup = sms_cleanup;
    state.get_clock_rate = sms_get_clock_rate;
    state.sync = sms_sync;
    state.run_callback = sms_run;
    state.state_save = sms_state_save;
    state.state_load = sms_state_load;

    /* Minimal alternative to the BIOS */
    if (state.bios == NULL)
    {
        /* Z80 interrupt mode and stack pointer */
        z80_context->state.im = 1;
        z80_context->state.sp = 0xdff0;

        hw_state.memory_control |= SMS_MEMORY_CTRL_BIOS_DISABLE;

        /* Leave the VDP in Mode4 */
        sms_vdp_control_write (vdp_context, SMS_VDP_CTRL_0_MODE_4);
        sms_vdp_control_write (vdp_context, TMS9928A_CODE_REG_WRITE | 0x00);

        /* Line counter starts at 0xff */
        sms_vdp_control_write (vdp_context, 0xff);
        sms_vdp_control_write (vdp_context, TMS9928A_CODE_REG_WRITE | 0x0a);
    }

    /* Begin emulation */
    state.run = RUN_STATE_RUNNING;
}


/*
 * Handle SMS I/O reads.
 */
static uint8_t sms_io_read (uint8_t addr)
{
    /* Game Gear specific registers */
    if (addr <= 0x06 && state.console == CONSOLE_GAME_GEAR)
    {
        if (addr == 0x00)
        {
            uint8_t value = 0;

            /* STT */
            if (!gamepad_1.state [GAMEPAD_BUTTON_START])
            {
                value |= BIT_7;
            }

            /* NJAP */
            if (state.region == REGION_WORLD)
            {
                value |= BIT_6;
            }

            /* NNTS */
            if (state.format == VIDEO_FORMAT_PAL)
            {
                value |= BIT_5;
            }

            return value;
        }
        else if (addr == 0x05)
        {
            /* Gear-to-Gear Status */
            return 0x00;
        }
    }

    /* VDP Counters */
    else if (addr >= 0x40 && addr <= 0x7f)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* V Counter */
            return sms_vdp_get_v_counter (vdp_context);
        }
        else
        {
            /* H Counter */
            return sms_vdp_get_h_counter (vdp_context);
        }
    }

    /* VDP */
    else if (addr >= 0x80 && addr <= 0xbf)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* VDP Data Register */
            return sms_vdp_data_read (vdp_context);
        }
        else
        {
            /* VDP Status Flags */
            return sms_vdp_status_read (vdp_context);
        }
    }

    /* Controller inputs */
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
                    if ((hw_state.io_control & SMS_IO_TH_A_DIRECTION) == 0 && (hw_state.io_control & SMS_IO_TH_A_LEVEL))
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
                if ((hw_state.io_control & SMS_IO_TH_A_DIRECTION) == 0)
                {
                    port_1_th = !(hw_state.io_control & SMS_IO_TH_A_LEVEL);

                    if (gamepad_1.type == GAMEPAD_TYPE_SMS_PADDLE)
                    {
                        export_paddle = true;
                    }

                }

                if ((hw_state.io_control & SMS_IO_TH_B_DIRECTION) == 0)
                {
                    port_2_th = !(hw_state.io_control & SMS_IO_TH_B_LEVEL);
                }
            }

            if (gamepad_1.type == GAMEPAD_TYPE_SMS_PHASER)
            {
                port_1_th |= sms_vdp_get_phaser_th (vdp_context, z80_context->cycle_count);
            }

            /* I/O Port B/misc */
            return (gamepad_2.state [GAMEPAD_DIRECTION_LEFT]    ? 0 : BIT_0) |
                   (gamepad_2.state [GAMEPAD_DIRECTION_RIGHT]   ? 0 : BIT_1) |
                   (gamepad_2.state [GAMEPAD_BUTTON_1]          ? 0 : BIT_2) |
                   (gamepad_2.state [GAMEPAD_BUTTON_2]          ? 0 : BIT_3) |
                   (/* TODO: RESET */                         0 ? 0 : BIT_4) |
                   (port_1_th                                   ? 0 : BIT_6) |
                   (port_2_th                                   ? 0 : BIT_7);
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
    if (addr <= 0x06 && state.console == CONSOLE_GAME_GEAR)
    {
        if (addr == 0x06)
        {
            /* Stereo sound register */
            sn76489_state.gg_stereo = data;
        }
    }

    else if (addr >= 0x00 && addr <= 0x3f)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* Memory Control Register */
            hw_state.memory_control = data;
        }
        else
        {
            /* I/O Control Register */
            hw_state.io_control = data;
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
            sms_vdp_data_write (vdp_context, data);
        }
        else
        {
            /* VDP Control Register */
            sms_vdp_control_write (vdp_context, data);
        }
    }

    /* Minimal SDSC Debug Console */
    if (addr == 0xfd && (hw_state.memory_control & 0x04))
    {
        fprintf (stdout, "%c", data);
        fflush (stdout);
    }
}


/*
 * Handle SMS memory reads.
 */
static uint8_t sms_memory_read (uint16_t addr)
{
    /* Cartridge, card, BIOS, expansion slot */
    if (addr >= 0x0000 && addr <= 0xbfff)
    {
        uint8_t slot = (addr >> 14);
        uint32_t bank_base = hw_state.mapper_bank [slot] * ((uint32_t) 16 << 10);
        uint16_t offset    = addr & 0x3fff;

        /* The first 1 KiB of slot 0 is not affected by mapping */
        if (slot == 0 && offset < (1 << 10))
        {
            bank_base = 0;
        }

        /* BIOS */
        if (state.bios != NULL && !(hw_state.memory_control & SMS_MEMORY_CTRL_BIOS_DISABLE))
        {
            /* Assumes a power-of-two BIOS size */
            return state.bios [(bank_base + offset) & (state.bios_size - 1)];
        }

        /* On-cartridge SRAM */
        if (hw_state.sram_enable && slot == 2)
        {
            return state.sram [offset & (SMS_SRAM_SIZE - 1)];
        }

        /* Cartridge ROM */
        if (state.rom != NULL && !(hw_state.memory_control & SMS_MEMORY_CTRL_CART_DISABLE))
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
        /* Only accept a right-eye field if we have first seen a left-eye field.
         * This avoids a false-positive if games initialise the register to zero. */
        else if (sms_3d_field == SMS_3D_FIELD_LEFT)
        {
            sms_3d_field = SMS_3D_FIELD_RIGHT;
        }
    }

    if (hw_state.mapper == SMS_MAPPER_UNKNOWN)
    {
        if (addr == 0xfffc || addr == 0xfffd ||
            addr == 0xfffe || addr == 0xffff)
        {
            hw_state.mapper = SMS_MAPPER_SEGA;
        }
        else if (addr == 0x4000 || addr == 0x8000)
        {
            hw_state.mapper = SMS_MAPPER_CODEMASTERS;
        }
        else if (addr == 0xa000)
        {
            hw_state.mapper = SMS_MAPPER_KOREAN;
        }
    }

    if (hw_state.mapper == SMS_MAPPER_SEGA)
    {
        if (addr == 0xfffc)
        {
            hw_state.sram_enable = (data & BIT_3) ? true : false;

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
            hw_state.mapper_bank [0] = data & 0x3f;
        }
        else if (addr == 0xfffe)
        {
            hw_state.mapper_bank [1] = data & 0x3f;
        }
        else if (addr == 0xffff)
        {
            hw_state.mapper_bank [2] = data & 0x3f;
        }
    }

    if (hw_state.mapper == SMS_MAPPER_CODEMASTERS)
    {
        /* TODO: There are differences from the Sega mapper. Do any games rely on them?
         *  1. Initial banks are different (0, 1, 0) instead of (0, 1, 2).
         *  2. The first 1KB is not protected.
         */
        if (addr == 0x0000)
        {
            hw_state.mapper_bank [0] = data & 0x3f;
        }
        else if (addr == 0x4000)
        {
            hw_state.mapper_bank [1] = data & 0x3f;
        }
        else if (addr == 0x8000)
        {
            hw_state.mapper_bank [2] = data & 0x3f;

            if (data & BIT_7)
            {
                snepulator_error ("Error", "Codemasters SRAM not implemented.");
            }
        }
    }

    if (hw_state.mapper == SMS_MAPPER_KOREAN)
    {
        if (addr == 0xa000)
        {
            hw_state.mapper_bank [2] = data & 0x3f;
        }
    }

    /* On-cartridge SRAM */
    if (hw_state.sram_enable && addr >= 0x8000 && addr <= 0xbfff)
    {
        state.sram [addr & (SMS_SRAM_SIZE - 1)] = data;
        sram_used = true;
    }

    /* RAM + mirror */
    if (addr >= 0xc000 && addr <= 0xffff)
    {
        state.ram [addr & (SMS_RAM_SIZE - 1)] = data;
    }
}


/*
 * Process the new 3d field to update the anaglyph output image.
 */
static void sms_process_3d_field (TMS9928A_Context *context)
{
    bool update_red = false;
    bool update_green = false;
    bool update_blue = false;
    float_Colour pixel;

    switch (state.video_3d_mode)
    {
        case VIDEO_3D_LEFT_ONLY:
            if (sms_3d_field == SMS_3D_FIELD_LEFT)
            {
                update_red = true;
                update_green = true;
                update_blue = true;
            }
            break;

        case VIDEO_3D_RIGHT_ONLY:
            if (sms_3d_field == SMS_3D_FIELD_RIGHT)
            {
                update_red = true;
                update_green = true;
                update_blue = true;
            }
            break;

        case VIDEO_3D_RED_CYAN:
            if (sms_3d_field == SMS_3D_FIELD_LEFT)
            {
                update_red = true;
            }
            else
            {
                update_green = true;
                update_blue = true;
            }
            break;

        case VIDEO_3D_RED_GREEN:
            if (sms_3d_field == SMS_3D_FIELD_LEFT)
            {
                update_red = true;
            }
            else
            {
                update_green = true;
            }
            break;

        case VIDEO_3D_MAGENTA_GREEN:
            if (sms_3d_field == SMS_3D_FIELD_LEFT)
            {
                update_red = true;
                update_blue = true;
            }
            else
            {
                update_green = true;
            }
            break;
    }

    for (uint32_t i = 0; i < (VIDEO_BUFFER_WIDTH * VIDEO_BUFFER_LINES); i++)
    {
        pixel = colour_saturation (context->frame_buffer [i], state.video_3d_saturation);

        if (update_red)
        {
            state.video_out_data [i].r = pixel.r;
        }
        if (update_green)
        {
            state.video_out_data [i].g = pixel.g;
        }
        if (update_blue)
        {
            state.video_out_data [i].b = pixel.b;
        }

        /* Special case where blue is not used */
        if (state.video_3d_mode == VIDEO_3D_RED_GREEN)
        {
            state.video_out_data [i].b = 0.0;
        }
    }
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

    pthread_mutex_lock (&sms_state_mutex);

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
        z80_run_cycles (z80_context, 228 + state.overclock);
        psg_run_cycles (228);
        sms_vdp_run_one_scanline (vdp_context);
    }

    pthread_mutex_unlock (&sms_state_mutex);
}


/*
 * Import SMS state from a file.
 */
static void sms_state_load (const char *filename)
{
    const char *console_id;
    uint32_t sections_loaded;

    if (load_state_begin (filename, &console_id, &sections_loaded) == -1)
    {
        return;
    }

    pthread_mutex_lock (&sms_state_mutex);

    if (!strncmp (console_id, CONSOLE_ID_SMS, 4))
    {
        state.console = CONSOLE_MASTER_SYSTEM;
    }
    else if (!strncmp (console_id, CONSOLE_ID_GAME_GEAR, 4))
    {
        state.console = CONSOLE_GAME_GEAR;
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

        if (!strncmp (section_id, SECTION_ID_SMS_HW, 4))
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
            if (size == SMS_RAM_SIZE)
            {
                memcpy (state.ram, data, SMS_RAM_SIZE);
            }
            else
            {
                snepulator_error ("Error", "Save-state contains incorrect RAM size");
            }
        }
        else if (!strncmp (section_id, SECTION_ID_SRAM, 4))
        {
            sram_used = true;
            if (size == SMS_SRAM_SIZE)
            {
                memcpy (state.sram, data, SMS_SRAM_SIZE);
            }
            else
            {
                snepulator_error ("Error", "Save-state contains incorrect SRAM size");
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

    pthread_mutex_unlock (&sms_state_mutex);

    load_state_end ();
}


/*
 * Export SMS state to a file.
 */
static void sms_state_save (const char *filename)
{
    pthread_mutex_lock (&sms_state_mutex);

    /* Begin creating a new save state. */
    if (state.console == CONSOLE_GAME_GEAR)
    {
        save_state_begin (CONSOLE_ID_GAME_GEAR);
    }
    else
    {
        save_state_begin (CONSOLE_ID_SMS);
    }

    save_state_section_add (SECTION_ID_SMS_HW, 1, sizeof (hw_state), &hw_state);

    z80_state_save (z80_context);
    save_state_section_add (SECTION_ID_RAM, 1, SMS_RAM_SIZE, state.ram);
    if (sram_used)
    {
        save_state_section_add (SECTION_ID_SRAM, 1, SMS_SRAM_SIZE, state.sram);
    }

    tms9928a_state_save (vdp_context);
    save_state_section_add (SECTION_ID_VRAM, 1, TMS9928A_VRAM_SIZE, vdp_context->vram);

    sn76489_state_save ();

    pthread_mutex_unlock (&sms_state_mutex);

    save_state_write (filename);
}


/*
 * Backup the on-cartridge SRAM.
 */
static void sms_sync (void)
{
    if (sram_used)
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
