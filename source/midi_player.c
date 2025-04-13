/*
 * Snepulator
 * MIDI Player implementation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snepulator.h"
#include "util.h"

#include "sound/band_limit.h"
#include "sound/sn76489.h"
#include "sound/ym2413.h"
#include "vgm_player.h"
#include "midi_player.h"

extern Snepulator_State state;


/*
 * Callback to supply audio frames.
 */
static void midi_player_audio_callback (void *context_ptr, int16_t *stream, uint32_t count)
{
    MIDI_Player_Context *context = (MIDI_Player_Context *) context_ptr;

    if (context->ym2413_clock)
    {
        ym2413_get_samples (context->ym2413_context, stream, count);
    }
}


/*
 * Clean up structures and free memory.
 */
static void midi_player_cleanup (void *context_ptr)
{
    MIDI_Player_Context *context = (MIDI_Player_Context *) context_ptr;

    if (context->ym2413_context != NULL)
    {
        free (context->ym2413_context);
        context->ym2413_context = NULL;
    }

    if (context->midi != NULL)
    {
        free (context->midi);
        context->midi = NULL;
    }
}


/*
 * Read a 32-bit big-endian value from the MIDI file.
 */
static uint32_t midi_read_32 (MIDI_Player_Context *context)
{
    uint32_t value = util_ntoh32 (* (uint32_t *) &context->midi [context->index]);
    context->index += 4;
    return value;
}


/*
 * Read a 16-bit big-endian value from the MIDI file.
 */
static uint16_t midi_read_16 (MIDI_Player_Context *context)
{
    uint16_t value = util_ntoh16 (* (uint16_t *) &context->midi [context->index]);
    context->index += 2;
    return value;
}


/*
 * Read a variable-width value from the MIDI file.
 */
static uint32_t midi_read_variable_length (MIDI_Player_Context *context)
{
    uint32_t byte_0 = context->midi [context->index];
    context->index += 1;
    uint32_t value = byte_0 & 0x7f;
    if ((byte_0 & 0x80) == 0)
    {
        return value;
    }

    uint32_t byte_1 = context->midi [context->index];
    context->index += 1;
    value = (value << 7) | (byte_1 & 0x7f);
    if ((byte_1 & 0x80) == 0)
    {
        return value;
    }

    uint32_t byte_2 = context->midi [context->index];
    context->index += 1;
    value = (value << 7) | (byte_2 & 0x7f);
    if ((byte_2 & 0x80) == 0)
    {
        return value;
    }

    uint32_t byte_3 = context->midi [context->index];
    context->index += 1;
    value = (value << 7) | (byte_3 & 0x7f);

    return value;
}


/*
 * Read a MIDI track header
 */
static int midi_read_track_header (MIDI_Player_Context *context)
{
    if (memcmp (&context->midi [context->index], "MTrk", 4) != 0)
    {
        snepulator_error ("MIDI Error", "End of chunk was not followed by MTrk chunk", context->format);
        return -1;
    }
    context->index += 4;

    uint32_t chunk_length = midi_read_32 (context);
    if (context->index + chunk_length > context->midi_size)
    {
        snepulator_error ("MIDI Error", "Invalid chunk size would run off end of file", chunk_length);
        return -1;
    }
    context->track_end = context->index + chunk_length;

    printf ("MIDI Track has chunk_length of %d bytes.\n", chunk_length);

    /* MIDI tracks are a sequence of (delta_time, event) pairs. */
    context->expect = EXPECT_DELTA_TIME;

    return 0;
}


/*
 * Read and process a Meta Event from the MIDI file.
 */
static int midi_read_meta_event (MIDI_Player_Context *context)
{
    uint8_t type = context->midi [context->index++];
    uint32_t length = midi_read_variable_length (context);

    switch (type)
    {
        case 0x01: /* Text */
            printf ("MIDI Text: %s\n", &context->midi [context->index]);
            context->index += length;
            break;

        case 0x06: /* Marker */
            printf ("MIDI Marker: %s\n", &context->midi [context->index]);
            context->index += length;
            break;

        case 0x2f: /* End of Track */
            printf ("MIDI End of Track.\n");
            /* TODO: Sort out a clean way of returning to the logo-screen */
            snepulator_error ("MIDI", "Reached event End of Track");
            break;

        case 0x51: /* TODO: Tempo */
        case 0x54: /* TODO: SMPTE Offset */
        case 0x58: /* TODO: Time Signature */
        default:
            /* Skip over data */
            context->index += length;
            printf ("Meta-event 0x%02x not implemented.\n", type);
    }

    return 0;
}


/*
 * Read and process a MIDI event from the MIDI file.
 */
static int midi_read_event (MIDI_Player_Context *context)
{
    uint8_t event = context->midi [context->index++];

    if (event == 0xff)
    {
        return midi_read_meta_event (context);
    }
    else if (event >= 0xf0 && event < 0xf8)
    {
        snepulator_error ("MIDI Error", "sysex-event 0x%02x not implemented.", event);
        return -1;
    }
    else if (event < 0xf0)
    {
        if (event & 0x80)
        {
            context->status = event;
            event = context->midi [context->index++];
        }

        /* Note: The MIDI Status, telling us the event type is stored in context->status.
         *       The first byte of the event (note, controller, program, etc) is stored in 'event' */
        uint8_t channel = context->status & 0x0f;

        switch (context->status & 0xf0)
        {
            case 0x80: /* Note Off */
                /* TODO: Ignored for now. */
                printf ("MIDI Channel %d note %d off. (ignored)\n", channel + 1, event);
                context->index += 1;
                break;

            case 0x90: /* Note On */
                /* TODO: Ignored for now. */
                printf ("MIDI Channel %d note %d %s. (ignored)\n", channel + 1,
                        event, context->midi [context->index] ? "on" : "off");
                context->index += 1;
                break;

            case 0xa0: /* Polyphonic Pressure */
                /* TODO: Ignored for now. */
                printf ("MIDI Channel %d note %d aftertouch %d. (ignored)\n", channel + 1,
                        event, context->midi [context->index]);
                context->index += 1;
                break;

            case 0xb0: /* Controller */
                /* TODO: Ignored for now. */
                printf ("MIDI Channel %d controller [%d] set to %d. (ignored)\n", channel + 1,
                        event, context->midi [context->index]);
                context->index += 1;
                break;

            case 0xc0: /* Program Change */
                context->channel_program [channel] = event;
                printf ("MIDI Channel %d program set to %d.\n", channel + 1, event + 1);
                break;

            case 0xe0: /* Pitch Bend */
                /* TODO: Ignored for now. */
                printf ("MIDI Channel %d pitch-bend set to %d. (ignored)\n", channel + 1,
                        event + (context->midi [context->index] << 8));
                context->index += 1;
                break;

            default:
                snepulator_error ("MIDI Error", "midi-event 0x%02x not implemented.", context->status);
                return -1;
        }
    }
    else
    {
        /* Unknown event */
        snepulator_error ("MIDI Error", "Unknown event 0x%02x", event);
        return -1;
    }

    /* TODO: Consider return value to indicate if tick_length has
     *       changed as a result of this message */
    return 0;
}


/*
 * Run the MIDI player for the specified length of time.
 * Clock-rate is the NTSC Colourburst frequency.
 * Called with the run_mutex held.
 *
 * TODO: For now, format 0 is assumed; a single track that
 *       can just be read sequentially.
 *
 * TODO: For better polyphony, consider emulating a 2nd ym2413.
 *
 * TODO: Consider adding a wrapper around reading bytes from the
 *       MIDI file that could check for things like crossing the
 *       track-boundary or end-of-file.
 */
static void midi_player_run (void *context_ptr, uint32_t clocks)
{
    MIDI_Player_Context *context = (MIDI_Player_Context *) context_ptr;
    int ret = 0;

    context->clocks += clocks;

    while (state.run == RUN_STATE_RUNNING &&
           context->clocks > context->tick_length)
    {
        if (context->index >= context->track_end && context->expect != EXPECT_TRACK_HEADER)
        {
            snepulator_error ("MIDI Error", "Track data runs longer than specified track length");
        }

        /* Process events until we reach a delay */
        while (context->delay == 0 &&
               (context->index < context->track_end || context->expect == EXPECT_TRACK_HEADER))
        {
            switch (context->expect)
            {
                case EXPECT_TRACK_HEADER:
                    /* TODO: Move above code into a function.
                     *       Will need to be reworked for format-1 later on. */
                    ret = midi_read_track_header (context);
                    if (ret == -1)
                    {
                        return;
                    }

                    break;
                case EXPECT_EVENT:
                    ret = midi_read_event (context);
                    if (ret == -1)
                    {
                        return;
                    }
                    context->expect = EXPECT_DELTA_TIME;
                    /* TODO: Some events, such as tempo-change or time-signature-change,
                     *       will change context->tick_length. If this happens and results
                     *       in a tick_length greater than the remaining clocks, we need
                     *       to return early to prevent clocks going negative. */
                    break;
                case EXPECT_DELTA_TIME:
                default:
                    context->delay = midi_read_variable_length (context);
                    context->expect = EXPECT_EVENT;
                    break;
            }
        }

        /* During delay, we run the YM2413 */
        if (context->ym2413_clock)
        {
            ym2413_run_cycles (context->ym2413_context, context->ym2413_clock, context->tick_length);
        }

        /* Subtract this sample's delay */
        if (context->delay)
        {
            context->delay--;
        }

        context->clocks -= context->tick_length;
    }
}


/*
 * Initialize the MIDI Player
 */
MIDI_Player_Context *midi_player_init (void)
{
    MIDI_Player_Context *context;
    YM2413_Context *ym2413_context;

    /* Check we've been passed a filename */
    if (state.cart_filename == NULL)
    {
        snepulator_error ("Error", "No file");
        return NULL;
    }

    context = calloc (1, sizeof (MIDI_Player_Context));
    if (context == NULL)
    {
        snepulator_error ("Error", "Unable to allocate memory for MIDI_Player_Context");
        return NULL;
    }

    /* Default values */
    /* TODO: Assumes 4/4 time signature. */
    context->tempo = 500000; /* 120 bpm */

    /* Load MIDI file */
    if (util_load_file (&context->midi, &context->midi_size, state.cart_filename) == -1)
    {
        snepulator_error ("Error", "Unable to load MIDI file");
        midi_player_cleanup (context);
        free (context);
        return NULL;
    }

    /* Read header */
    if (memcmp (context->midi, "MThd", 4) != 0)
    {
        snepulator_error ("MIDI Error", "Not a MIDI file");
        midi_player_cleanup (context);
        free (context);
        return NULL;
    }
    context->index += 4;

    uint32_t header_length = midi_read_32 (context);
    if (header_length < 6)
    {
        snepulator_error ("MIDI Error", "Invalid HThd length %d.", header_length);
        midi_player_cleanup (context);
        free (context);
        return NULL;
    }

    context->format = midi_read_16 (context);
    context->n_tracks = midi_read_16 (context);
    context->tick_div = midi_read_16 (context);

    if (context->format != 0)
    {
        snepulator_error ("MIDI Error", "MIDI format '%d' is not supported.", context->format);
        midi_player_cleanup (context);
        free (context);
        return NULL;
    }

    /* Skip any additional header fields */
    if (header_length > 6)
    {
        context->index += header_length - 6;
    }

    fprintf (stdout, "%d KiB MIDI %s loaded.\n", context->midi_size >> 10, state.cart_filename);
    /* DEBUG */
    fprintf (stdout, "format = %d, n_tracks = %d, tick_div=0x%04x\n", context->format, context->n_tracks, context->tick_div);

    /* Calculate length of a midi-tick in colourburst clocks */
    if ((context->tick_div & 0x8000) == 0)
    {
        /* TODO: Assuming 4/4 time signature */
        /* TODO: About 0.7 seconds could be lost per hour, as the true tick
         *       length is not going to be a whole number of clock cycles.
         *       We shouldn't need to lose this much, especially when delays
         *       will be multiple ticks long. */
        context->tick_length = ((uint64_t) NTSC_COLOURBURST_FREQ * context->tempo) /
                               ((uint64_t) (context->tick_div & 0x7fff) * 1000000);
        printf ("context->tick_length = %u.\n", context->tick_length);
    }
    else
    {
        snepulator_error ("MIDI Error", "Timecode timing internals not supported");
        free (context);
        return NULL;
    }

    /* Initialise sound chip */
    ym2413_context = ym2413_init ();
    context->ym2413_context = ym2413_context;

    /* Initial video parameters */
    state.video_start_x = VIDEO_SIDE_BORDER;
    state.video_start_y = (VIDEO_BUFFER_LINES - 192) / 2;
    state.video_width   = 256;
    state.video_height  = 192;

    /* Hook up callbacks */
    state.audio_callback = midi_player_audio_callback;
    state.cleanup = midi_player_cleanup;
    state.run_callback = midi_player_run;

    /* Start playing */
    context->track_end = context->index;
    context->expect = EXPECT_TRACK_HEADER;
    state.clock_rate = NTSC_COLOURBURST_FREQ;
    state.run = RUN_STATE_RUNNING;

    return context;
}
