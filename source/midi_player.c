/*
 * Snepulator
 * MIDI Player implementation.
 */

/*
 * TODO List:
 *  - Tempo changes
 *  - Time signature changes
 *  - Rhythm
 *  - Volume changes
 *  - Sustain pedal
 *  - Velocity
 *  - Second ym2413 for better polyphony.
 *  - Improve timing accuracy
 *  - Format-1 midi files
 *  - Pitch bend
 *  - Fine tuning
 *
 * Maybe list:
 *  - Portamento
 *  - Balance / pan
 *  - Soft pedal
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
extern void vgm_player_draw_frame (VGM_Player_Context *context);

/* General MIDI mapping:
 *  1: Violin,      2: Guitar,       3: Piano,        4: Flute,
 *  5: Clarinet,    6: Oboe,         7: Trumpet,      8: Organ,
 *  9: Horn,       10: Synthesizer, 11: Harpsichord, 12: Vibraphone,
 * 13: Synth Bass, 14: Wood Bass,   15: Electric Guitar */
static const uint8_t midi_program_to_ym2413 [128] =
{
     3,  3,  3,  3,  3,  3, 11,  3, /* Pianos */
    12, 12, 12, 12, 12, 12, 12, 11, /* Chromatic Percussion */
     8,  8,  8,  8,  8,  8,  8,  8, /* Organs */
     2,  2, 15, 15, 15, 15, 15, 15, /* Guitars */
    14, 14, 14, 14, 14, 14, 13, 13, /* Basses */
     1,  1,  1,  1,  1,  2,  2, 13, /* Strings */
     1,  1,  1,  1,  4,  4,  4, 10, /* Ensemble */
     7,  7,  9,  7,  9,  7,  7,  7, /* Brass */
     6,  6,  6,  6,  6,  6,  6,  5, /* Reed */
     4,  4,  4,  4,  4,  4,  4,  4, /* Pipe */
     5, 10,  4,  4, 15,  4, 10, 10, /* Synth Lead */
    12,  1, 10,  4,  4,  4,  4,  4, /* Synth Pad */
     3,  1, 12,  2,  7,  4, 14,  1, /* Synth Effects */
     2,  2,  2, 11, 12, 10,  1,  6, /* Ethnic */
    12, 12, 12, 13, 13, 13, 13, 11, /* Percussive */
     0,  0,  0,  0,  0,  0,  0,  0, /* Sound Effects */
};

typedef struct note_s
{
    uint16_t fnum;
    uint8_t block;
} note_t;

/* Mapping from midi note to (fnum, block)
 * Due to the 3-bit block saturating:
 * - Notes 115-126 will sound one octave low.
 * - Note 127 will sound two octaves low. */
static const note_t midi_note_to_ym2413 [128] =
{
    { 86, 0}, { 91, 0}, { 97, 0}, {102, 0}, {109, 0}, {115, 0}, {122, 0}, {129, 0}, {137, 0}, {145, 0}, {153, 0}, {163, 0},
    {172, 0}, {182, 0}, {193, 0}, {205, 0}, {217, 0}, {320, 0}, {244, 0}, {258, 0}, {274, 0}, {290, 0}, {307, 0}, {326, 0},
    {345, 0}, {365, 0}, {387, 0}, {410, 0}, {435, 0}, {460, 0}, {488, 0}, {258, 1}, {274, 1}, {290, 1}, {307, 1}, {326, 1},
    {345, 1}, {365, 1}, {387, 1}, {410, 1}, {435, 1}, {460, 1}, {488, 1}, {258, 2}, {274, 2}, {290, 2}, {307, 2}, {326, 2},
    {345, 2}, {365, 2}, {387, 2}, {410, 2}, {435, 2}, {460, 2}, {488, 2}, {258, 3}, {274, 3}, {290, 3}, {307, 3}, {326, 3},
    {345, 3}, {365, 3}, {387, 3}, {410, 3}, {435, 3}, {460, 3}, {488, 3}, {258, 4}, {274, 4}, {290, 4}, {345, 4}, {387, 4},
    {410, 4}, {435, 4}, {460, 4}, {488, 4}, {258, 5}, {274, 5}, {290, 5}, {307, 5}, {326, 5}, {345, 5}, {365, 5}, {387, 5},
    {410, 5}, {435, 5}, {460, 5}, {488, 5}, {258, 6}, {274, 6}, {290, 6}, {307, 6}, {326, 6}, {345, 6}, {365, 6}, {387, 6},
    {410, 6}, {435, 6}, {460, 6}, {488, 6}, {258, 7}, {274, 7}, {290, 7}, {307, 7}, {326, 7}, {345, 7}, {365, 7}, {387, 7},
    {410, 7}, {435, 7}, {460, 7}, {488, 7}, {258, 7}, {274, 7}, {290, 7}, {307, 7}, {326, 7}, {345, 7}, {365, 7}, {387, 7},
    {410, 7}, {435, 7}, {460, 7}, {488, 7}, {258, 7}
};


/*
 * Callback to supply audio frames.
 */
static void midi_player_audio_callback (void *context_ptr, int16_t *stream, uint32_t count)
{
    MIDI_Player_Context *context = (MIDI_Player_Context *) context_ptr;

    ym2413_get_samples (context->ym2413_context, stream, count);
}


/*
 * Return a synth channel to the queue.
 */
static void midi_synth_queue_put (MIDI_Player_Context *context, uint8_t synth_id)
{
    context->synth_queue [(context->synth_queue_put++) & 0x0f] = synth_id;
}


/*
 * Take a synth channel from the queue.
 */
static uint8_t midi_synth_queue_get (MIDI_Player_Context *context)
{
    return context->synth_queue [(context->synth_queue_get++) & 0x0f];
}


/*
 * Key up event
 */
static void midi_player_key_up (MIDI_Player_Context *context, uint8_t channel, uint8_t key)
{
    /* Nothing to do if the key is already up */
    if (context->channel [channel].key [key] == 0)
    {
        return;
    }

    /* Mark the key as up */
    context->channel [channel].key [key] = 0;

    /* Synth-id to free up */
    uint8_t synth_id = context->channel [channel].synth_id [key];

    /* Register write for key-up event on ym2413 */
    uint8_t r20_value = context->ym2413_context->state.r20_channel_params [synth_id].r20_channel_params;
    r20_value &= 0xef;
    ym2413_addr_write (context->ym2413_context, 0x20 + synth_id);
    ym2413_data_write (context->ym2413_context, r20_value);

    /* Return the channel to the queue */
    midi_synth_queue_put (context, synth_id);
}


/*
 * Key down event.
 * TODO: For now velocity is ignored.
 */
static void midi_player_key_down (MIDI_Player_Context *context, uint8_t channel, uint8_t key)
{
    /* TODO: Handle percussion */
    if (channel == 9)
    {
        return;
    }

    /* If the key was already down, generate an up event to free the previous synth channel. */
    if (context->channel [channel].key [key] > 0)
    {
        midi_player_key_up (context, channel, key);
    }

    /* If we don't have any free synth channels, drop the event */
    if (context->synth_queue_get == context->synth_queue_put)
    {
        return;
    }

    /* Mark the key as down */
    context->channel [channel].key [key] = 127;

    /* Get the synth channel from the queue */
    uint8_t synth_id = midi_synth_queue_get (context);
    context->channel [channel].synth_id [key] = synth_id;

    /* Set the instrument */
    uint8_t r30_value = midi_program_to_ym2413 [context->channel [channel].program] << 4;
    ym2413_addr_write (context->ym2413_context, 0x30 + synth_id);
    ym2413_data_write (context->ym2413_context, r30_value);

    /* Write lower eight bits of fnum */
    note_t note = midi_note_to_ym2413 [key];
    ym2413_addr_write (context->ym2413_context, 0x10 + synth_id);
    ym2413_data_write (context->ym2413_context, note.fnum);

    /* Write the key-down, block, and remaining bit of fnum */
    uint8_t r20_value = 0x10 | (note.block << 1) | (note.fnum >> 8);
    ym2413_addr_write (context->ym2413_context, 0x20 + synth_id);
    ym2413_data_write (context->ym2413_context, r20_value);
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
 * Update the tick_length to handle tempo changes.
 * TODO: Instead of setting a tick length, consider calculating each
 *       delay in clock cycles. This would be more accurate over time.
 */
static void midi_update_tick_length (MIDI_Player_Context *context)
{
    /* Metrical Time */
    if ((context->tick_div & 0x8000) == 0)
    {
        /* TODO: Assuming 4/4 time signature */
        /* TODO: About 0.7 seconds could be lost per hour, as the true tick
         *       length is not going to be a whole number of clock cycles.
         *       We shouldn't need to lose this much, especially when delays
         *       will be multiple ticks long. */
        context->tick_length = ((uint64_t) NTSC_COLOURBURST_FREQ * context->tempo) /
                               ((uint64_t) (context->tick_div & 0x7fff) * 1000000);
    }

    /* Timecode Time */
    else
    {
        snepulator_error ("MIDI Error", "Timecode timing internals not supported");
    }
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
            break;

        case 0x06: /* Marker */
            printf ("MIDI Marker: %s\n", &context->midi [context->index]);
            break;

        case 0x2f: /* End of Track */
            printf ("MIDI End of Track.\n");
            /* TODO: Sort out a clean way of returning to the logo-screen */
            snepulator_error ("MIDI", "Reached event End of Track");
            break;

        case 0x51: /* Tempo */
            context->tempo = util_ntoh32 (*(uint32_t *) &context->midi [context->index]) >> 8;
            midi_update_tick_length (context);
            break;

        case 0x54: /* TODO: SMPTE Offset */
        case 0x58: /* TODO: Time Signature */
        default:
            /* Skip over data */
            printf ("Meta-event 0x%02x not implemented.\n", type);
    }

    context->index += length;

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
        uint8_t key;
        uint8_t velocity;

        switch (context->status & 0xf0)
        {
            case 0x80: /* Note Off */
                key = event & 0x7f;
                velocity = context->midi [context->index++];
                midi_player_key_up (context, channel, key);
                break;

            case 0x90: /* Note On */
                key = event & 0x7f;
                velocity = context->midi [context->index++];
                if (velocity > 0)
                {
                    midi_player_key_down (context, channel, key);
                }
                else
                {
                    midi_player_key_up (context, channel, key);
                }
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
                context->channel [channel].program = event & 0x7f;
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
        /* TODO: Consider storing millicycles like with the consoles,
         *       as breaking things up into units of tick_length loses time. */
        ym2413_run_cycles (context->ym2413_context, NTSC_COLOURBURST_FREQ, context->tick_length);

        /* Subtract this sample's delay */
        if (context->delay)
        {
            context->delay--;
        }

        context->clocks -= context->tick_length;
    }

    context->frame_clock_counter += clocks;
    /* Check if we need a new visualizer frame. 60 fps. */
    if (context->frame_clock_counter >= 59659)
    {
        context->frame_clock_counter -= 59659;

        /* For now, fake a VGM_Player_Context so that we can use the VGM Player's visualisation. */
        context->vgm_player_context.ym2413_clock = NTSC_COLOURBURST_FREQ;
        context->vgm_player_context.ym2413_context = context->ym2413_context;
        context->vgm_player_context.current_sample = context->index;
        context->vgm_player_context.total_samples = context->midi_size;
        vgm_player_draw_frame (&context->vgm_player_context);
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
    midi_update_tick_length (context);
    if (state.run == RUN_STATE_ERROR)
    {
        free (context);
        return NULL;
    }

    /* Initialise sound chip */
    ym2413_context = ym2413_init ();
    context->ym2413_context = ym2413_context;
    ym2413_addr_write (ym2413_context, 0x0e);
    ym2413_data_write (ym2413_context, 0x20); /* Rhythm mode */

    /* Add the six channels to the synth-queue */
    for (uint32_t i = 0; i < 6; i++)
    {
        midi_synth_queue_put (context, i);
    }

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
