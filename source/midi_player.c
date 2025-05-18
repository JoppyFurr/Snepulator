/*
 * Snepulator
 * MIDI Player implementation.
 */

/*
 * Missing features list:
 *  - Improve timing accuracy. Consider calculating the number of clock cycles
 *    to delay. Consider any lost fractional clocks.
 *  - Registered Parameter Number (RPN) values (Pitch Bend sensitivity, Fine Tuning)
 *  - Consider for instruments with a slower decay, even if the key is up, they could
 *    still be affected by volume, pitch-bends, or a late release of the sustain pedal.
 *  - Other MIDI controllers (Portamento, soft pedal, etc)
 *  - Investigate a non-linear curve for velocity.
 *  - Check the maths on MIDI volume / expression. We should try match the change in dB
 *  - TODO: Pass around syth_id where possible and have the called function split instance and channel.
 *  - TODO: Wrapper for ym2413_addr/data_write that handles synth_id.
 *  - Multi-chip percussion
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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


/* General MIDI mapping:
 * 0x01: High Hat,   0x02: Top Cymbal,  0x04: Tom-tom,
 * 0x08: Snare Drum, 0x10: Bass Drum */
static const uint8_t midi_percussion_to_ym2413 [128] =
{
       0,    0,    0,    0,    0,    0,    0,    0,
       0,    0,    0,    0,    0,    0,    0,    0,
       0,    0,    0,    0,    0,    0,    0,    0,
       0,    0,    0,    0,    0,    0,    0,    0,
       0,    0,    0, 0x10, 0x10, 0x08, 0x08, 0x08,
    0x08, 0x04, 0x01, 0x04, 0x01, 0x04, 0x01, 0x04,
    0x04, 0x02, 0x04, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x02, 0x02, 0x08, 0x02, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x08, 0x04, 0x01, 0x01, 0x01, 0x01, 0x02,
    0x02, 0x08, 0x08, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x02, 0x02,    0,    0,    0,    0,    0,    0,
       0,    0,    0,    0,    0,    0,    0,    0,
       0,    0,    0,    0,    0,    0,    0,    0,
       0,    0,    0,    0,    0,    0,    0,    0,
       0,    0,    0,    0,    0,    0,    0,    0,
       0,    0,    0,    0,    0,    0,    0,    0,
};


/*
 * Callback to supply audio frames.
 */
static void midi_player_audio_callback (void *context_ptr, int16_t *stream, uint32_t count)
{
    MIDI_Player_Context *context = (MIDI_Player_Context *) context_ptr;

    for (uint32_t i = 0; i < MIDI_YM2413_COUNT; i++)
    {
        ym2413_get_samples (context->ym2413_context [i], stream, count);
    }
}


/*
 * Return a synth channel to the queue.
 */
static void midi_synth_queue_put (MIDI_Player_Context *context, uint8_t synth_id)
{
    context->synth_queue [(context->synth_queue_put++) % MIDI_SYNTH_QUEUE_SIZE] = synth_id;
}


/*
 * Take a synth channel from the queue.
 */
static uint8_t midi_synth_queue_get (MIDI_Player_Context *context)
{
    return context->synth_queue [(context->synth_queue_get++) % MIDI_SYNTH_QUEUE_SIZE];
}


/*
 * Update the fnum and block for a ym2413 channel.
 */
/* TODO: Should this take a midi-context and synth_id instead? */
static void midi_update_ym2413_fnum (YM2413_Context *context, uint8_t synth_id, uint8_t key, uint16_t pitch_bend)
{
    double bend = 2.0 * (pitch_bend - 8192) / 8192.0; /* Default range is +/- 2 semitones */
    double frequency = 440.0 * pow (2, (key - 69 + bend) / 12.0);
    double fnum_float = frequency * 524288 / (NTSC_COLOURBURST_FREQ / 72.0);
    uint8_t block = 0;

    /* Fnum is a 9-bit value, shifted by block */
    while (round (fnum_float) > 511)
    {
        fnum_float /= 2.0;
        block += 1;
    }

    /* Block is represented by only three bits */
    if (block > 7)
    {
        block = 7;
    }
    uint16_t fnum = round (fnum_float);

    /* Write lower eight bits of fnum */
    ym2413_addr_write (context, 0x10 + synth_id);
    ym2413_data_write (context, fnum);

    /* Write the block, and remaining bit of fnum */
    uint8_t r20_value = context->state.r20_channel_params [synth_id].r20_channel_params & 0xf0;
    r20_value |= (block << 1) | (fnum >> 8);
    ym2413_addr_write (context, 0x20 + synth_id);
    ym2413_data_write (context, r20_value);
}


/*
 * Key up event (percussion)
 */
static void midi_player_percussion_up (MIDI_Player_Context *context, MIDI_Channel *channel, uint8_t key)
{
    uint8_t percussion_bit = midi_percussion_to_ym2413 [key];

    /* Only the General Midi percussion keys have been mapped */
    if (percussion_bit == 0)
    {
        return;
    }

    /* Clear percussion key */
    uint8_t r0e_value = context->ym2413_context [0]->state.r0e_rhythm;
    r0e_value &= ~percussion_bit;
    ym2413_addr_write (context->ym2413_context [0], 0x0e);
    ym2413_data_write (context->ym2413_context [0], r0e_value);
}


/*
 * Key down event (percussion)
 *
 * As an initial implementation, no state is kept about which key triggered
 * the event. The events are simply mapped and passed along to the ym2413.
 * This has the limitation that the up and down events for different MIDI
 * keys became mixed together if they share the same ym2413 percussion sound.
 *
 * TODO: An improvement would be to run multiple ym2413 chips in rhythm mode,
 *       allowing more than one instance of a percussion patch to sound at once.
 *       And/or, to give precedence to the instance with the highest velocity.
 */
static void midi_player_percussion_down (MIDI_Player_Context *context, MIDI_Channel *channel, uint8_t key, uint8_t velocity)
{
    uint8_t percussion_bit = midi_percussion_to_ym2413 [key];

    /* Only the General Midi percussion keys have been mapped */
    if (percussion_bit == 0)
    {
        return;
    }

    /* Set percussion key */
    uint8_t r0e_value = context->ym2413_context [0]->state.r0e_rhythm;
    r0e_value |= percussion_bit;
    ym2413_addr_write (context->ym2413_context [0], 0x0e);
    ym2413_data_write (context->ym2413_context [0], r0e_value);

    /* Set percussion volume */
    uint8_t volume = (16129 - velocity * channel->volume) >> 10;
    uint8_t reg_value;
    switch (percussion_bit)
    {
        case 0x01: /* High Hat */
            reg_value = context->ym2413_context [0]->state.r30_channel_params [7].r30_channel_params & 0x0f;
            reg_value |= volume << 4;
            ym2413_addr_write (context->ym2413_context [0], 0x37);
            ym2413_data_write (context->ym2413_context [0], reg_value);

        case 0x02: /* Top Cymbal */
            reg_value = context->ym2413_context [0]->state.r30_channel_params [8].r30_channel_params & 0xf0;
            reg_value |= volume;
            ym2413_addr_write (context->ym2413_context [0], 0x38);
            ym2413_data_write (context->ym2413_context [0], reg_value);

        case 0x04: /* Tom Tom */
            reg_value = context->ym2413_context [0]->state.r30_channel_params [8].r30_channel_params & 0x0f;
            reg_value |= volume << 4;
            ym2413_addr_write (context->ym2413_context [0], 0x38);
            ym2413_data_write (context->ym2413_context [0], reg_value);

        case 0x08: /* Snare Drum */
            reg_value = context->ym2413_context [0]->state.r30_channel_params [7].r30_channel_params & 0xf0;
            reg_value |= volume;
            ym2413_addr_write (context->ym2413_context [0], 0x37);
            ym2413_data_write (context->ym2413_context [0], reg_value);

        case 0x10: /* Bass Drum */
            reg_value = volume;
            ym2413_addr_write (context->ym2413_context [0], 0x36);
            ym2413_data_write (context->ym2413_context [0], reg_value);

        default:
            break;
    }
}


/*
 * Key up event
 */
static void midi_player_key_up (MIDI_Player_Context *context, MIDI_Channel *channel, uint8_t key)
{
    /* Nothing to do if the key is already up */
    if (channel->key [key] == 0)
    {
        return;
    }

    /* Mark the key as up */
    channel->key [key] = 0;

    /* Synth-id to free up */
    uint8_t synth_id = channel->synth_id [key];

    /* Register write for key-up event on ym2413 */
    uint8_t r20_value = context->ym2413_context [synth_id / 6]->state.r20_channel_params [synth_id % 6].r20_channel_params;
    r20_value &= 0xef;
    ym2413_addr_write (context->ym2413_context [synth_id / 6], 0x20 + (synth_id % 6));
    ym2413_data_write (context->ym2413_context [synth_id / 6], r20_value);

    /* Return the channel to the queue */
    midi_synth_queue_put (context, synth_id);
}


/*
 * Key down event.
 */
static void midi_player_key_down (MIDI_Player_Context *context, MIDI_Channel *channel, uint8_t key, uint8_t velocity)
{
    /* If the key was already down, generate an up event to free the previous synth channel. */
    if (channel->key [key] > 0)
    {
        midi_player_key_up (context, channel, key);
    }

    /* Only continue if we have a valid instrument mapping */
    if (midi_program_to_ym2413 [channel->program] == 0)
    {
        return;
    }

    /* If we don't have any free synth channels, drop the event */
    if (context->synth_queue_get == context->synth_queue_put)
    {
        return;
    }

    /* Mark the key as down */
    channel->key [key] = velocity;

    /* Get the synth channel from the queue */
    uint8_t synth_id = midi_synth_queue_get (context);
    channel->synth_id [key] = synth_id;

    /* Set the instrument and volume */
    uint8_t volume = (16129 - velocity * channel->volume) >> 10;
    uint8_t r30_value = (midi_program_to_ym2413 [channel->program] << 4) | volume;
    ym2413_addr_write (context->ym2413_context [synth_id / 6], 0x30 + (synth_id % 6));
    ym2413_data_write (context->ym2413_context [synth_id / 6], r30_value);

    /* Write the fnum and block */
    midi_update_ym2413_fnum (context->ym2413_context [synth_id / 6], synth_id % 6, key, channel->pitch_bend);

    /* Write the sustain, key-down, block, and remaining bit of fnum */
    uint8_t r20_value = context->ym2413_context [synth_id / 6]->state.r20_channel_params [synth_id % 6].r20_channel_params & 0x0f;
    r20_value |= (channel->sustain << 5) | 0x10;
    ym2413_addr_write (context->ym2413_context [synth_id / 6], 0x20 + (synth_id % 6));
    ym2413_data_write (context->ym2413_context [synth_id / 6], r20_value);
}


/*
 * Clean up structures and free memory.
 */
static void midi_player_cleanup (void *context_ptr)
{
    MIDI_Player_Context *context = (MIDI_Player_Context *) context_ptr;

    for (uint32_t i = 0; i < MIDI_YM2413_COUNT; i++)
    {
        if (context->ym2413_context [i] != NULL)
        {
            free (context->ym2413_context [i]);
            context->ym2413_context [i] = NULL;
        }
    }

    if (context->midi != NULL)
    {
        free (context->midi);
        context->midi = NULL;
    }

    if (context->track != NULL)
    {
        free (context->track);
        context->track = NULL;
    }
}


/*
 * Read a 32-bit big-endian value from the MIDI file.
 */
static uint32_t midi_read_32 (MIDI_Player_Context *context, MIDI_Track *track)
{
    if (track)
    {
        uint32_t value = util_ntoh32 (* (uint32_t *) &context->midi [track->index]);
        track->index += 4;
        return value;
    }
    else
    {
        uint32_t value = util_ntoh32 (* (uint32_t *) &context->midi [context->index]);
        context->index += 4;
        return value;
    }
}


/*
 * Read a 16-bit big-endian value from the MIDI file.
 */
static uint16_t midi_read_16 (MIDI_Player_Context *context, MIDI_Track *track)
{
    if (track)
    {
        uint16_t value = util_ntoh16 (* (uint16_t *) &context->midi [track->index]);
        track->index += 2;
        return value;
    }
    else
    {
        uint16_t value = util_ntoh16 (* (uint16_t *) &context->midi [context->index]);
        context->index += 2;
        return value;
    }
}


/*
 * Read a variable-width value from the MIDI file.
 */
static uint32_t midi_read_variable_length (MIDI_Player_Context *context, MIDI_Track *track)
{
    uint32_t byte_0 = context->midi [track->index];
    track->index += 1;
    uint32_t value = byte_0 & 0x7f;
    if ((byte_0 & 0x80) == 0)
    {
        return value;
    }

    uint32_t byte_1 = context->midi [track->index];
    track->index += 1;
    value = (value << 7) | (byte_1 & 0x7f);
    if ((byte_1 & 0x80) == 0)
    {
        return value;
    }

    uint32_t byte_2 = context->midi [track->index];
    track->index += 1;
    value = (value << 7) | (byte_2 & 0x7f);
    if ((byte_2 & 0x80) == 0)
    {
        return value;
    }

    uint32_t byte_3 = context->midi [track->index];
    track->index += 1;
    value = (value << 7) | (byte_3 & 0x7f);

    return value;
}


/*
 * Read a MIDI track header
 */
static int midi_read_track_header (MIDI_Player_Context *context, MIDI_Track *track)
{
    if (memcmp (&context->midi [context->index], "MTrk", 4) != 0)
    {
        snepulator_error ("MIDI Error", "End of chunk was not followed by MTrk chunk", context->format);
        return -1;
    }
    context->index += 4;

    uint32_t chunk_length = midi_read_32 (context, NULL);
    if (context->index + chunk_length > context->midi_size)
    {
        snepulator_error ("MIDI Error", "Invalid chunk size would run off end of file", chunk_length);
        return -1;
    }

    /* Initialize the current track */
    track->index = context->index;
    track->track_end = context->index + chunk_length;
    track->expect = EXPECT_DELTA_TIME;

    /* Advance MIDI index to the next track */
    context->index += chunk_length;

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
static int midi_read_meta_event (MIDI_Player_Context *context, MIDI_Track *track)
{
    uint8_t type = context->midi [track->index++];
    uint32_t length = midi_read_variable_length (context, track);

    switch (type)
    {
        case 0x01: /* Text */
            printf ("MIDI Text: %s\n", &context->midi [track->index]);
            break;

        case 0x02: /* Copyright */
            printf ("MIDI Copyright: %s\n", &context->midi [track->index]);
            break;

        case 0x03: /* Sequence / Track Name */
            if (track == &context->track [0])
            {
                printf ("MIDI Sequence Name: %s\n", &context->midi [track->index]);
            }
            else
            {
                printf ("MIDI Track %d Name: %s\n", (int) (track - &context->track [0]), &context->midi [track->index]);
            }
            break;

        case 0x04: /* Copyright */
            printf ("MIDI Instrument Name: %s\n", &context->midi [track->index]);
            break;

        case 0x05: /* Lyric */
            printf ("MIDI Lyric: %s\n", &context->midi [track->index]);
            break;

        case 0x06: /* Marker */
            printf ("MIDI Marker: %s\n", &context->midi [track->index]);
            break;

        case 0x07: /* Cue Point */
            printf ("MIDI Cue Point: %s\n", &context->midi [track->index]);
            break;

        case 0x08: /* Program Name */
            printf ("MIDI Program Name: %s\n", &context->midi [track->index]);
            break;

        case 0x09: /* Device Name */
            printf ("MIDI Device Name: %s\n", &context->midi [track->index]);
            break;

        case 0x20: /* MIDI Channel Prefix (ignore) */
        case 0x21: /* MIDI Port (ignore) */
            break;

        case 0x2f: /* End of Track */
            track->end_of_track = true;
            context->n_tracks_completed += 1;
            break;

        case 0x51: /* Tempo */
            context->tempo = util_ntoh32 (*(uint32_t *) &context->midi [track->index]) >> 8;
            midi_update_tick_length (context);
            break;

        case 0x54: /* SMPTE Offset */
            /* Specifies the time at which the track is to start.
             * Ignore this and just start right away. */
            break;

        case 0x58: /* Time Signature */
            /* Specifies the time signature, metronome timing, and number of 32nd notes per MIDI quarter-note.
             * Ignore this as it's not needed for playback, only for things like showing the bar number. */
            break;

        case 0x59: /* Key Signature */
            /* Specifies the number of flats or sharps in the key signature. Doesn't affect playback. */
            break;

        case 0x7f: /* Sequencer Specific Event */
            /* Used to store manufacturer / sequencer-specific information */
            break;

        default:
            /* Skip over data */
            printf ("Meta-event 0x%02x not implemented.\n", type);
    }

    track->index += length;

    return 0;
}


/*
 * Set a MIDI controller value
 */
static void midi_set_controller (MIDI_Player_Context *context, MIDI_Channel *channel, uint8_t controller, uint8_t value)
{
    switch (controller)
    {
        case 0: /* Bank Select (ignore) */
            break;

        case 1: /* Modulation - Not implemented */
            break;

        case 7: /* Channel Volume */
            channel->volume = value;

            /* Update any in-progress notes */
            for (uint8_t key = 0; key < 128; key++)
            {
                if (channel->key [key] > 0)
                {
                    uint8_t synth_id = channel->synth_id [key];
                    uint8_t r30_value = context->ym2413_context [synth_id / 6]->state.r30_channel_params [synth_id % 6].r30_channel_params & 0xf0;
                    r30_value |= (16129 - channel->key [key] * value) >> 10;
                    ym2413_addr_write (context->ym2413_context [synth_id / 6], 0x30 + (synth_id % 6));
                    ym2413_data_write (context->ym2413_context [synth_id / 6], r30_value);
                }
            }
            break;

        case 10: /* Pan - Not implemented */
            break;

        case 32: /* Bank Select LSB (ignore) */
            break;

        case 64: /* Sustain */
            /* Store the sustain as a 1-bit value for the ym2413 */
            channel->sustain = (value >= 64);

            /* Update any in-progress notes */
            for (uint8_t key = 0; key < 128; key++)
            {
                if (channel->key [key] > 0)
                {
                    uint8_t synth_id = channel->synth_id [key];
                    uint8_t r20_value = context->ym2413_context [synth_id / 6]->state.r20_channel_params [synth_id % 6].r20_channel_params & 0xdf;
                    r20_value |= channel->sustain << 5;
                    ym2413_addr_write (context->ym2413_context [synth_id / 6], 0x20 + (synth_id % 6));
                    ym2413_data_write (context->ym2413_context [synth_id / 6], r20_value);
                }
            }
            break;

        case 91: /* Reverb - Not implemented */
        case 93: /* Chorus - Not implemented */
        case 126: /* Mono Mode - Not implemented */
        case 127: /* Poly Mode - Assume polyphonic by default */
            break;

        default:
            printf ("MIDI controller [%d] set to %d. (ignored)\n", controller, value);
            break;
    }
}


/*
 * Read and process a MIDI event from the MIDI file.
 */
static int midi_read_event (MIDI_Player_Context *context, MIDI_Track *track)
{
    uint8_t event = context->midi [track->index++];

    if (event == 0xff)
    {
        return midi_read_meta_event (context, track);
    }
    else if (event >= 0xf0 && event < 0xf8)
    {
        /* Skip over any SysEx events or System Common messages */
        uint32_t length = midi_read_variable_length (context, track);
        track->index += length;
        return 0;
    }
    else if (event < 0xf0)
    {
        if (event & 0x80)
        {
            track->status = event;
            event = context->midi [track->index++];
        }

        /* Note: The MIDI Status, telling us the event type is stored in context->status.
         *       The first byte of the event (note, controller, program, etc) is stored in 'event' */
        uint8_t channel = track->status & 0x0f;
        uint8_t key;
        uint8_t velocity;
        uint8_t controller;
        uint8_t value;
        uint16_t bend;

        switch (track->status & 0xf0)
        {
            case 0x80: /* Note Off */
                key = event & 0x7f;
                velocity = context->midi [track->index++] & 0x7f;
                /* Channel 10 is used for percussion sounds */
                if (channel == 9)
                {
                    midi_player_percussion_up (context, &track->channel [channel], key);
                }
                else
                {
                    midi_player_key_up (context, &track->channel [channel], key);
                }
                break;

            case 0x90: /* Note On */
                key = event & 0x7f;
                velocity = context->midi [track->index++] & 0x7f;
                if (velocity > 0)
                {
                    /* Channel 10 is used for percussion sounds */
                    if (channel == 9)
                    {
                        midi_player_percussion_down (context, &track->channel [channel], key, velocity);
                    }
                    else
                    {
                        midi_player_key_down (context, &track->channel [channel], key, velocity);
                    }
                }
                else
                {
                    /* Channel 10 is used for percussion sounds */
                    if (channel == 9)
                    {
                        midi_player_percussion_up (context, &track->channel [channel], key);
                    }
                    else
                    {
                        midi_player_key_up (context, &track->channel [channel], key);
                    }
                }
                break;

            case 0xa0: /* Polyphonic Pressure - Not implemented */
                track->index += 1;
                break;

            case 0xb0: /* Controller */
                controller = event & 0x7f;
                value = context->midi [track->index++] & 0x7f;
                midi_set_controller (context, &track->channel [channel], controller, value);
                break;

            case 0xc0: /* Program Change */
                track->channel [channel].program = event & 0x7f;
                break;

            case 0xd0: /* Channel Pressure - Not implemented */
                break;

            case 0xe0: /* Pitch Bend */
                bend = (event & 0x7f) + ((context->midi [track->index++] & 0x7f) << 7);
                track->channel [channel].pitch_bend = bend;

                /* Pitch bend needs to be applied to notes which are already sounding */
                for (uint8_t key = 0; key < 128; key++)
                {
                    if (track->channel [channel].key [key] > 0)
                    {
                        uint8_t synth_id = track->channel [channel].synth_id [key];
                        midi_update_ym2413_fnum (context->ym2413_context [synth_id / 6], synth_id % 6, key, track->channel [channel].pitch_bend);
                    }
                }
                break;

            default:
                snepulator_error ("MIDI Error", "midi-event 0x%02x not implemented.", track->status);
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
        /* Process events until all tracks have reached a delay */
        for (uint32_t track = 0; track < context->n_tracks; track++)
        {
            if (context->track [track].end_of_track)
            {
                continue;
            }

            /* TODO: Combine this into the while loop below */
            if (context->track [track].index >= context->track [track].track_end)
            {
                snepulator_error ("MIDI Error", "Track data runs longer than specified track length");
            }

            while (context->track [track].delay == 0 && context->track [track].index < context->track [track].track_end)
            {
                switch (context->track [track].expect)
                {
                    case EXPECT_DELTA_TIME:
                        context->track [track].delay = midi_read_variable_length (context, &context->track [track]);
                        context->track [track].expect = EXPECT_EVENT;
                        break;

                    case EXPECT_EVENT:
                        ret = midi_read_event (context, &context->track [track]);
                        if (ret == -1)
                        {
                            return;
                        }
                        context->track [track].expect = EXPECT_DELTA_TIME;
                        break;

                    default:
                        snepulator_error ("MIDI Error", "Invalid state - Expect %d", context->track [track].expect);
                        break;
                }
            }
        }

        /* If the tempo has changed, the new tick_length may be more
         * than the number of remaining clocks. When this happens, the
         * return now so that we don't spend time we don't have. */
        if (context->clocks < context->tick_length)
        {
            break;
        }

        /* During delay, we run the YM2413 */
        /* TODO: Consider storing millicycles like with the consoles,
         *       as breaking things up into units of tick_length loses time. */
        for (uint32_t i = 0; i < MIDI_YM2413_COUNT; i++)
        {
            ym2413_run_cycles (context->ym2413_context [i], NTSC_COLOURBURST_FREQ, context->tick_length);
        }

        /* Subtract this tick_length delay from all tracks */
        for (uint32_t track = 0; track < context->n_tracks; track++)
        {
            if (context->track [track].delay)
            {
                context->track [track].delay--;
            }
        }

        /* TODO: Sort out a clean way of returning to the logo-screen */
        if (context->n_tracks_completed == context->n_tracks)
        {
            snepulator_error ("MIDI", "All tracks completed.");
        }

        context->clocks -= context->tick_length;
    }

    context->frame_clock_counter += clocks;
    /* Check if we need a new visualizer frame. 60 fps. */
    if (context->frame_clock_counter >= 59659)
    {
        context->frame_clock_counter -= 59659;

        /* TODO: Bring in the animation code to deal with the multiple chips. */
        /* For now, fake a VGM_Player_Context so that we can use the VGM Player's visualisation. */
        context->vgm_player_context.ym2413_clock = NTSC_COLOURBURST_FREQ;
        context->vgm_player_context.ym2413_context = context->ym2413_context [0];
        /* TODO: This approximation won't work well for multi-track MIDIs. Do we need to sum the delays and work out the duration? */
        context->vgm_player_context.current_sample = context->track [0].index;
        context->vgm_player_context.total_samples = context->track [0].track_end;
        vgm_player_draw_frame (&context->vgm_player_context);
    }
}


/*
 * Initialize the MIDI Player
 */
MIDI_Player_Context *midi_player_init (void)
{
    MIDI_Player_Context *context;

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

    uint32_t header_length = midi_read_32 (context, NULL);
    if (header_length < 6)
    {
        snepulator_error ("MIDI Error", "Invalid HThd length %d.", header_length);
        midi_player_cleanup (context);
        free (context);
        return NULL;
    }

    context->format = midi_read_16 (context, NULL);
    context->n_tracks = midi_read_16 (context, NULL);
    context->tick_div = midi_read_16 (context, NULL);

    if (context->format > 1)
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

    /* Allocate memory for tracks */
    context->track = calloc (context->n_tracks, sizeof (MIDI_Track));

    /* Set up tracks */
    for (uint32_t i = 0; i < context->n_tracks; i++)
    {
        int ret = midi_read_track_header (context, &context->track [i]);
        if (ret < 0)
        {
            midi_player_cleanup (context);
            free (context);
            return NULL;
        }

        for (uint32_t channel = 0; channel < 16; channel++)
        {
            context->track [i].channel [channel].pitch_bend = 8192;
        }
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
    for (uint32_t i = 0; i < MIDI_YM2413_COUNT; i++)
    {
        YM2413_Context *ym2413_context = ym2413_init ();
        ym2413_addr_write (ym2413_context, 0x0e);
        ym2413_data_write (ym2413_context, 0x20); /* Rhythm mode */
        context->ym2413_context [i] = ym2413_context;

        /* Add the six channels to the synth-queue */
        for (uint32_t c = 0; c < 6; c++)
        {
            midi_synth_queue_put (context, (i * 6) + c);
        }
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
    state.clock_rate = NTSC_COLOURBURST_FREQ;
    state.run = RUN_STATE_RUNNING;

    return context;
}
