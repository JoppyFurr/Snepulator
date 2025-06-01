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
 *  - Working scroll-bar for the bottom of the screen.
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
#include "video/visualiser.h"
#include "midi_player.h"

extern Snepulator_State state;

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

#define RHYTHM_HH 0 /* High Hat */
#define RHYTHM_TC 1 /* Top Cymbal */
#define RHYTHM_TT 2 /* Tom-tom */
#define RHYTHM_SD 3 /* Snare Drum */
#define RHYTHM_BD 4 /* Bass Drum */

/* General MIDI mapping:
 * 0x01: High Hat,   0x02: Top Cymbal,  0x04: Tom-tom,
 * 0x08: Snare Drum, 0x10: Bass Drum */
static const uint8_t midi_percussion_to_ym2413 [128] =
{
         0xff,      0xff,      0xff,      0xff,      0xff,      0xff,      0xff,      0xff,
         0xff,      0xff,      0xff,      0xff,      0xff,      0xff,      0xff,      0xff,
         0xff,      0xff,      0xff,      0xff,      0xff,      0xff,      0xff,      0xff,
         0xff,      0xff,      0xff,      0xff,      0xff,      0xff,      0xff,      0xff,
         0xff,      0xff,      0xff, RHYTHM_BD, RHYTHM_BD, RHYTHM_SD, RHYTHM_SD, RHYTHM_SD,
    RHYTHM_SD, RHYTHM_TT, RHYTHM_HH, RHYTHM_TT, RHYTHM_HH, RHYTHM_TT, RHYTHM_HH, RHYTHM_TT,
    RHYTHM_TT, RHYTHM_TC, RHYTHM_TT, RHYTHM_TC, RHYTHM_TC, RHYTHM_TC, RHYTHM_TC, RHYTHM_TC,
    RHYTHM_TC, RHYTHM_TC, RHYTHM_SD, RHYTHM_TC, RHYTHM_TT, RHYTHM_TT, RHYTHM_TT, RHYTHM_TT,
    RHYTHM_TT, RHYTHM_SD, RHYTHM_TT, RHYTHM_HH, RHYTHM_HH, RHYTHM_HH, RHYTHM_HH, RHYTHM_TC,
    RHYTHM_TC, RHYTHM_SD, RHYTHM_SD, RHYTHM_TT, RHYTHM_TT, RHYTHM_TT, RHYTHM_TT, RHYTHM_TT,
    RHYTHM_TC, RHYTHM_TC,      0xff,      0xff,      0xff,      0xff,      0xff,      0xff,
         0xff,      0xff,      0xff,      0xff,      0xff,      0xff,      0xff,      0xff,
         0xff,      0xff,      0xff,      0xff,      0xff,      0xff,      0xff,      0xff,
         0xff,      0xff,      0xff,      0xff,      0xff,      0xff,      0xff,      0xff,
         0xff,      0xff,      0xff,      0xff,      0xff,      0xff,      0xff,      0xff,
         0xff,      0xff,      0xff,      0xff,      0xff,      0xff,      0xff,      0xff,
};


/*
 * Callback to supply audio frames.
 */
static void midi_player_audio_callback (void *context_ptr, int32_t *stream, uint32_t count)
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
    if (synth_id & SYNTH_ID_RHYTHM_BIT)
    {
        uint8_t instrument = synth_id & SYNTH_ID_CHANNEL_MASK;
        context->rhythm_queue [instrument] [(context->rhythm_queue_put [instrument]++) % MIDI_RHYTHM_QUEUE_SIZE] = synth_id;
    }
    else
    {
        context->synth_queue [(context->synth_queue_put++) % MIDI_SYNTH_QUEUE_SIZE] = synth_id;
    }
}


/*
 * Take a melody channel from the queue.
 */
static uint8_t midi_synth_queue_get (MIDI_Player_Context *context)
{
    return context->synth_queue [(context->synth_queue_get++) % MIDI_SYNTH_QUEUE_SIZE];
}


/*
 * Take a rhythm channel from the queue.
 */
static uint8_t midi_rhythm_queue_get (MIDI_Player_Context *context, uint8_t instrument)
{
    return context->rhythm_queue [instrument] [(context->rhythm_queue_get [instrument]++) % MIDI_RHYTHM_QUEUE_SIZE];
}


/*
 * Read a YM2413 register.
 * The chip instance and specific address are calculated from synth_id.
 * For channel registers, the supplied address should be the channel-0 address of the register.
 * Note that the chip doesn't actually support reads, this peeks at its state.
 */
static uint8_t midi_ym2413_register_read (MIDI_Player_Context *context, uint8_t synth_id, uint8_t addr)
{
    YM2413_Context *ym2413_context = context->ym2413_context [synth_id >> 5];

    uint8_t channel = synth_id & SYNTH_ID_CHANNEL_MASK;

    /* Ignore channel for rhythm sounds */
    if (synth_id & SYNTH_ID_RHYTHM_BIT)
    {
        channel = 0;
    }

    switch (addr)
    {
        case 0x0e:
            return ym2413_context->state.r0e_rhythm;

        case 0x10:
            return ym2413_context->state.r10_channel_params [channel].r10_channel_params;

        case 0x20:
            return ym2413_context->state.r20_channel_params [channel].r20_channel_params;

        case 0x30:
            return ym2413_context->state.r30_channel_params [channel].r30_channel_params;

        /* Rhythm volumes */
        case 0x36:
        case 0x37:
        case 0x38:
            return ym2413_context->state.r30_channel_params [addr & 0x0f].r30_channel_params;

        default:
            break;
    }

    snepulator_error ("Error", "Invalid ym2413 register addresrs 0x%02x", addr);
    return 0xff;
}


/*
 * Write to a YM2413 register.
 * The chip instance and specific address are calculated from synth_id.
 * The supplied address should be the channel-0 address of the register.
 *
 * For non-melody registers, the channel-bits should be 0.
 */
static void midi_ym2413_register_write (MIDI_Player_Context *context, uint8_t synth_id, uint8_t addr, uint8_t value)
{
    YM2413_Context *ym2413_context = context->ym2413_context [synth_id >> 5];
    uint8_t channel = synth_id & SYNTH_ID_CHANNEL_MASK;

    /* Automatically select the address for melody channel registers */
    if ((synth_id & SYNTH_ID_RHYTHM_BIT) == 0 && (addr == 0x10 || addr == 0x20 || addr == 0x30))
    {
        addr += channel;
    }

    ym2413_addr_write (ym2413_context, addr);
    ym2413_data_write (ym2413_context, value);
}


/*
 * Update the fnum and block for a ym2413 channel.
 */
static void midi_ym2413_update_fnum (MIDI_Player_Context *context, uint8_t synth_id, uint8_t key, uint16_t pitch_bend)
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
    midi_ym2413_register_write (context, synth_id, 0x10, fnum);

    /* Write the block, and remaining bit of fnum */
    uint8_t r20_value = midi_ym2413_register_read (context, synth_id, 0x20);
    r20_value &= 0xf0;
    r20_value |= (block << 1) | (fnum >> 8);
    midi_ym2413_register_write (context, synth_id, 0x20, r20_value);
}


/*
 * Calculate the 4-bit ym2413 volume from channel volume and velocity.
 */
static void midi_ym2413_update_volume (MIDI_Player_Context *context, uint8_t synth_id, uint8_t volume, uint8_t expression, uint8_t velocity)
{
    double attenuation = -40.0 * log10 (volume / 127.0)
                       + -40.0 * log10 (expression / 127.0)
                       + -40.0 * log10 (velocity / 127.0);

    /* Limit the attenuation to the ym2413 maximum */
    if (attenuation > 45.0)
    {
        attenuation = 45.0;
    }

    /* Each step on the ym2413 represents 3 dB. */
    uint8_t ym2413_volume = round (attenuation / 3.0);

    /* Write the volume into the ym2413 */
    if (synth_id & SYNTH_ID_RHYTHM_BIT)
    {
        uint8_t reg_value;
        switch (synth_id & SYNTH_ID_CHANNEL_MASK)
        {
            case RHYTHM_HH: /* High Hat */
                reg_value = midi_ym2413_register_read (context, synth_id, 0x37);
                reg_value &= 0x0f;
                reg_value |= ym2413_volume << 4;
                midi_ym2413_register_write (context, synth_id, 0x37, reg_value);

            case RHYTHM_TC: /* Top Cymbal */
                reg_value = midi_ym2413_register_read (context, synth_id, 0x38);
                reg_value &= 0xf0;
                reg_value |= ym2413_volume;
                midi_ym2413_register_write (context, synth_id, 0x38, reg_value);

            case RHYTHM_TT: /* Tom Tom */
                reg_value = midi_ym2413_register_read (context, synth_id, 0x38);
                reg_value &= 0x0f;
                reg_value |= ym2413_volume << 4;
                midi_ym2413_register_write (context, synth_id, 0x38, reg_value);

            case RHYTHM_SD: /* Snare Drum */
                reg_value = midi_ym2413_register_read (context, synth_id, 0x37);
                reg_value &= 0xf0;
                reg_value |= ym2413_volume;
                midi_ym2413_register_write (context, synth_id, 0x37, reg_value);

            case RHYTHM_BD: /* Bass Drum */
                reg_value = ym2413_volume;
                midi_ym2413_register_write (context, synth_id, 0x36, reg_value);

            default:
                break;
        }
    }
    else
    {
        uint8_t r30_value = midi_ym2413_register_read (context, synth_id, 0x30);
        r30_value &= 0xf0;
        r30_value |= ym2413_volume;
        midi_ym2413_register_write (context, synth_id, 0x30, r30_value);
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
    if (synth_id & SYNTH_ID_RHYTHM_BIT)
    {
        uint8_t r0e_value = midi_ym2413_register_read (context, synth_id, 0x0e);
        r0e_value &= ~(1 << (synth_id & SYNTH_ID_CHANNEL_MASK));
        midi_ym2413_register_write (context, synth_id, 0x0e, r0e_value);
    }
    else
    {
        /* Register write for key-up event on ym2413 */
        uint8_t r20_value = midi_ym2413_register_read (context, synth_id, 0x20);
        r20_value &= 0xef;
        midi_ym2413_register_write (context, synth_id, 0x20, r20_value);
    }

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
    midi_ym2413_register_write (context, synth_id, 0x30, midi_program_to_ym2413 [channel->program] << 4);
    midi_ym2413_update_volume (context, synth_id, channel->volume, channel->expression, velocity);

    /* Write the fnum and block */
    midi_ym2413_update_fnum (context, synth_id, key, channel->pitch_bend);

    /* Write the sustain, key-down, block, and remaining bit of fnum */
    uint8_t r20_value = midi_ym2413_register_read (context, synth_id, 0x20);
    r20_value &= 0x0f;
    r20_value |= (channel->sustain << 5) | 0x10;
    midi_ym2413_register_write (context, synth_id, 0x20, r20_value);
}


/*
 * Key down event (percussion)
 */
static void midi_player_percussion_down (MIDI_Player_Context *context, MIDI_Channel *channel, uint8_t key, uint8_t velocity)
{
    /* If the key was already down, generate an up event to free the previous synth channel. */
    if (channel->key [key] > 0)
    {
        midi_player_key_up (context, channel, key);
    }

    /* Only the General Midi percussion keys have been mapped */
    uint8_t instrument = midi_percussion_to_ym2413 [key];
    if (instrument == 0xff)
    {
        return;
    }

    /* If we don't have any free synth channels, drop the event */
    if (context->rhythm_queue_get [instrument] == context->rhythm_queue_put [instrument])
    {
        return;
    }

    /* Mark the key as down */
    channel->key [key] = velocity;

    /* Get the synth channel from the queue */
    uint8_t synth_id = midi_rhythm_queue_get (context, instrument);
    channel->synth_id [key] = synth_id;

    /* Set percussion volume */
    midi_ym2413_update_volume (context, synth_id, channel->volume, channel->expression, velocity);

    /* Set percussion key */
    uint8_t r0e_value = midi_ym2413_register_read (context, synth_id, 0x0e);
    r0e_value |= (1 << instrument);
    midi_ym2413_register_write (context, synth_id, 0x0e, r0e_value);
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
 * Draw a filled rectangle.
 */
static void draw_rect (MIDI_Player_Context *context,
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
static void midi_player_draw_frame (MIDI_Player_Context *context)
{
    /* Horizontal layout of the bars */
    /* MIDI playback uses ym2413 rhythm mode, so 11 bars per chip. */
    uint32_t bar_width = 12;
    uint32_t bar_gap = 8;
    uint32_t bar_area_width = bar_width * 11 + bar_gap * (11 - 1);
    uint32_t first_bar = (state.video_width - bar_area_width) / 2;

    for (uint32_t i = 0; i < MIDI_YM2413_COUNT; i++)
    {
        uint32_t bar_count = 0;
        uint32_t bar_value [11] = { };

        uint16_t volume;
        uint16_t eg_level;
        uint16_t attenuation;

        YM2413_Context *ym2413_context = context->ym2413_context [i];

        /* Melody Channels */
        for (uint32_t channel = 0; channel < 6; channel++)
        {
            volume  = ym2413_context->state.r30_channel_params [channel].volume;
            eg_level = ym2413_context->state.carrier [channel].eg_level;
            attenuation = volume + (eg_level >> 3);
            bar_value [bar_count++] = (attenuation >= 15) ? 0 : 15 - attenuation;
        }

        /* Bass Drum */
        volume = ym2413_context->state.rhythm_volume_bd;
        eg_level = ym2413_context->state.carrier [YM2413_BASS_DRUM_CH].eg_level;
        attenuation = volume + (eg_level >> 3);
        bar_value [bar_count++] = (attenuation >= 15) ? 0 : 15 - attenuation;

        /* High Hat */
        volume = ym2413_context->state.rhythm_volume_hh;
        eg_level = ym2413_context->state.modulator [YM2413_HIGH_HAT_CH].eg_level;
        attenuation = volume + (eg_level >> 3);
        bar_value [bar_count++] = (attenuation >= 15) ? 0 : 15 - attenuation;

        /* Snare Drum */
        volume = ym2413_context->state.rhythm_volume_sd;
        eg_level = ym2413_context->state.carrier [YM2413_SNARE_DRUM_CH].eg_level;
        attenuation = volume + (eg_level >> 3);
        bar_value [bar_count++] = (attenuation >= 15) ? 0 : 15 - attenuation;

        /* Tom Tom */
        volume = ym2413_context->state.rhythm_volume_tt;
        eg_level = ym2413_context->state.modulator [YM2413_TOM_TOM_CH].eg_level;
        attenuation = volume + (eg_level >> 3);
        bar_value [bar_count++] = (attenuation >= 15) ? 0 : 15 - attenuation;

        /* Top Cymbal */
        volume = ym2413_context->state.rhythm_volume_tc;
        eg_level = ym2413_context->state.carrier [YM2413_TOP_CYMBAL_CH].eg_level;
        attenuation = volume + (eg_level >> 3);
        bar_value [bar_count++] = (attenuation >= 15) ? 0 : 15 - attenuation;

        /* Draw the segments */
        uint32_t top = 8 + (68 * i);
        uint32_t low_segment = top + 52;
        for (uint32_t bar = 0; bar < bar_count; bar++)
        {
            for (uint32_t segment = bar_value [bar]; segment > 0; segment--)
            {
                draw_rect (context, first_bar + bar * (bar_width + bar_gap), low_segment - 4 * (segment - 1), bar_width, 2,
                           (segment == bar_value [bar]) ? colours_peak [segment - 1] : colours_base [segment - 1]);
            }
        }

        /* Underline */
        draw_rect (context, first_bar - 8, top + 58, bar_area_width + 16, 1, white);
    }

    /* Progress Bar */
    /* TODO: This approximation won't work well for multi-track MIDIs. Do we need to sum the delays and work out the duration? */
    uint32_t current_sample =context->track [0].index;
    uint32_t total_samples = context->track [0].track_end;
    draw_rect (context, 32, 216, 192, 1, dark_grey);
    uint32_t progress = current_sample * (uint64_t) 184 / total_samples;
    draw_rect (context, 32 + progress, 216, 8, 1, light_grey);

    /* Pass the completed frame on for rendering */
    snepulator_frame_done (context->frame_buffer);

    /* Clear the buffer for the next frame. */
    memset (context->frame_buffer, 0, sizeof (context->frame_buffer));
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
 * Set a MIDI RPN data value
 */
static void midi_set_rpn (MIDI_Player_Context *context, MIDI_Channel *channel, uint8_t controller, uint8_t value)
{
    if (controller == 96 || controller == 97)
    {
        printf ("MIDI: RPN increment/decrement not implemented.\n");
        return;
    }

    switch (channel->rpn)
    {
        case 0: /* Pitch Bend Sensitivity */
            printf ("MIDI: RPN %d (pitch-bend-sensitivity) not implemented.\n", channel->rpn);
            break;

        case 1: /* Channel Fine Tuning */
            printf ("MIDI: RPN %d (channel-fine-tuning) not implemented.\n", channel->rpn);
            break;

        case 2: /* Channel Coarse Tuning */
            printf ("MIDI: RPN %d (channel-coarse-tuning) not implemented.\n", channel->rpn);
            break;

        case 3: /* Select Tuning Program */
            printf ("MIDI: RPN %d (select-tuning-program) not implemented.\n", channel->rpn);
            break;

        case 4: /* Select Tuning Bank */
            printf ("MIDI: RPN %d (select-tuning-bank) not implemented.\n", channel->rpn);
            break;

        case 5: /* Modulation Depth Range */
            printf ("MIDI: RPN %d (modulation-depth-range) not implemented.\n", channel->rpn);
            break;

        case 0x3fff: /* Null Function */
        default:
            /* RPNs outside of the six defined are excluded from the switch statement are silently dropped. */
            break;
    }
}


/*
 * Set a MIDI controller value
 */
static void midi_set_controller (MIDI_Player_Context *context, MIDI_Channel *channel, uint8_t controller, uint8_t value)
{
    switch (controller)
    {
        case 1: /* Modulation */
            printf ("MIDI: Controller %d (modulation) not implemented.\n", controller);
            break;

        case 5: /* Portamento Time */
            printf ("MIDI: Controller %d (portamento-time) not implemented.\n", controller);
            break;

        case 6: /* Data Entry MSB */
            midi_set_rpn (context, channel, controller, value);
            break;

        case 7: /* Channel Volume */
            channel->volume = value;

            /* Update any in-progress notes */
            for (uint8_t key = 0; key < 128; key++)
            {
                if (channel->key [key] > 0)
                {
                    uint8_t synth_id = channel->synth_id [key];
                    midi_ym2413_update_volume (context, synth_id, channel->volume, channel->expression, channel->key [key]);
                }
            }
            break;

        case 11: /* Expression */
            channel->expression = value;

            /* Update any in-progress notes */
            for (uint8_t key = 0; key < 128; key++)
            {
                if (channel->key [key] > 0)
                {
                    uint8_t synth_id = channel->synth_id [key];
                    midi_ym2413_update_volume (context, synth_id, channel->volume, channel->expression, channel->key [key]);
                }
            }
            break;

        case 38: /* Data Entry LSB */
            midi_set_rpn (context, channel, controller, value);
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

                    /* Only for melody channels */
                    if (synth_id & SYNTH_ID_RHYTHM_BIT)
                    {
                        break;
                    }

                    uint8_t r20_value = midi_ym2413_register_read (context, synth_id, 0x20);
                    r20_value &= 0xdf;
                    r20_value |= channel->sustain << 5;
                    midi_ym2413_register_write (context, synth_id, 0x20, r20_value);
                }
            }
            break;

        case 65: /* Portamento switch */
            printf ("MIDI: Controller %d (portamento-switch) not implemented.\n", controller);
            break;

        case 66: /* Sostenuto */
            printf ("MIDI: Controller %d (sostenuto-switch) not implemented.\n", controller);
            break;

        case 67: /* Soft pedal */
            printf ("MIDI: Controller %d (soft-pedal) not implemented.\n", controller);
            break;

        case 68: /* Legato foot-switch */
            printf ("MIDI: Controller %d (legato-footswitch) not implemented.\n", controller);
            break;

        case 69: /* Hold 2 - Not implemented */
            printf ("MIDI: Controller %d (hold-2) not implemented.\n", controller);
            break;

        case 70: /* Sound Controller 1 - Sound Variation */
        case 71: /* Sound Controller 2 - Resonance */
        case 72: /* Sound Controller 3 - Release */
        case 73: /* Sound Controller 4 - Attack */
        case 74: /* Sound Controller 5 - Cut-off frequency */
            printf ("MIDI: Controller %d (sound-controller) not implemented.\n", controller);
            break;

        case 84: /* Portamento CC Control */
            printf ("MIDI: Controller %d (portamento-cc-control) not implemented.\n", controller);
            break;

        case 91: /* Effect 1 Depth - Reverb */
        case 92: /* Effect 2 Depth - Tremolo */
        case 93: /* Effect 3 Depth - Chorus */
        case 94: /* Effect 4 Depth - Detune */
        case 95: /* Effect 5 Depth - Phaser */
            printf ("MIDI: Controller %d (effect-depth) not implemented.\n", controller);
            break;

        case 96: /* Data Entry - Increment */
        case 97: /* Data Entry - Decrement */
            midi_set_rpn (context, channel, controller, value);
            break;


        case 98: /* NRPN LSB */
        case 99: /* NRPN MSB */
            channel->rpn = 0x3fff; /* Null function */
            break;

        case 100: /* RPN LSB */
            channel->rpn = (channel->rpn & 0x3f80) | value;
            break;

        case 101: /* RPN MSB */
            channel->rpn = (channel->rpn & 0x007f) | (value << 7);
            break;

        case 120: /* All Sound Off */
            printf ("MIDI: Controller %d (all-sound-off) not implemented.\n", controller);
            break;

        case 121: /* Reset All Controllers */
            printf ("MIDI: Controller %d (reset-all-controllers) not implemented.\n", controller);
            break;

        case 123: /* All Notes Off */
            printf ("MIDI: Controller %d (all-notes-off) not implemented.\n", controller);
            break;

        default:
            /* Controllers excluded from the switch statement are silently dropped.
             * This includes undefined values, unspecific things like "Generic On/Off switch", and effects
             * not suitable for the ym2413 such as bank-select, balance, and high-resolution values. */
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
                midi_player_key_up (context, &track->channel [channel], key);
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
                    midi_player_key_up (context, &track->channel [channel], key);
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

                /* Only for melody channels */
                if (channel == 9)
                {
                    break;
                }

                /* Pitch bend needs to be applied to notes which are already sounding */
                for (uint8_t key = 0; key < 128; key++)
                {
                    if (track->channel [channel].key [key] > 0)
                    {
                        uint8_t synth_id = track->channel [channel].synth_id [key];
                        midi_ym2413_update_fnum (context, synth_id, key, track->channel [channel].pitch_bend);
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

    return 0;
}


/*
 * Run the MIDI player for the specified length of time.
 * Clock-rate is the NTSC Colourburst frequency.
 * Called with the run_mutex held.
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
        midi_player_draw_frame (context);
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
            context->track [i].channel [channel].volume = 100;
            context->track [i].channel [channel].expression = 127;
            context->track [i].channel [channel].rpn = 0x3fff;
        }
    }

    fprintf (stdout, "%d KiB MIDI %s loaded.\n", context->midi_size >> 10, state.cart_filename);

    /* Calculate length of a midi-tick in colourburst clocks */
    midi_update_tick_length (context);
    if (state.run == RUN_STATE_ERROR)
    {
        free (context);
        return NULL;
    }

    /* Initialise sound chip */
    for (uint32_t chip = 0; chip < MIDI_YM2413_COUNT; chip++)
    {
        YM2413_Context *ym2413_context = ym2413_init ();
        context->ym2413_context [chip] = ym2413_context;

        /* Enable rhythm mode and set recommended fnum & block parameters. */
        midi_ym2413_register_write (context, (chip << 5), 0x0e, 0x20);
        midi_ym2413_register_write (context, (chip << 5), 0x16, 0x20);
        midi_ym2413_register_write (context, (chip << 5), 0x26, 0x05);
        midi_ym2413_register_write (context, (chip << 5), 0x17, 0x50);
        midi_ym2413_register_write (context, (chip << 5), 0x27, 0x05);
        midi_ym2413_register_write (context, (chip << 5), 0x18, 0xc0);
        midi_ym2413_register_write (context, (chip << 5), 0x28, 0x01);

        /* Add the six melody channels to the synth-queue */
        for (uint32_t channel = 0; channel < 6; channel++)
        {
            midi_synth_queue_put (context, (chip << 5) + channel);
        }

        /* Add the five rhythm instruments to the five rhythm queues */
        for (uint32_t instrument = 0; instrument < 5; instrument++)
        {
            midi_synth_queue_put (context, (chip << 5) | SYNTH_ID_RHYTHM_BIT | instrument);
        }
    }

    /* Initial video parameters */
    state.video_start_x = VIDEO_SIDE_BORDER;
    state.video_start_y = (VIDEO_BUFFER_LINES - 224) / 2;
    state.video_width   = 256;
    state.video_height  = 224;

    /* Hook up callbacks */
    state.audio_callback = midi_player_audio_callback;
    state.cleanup = midi_player_cleanup;
    state.run_callback = midi_player_run;

    /* Start playing */
    state.clock_rate = NTSC_COLOURBURST_FREQ;
    state.run = RUN_STATE_RUNNING;

    return context;
}
