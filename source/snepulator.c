/*
 * Snepulator
 * Main OS-independent code
 */

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "snepulator.h"
#include "util.h"
#include "config.h"
#include "database/sms_db.h"

#include "cpu/m68k.h"
#include "cpu/z80.h"
#include "video/tms9928a.h"
#include "video/sms_vdp.h"
#include "video/smd_vdp.h"
#include "sound/band_limit.h"
#include "sound/sn76489.h"
#include "sound/ym2413.h"
#include "colecovision.h"
#include "logo.h"
#include "sg-1000.h"
#include "sms.h"
#include "smd.h"
#include "vgm_player.h"
#include "midi_player.h"

/* Images */
#include "../images/snepulator_paused.c"

#include "gamepad.h"
extern Snepulator_State state;
extern Snepulator_Gamepad gamepad [3];


/*
 * Set and run a console BIOS.
 *
 * The console is detected by file extension.
 */
void snepulator_bios_set (const char *path)
{
    snepulator_reset ();

    if (state.cart_filename != NULL)
    {
        free (state.cart_filename);
        state.cart_filename = NULL;
    }

    state.console = snepulator_select_console_for_rom (path);

    switch (state.console)
    {
        case CONSOLE_COLECOVISION:
            if (state.colecovision_bios_filename != NULL)
            {
                free (state.colecovision_bios_filename);
            }
            /* Store and use the BIOS */
            config_string_set ("colecovision", "bios", path);
            config_write ();

            state.colecovision_bios_filename = strdup (path);
            state.console_context = colecovision_init ();
            break;
        case CONSOLE_MASTER_SYSTEM:
        default:
            if (state.sms_bios_filename != NULL)
            {
                free (state.sms_bios_filename);
            }
            /* Store and use the BIOS */
            config_string_set ("sms", "bios", path);
            config_write ();

            state.sms_bios_filename = strdup (path);
            state.console_context = sms_init ();
            break;
    }
}


/*
 * Import settings from configuration from file.
 */
int snepulator_config_import (void)
{
    char *string = NULL;
    uint32_t uint = 0;

    config_read ();

    /* SMS Region - Defaults to World */
    state.region = REGION_WORLD;
    if (config_string_get ("sms", "region", &string) == 0)
    {
        if (strcmp (string, "Japan") == 0)
        {
            state.region = REGION_JAPAN;
        }
    }

    /* Video Format - Defaults to Auto */
    state.format = VIDEO_FORMAT_NTSC;
    state.format_auto = true;
    if (config_string_get ("sms", "format", &string) == 0)
    {
        if (strcmp (string, "PAL") == 0)
        {
            state.format = VIDEO_FORMAT_PAL;
            state.format_auto = false;
        }
        else if (strcmp (string, "NTSC") == 0)
        {
            state.format = VIDEO_FORMAT_NTSC;
            state.format_auto = false;
        }
    }

    /* SMS FM Sound */
    state.fm_sound = false;
    if (config_uint_get ("sms", "fm-sound", &uint) == 0)
    {
        state.fm_sound = uint;
    }

    /* Overclock - Defaults to off */
    state.overclock = 0;
    if (config_uint_get ("hacks", "overclock", &uint) == 0)
    {
        state.overclock = uint;
    }

    /* Remove Sprite Limit - Defaults to off */
    state.remove_sprite_limit = false;
    if (config_uint_get ("hacks", "remove-sprite-limit", &uint) == 0)
    {
        state.remove_sprite_limit = uint;
    }

    /* Disable blanking - Defaults to off */
    state.disable_blanking = false;
    if (config_uint_get ("hacks", "disable-blanking", &uint) == 0)
    {
        state.disable_blanking = uint;
    }

    /* Video Integer Scaling - Defaults to true */
    state.integer_scaling = true;
    if (config_uint_get ("video", "integer-scaling", &uint) == 0)
    {
        state.integer_scaling = uint;
    }

    /* Video pixel aspect ratio - Defaults to 1:1 */
    state.video_par_setting = VIDEO_PAR_1_1;
    state.video_par = 1.0;
    if (config_string_get ("video", "pixel-aspect-ratio", &string) == 0)
    {
        if (strcmp (string, "8:7") == 0)
        {
            state.video_par_setting = VIDEO_PAR_8_7;
            state.video_par = 8.0 / 7.0;
        }
        else if (strcmp (string, "6:5") == 0)
        {
            state.video_par_setting = VIDEO_PAR_6_5;
            state.video_par = 6.0 / 5.0;
        }
        else if (strcmp (string, "18:13") == 0)
        {
            state.video_par_setting = VIDEO_PAR_18_13;
            state.video_par = 18.0 / 13.0;
        }
    }

    /* Video Filter - Defaults to Scanlines */
    state.shader = SHADER_SCANLINES;
    if (config_string_get ("video", "filter", &string) == 0)
    {
        if (strcmp (string, "Dot Matrix") == 0)
        {
            state.shader = SHADER_DOT_MATRIX;
        }
        else if (strcmp (string, "Linear") == 0)
        {
            state.shader = SHADER_LINEAR;
        }
        else if (strcmp (string, "Nearest") == 0)
        {
            state.shader = SHADER_NEAREST;
        }
        else if (strcmp (string, "Nearest-soft") == 0)
        {
            state.shader = SHADER_NEAREST_SOFT;
        }
    }

    /* TMS9928a Palette - Defaults to TMS9928a */
    state.override_tms_palette = tms9928a_palette;
    if (config_string_get ("video", "tms9928a-palette", &string) == 0)
    {
        if (strcmp (string, "Auto") == 0)
        {
            state.override_tms_palette = NULL;
        }
        else if (strcmp (string, "TMS9928a-Uncorrected") == 0)
        {
            state.override_tms_palette = tms9928a_palette_uncorrected;
        }
        else if (strcmp (string, "SMS-Legacy") == 0)
        {
            state.override_tms_palette = sms_vdp_legacy_palette;
        }
    }

    /* 3D Video Mode - Defaults to Red-Cyan */
    state.video_3d_mode = VIDEO_3D_RED_CYAN;
    if (config_string_get ("video", "3d-mode", &string) == 0)
    {
        if (strcmp (string, "Red-Green") == 0)
        {
            state.video_3d_mode = VIDEO_3D_RED_GREEN;
        }
        else if (strcmp (string, "Magenta-Green") == 0)
        {
            state.video_3d_mode = VIDEO_3D_MAGENTA_GREEN;
        }
        else if (strcmp (string, "Left-Only") == 0)
        {
            state.video_3d_mode = VIDEO_3D_LEFT_ONLY;
        }
        else if (strcmp (string, "Right-Only") == 0)
        {
            state.video_3d_mode = VIDEO_3D_RIGHT_ONLY;
        }
    }

    /* 3D Video Colour Saturation - Defaults to 25% */
    state.video_3d_saturation = 0.25;
    if (config_uint_get ("video", "3d-saturation", &uint) == 0)
    {
        state.video_3d_saturation = uint / 100.0;
    }

    /* BIOS Paths */
    if (config_string_get ("sms", "bios", &string) == 0)
    {
        state.sms_bios_filename = strdup (string);
    }
    if (config_string_get ("colecovision", "bios", &string) == 0)
    {
        state.colecovision_bios_filename = strdup (string);
    }

    return 0;
}


/*
 * Clear the screen.
 */
void snepulator_clear_video (void)
{
    pthread_mutex_lock (&state.video_mutex);
    memset (state.video_ring, 0, sizeof (state.video_ring));
    state.video_read_index = 0;
    state.video_write_index = 0;
    pthread_mutex_unlock (&state.video_mutex);
}


/*
 * Set state.console using the ROM file.
 */
Console snepulator_select_console_for_rom (const char *path)
{
    /* If no ROM is selected, display the logo. */
    if (path == NULL)
    {
        return CONSOLE_LOGO;
    }

    /* First priority: Check for a known console-specific file extension */
    char *extension_ptr = strrchr (path, '.');
    if (extension_ptr != NULL)
    {
        char extension[16] = { '\0' };
        for (int i = 0; i < 15 && extension_ptr[i] != '\0'; i++)
        {
            extension [i] = tolower (extension_ptr [i]);
        }

        if (strcmp (extension, ".vgm") == 0 ||
            strcmp (extension, ".vgz") == 0)
        {
            return CONSOLE_VGM_PLAYER;
        }
        else if (strcmp (extension, ".mid") == 0 ||
                 strcmp (extension, ".midi") == 0 ||
                 strcmp (extension, ".smf") == 0)
        {
            return CONSOLE_MIDI_PLAYER;
        }
        else if (strcmp (extension, ".col") == 0)
        {
            return CONSOLE_COLECOVISION;
        }
        else if (strcmp (extension, ".sg") == 0)
        {
            return CONSOLE_SG_1000;
        }
        else if (strcmp (extension, ".sms") == 0)
        {
            return CONSOLE_MASTER_SYSTEM;
        }
        else if (strcmp (extension, ".gg") == 0)
        {
            return CONSOLE_GAME_GEAR;
        }
#ifdef DEVELOPER_BUILD
        else if (strcmp (extension, ".md") == 0 ||
                 strcmp (extension, ".gen") == 0)
        {
            /* TODO: Support for .smd roms */
            return CONSOLE_MEGA_DRIVE;
        }
#endif
    }

    /* Second priority: Check if the ROM matches any known headers */
    uint16_t first_word;
    if (util_file_read_bytes ((void *) &first_word, 0, 2, path) == EXIT_SUCCESS)
    {
        if (first_word == 0xaa55 || first_word == 0x55aa)
        {
            return CONSOLE_COLECOVISION;
        }
    }

    /* TODO: Eventually, check for SMS and Super Magic Drive headers.
     *       For now this can be skipped as Master System is the default. */

    /* No clear match - default to Master System */
    return CONSOLE_MASTER_SYSTEM;
}


/*
 * Disable screen blanking when the blanking bit is set.
 */
void snepulator_disable_blanking_set (bool disable_blanking)
{
    state.disable_blanking = disable_blanking;
    config_uint_set ("hacks", "disable-blanking", disable_blanking);

    if (state.update_settings != NULL)
    {
        state.update_settings (state.console_context);
    }

    config_write ();
}


/*
 * Enable the FM Sound Unit for the SMS.
 */
void snepulator_fm_sound_set (bool enable)
{
    state.fm_sound = enable;
    config_uint_set ("sms", "fm-sound", enable);

    if (state.update_settings != NULL)
    {
        state.update_settings (state.console_context);
    }

    config_write ();
}


/*
 * Send a completed frame for display.
 *
 * A buffer is used to avoid skipping frames when
 * the host and emulated console have a similar framerate.
 */
void snepulator_frame_done (uint_pixel *frame)
{
    /* Queue the frame.
     *  -> If there are already two frames queued, replace the last frame.
     *  -> Otherwise, append the new frame to the queue. */
    pthread_mutex_lock (&state.video_mutex);
    if (state.video_write_index < state.video_read_index + 2)
    {
        state.video_write_index++;
    }
    memcpy (state.video_ring [state.video_write_index % 3], frame, sizeof (state.video_ring [0]));
    pthread_mutex_unlock (&state.video_mutex);
}


/*
 * Get a pointer to the currently displayed frame.
 */
uint_pixel *snepulator_get_current_frame (void)
{
    return state.video_ring [state.video_read_index % 3];
}


/*
 * Advance the video_read_index and get a pointer
 * to the new frame.
 */
uint_pixel *snepulator_get_next_frame (void)
{
    pthread_mutex_lock (&state.video_mutex);
    if (state.video_read_index < state.video_write_index)
    {
        state.video_read_index++;
    }
    pthread_mutex_unlock (&state.video_mutex);

    return state.video_ring [state.video_read_index % 3];
}


/*
 * Enable integer scaling.
 *
 * When enabled, the output video resolution is an
 * integer multiple of the console native resolution.
 */
void snepulator_integer_scaling_set (bool integer_scaling)
{
    state.integer_scaling = integer_scaling;
    config_uint_set ("video", "integer-scaling", integer_scaling);

    config_write ();
}


/*
 * Set whether or not to overclock.
 */
void snepulator_overclock_set (bool overclock)
{
    if (overclock)
    {
        /* A 50% overclock */
        state.overclock = 114;
    }
    else
    {
        state.overclock = 0;
    }

    config_uint_set ("hacks", "overclock", state.overclock);

    if (state.update_settings != NULL)
    {
        state.update_settings (state.console_context);
    }

    config_write ();
}


/*
 * Override the palette used for tms9928a modes.
 */
void snepulator_override_tms9928a_palette (uint_pixel *palette)
{
    char *config_string = NULL;

    state.override_tms_palette = palette;

    if (palette == tms9928a_palette)
    {
        config_string = "TMS9928a";
    }
    else if (palette == tms9928a_palette_uncorrected)
    {
        config_string = "TMS9928a-Uncorrected";
    }
    else if (palette == sms_vdp_legacy_palette)
    {
        config_string = "SMS-Legacy";
    }
    else
    {
        config_string = "Auto";
    }

    config_string_set ("video", "tms9928a-palette", config_string);

    if (state.update_settings != NULL)
    {
        state.update_settings (state.console_context);
    }

    config_write ();
}


/*
 * Animate the pause screen.
 */
void snepulator_pause_animate (void)
{
    uint_pixel frame_buffer [VIDEO_BUFFER_WIDTH * VIDEO_BUFFER_LINES];

    /* Each letter overlaps by one pixel */
    uint32_t x_base = VIDEO_BUFFER_WIDTH / 2 - (snepulator_paused.width - 5) / 2;
    uint32_t y_base = VIDEO_BUFFER_LINES / 2 - snepulator_paused.height / 2;
    const uint32_t letter_position [7] = {  0, 23, 46, 69,  92, 115, 138 };

    /* Draw over a greyscale copy of the last-drawn frame */
    memcpy (frame_buffer, state.video_pause_data, sizeof (state.video_ring [0]));

    uint32_t frame_time = util_get_ticks ();
    for (uint32_t letter = 0; letter < 6; letter++)
    {
        uint32_t letter_start = letter_position [letter];
        uint32_t letter_end = letter_position [letter + 1];

        float y_offset = 5.0 * sin (frame_time / -400.0 + letter_start / 20.0);

        for (uint32_t x = letter_start; x < letter_end; x++)
        {
            for (uint32_t y = 0; y < snepulator_paused.height; y++)
            {
                /* Treat black as transparent */
                if (snepulator_paused.pixel_data [(x + y * snepulator_paused.width) * 3 + 0] == 0)
                {
                    continue;
                }

                frame_buffer [(x + x_base - letter) + (uint32_t) (y + y_base + y_offset) * VIDEO_BUFFER_WIDTH].r =
                    snepulator_paused.pixel_data [(x + y * snepulator_paused.width) * 3 + 0];
                frame_buffer [(x + x_base - letter) + (uint32_t) (y + y_base + y_offset) * VIDEO_BUFFER_WIDTH].g =
                    snepulator_paused.pixel_data [(x + y * snepulator_paused.width) * 3 + 1];
                frame_buffer [(x + x_base - letter) + (uint32_t) (y + y_base + y_offset) * VIDEO_BUFFER_WIDTH].b =
                    snepulator_paused.pixel_data [(x + y * snepulator_paused.width) * 3 + 2];
            }
        }
    }

    snepulator_frame_done (frame_buffer);
}


/*
 * Pause or resume emulation.
 *
 * Will have no effect if the state is INIT or EXIT.
 */
void snepulator_pause_set (bool pause)
{
    /* Pause */
    if (pause == true && state.run == RUN_STATE_RUNNING)
    {
        state.run = RUN_STATE_PAUSED;

        /* Because emulation is paused, we don't need to lock.
         * No new frames will come from the emulated console. */
        uint_pixel *current_frame = snepulator_get_current_frame ();

        /* Convert the screen to black and white, and sore in the pause buffer */
        for (int x = 0; x < (VIDEO_BUFFER_WIDTH * VIDEO_BUFFER_LINES); x++)
        {
            state.video_pause_data [x] = util_to_greyscale (current_frame [x]);
        }

        snepulator_frame_done (state.video_pause_data);
    }

    /* Un-pause */
    else if (pause == false && state.run == RUN_STATE_PAUSED)
    {
        state.run = RUN_STATE_RUNNING;
    }
}


/*
 * Set the console region.
 */
void snepulator_region_set (Console_Region region)
{
    if (region == REGION_WORLD)
    {
        state.region = REGION_WORLD;
        config_string_set ("sms", "region", "World");
    }
    else if (region == REGION_JAPAN)
    {
        state.region = REGION_JAPAN;
        config_string_set ("sms", "region", "Japan");
    }

    if (state.update_settings != NULL)
    {
        state.update_settings (state.console_context);
    }

    config_write ();
}


/*
 * Set whether or not to remove the sprite limit.
 */
void snepulator_remove_sprite_limit_set (bool remove_sprite_limit)
{
    state.remove_sprite_limit = remove_sprite_limit;
    config_uint_set ("hacks", "remove-sprite-limit", remove_sprite_limit);

    if (state.update_settings != NULL)
    {
        state.update_settings (state.console_context);
    }

    config_write ();
}


/*
 * Clear the state of the currently running system.
 */
void snepulator_reset (void)
{
    /* Stop emulation */
    snepulator_pause_set (true);

    /* Save any battery-backed memory. */
    if (state.sync != NULL)
    {
        state.sync (state.console_context);
    }

    /* Mark the system as not-ready. */
    if (state.run == RUN_STATE_RUNNING || state.run == RUN_STATE_PAUSED)
    {
        state.run = RUN_STATE_INIT;
    }

    /* Don't free resources in the middle of the run_callback */
    pthread_mutex_lock (&state.run_mutex);

    /* Free any console-specific resources */
    if (state.cleanup != NULL)
    {
        state.cleanup (state.console_context);
    }
    if (state.console_context != NULL)
    {
        free (state.console_context);
        state.console_context = NULL;
    }
    state.console = CONSOLE_NONE;

    pthread_mutex_unlock (&state.run_mutex);

    /* Clear callback functions */
    state.audio_callback = NULL;
    state.cleanup = NULL;
    state.get_rom_hash = NULL;
    state.run_callback = NULL;
    state.soft_reset = NULL;
    state.sync = NULL;
    state.state_load = NULL;
    state.state_save = NULL;
    state.update_settings = NULL;
#ifdef DEVELOPER_BUILD
    state.diagnostics_show = NULL;
#endif

    snepulator_clear_video ();

    /* Clear additional video parameters */
    state.video_start_x = VIDEO_SIDE_BORDER;
    state.video_start_y = VIDEO_TOP_BORDER_192;
    state.video_width = 256;
    state.video_height = 192;

    /* Auto format default to NTSC */
    if (state.format_auto)
    {
        state.format = VIDEO_FORMAT_NTSC;
    }

    /* Reset the tick counter */
    state.run_timer = util_get_ticks_us ();
    state.micro_clocks = 0;
    state.clock_rate = 0;
}


/*
 * Set the currently running ROM and initialise the system.
 */
void snepulator_rom_set (const char *path)
{
    /* Do not start running a new ROM while in the error state. */
    if (state.run == RUN_STATE_ERROR)
    {
        return;
    }

    if (state.cart_filename != NULL)
    {
        free (state.cart_filename);
        state.cart_filename = NULL;
    }

    if (path != NULL)
    {
        state.cart_filename = strdup (path);
    }

    Console console = snepulator_select_console_for_rom (state.cart_filename);
    snepulator_system_init (console);
}


/*
 * Run emulation for the specified amount of time.
 */
void snepulator_run (uint32_t cycles)
{
    pthread_mutex_lock (&state.run_mutex);

    if (state.run == RUN_STATE_PAUSED)
    {
        /* Allow the console pause button to unpause emulation */
        static bool pause_prev = false;
        bool pause_now = gamepad [1].state [GAMEPAD_BUTTON_START];

        if (pause_now && !pause_prev)
        {
            snepulator_pause_set (false);
        }
        pause_prev = pause_now;
    }

    if (state.run == RUN_STATE_RUNNING || state.console == CONSOLE_LOGO)
    {
        state.run_callback (state.console_context, cycles);
    }

    pthread_mutex_unlock (&state.run_mutex);

    /* Nothing left to run, return to the INIT state
     * now that the mutex has been unlocked. */
    if (state.run == RUN_STATE_STOP)
    {
        state.run = RUN_STATE_INIT;
        snepulator_rom_set (NULL);
    }
}


/*
 * Load the console state from file.
 */
void snepulator_state_load (void *context, const char *filename)
{
    pthread_mutex_lock (&state.run_mutex);

    if (state.state_load != NULL)
    {
        state.state_load (context, filename);
    }

    pthread_mutex_unlock (&state.run_mutex);
}


/*
 * Save the console state to file.
 */
void snepulator_state_save (void *context, const char *filename)
{
    pthread_mutex_lock (&state.run_mutex);

    if (state.state_load != NULL)
    {
        state.state_save (context, filename);
    }

    pthread_mutex_unlock (&state.run_mutex);
}


/*
 * Call the appropriate initialisation for the chosen ROM
 */
void snepulator_system_init (Console console)
{
    snepulator_reset ();

    /* Lock to ensure that initialization is complete before emulation starts. */
    pthread_mutex_lock (&state.run_mutex);

    state.console = console;

    switch (state.console)
    {
        case CONSOLE_LOGO:
            state.console_context = logo_init ();
            break;

        case CONSOLE_VGM_PLAYER:
            state.console_context = vgm_player_init ();
            break;

        case CONSOLE_MIDI_PLAYER:
            state.console_context = midi_player_init ();
            break;

        case CONSOLE_COLECOVISION:
            state.console_context = colecovision_init ();
            break;

        case CONSOLE_SG_1000:
            state.console_context = sg_1000_init ();
            break;

        case CONSOLE_MASTER_SYSTEM:
        case CONSOLE_GAME_GEAR:
            state.console_context = sms_init ();
            break;

#ifdef DEVELOPER_BUILD
        case CONSOLE_MEGA_DRIVE:
            state.console_context = smd_init ();
            break;
#endif

        default:
            /* Default to Master System */
            state.console_context = sms_init ();
            break;
    }
    pthread_mutex_unlock (&state.run_mutex);
}


/*
 * Set the video 3D mode.
 */
void snepulator_video_3d_mode_set (Video_3D_Mode mode)
{
    if (mode == VIDEO_3D_RED_CYAN)
    {
        state.video_3d_mode = VIDEO_3D_RED_CYAN;
        config_string_set ("video", "3d-mode", "Red-Cyan");
    }
    else if (mode == VIDEO_3D_RED_GREEN)
    {
        state.video_3d_mode = VIDEO_3D_RED_GREEN;
        config_string_set ("video", "3d-mode", "Red-Green");
    }
    else if (mode == VIDEO_3D_MAGENTA_GREEN)
    {
        state.video_3d_mode = VIDEO_3D_MAGENTA_GREEN;
        config_string_set ("video", "3d-mode", "Magenta-Green");
    }
    else if (mode == VIDEO_3D_LEFT_ONLY)
    {
        state.video_3d_mode = VIDEO_3D_LEFT_ONLY;
        config_string_set ("video", "3d-mode", "Left-Only");
    }
    else if (mode == VIDEO_3D_RIGHT_ONLY)
    {
        state.video_3d_mode = VIDEO_3D_RIGHT_ONLY;
        config_string_set ("video", "3d-mode", "Right-Only");
    }

    config_write ();
}


/*
 * Set the video 3D colour saturation.
 */
void snepulator_video_3d_saturation_set (double saturation)
{
    if (saturation >= 0.0 && saturation <= 1.0)
    {
        state.video_3d_saturation = saturation;
        config_uint_set ("video", "3d-saturation", saturation * 100);
    }

    config_write ();
}


/*
 * Set the console video filter.
 */
void snepulator_video_filter_set (Shader shader)
{
    if (shader == SHADER_NEAREST)
    {
        state.shader = SHADER_NEAREST;
        config_string_set ("video", "filter", "Nearest");
    }
    else if (shader == SHADER_NEAREST_SOFT)
    {
        state.shader = SHADER_NEAREST_SOFT;
        config_string_set ("video", "filter", "Nearest-soft");
    }
    else if (shader == SHADER_LINEAR)
    {
        state.shader = SHADER_LINEAR;
        config_string_set ("video", "filter", "Linear");
    }
    else if (shader == SHADER_SCANLINES)
    {
        state.shader = SHADER_SCANLINES;
        config_string_set ("video", "filter", "Scanlines");
    }
    else if (shader == SHADER_DOT_MATRIX)
    {
        state.shader = SHADER_DOT_MATRIX;
        config_string_set ("video", "filter", "Dot Matrix");
    }

    config_write ();
}


/*
 * Set the video format.
 */
void snepulator_video_format_set (Video_Format format)
{
    if (format == VIDEO_FORMAT_NTSC)
    {
        state.format_auto = false;
        state.format = VIDEO_FORMAT_NTSC;
        config_string_set ("sms", "format", "NTSC");
    }
    else if (format == VIDEO_FORMAT_PAL)
    {
        state.format_auto = false;
        state.format = VIDEO_FORMAT_PAL;
        config_string_set ("sms", "format", "PAL");
    }
    else if (format == VIDEO_FORMAT_AUTO)
    {
        state.format_auto = true;
        config_string_set ("sms", "format", "Auto");
    }

    if (state.update_settings != NULL)
    {
        state.update_settings (state.console_context);
    }

    config_write ();
}


/*
 * Set the pixel aspect ratio.
 */
void snepulator_video_par_set (Video_PAR par)
{
    if (par == VIDEO_PAR_1_1)
    {
        state.video_par_setting = par;
        state.video_par = 1.0;
        config_string_set ("video", "pixel-aspect-ratio", "1:1");
    }
    else if (par == VIDEO_PAR_8_7)
    {
        state.video_par_setting = par;
        state.video_par = 8.0 / 7.0;
        config_string_set ("video", "pixel-aspect-ratio", "8:7");
    }
    else if (par == VIDEO_PAR_6_5)
    {
        state.video_par_setting = par;
        state.video_par = 6.0 / 5.0;
        config_string_set ("video", "pixel-aspect-ratio", "6:5");
    }
    else if (par == VIDEO_PAR_18_13)
    {
        state.video_par_setting = par;
        state.video_par = 18.0 / 13.0;
        config_string_set ("video", "pixel-aspect-ratio", "18:13");
    }

    config_write ();
}
