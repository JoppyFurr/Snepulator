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

extern Snepulator_State state;


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
 * Returns the clock-rate in Hz.
 * For now, assumes all chips share the same clock rate.
 */
static uint32_t vgm_player_get_clock_rate (void *context_ptr)
{
    VGM_Player_Context *context = (VGM_Player_Context *) context_ptr;

    if (context->sn76489_clock)
    {
        return context->sn76489_clock;
    }
    else
    {
        return context->ym2413_clock;
    }
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
static void vgm_player_run (void *context_ptr, uint32_t ms)
{
    VGM_Player_Context *context = (VGM_Player_Context *) context_ptr;
    uint32_t samples;

    /* Convert the time into 44.1 kHz samples to run. This is the internal sample rate in the VGM specification. */
    /* Spare time is stored as deci-samples in time_ds. */
    context->time_ds += ms * 441;
    samples = context->time_ds / 10;
    context->time_ds -= samples * 10;

    while (samples--)
    {
        vgm_player_run_until_delay (context);

        if (context->sn76489_clock)
        {
            context->sn76489_millicycles += context->sn76489_clock * 1000 / 44100;
            uint32_t cycles = context->sn76489_millicycles / 1000;
            context->sn76489_millicycles -= cycles * 1000;
            psg_run_cycles (context->sn76489_context, cycles);
        }

        if (context->ym2413_clock)
        {
            context->ym2413_millicycles += context->ym2413_clock * 1000 / 44100;
            uint32_t cycles = context->ym2413_millicycles / 1000;
            context->ym2413_millicycles -= cycles * 1000;
            ym2413_run_cycles (context->ym2413_context, cycles);
        }

        /* Subtract this sample's delay */
        if (context->delay)
        {
            context->delay--;
        }
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
    context->vgm_loop      = *((uint32_t *) (&context->vgm [0x1c])) + 0x1c;

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
    state.get_clock_rate = vgm_player_get_clock_rate;
    state.run_callback = vgm_player_run;

    /* Start playing */
    context->index = context->vgm_start;
    state.run = RUN_STATE_RUNNING;

    return context;
}
