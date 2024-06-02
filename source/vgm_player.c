/*
 * Snepulator
 * VGM Player implementation.
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

#include "sound/band_limit.h"
#include "sound/sn76489.h"
#include "sound/ym2413.h"
#include "vgm_player.h"

/* Colours for visualisation */
static const uint_pixel bright_red    = { .r = 255, .g =   0, .b =   0 };
static const uint_pixel dim_red       = { .r = 170, .g =   0, .b =   0 };
static const uint_pixel bright_yellow = { .r = 255, .g = 255, .b =   0 };
static const uint_pixel dim_yellow    = { .r = 170, .g = 170, .b =   0 };
static const uint_pixel bright_green  = { .r =   0, .g = 255, .b =   0 };
static const uint_pixel dim_green     = { .r =   0, .g = 170, .b =   0 };
static const uint_pixel white         = { .r = 255, .g = 255, .b = 255 };
static const uint_pixel light_grey    = { .r = 170, .g = 170, .b = 170 };
static const uint_pixel dark_grey     = { .r =  85, .g =  85, .b =  85 };

static const uint_pixel colours_peak [15] = { bright_green, bright_green,  bright_green,  bright_green, bright_green,
                                              bright_green, bright_green,  bright_green,  bright_green, bright_green,
                                              bright_green, bright_yellow, bright_yellow, bright_red,   bright_red };

static const uint_pixel colours_base [15] = { dim_green, dim_green,  dim_green,  dim_green, dim_green,
                                              dim_green, dim_green,  dim_green,  dim_green, dim_green,
                                              dim_green, dim_yellow, dim_yellow, dim_red,   dim_red };

extern Snepulator_State state;
extern pthread_mutex_t video_mutex;


/*
 * Callback to supply audio frames.
 */
static void vgm_player_audio_callback (void *context_ptr, int16_t *stream, uint32_t count)
{
    VGM_Player_Context *context = (VGM_Player_Context *) context_ptr;

    if (context->sn76489_clock)
    {
        sn76489_get_samples (context->sn76489_context, stream, count);
    }

    if (context->ym2413_clock)
    {
        ym2413_get_samples (context->ym2413_context, stream, count);
    }
}


/*
 * Clean up structures and free memory.
 */
static void vgm_player_cleanup (void *context_ptr)
{
    VGM_Player_Context *context = (VGM_Player_Context *) context_ptr;

    if (context->sn76489_context != NULL)
    {
        if (context->sn76489_context->bandlimit_context_l)
        {
            free (context->sn76489_context->bandlimit_context_l);
        }
        if (context->sn76489_context->bandlimit_context_r)
        {
            free (context->sn76489_context->bandlimit_context_r);
        }

        free (context->sn76489_context);
        context->sn76489_context = NULL;
    }

    if (context->ym2413_context != NULL)
    {
        free (context->ym2413_context);
        context->ym2413_context = NULL;
    }

    if (context->vgm != NULL)
    {
        free (context->vgm);
        context->vgm = NULL;
    }
}


/*
 * Draw a filled rectangle.
 */
static void draw_rect (VGM_Player_Context *context,
                       uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       uint_pixel colour)
{
    const uint32_t start_x = x + state.video_start_x;
    const uint32_t start_y = y + state.video_start_y;
    const uint32_t end_x = start_x + w;
    const uint32_t end_y = start_y + h;

    for (uint32_t y = start_y; y < end_y; y++)
    {
        for (uint32_t x = start_x; x < end_x; x++)
        {
            context->frame_buffer [x + y * VIDEO_BUFFER_WIDTH] = colour;
        }
    }
}


/*
 * Draw a frame of the visualisation.
 */
static void vgm_player_draw_frame (VGM_Player_Context *context)
{
    uint32_t bar_count = 0;
    uint32_t bar_value [15] = { };

    if (context->sn76489_clock)
    {
        bar_value [bar_count++] = 15 - context->sn76489_context->state.vol_0;
        bar_value [bar_count++] = 15 - context->sn76489_context->state.vol_1;
        bar_value [bar_count++] = 15 - context->sn76489_context->state.vol_2;
        bar_value [bar_count++] = 15 - context->sn76489_context->state.vol_3;
    }

    /* Some ym2413 tunes regularly switch between melody and
     * rhythm modes so always show the higher number of channels */
    if (context->ym2413_clock)
    {
        uint32_t melody_channels = (context->ym2413_context->state.rhythm_mode) ? 6 : 9;

        for (uint32_t channel = 0; channel < melody_channels; channel++)
        {
            uint16_t volume  = context->ym2413_context->state.r30_channel_params [channel].volume;
            uint16_t eg_level = context->ym2413_context->state.carrier [channel].eg_level;
            uint16_t attenuation = volume + (eg_level >> 3);

            bar_value [bar_count++] = (attenuation >= 15) ? 0 : 15 - attenuation;
        }

        if (context->ym2413_context->state.rhythm_mode)
        {
            uint16_t volume;
            uint16_t eg_level;
            uint16_t attenuation;

            /* Bass Drum */
            volume = context->ym2413_context->state.rhythm_volume_bd;
            eg_level = context->ym2413_context->state.carrier [YM2413_BASS_DRUM_CH].eg_level;
            attenuation = volume + (eg_level >> 3);
            bar_value [bar_count++] = (attenuation >= 15) ? 0 : 15 - attenuation;

            /* High Hat */
            volume = context->ym2413_context->state.rhythm_volume_hh;
            eg_level = context->ym2413_context->state.modulator [YM2413_HIGH_HAT_CH].eg_level;
            attenuation = volume + (eg_level >> 3);
            bar_value [bar_count++] = (attenuation >= 15) ? 0 : 15 - attenuation;

            /* Snare Drum */
            volume = context->ym2413_context->state.rhythm_volume_sd;
            eg_level = context->ym2413_context->state.carrier [YM2413_SNARE_DRUM_CH].eg_level;
            attenuation = volume + (eg_level >> 3);
            bar_value [bar_count++] = (attenuation >= 15) ? 0 : 15 - attenuation;

            /* Tom Tom */
            volume = context->ym2413_context->state.rhythm_volume_tt;
            eg_level = context->ym2413_context->state.modulator [YM2413_TOM_TOM_CH].eg_level;
            attenuation = volume + (eg_level >> 3);
            bar_value [bar_count++] = (attenuation >= 15) ? 0 : 15 - attenuation;

            /* Top Cymbal */
            volume = context->ym2413_context->state.rhythm_volume_tc;
            eg_level = context->ym2413_context->state.carrier [YM2413_TOP_CYMBAL_CH].eg_level;
            attenuation = volume + (eg_level >> 3);
            bar_value [bar_count++] = (attenuation >= 15) ? 0 : 15 - attenuation;
        }
        else
        {
            bar_count += 2;
        }
    }

    /* Adjust the bar width and spacing based on the number of channels */
    uint32_t bar_width = (bar_count >= 13) ? 10 :
                         (bar_count >=  9) ? 12 : 16;
    uint32_t bar_gap =   (bar_count >= 15) ?  5 :
                         (bar_count >= 11) ?  8 : 16;

    /* Bars */
    uint32_t bar_area_width = bar_width * bar_count + bar_gap * (bar_count - 1);
    uint32_t first_bar = (state.video_width - bar_area_width) / 2;

    for (uint32_t i = 0; i < bar_count; i++)
    {
        for (uint32_t segment = bar_value [i]; segment > 0; segment--)
        {
            draw_rect (context, first_bar + i * (bar_width + bar_gap), 120 - 4 * segment, bar_width, 2,
                       (segment == bar_value [i]) ? colours_peak [segment - 1] : colours_base [segment - 1]);
        }
    }

    /* Underline */
    draw_rect (context, first_bar - 8, 122, bar_area_width + 16, 1, white);

    /* Progress Bar */
    draw_rect (context, 32, 176, 192, 1, dark_grey);
    uint32_t progress = context->current_sample * (uint64_t) 184 / context->total_samples;
    draw_rect (context, 32 + progress, 176, 8, 1, light_grey);

    /* Pass the completed frame on for rendering */
    pthread_mutex_lock (&video_mutex);
    memcpy (state.video_out_data, context->frame_buffer, sizeof (context->frame_buffer));
    pthread_mutex_unlock (&video_mutex);

    /* Clear the buffer for the next frame. */
    memset (context->frame_buffer, 0, sizeof (context->frame_buffer));
}


static void vgm_player_run_until_delay (VGM_Player_Context *context)
{
    uint8_t command;
    uint8_t data;
    uint8_t addr;

    /* Process commands until we reach a delay */
    while (context->delay == 0)
    {
        if (context->index > context->vgm_size)
        {
            snepulator_error ("Error", "End of VGM file.");
        }

        command = context->vgm [context->index++];

        switch (command)
        {
            /* Game Gear Stereo data */
            case 0x4f:
                data = context->vgm [context->index++];
                context->sn76489_context->state.gg_stereo = data;
                break;

            /* SN76489 Data */
            case 0x50:
                data = context->vgm [context->index++];
                sn76489_data_write (context->sn76489_context, data);
                break;

            /* YM2413 Data */
            case 0x51:
                addr = context->vgm [context->index++];
                data = context->vgm [context->index++];
                ym2413_addr_write (context->ym2413_context, addr);
                ym2413_data_write (context->ym2413_context, data);
                break;

            /* Delay <n> samples */
            case 0x61:
                context->delay += * (uint16_t *)(&context->vgm [context->index]);
                context->index += 2;
                break;

            /* Delay one 60 Hz frame */
            case 0x62:
                context->delay += 735;
                break;

            /* Delay one 50 Hz frame */
            case 0x63:
                context->delay += 882;
                break;

            /* End of sound data */
            case 0x66:
                context->index = context->vgm_loop;
                context->current_sample = context->total_samples - context->loop_samples;
                break;

            /* 0x7n: Wait n+1 samples */
            case 0x70: case 0x71: case 0x72: case 0x73:
            case 0x74: case 0x75: case 0x76: case 0x77:
            case 0x78: case 0x79: case 0x7a: case 0x7b:
            case 0x7c: case 0x7d: case 0x7e: case 0x7f:
                context->delay += 1 + (command & 0x0f);
                break;

            /* AY8910 - PSG used in the MSX. Ignore writes.
             * Some VGM files begin by initialising this chip. */
            case 0xa0:
                context->index += 2;
                break;

            /* SCC - MSX Sound Creative Chip. Ignore writes.
             * Some VGM files begin by initialising this chip. */
            case 0xd2:
                context->index += 3;
                break;

            default:
                fprintf (stderr, "Unknown command %02x.\n", command);
                break;

        }
    }
}


/*
 * Emulate the VGM player for the specified length of time.
 * Called with the run_mutex held.
 */
static void vgm_player_run (void *context_ptr, uint32_t samples)
{
    VGM_Player_Context *context = (VGM_Player_Context *) context_ptr;

    context->frame_sample_counter += samples;

    while (samples--)
    {
        vgm_player_run_until_delay (context);

        if (context->sn76489_clock)
        {
            context->sn76489_millicycles += context->sn76489_clock * 1000 / 44100;
            uint32_t cycles = context->sn76489_millicycles / 1000;
            context->sn76489_millicycles -= cycles * 1000;
            sn76489_run_cycles (context->sn76489_context, context->sn76489_clock, cycles);
        }

        if (context->ym2413_clock)
        {
            context->ym2413_millicycles += context->ym2413_clock * 1000 / 44100;
            uint32_t cycles = context->ym2413_millicycles / 1000;
            context->ym2413_millicycles -= cycles * 1000;
            ym2413_run_cycles (context->ym2413_context, context->ym2413_clock, cycles);
        }

        /* Subtract this sample's delay */
        if (context->delay)
        {
            context->delay--;
        }

        /* Keep track of how much we've played for the progress bar */
        context->current_sample++;
    }

    /* Check if we need a new visualizer frame. 60 fps. */
    if (context->frame_sample_counter >= 735)
    {
        context->frame_sample_counter -= 735;
        vgm_player_draw_frame (context);
    }
}


/*
 * Initialize the VGM Player
 */
VGM_Player_Context *vgm_player_init (void)
{
    VGM_Player_Context *context;
    SN76489_Context *sn76489_context;
    YM2413_Context *ym2413_context;

    /* Check we've been passed a filename */
    if (state.cart_filename == NULL)
    {
        snepulator_error ("Error", "No file");
        return NULL;
    }

    context = calloc (1, sizeof (VGM_Player_Context));
    if (context == NULL)
    {
        snepulator_error ("Error", "Unable to allocate memory for VGM_Player_Context");
        return NULL;
    }

    /* Load and check VGM file. Note that zlib also works for loading uncompressed files. */
    if (util_load_gzip_file (&context->vgm, &context->vgm_size, state.cart_filename) == -1)
    {
        snepulator_error ("Error", "Unable to load VGM file");
        vgm_player_cleanup (context);
        free (context);
        return NULL;
    }

    if (memcmp (context->vgm, "Vgm ", 4) != 0)
    {
        snepulator_error ("Error", "Not a VGM file");
        vgm_player_cleanup (context);
        free (context);
        return NULL;
    }

    fprintf (stdout, "%d KiB VGM %s loaded.\n", context->vgm_size >> 10, state.cart_filename);

    /* Initialise sound chips */
    /* TODO: Right now, the PSG implementation only returns stereo sound
     *       if state.console is set to CONSOLE_GAME_GEAR. This should
     *       instead be a parameter when initializing. */
    sn76489_context = sn76489_init ();
    context->sn76489_context = sn76489_context;
    ym2413_context = ym2413_init ();
    context->ym2413_context = ym2413_context;

    /* Initial video parameters */
    state.video_start_x = VIDEO_SIDE_BORDER;
    state.video_start_y = (VIDEO_BUFFER_LINES - 192) / 2;
    state.video_width   = 256;
    state.video_height  = 192;
    state.video_has_border = false;

    /* Read header */
    context->version       = *(uint32_t *) (&context->vgm [0x08]);
    context->sn76489_clock = *(uint32_t *) (&context->vgm [0x0c]);
    context->ym2413_clock  = *(uint32_t *) (&context->vgm [0x10]);
    context->total_samples = *(uint32_t *) (&context->vgm [0x18]);
    context->vgm_loop      = *((uint32_t *) (&context->vgm [0x1c])) + 0x1c;
    context->loop_samples  = *(uint32_t *) (&context->vgm [0x20]);

    if (context->version >= 0x150)
    {
        context->vgm_start = *((uint32_t *) (&context->vgm [0x34])) + 0x34;
    }
    else
    {
        context->vgm_start = 0x40;
    }

    /* If we haven't been given a loop point, loop the whole file */
    if (context->vgm_loop < context->vgm_start)
    {
        context->vgm_loop = context->vgm_start;
        context->loop_samples = context->total_samples;
    }

    /* Hook up callbacks */
    state.audio_callback = vgm_player_audio_callback;
    state.cleanup = vgm_player_cleanup;
    state.run_callback = vgm_player_run;

    /* Start playing */
    context->index = context->vgm_start;
    state.clock_rate = 44100;
    state.run = RUN_STATE_RUNNING;

    return context;
}
