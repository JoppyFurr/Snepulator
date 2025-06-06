/*
 * Snepulator
 * YM2413 FM synthesizer chip implementation.
 *
 * Based on reverse engineering documents by Andete,
 * and some experiments of my own with a ym2413 chip.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../snepulator.h"
#include "../util.h"
#include "../save_state.h"

extern Snepulator_State state;

#include "ym2413.h"

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

/* Represents the level of a single melody channel at maximum volume */
#define BASE_VOLUME 4096

/* Use a special type definition to mark sign-magnitude numbers.
 * The most significant bit is used to indicate if the number is negative. */
#define SIGN_BIT 0x8000
#define MAG_BITS 0x7fff
typedef uint16_t signmag16_t;

static uint32_t exp_table [256] = { };
static uint32_t log_sin_table [256] = { };
static uint32_t am_table [210] = { };

/* Note: Values are doubled when compared to the
 * datasheet to deal with the first entry being ½ */
static const uint32_t factor_table [16] = {
     1,  2,  4,  6,  8, 10, 12, 14,
    16, 18, 20, 20, 24, 24, 30, 30
};

static const int8_t vibrato_table [8] [8] = {
    { 0, 0, 0, 0, 0,  0,  0,  0 },
    { 0, 0, 1, 0, 0,  0, -1,  0 },
    { 0, 1, 2, 1, 0, -1, -2, -1 },
    { 0, 1, 3, 1, 0, -1, -3, -1 },
    { 0, 2, 4, 2, 0, -2, -4, -2 },
    { 0, 2, 5, 2, 0, -2, -5, -2 },
    { 0, 3, 6, 3, 0, -3, -6, -3 },
    { 0, 3, 7, 3, 0, -3, -7, -3 }
};

static const uint8_t instrument_rom [15] [8] = {
    { 0x71, 0x61, 0x1E, 0x17, 0xD0, 0x78, 0x00, 0x17 }, /* Violin */
    { 0x13, 0x41, 0x1A, 0x0D, 0xD8, 0xF7, 0x23, 0x13 }, /* Guitar */
    { 0x13, 0x01, 0x99, 0x00, 0xF2, 0xC4, 0x11, 0x23 }, /* Piano */
    { 0x31, 0x61, 0x0E, 0x07, 0xA8, 0x64, 0x70, 0x27 }, /* Flute */
    { 0x32, 0x21, 0x1E, 0x06, 0xE0, 0x76, 0x00, 0x28 }, /* Clarinet */
    { 0x31, 0x22, 0x16, 0x05, 0xE0, 0x71, 0x00, 0x18 }, /* Oboe */
    { 0x21, 0x61, 0x1D, 0x07, 0x82, 0x81, 0x10, 0x07 }, /* Trumpet */
    { 0x23, 0x21, 0x2D, 0x14, 0xA2, 0x72, 0x00, 0x07 }, /* Organ */
    { 0x61, 0x61, 0x1B, 0x06, 0x64, 0x65, 0x10, 0x17 }, /* Horn */
    { 0x41, 0x61, 0x0B, 0x18, 0x85, 0xF7, 0x71, 0x07 }, /* Synthesizer */
    { 0x13, 0x01, 0x83, 0x11, 0xFA, 0xE4, 0x10, 0x04 }, /* Harpsichord */
    { 0x17, 0xC1, 0x24, 0x07, 0xF8, 0xF8, 0x22, 0x12 }, /* Vibraphone */
    { 0x61, 0x50, 0x0C, 0x05, 0xC2, 0xF5, 0x20, 0x42 }, /* Synthesizer Bass */
    { 0x01, 0x01, 0x55, 0x03, 0xC9, 0x95, 0x03, 0x02 }, /* Wood Bass */
    { 0x61, 0x41, 0x89, 0x03, 0xF1, 0xE4, 0x40, 0x13 }  /* Electric Guitar */
};

static const uint8_t rhythm_rom [3] [8] = {
    { 0x01, 0x01, 0x18, 0x0f, 0xdf, 0xf8, 0x6a, 0x6d }, /* Bass Drum */
    { 0x01, 0x01, 0x00, 0x00, 0xc8, 0xd8, 0xa7, 0x48 }, /* Snare Drum / High Hat */
    { 0x05, 0x01, 0x00, 0x00, 0xf8, 0xaa, 0x59, 0x55 }, /* Tom Tom / Top Cymbal */
};


/*
 * Convert signmag16_t to int16_t.
 */
static inline int16_t signmag_convert (signmag16_t value)
{
    if (value & SIGN_BIT)
    {
        return -(value & MAG_BITS);
    }
    else
    {
        return value;
    }
}


/*
 * Handle changes driven by writes to channel registers.
 * Calculate values that only change when the register values change.
 */
static void ym2413_handle_channel_update (YM2413_Context *context, uint8_t channel)
{
    /* Melody calculations */
    if (channel < ((context->state.rhythm_mode) ? 6 : 9))
    {
        /* Key-on / key-off */
        bool key_on = context->state.r20_channel_params [channel].key_on;
        YM2413_Operator_State *modulator = &context->state.modulator [channel];
        YM2413_Operator_State *carrier = &context->state.carrier [channel];

        /* Key-on: Transition to DAMP */
        if (key_on && carrier->eg_state == YM2413_STATE_RELEASE)
        {
            modulator->eg_state = YM2413_STATE_DAMP;
            carrier->eg_state = YM2413_STATE_DAMP;
        }
        /* Key-off: Transition to RELEASE */
        else if (!key_on)
        {
            modulator->eg_state = YM2413_STATE_RELEASE;
            carrier->eg_state = YM2413_STATE_RELEASE;
        }

        uint16_t inst = context->state.r30_channel_params [channel].instrument;
        YM2413_Instrument *instrument = (inst == 0) ? &context->state.regs_custom
                                                    : (YM2413_Instrument *) instrument_rom [inst - 1];

        /* Calculate modulator effective rates */
        YM2413_Envelope_Params *modulator_envelope = &context->calculated [channel].modulator_envelope;
        bool sustain = context->state.r20_channel_params [channel].sustain;
        uint32_t modulator_key_scale_rate = context->state.r20_channel_params [channel].r20_channel_params & 0x0f;
        if (instrument->modulator_key_scale_rate == 0)
        {
            modulator_key_scale_rate >>= 2;
        }
        modulator_envelope->effective_damp = 48 + modulator_key_scale_rate;
        modulator_envelope->effective_attack = (instrument->modulator_attack_rate << 2) + modulator_key_scale_rate;
        modulator_envelope->effective_decay = (instrument->modulator_decay_rate == 0) ? 0
                                            : (instrument->modulator_decay_rate << 2) + modulator_key_scale_rate;
        modulator_envelope->effective_sustain_level = instrument->modulator_sustain_level << 3;

        if (instrument->modulator_envelope_type == 0)
        {
            /* Percussive Tone */
            modulator_envelope->effective_release_1 = (instrument->modulator_release_rate == 0) ? 0
                                                    : (instrument->modulator_release_rate << 2) + modulator_key_scale_rate;
            modulator_envelope->effective_release_2 = 0; /* Modulator envelope only runs when the key is pressed */
        }
        else
        {
            /* Sustained Tone */
            modulator_envelope->effective_release_1 = 0;
            modulator_envelope->effective_release_2 = 0; /* Modulator envelope only runs when the key is pressed */
        }

        /* Calculate carrier effective rates */
        YM2413_Envelope_Params *carrier_envelope = &context->calculated [channel].carrier_envelope;
        uint32_t carrier_key_scale_rate = context->state.r20_channel_params [channel].r20_channel_params & 0x0f;
        if (instrument->carrier_key_scale_rate == 0)
        {
            carrier_key_scale_rate >>= 2;
        }
        carrier_envelope->effective_damp = 48 + carrier_key_scale_rate;
        carrier_envelope->effective_attack = (instrument->carrier_attack_rate << 2) + carrier_key_scale_rate;
        carrier_envelope->effective_decay = (instrument->carrier_decay_rate == 0) ? 0
                                          : (instrument->carrier_decay_rate << 2) + carrier_key_scale_rate;
        carrier_envelope->effective_sustain_level = instrument->carrier_sustain_level << 3;

        if (instrument->carrier_envelope_type == 0)
        {
            /* Percussive Tone */
            carrier_envelope->effective_release_1 = (instrument->carrier_release_rate == 0) ? 0
                                                  : (instrument->carrier_release_rate << 2) + carrier_key_scale_rate;
            carrier_envelope->effective_release_2 = ((sustain ? 5 : 7) << 2) + carrier_key_scale_rate;
        }
        else
        {
            /* Sustained Tone */
            carrier_envelope->effective_release_1 = 0;
            carrier_envelope->effective_release_2 = (instrument->carrier_release_rate == 0 && !sustain) ? 0
                                                  : ((sustain ? 5 : instrument->carrier_release_rate) << 2) + carrier_key_scale_rate;
        }
    }

    /* Bass Drum */
    else if (context->state.rhythm_mode && channel == 6)
    {
        YM2413_Instrument *instrument = (YM2413_Instrument *) rhythm_rom [0];
        uint32_t key_scale_rate = (context->state.r20_channel_params [6].r20_channel_params & 0x0f) >> 2;
        bool sustain = context->state.r20_channel_params [6].sustain;

        /* Calculate modulator effective rates */
        YM2413_Envelope_Params *modulator_envelope = &context->calculated [6].modulator_envelope;
        modulator_envelope->effective_damp = 48 + key_scale_rate;
        modulator_envelope->effective_attack = (instrument->modulator_attack_rate << 2) + key_scale_rate;
        modulator_envelope->effective_decay = (instrument->modulator_decay_rate << 2) + key_scale_rate;
        modulator_envelope->effective_sustain_level = instrument->modulator_sustain_level << 3;
        modulator_envelope->effective_release_1 = (instrument->modulator_release_rate << 2) + key_scale_rate;
        modulator_envelope->effective_release_2 = 0;

        /* Calculate carrier effective rates */
        YM2413_Envelope_Params *carrier_envelope = &context->calculated [6].carrier_envelope;
        carrier_envelope->effective_damp = 48 + key_scale_rate;
        carrier_envelope->effective_attack = (instrument->carrier_attack_rate << 2) + key_scale_rate;
        carrier_envelope->effective_decay = (instrument->carrier_decay_rate << 2) + key_scale_rate;
        carrier_envelope->effective_sustain_level = instrument->carrier_sustain_level << 3;
        carrier_envelope->effective_release_1 = (instrument->carrier_release_rate << 2) + key_scale_rate;
        carrier_envelope->effective_release_2 = ((sustain ? 5 : 7) << 2) + key_scale_rate;
    }
    /* High Hat / Snare Drum */
    else if (context->state.rhythm_mode && channel == 7)
    {
        YM2413_Instrument *instrument = (YM2413_Instrument *) rhythm_rom [1];
        uint16_t key_scale_rate = (context->state.r20_channel_params [7].r20_channel_params & 0x0f) >> 2;
        bool sustain = context->state.r20_channel_params [7].sustain;

        /* Calculate High Hat effective rates */
        YM2413_Envelope_Params *high_hat_envelope = &context->calculated [7].modulator_envelope;
        high_hat_envelope->effective_damp = 48 + key_scale_rate;
        high_hat_envelope->effective_attack = (instrument->modulator_attack_rate << 2) + key_scale_rate;
        high_hat_envelope->effective_decay = (instrument->modulator_decay_rate << 2) + key_scale_rate;
        high_hat_envelope->effective_sustain_level = instrument->modulator_sustain_level << 3;
        high_hat_envelope->effective_release_1 = (instrument->modulator_release_rate << 2) + key_scale_rate;
        high_hat_envelope->effective_release_2 = ((sustain ? 5 : 7) << 2) + key_scale_rate;

        /* Calculate Snare Drum effective rates */
        YM2413_Envelope_Params *snare_drum_envelope = &context->calculated [7].carrier_envelope;
        snare_drum_envelope->effective_damp = 48 + key_scale_rate;
        snare_drum_envelope->effective_attack = (instrument->carrier_attack_rate << 2) + key_scale_rate;
        snare_drum_envelope->effective_decay = (instrument->carrier_decay_rate << 2) + key_scale_rate;
        snare_drum_envelope->effective_sustain_level = instrument->carrier_sustain_level << 3;
        snare_drum_envelope->effective_release_1 = (instrument->carrier_release_rate << 2) + key_scale_rate;
        snare_drum_envelope->effective_release_2 = ((sustain ? 5 : 7) << 2) + key_scale_rate;
    }
    /* Tom Tom / Top Cymbal */
    else if (context->state.rhythm_mode && channel == 8)
    {
        YM2413_Instrument *instrument = (YM2413_Instrument *) rhythm_rom [2];
        uint16_t key_scale_rate = (context->state.r20_channel_params [8].r20_channel_params & 0x0f) >> 2;
        bool sustain = context->state.r20_channel_params [8].sustain;

        /* Calculate Tom Tom effective rates */
        YM2413_Envelope_Params *tom_tom_envelope = &context->calculated [8].modulator_envelope;
        tom_tom_envelope->effective_damp = 48 + key_scale_rate;
        tom_tom_envelope->effective_attack = (instrument->modulator_attack_rate << 2) + key_scale_rate;
        tom_tom_envelope->effective_decay = (instrument->modulator_decay_rate << 2) + key_scale_rate;
        tom_tom_envelope->effective_sustain_level = instrument->modulator_sustain_level << 3;
        tom_tom_envelope->effective_release_1 = (instrument->modulator_release_rate << 2) + key_scale_rate;
        tom_tom_envelope->effective_release_2 = ((sustain ? 5 : instrument->modulator_release_rate) << 2) + key_scale_rate;

        /* Calculate Top Cymbal effective rates */
        YM2413_Envelope_Params *top_cymbal_envelope = &context->calculated [8].carrier_envelope;
        top_cymbal_envelope->effective_damp = 48 + key_scale_rate;
        top_cymbal_envelope->effective_attack = (instrument->carrier_attack_rate << 2) + key_scale_rate;
        top_cymbal_envelope->effective_decay = (instrument->carrier_decay_rate << 2) + key_scale_rate;
        top_cymbal_envelope->effective_sustain_level = instrument->carrier_sustain_level << 3;
        top_cymbal_envelope->effective_release_1 = (instrument->carrier_release_rate << 2) + key_scale_rate;
        top_cymbal_envelope->effective_release_2 = ((sustain ? 5 : 7) << 2) + key_scale_rate;
    }
}


/*
 * Handle key-on / key-off events for rhythm instruments.
 */
static void ym2413_handle_rhythm_keys (YM2413_Context *context)
{
    if (context->state.rhythm_mode)
    {
        bool bass_drum_key  = context->state.rhythm_key_bd || context->state.r20_channel_params [YM2413_BASS_DRUM_CH].key_on;
        bool high_hat_key   = context->state.rhythm_key_hh || context->state.r20_channel_params [YM2413_HIGH_HAT_CH].key_on;
        bool snare_drum_key = context->state.rhythm_key_sd || context->state.r20_channel_params [YM2413_SNARE_DRUM_CH].key_on;
        bool tom_tom_key    = context->state.rhythm_key_tt || context->state.r20_channel_params [YM2413_TOM_TOM_CH].key_on;
        bool top_cymbal_key = context->state.rhythm_key_tc || context->state.r20_channel_params [YM2413_TOP_CYMBAL_CH].key_on;

        /* Bass Drum */
        if (bass_drum_key && context->state.carrier [YM2413_BASS_DRUM_CH].eg_state == YM2413_STATE_RELEASE)
        {
             context->state.modulator [YM2413_BASS_DRUM_CH].eg_state = YM2413_STATE_DAMP;
             context->state.carrier [YM2413_BASS_DRUM_CH].eg_state = YM2413_STATE_DAMP;
        }
        else if (!bass_drum_key)
        {
             context->state.carrier [YM2413_BASS_DRUM_CH].eg_state = YM2413_STATE_RELEASE;
        }

        /* High Hat */
        if (high_hat_key && context->state.modulator [YM2413_HIGH_HAT_CH].eg_state == YM2413_STATE_RELEASE)
        {
            context->state.modulator [YM2413_HIGH_HAT_CH].eg_state = YM2413_STATE_DAMP;
        }
        else if (!high_hat_key)
        {
            context->state.modulator [YM2413_HIGH_HAT_CH].eg_state = YM2413_STATE_RELEASE;
        }

        /* Snare Drum */
        if (snare_drum_key && context->state.carrier [YM2413_SNARE_DRUM_CH].eg_state == YM2413_STATE_RELEASE)
        {
            context->state.carrier [YM2413_SNARE_DRUM_CH].eg_state = YM2413_STATE_DAMP;
        }
        else if (!snare_drum_key)
        {
            context->state.carrier [YM2413_SNARE_DRUM_CH].eg_state = YM2413_STATE_RELEASE;
        }

        /* Tom Tom */
        if (tom_tom_key && context->state.modulator [YM2413_TOM_TOM_CH].eg_state == YM2413_STATE_RELEASE)
        {
            context->state.modulator [YM2413_TOM_TOM_CH].eg_state = YM2413_STATE_DAMP;
        }
        else if (!tom_tom_key)
        {
            context->state.modulator [YM2413_TOM_TOM_CH].eg_state = YM2413_STATE_RELEASE;
        }

        /* Top Cymbal */
        if (top_cymbal_key && context->state.carrier [YM2413_TOP_CYMBAL_CH].eg_state == YM2413_STATE_RELEASE)
        {
            context->state.carrier [YM2413_TOP_CYMBAL_CH].eg_state = YM2413_STATE_DAMP;
        }
        else if (!top_cymbal_key)
        {
            context->state.carrier [YM2413_TOP_CYMBAL_CH].eg_state = YM2413_STATE_RELEASE;
        }
    }
}


/*
 * Write data to the latched register address.
 */
void ym2413_data_write (YM2413_Context *context, uint8_t data)
{
    uint8_t addr = context->state.addr_latch;

    pthread_mutex_lock (&context->mutex);

    if (addr >= 0x00 && addr <= 0x07)
    {
        uint32_t melody_channels = (context->state.rhythm_mode) ? 6 : 9;

        ((uint8_t *) &context->state.regs_custom.r00) [addr] = data;

        for (int channel = 0; channel < melody_channels; channel++)
        {
            if (context->state.r30_channel_params [channel].instrument == 0)
            {
                ym2413_handle_channel_update (context, channel);
            }
        }
    }
    else if (addr == 0x0e)
    {
        bool rhythm_mode_was = !!context->state.rhythm_mode;
        context->state.r0e_rhythm = data;

        /* Handle transitions in and out of rhythm mode */
        if (rhythm_mode_was == false && context->state.rhythm_mode == true)
        {
            /* Some of the operators don't normally experience release,
             * so put them into release when entering rhythm mode. */
            context->state.modulator [YM2413_HIGH_HAT_CH].eg_state = YM2413_STATE_RELEASE;
            context->state.modulator [YM2413_HIGH_HAT_CH].eg_level = 127;
            context->state.modulator [YM2413_TOM_TOM_CH].eg_state = YM2413_STATE_RELEASE;
            context->state.modulator [YM2413_TOM_TOM_CH].eg_level = 127;

            ym2413_handle_channel_update (context, 6);
            ym2413_handle_channel_update (context, 7);
            ym2413_handle_channel_update (context, 8);
        }
        else if (rhythm_mode_was == true && context->state.rhythm_mode == false)
        {
            ym2413_handle_channel_update (context, 6);
            ym2413_handle_channel_update (context, 7);
            ym2413_handle_channel_update (context, 8);
        }

        ym2413_handle_rhythm_keys (context);
    }
    else if (addr == 0x0f)
    {
        context->state.r0f_test = data;
    }
    else if (addr >= 0x10 && addr <= 0x19)
    {
        ((uint8_t *) &context->state.r10_channel_params) [addr - 0x10] = data;
        ym2413_handle_channel_update (context, addr - 0x10);
    }
    else if (addr >= 0x20 && addr <= 0x29)
    {
        ((uint8_t *) &context->state.r20_channel_params) [addr - 0x20] = data;
        ym2413_handle_channel_update (context, addr - 0x20);
        if ((addr - 0x10) >= YM2413_BASS_DRUM_CH)
        {
            ym2413_handle_rhythm_keys (context);
        }
    }
    else if (addr >= 0x30 && addr <= 0x39)
    {
        ((uint8_t *) &context->state.r30_channel_params) [addr - 0x30] = data;
        ym2413_handle_channel_update (context, addr - 0x30);
    }

    pthread_mutex_unlock (&context->mutex);
}


/*
 * Latch a register address.
 */
void ym2413_addr_write (YM2413_Context *context, uint8_t addr)
{
    /* Register mirroring */
    if ((addr >= 0x19 && addr <= 0x1f) ||
        (addr >= 0x29 && addr <= 0x2f) ||
        (addr >= 0x39 && addr <= 0x3f))
    {
        addr -= 0x09;
    }

    context->state.addr_latch = addr;
}


/*
 * Populate the exp () table.
 * Note that we keep the always-set bit-10.
 */
static void ym2413_populate_exp_table (void)
{
    for (int i = 0; i < 256; i++)
    {
        exp_table [i] = round (exp2 (i / 256.0) * 1024);
    }
}


/*
 * Lookup an entry using the exp table.
 * Input is fixed-point with 8 fractional bits.
 * Output range is ±4084.
 */
static signmag16_t ym2413_exp (signmag16_t val)
{
    /* Note that the index is inverted to account
     * for the log-sine table using -log2. */
    uint8_t fractional = ~(val & 0xff);
    uint16_t integral = (val & MAG_BITS) >> 8;

    int16_t result = (exp_table [fractional] << 1) >> integral;

    /* Propagate the sign */
    result |= (val & SIGN_BIT);

    return result;
}


/*
 * Populate the log (sin ()) table.
 * Fixed-point with 8 fractional bits.
 */
static void ym2413_populate_log_sin_table (void)
{
    for (int i = 0; i < 256; i++)
    {
        log_sin_table [i] = round (-log2 (sin ((i + 0.5) * M_PI / 2.0 / 256.0)) * 256.0);
    }
}


/*
 * Lookup an entry from the log-sin table.
 * A 10-bit phase is used to index the table.
 * As the 256-entry table stores only the first quarter
 * of the sine wave, mirroring and flipping is used to
 * give a 1024-entry waveform.
 */
static signmag16_t ym2413_sin (uint16_t phase)
{
    uint8_t index = phase & 0xff;

    /* Mirror the table for the 2nd and 4th quarter of the wave.
     * Instead of negating then number, we invert the bits to
     * account for the wave samples representing 0.5 - 255.5. */
    if (phase & (1 << 8))
    {
        index = ~index;
    }

    int16_t result = log_sin_table [index & 0xff];

    /* The second half of the sine wave is identical to the
     * first, but with the sign bit set to indicate that
     * values are negative. To avoid branching, this is done
     * by shifting the phase MSB into the sign bit position. */
    result |= (phase << 6) & SIGN_BIT;

    return result;
}


/*
 * Populate the am table.
 * The 210-entry table defines the triangle wave used for AM.
 */
static void ym2413_populate_am_table (void)
{
    uint8_t index = 0;

    for (int i = 0; i < 15; i++)
    {
        am_table [index++] = 0;
    }
    for (int value = 1; value <= 12; value++)
    {
        for (int i = 0; i < 8; i++)
        {
            am_table [index++] = value << 4;
        }
    }
    for (int i = 0; i < 3; i++)
    {
        am_table [index++] = 13 << 4;
    }
    for (int value = 12; value >= 1; value--)
    {
        for (int i = 0; i < 8; i++)
        {
            am_table [index++] = value << 4;
        }
    }
}


static uint8_t ksl_table [16] = {
     0,  48,  64,  74,  80,  86,  90,  94,
    96, 100, 102, 104, 106, 108, 110, 112
};


/*
 * Calculate the ksl value for the current note.
 */
static uint8_t ym2413_ksl (uint8_t ksl, uint8_t block, uint16_t fnum)
{
    int16_t level = ksl_table [fnum >> 5] - 16 * (7 - block);

    if (ksl == 0 || level <= 0)
    {
        return 0;
    }

    return level >> (3 - ksl);
}


static uint8_t eg_step_table [4] [8] = {
    { 0, 1, 0, 1, 0, 1, 0, 1 }, /* 4 of 8 */
    { 0, 1, 0, 1, 1, 1, 0, 1 }, /* 5 of 8 */
    { 0, 1, 1, 1, 0, 1, 1, 1 }, /* 6 of 8 */
    { 0, 1, 1, 1, 1, 1, 1, 1 }, /* 7 of 8 */
};


static uint8_t eg_step_table_fast_decay [4] [8] = {
    { 1, 1, 1, 1, 1, 1, 1, 1 }, /* 0 of 8 are +2 */
    { 2, 1, 1, 1, 2, 1, 1, 1 }, /* 2 of 8 are +2 */
    { 2, 1, 2, 1, 2, 1, 2, 1 }, /* 4 of 8 are +2 */
    { 2, 2, 2, 1, 2, 2, 2, 1 }, /* 6 of 8 are +2 */
};


/*
 * Calculate the new eg_level during the attack phase of the envelope.
 * Input:  Current level, 6-bit effective rate.
 * Output: New level.
 */
static uint16_t ym2413_attack (YM2413_Context *context, uint16_t current_level, uint16_t rate)
{
    /* Attack never progresses when the rate is below four */
    if (rate < 4)
    {
        return current_level;
    }
    else if (rate < 48)
    {
        /* Note: The two least-significant bits are ignored here, as described in Andete's
         * reverse engineering documents.
         * A possible explanation for this decision may be that including them in the check
         * would force table_col to be 0, which will never trigger a change in level. As it
         * is, only the left half of the table can be reached. */
        if (((context->state.global_counter & (0x1fff >> (rate >> 2))) & 0x1ffc) == 0)
        {
            uint8_t table_row = rate & 0x03;
            uint8_t table_col = context->state.global_counter & 0x07;
            if (eg_step_table [table_row] [table_col])
            {
                return current_level - (current_level >> 4) - 1;
            }
        }
    }
    else if (rate < 60)
    {
        uint8_t table_row = rate & 3;
        uint8_t table_col = (context->state.global_counter >> 1) & 0x06;
        uint8_t n = 16 - (rate >> 2);

        if (eg_step_table [table_row] [table_col])
        {
            n -= 1;
        }

        return current_level - (current_level >> n) - 1;
    }

    /* No change */
    return current_level;
}


/*
 * Calculate the number of eg_level steps to decay by.
 * Note that for rates 52..59, we don't copy the hardware behaviour exactly.
 * Instead, values are chosen that should result in a smoother curve.
 * Input:  6-bit effective rate.
 * Output: 0, 1, or 2 steps.
 */
static uint16_t ym2413_decay (YM2413_Context *context, uint16_t rate)
{
    uint32_t global_counter = context->state.global_counter;

    /* Never decay for rates below four */
    if (rate < 4)
    {
        return 0;
    }
    /* Zero or one step for rate 4..55 */
    else if (rate < 56)
    {
        /* Only do a table lookup if the bits we shift off the global
         * counter are all zeros. */
        if ((global_counter & (0x1fff >> (rate >> 2))) == 0)
        {
            uint8_t table_row = rate & 0x03;
            uint8_t table_col = (global_counter >> (13 - (rate >> 2))) & 0x07;
            return eg_step_table [table_row] [table_col];
        }
    }
    /* One or two steps for rate 56..59 */
    else if (rate < 60)
    {
        uint8_t table_row = rate & 0x03;
        uint8_t table_col = global_counter & 0x07;
        return eg_step_table_fast_decay [table_row] [table_col];
    }
    /* Always decay by two steps for rates 60+ */
    else
    {
        return 2;
    }

    /* Zero steps if the global counter didn't trigger an earlier return */
    return 0;
}


/*
 * Run the envelope generator for one sample of an operator.
 *
 * Returns true on the damp->attack transition to indicate that phase may need to be reset.
 */
static bool ym2413_envelope_cycle (YM2413_Context *context, YM2413_Operator_State *operator,
                                   YM2413_Envelope_Params *params)
{
    bool phase_reset = false;

    /* Damp->Attack Transition */
    if (operator->eg_state == YM2413_STATE_DAMP && operator->eg_level >= 120)
    {
        operator->eg_state = YM2413_STATE_ATTACK;
        phase_reset = true;

        /* Skip the attack phase if the rate is high enough */
        if (params->effective_attack >= 60)
        {
            operator->eg_state = YM2413_STATE_DECAY;
            operator->eg_level = 0;
        }
    }

    switch (operator->eg_state)
    {
        case YM2413_STATE_DAMP:
            operator->eg_level += ym2413_decay (context, params->effective_damp);
            break;

        case YM2413_STATE_ATTACK:
            operator->eg_level = ym2413_attack (context, operator->eg_level, params->effective_attack);
            if (operator->eg_level == 0)
            {
                operator->eg_state = YM2413_STATE_DECAY;
            }
            break;

        case YM2413_STATE_DECAY:
            operator->eg_level += ym2413_decay (context, params->effective_decay);
            if (operator->eg_level >= params->effective_sustain_level)
            {
                operator->eg_state = YM2413_STATE_SUSTAIN;
            }
            break;

        case YM2413_STATE_SUSTAIN:
            operator->eg_level += ym2413_decay (context, params->effective_release_1);
            break;

        case YM2413_STATE_RELEASE:
            operator->eg_level += ym2413_decay (context, params->effective_release_2);
            break;
    }

    ENFORCE_MAXIMUM (operator->eg_level, 127);
    return phase_reset;
}


/*
 * Run one sample of a YM2413 channel.
 */
static int16_t ym2413_run_channel_sample (YM2413_Context *context, uint16_t channel, YM2413_Instrument *instrument,
                                          bool key_on, uint16_t volume)
{
    YM2413_Operator_State *modulator = &context->state.modulator [channel];
    YM2413_Operator_State *carrier = &context->state.carrier [channel];

    /* Channel parameters */
    uint16_t fnum    = context->state.r10_channel_params [channel].fnum |
          (((uint16_t) context->state.r20_channel_params [channel].fnum_9) << 8);
    uint16_t block   = context->state.r20_channel_params [channel].block;

    int16_t fm = vibrato_table [fnum >> 6] [(context->state.global_counter >> 10) & 0x07];

    /* Run envelope generator */
    ym2413_envelope_cycle (context, modulator, &context->calculated [channel].modulator_envelope);
    if (ym2413_envelope_cycle (context, carrier, &context->calculated [channel].carrier_envelope))
    {
        carrier->phase = 0;
        modulator->phase = 0;
    }

    /* Modulator Phase */
    uint16_t factor = factor_table [instrument->modulator_multiplication_factor];
    int16_t modulator_fm = (instrument->modulator_vibrato) ? fm : 0;
    modulator->phase += ((((fnum << 1) + modulator_fm) * factor) << block) >> 2;

    /* Modulator Output */
    uint16_t total_level = instrument->modulator_total_level;
    uint16_t feedback = 0;
    if (instrument->modulator_feedback_level)
    {
        feedback = (context->state.feedback [channel] [0] +
                    context->state.feedback [channel] [1]) >> (9 - instrument->modulator_feedback_level);
    }
    signmag16_t log_modulator_value = ym2413_sin ((modulator->phase >> 9) + feedback);
    log_modulator_value += total_level << 5;
    log_modulator_value += modulator->eg_level << 4;
    log_modulator_value += ym2413_ksl (instrument->modulator_key_scale_level, block, fnum) << 4;
    log_modulator_value += (instrument->modulator_am) ? context->state.am_value : 0;
    uint16_t modulator_value = ym2413_exp (log_modulator_value);

    if (modulator_value & SIGN_BIT)
    {
        /* When the 'waveform' bit is set, the negative half of the wave is flattened to zero. */
        modulator_value = instrument->modulator_waveform ? 0 : -(modulator_value & MAG_BITS);
    }

    /* Feedback is stored after the waveform bit is applied. */
    context->state.feedback [channel] [context->state.global_counter % 2] = modulator_value;

    /* Carrier Phase */
    factor = factor_table [instrument->carrier_multiplication_factor];
    int16_t carrier_fm = (instrument->carrier_vibrato) ? fm : 0;
    carrier->phase += ((((fnum << 1) + carrier_fm) * factor) << block) >> 2;

    /* If the EG level is above the threshold , no sound is output */
    if (carrier->eg_level >= 124)
    {
        return 0;
    }

    /* Carrier Output */
    signmag16_t log_carrier_value = ym2413_sin ((carrier->phase >> 9) + modulator_value);
    log_carrier_value += volume << 7;
    log_carrier_value += carrier->eg_level << 4;
    log_carrier_value += ym2413_ksl (instrument->carrier_key_scale_level, block, fnum) << 4;
    log_carrier_value += (instrument->carrier_am) ? context->state.am_value : 0;
    signmag16_t carrier_value = ym2413_exp (log_carrier_value);

    if (carrier_value & SIGN_BIT)
    {
        /* When the 'waveform' bit is set, the negative half of the wave is flattened to zero. */
        if (instrument->carrier_waveform)
        {
            carrier_value &= SIGN_BIT;
        }
        return -((carrier_value & MAG_BITS) >> 1);
    }
    else
    {
        return (carrier_value & MAG_BITS) >> 1;
    }
}


/*
 * Run one sample for each YM2413 rhythm instrument.
 */
static int16_t ym2413_run_rhythm_sample (YM2413_Context *context)
{
    YM2413_Instrument *sd_hh_instrument = (YM2413_Instrument *) rhythm_rom [1];
    YM2413_Instrument *tt_tc_instrument = (YM2413_Instrument *) rhythm_rom [2];
    uint16_t sd_hh_fnum    = context->state.r10_channel_params [YM2413_SNARE_DRUM_CH].fnum |
                (((uint16_t) context->state.r20_channel_params [YM2413_SNARE_DRUM_CH].fnum_9) << 8);
    uint16_t sd_hh_block   = context->state.r20_channel_params [YM2413_SNARE_DRUM_CH].block;
    uint16_t tt_tc_fnum    = context->state.r10_channel_params [YM2413_TOM_TOM_CH].fnum |
                (((uint16_t) context->state.r20_channel_params [YM2413_TOM_TOM_CH].fnum_9) << 8);
    uint16_t tt_tc_block   = context->state.r20_channel_params [YM2413_TOM_TOM_CH].block;
    int16_t output_level   = 0;

    /* Bass Drum */
    output_level += ym2413_run_channel_sample (context, YM2413_BASS_DRUM_CH, (YM2413_Instrument *) rhythm_rom [0],
                                               context->state.rhythm_key_bd, context->state.rhythm_volume_bd) << 1;

    /* Single-operator Instruments */
    YM2413_Operator_State *high_hat = &context->state.modulator [YM2413_HIGH_HAT_CH];
    YM2413_Operator_State *snare_drum = &context->state.carrier [YM2413_SNARE_DRUM_CH];
    YM2413_Operator_State *tom_tom = &context->state.modulator [YM2413_TOM_TOM_CH];
    YM2413_Operator_State *top_cymbal = &context->state.carrier [YM2413_TOP_CYMBAL_CH];

    /* High Hat */
    if (ym2413_envelope_cycle (context, high_hat, &context->calculated [YM2413_HIGH_HAT_CH].modulator_envelope))
    {
        high_hat->phase = 0;
    }

    uint16_t hh_factor = factor_table [sd_hh_instrument->modulator_multiplication_factor];
    high_hat->phase += (((sd_hh_fnum << 1) * hh_factor) << sd_hh_block) >> 2;
    uint16_t lfsr_bit = context->state.hh_lfsr & 0x0001;
    uint32_t lfsr_xor = (lfsr_bit) ? 0x800302 : 0;
    context->state.hh_lfsr = (context->state.hh_lfsr ^ lfsr_xor) >> 1;

    if (high_hat->eg_level < 124)
    {
        uint16_t phase_bit = (((top_cymbal->phase >> 14) ^ (top_cymbal->phase >> 12)) &
                              ((high_hat->phase   >> 16) ^ (high_hat->phase   >> 11)) &
                              ((top_cymbal->phase >> 14) ^ (high_hat->phase   >> 12))) & 0x01;

        signmag16_t log_hh_value = ((phase_bit ^ lfsr_bit) ? 425 : 16) | (phase_bit << 15);

        log_hh_value += context->state.rhythm_volume_hh << 7;
        log_hh_value += high_hat->eg_level << 4;

        signmag16_t hh_value = ym2413_exp (log_hh_value);
        output_level += signmag_convert (hh_value);
    }

    /* Snare Drum */
    if(ym2413_envelope_cycle (context, snare_drum, &context->calculated [YM2413_SNARE_DRUM_CH].carrier_envelope))
    {
        snare_drum->phase = 0;
    }

    uint16_t sd_factor = factor_table [sd_hh_instrument->carrier_multiplication_factor];
    snare_drum->phase += (((sd_hh_fnum << 1) * sd_factor) << sd_hh_block) >> 2;
    lfsr_bit = context->state.sd_lfsr & 0x0001;
    lfsr_xor = (lfsr_bit) ? 0x800302 : 0;
    context->state.sd_lfsr = (context->state.sd_lfsr ^ lfsr_xor) >> 1;

    if (snare_drum->eg_level < 124)
    {
        uint16_t phase_bit = (snare_drum->phase >> 17) & 0x0001;
        signmag16_t log_sd_value = (phase_bit ^ lfsr_bit) ? 0 : 2137; /* 0 = maximum, 2137 = minimum */
        log_sd_value |= phase_bit << 15; /* Sign comes from phase */
        log_sd_value += context->state.rhythm_volume_sd << 7;
        log_sd_value += snare_drum->eg_level << 4;

        signmag16_t sd_value = ym2413_exp (log_sd_value);
        output_level += signmag_convert (sd_value);
    }

    /* Tom Tom */
    if (ym2413_envelope_cycle (context, tom_tom, &context->calculated [YM2413_TOM_TOM_CH].modulator_envelope))
    {
        tom_tom->phase = 0;
    }

    uint16_t tt_factor = factor_table [tt_tc_instrument->modulator_multiplication_factor];
    tom_tom->phase += (((tt_tc_fnum << 1) * tt_factor) << tt_tc_block) >> 2;

    if (tom_tom->eg_level < 124)
    {
        signmag16_t log_tt_value = ym2413_sin (tom_tom->phase >> 9);
        log_tt_value += context->state.rhythm_volume_tt << 7;
        log_tt_value += tom_tom->eg_level << 4;

        signmag16_t tt_value = ym2413_exp (log_tt_value);
        output_level += signmag_convert (tt_value);
    }

    /* Top Cymbal */
    if (ym2413_envelope_cycle (context, top_cymbal, &context->calculated [YM2413_TOP_CYMBAL_CH].carrier_envelope))
    {
        top_cymbal->phase = 0;
    }

    uint16_t tc_factor = factor_table [tt_tc_instrument->carrier_multiplication_factor];
    top_cymbal->phase += (((tt_tc_fnum << 1) * tc_factor) << tt_tc_block) >> 2;

    if (top_cymbal->eg_level < 124)
    {
        signmag16_t log_tc_value = ((((top_cymbal->phase >> 14) ^ (top_cymbal->phase >> 12)) &
                                     ((high_hat->phase   >> 16) ^ (high_hat->phase   >> 11)) &
                                     ((top_cymbal->phase >> 14) ^ (high_hat->phase   >> 12))) & 0x01 ) ? 0 : SIGN_BIT;

        log_tc_value += context->state.rhythm_volume_tc << 7;
        log_tc_value += top_cymbal->eg_level << 4;

        signmag16_t tc_value = ym2413_exp (log_tc_value);
        output_level += signmag_convert (tc_value);
    }

    return output_level;
}


/*
 * Run the YM2413 for a number of CPU clock cycles.
 */
void _ym2413_run_cycles (YM2413_Context *context, uint32_t clock_rate, uint32_t cycles)
{
    /* The YM2413 takes 72 cycles to update all 18 operators */
    static uint32_t excess = 0;
    cycles += excess;
    uint32_t ym_samples = cycles / 72;
    excess = cycles - (ym_samples * 72);

    /* Reset the ring buffer if the clock rate changes */
    if (state.console_context != NULL &&
        clock_rate != context->clock_rate)
    {
        context->clock_rate = clock_rate;
        context->read_index = 0;
        context->write_index = 0;
        context->completed_samples = 0;
    }

    uint32_t melody_channels = (context->state.rhythm_mode) ? 6 : 9;

    /* If we're about to overwrite samples that haven't been read yet,
     * skip the read_index forward to discard some of the backlog. */
    if (context->write_index + (ym_samples * AUDIO_SAMPLE_RATE * 72 / context->clock_rate) >= context->read_index + YM2413_RING_SIZE)
    {
        context->read_index += YM2413_RING_SIZE / 4;
    }

    while (ym_samples--)
    {
        int16_t output_level = 0;
        context->state.global_counter++;

        /* AM Counter */
        if ((context->state.global_counter & 0x3f) == 0x00)
        {
            context->state.am_counter = (context->state.am_counter + 1) % 210;
            context->state.am_value = am_table [context->state.am_counter];
        }

        for (uint32_t channel = 0; channel < melody_channels; channel++)
        {
            /* Melody-specific channel Parameters */
            bool     key_on  = context->state.r20_channel_params [channel].key_on;
            uint16_t volume  = context->state.r30_channel_params [channel].volume;
            uint16_t inst    = context->state.r30_channel_params [channel].instrument;
            YM2413_Instrument *instrument = (inst == 0) ? &context->state.regs_custom
                                                        : (YM2413_Instrument *) instrument_rom [inst - 1];

            output_level += ym2413_run_channel_sample (context, channel, instrument, key_on, volume);
        }

        if (context->state.rhythm_mode)
        {
            output_level += ym2413_run_rhythm_sample (context);
        }

        /* Propagate new samples into ring buffer.
         * Linear interpolation to get 48 kHz from 49.7… kHz */
        if (context->completed_samples * AUDIO_SAMPLE_RATE * 72 > context->write_index * context->clock_rate)
        {
            float portion = (float) ((context->write_index * context->clock_rate) % (AUDIO_SAMPLE_RATE * 72)) /
                            (float) (AUDIO_SAMPLE_RATE * 72);

            int16_t sample = roundf (portion * output_level + (1.0 - portion) * context->previous_output_level);
            context->sample_ring [context->write_index % YM2413_RING_SIZE] = BASE_VOLUME * sample / 2042;
            context->write_index++;
        }

        context->previous_output_level = output_level;
        context->completed_samples++;
    }
}


/*
 * Run the YM2413 for a number of CPU clock cycles (mutex-wrapper)
 *
 * Allows two threads to request sound to be generated:
 *  1. The emulation loop, this is the usual case.
 *  2. Additional samples needed to keep the sound card from running out.
 */
void ym2413_run_cycles (YM2413_Context *context, uint32_t clock_rate, uint32_t cycles)
{
    pthread_mutex_lock (&context->mutex);
    _ym2413_run_cycles (context, clock_rate, cycles);
    pthread_mutex_unlock (&context->mutex);
}


/*
 * Retrieves a block of samples from the sample-ring.
 * Assumes that the number of samples requested fits evenly into the ring buffer.
 */
void ym2413_get_samples (YM2413_Context *context, int32_t *stream, uint32_t count)
{
    if (context->read_index + count > context->write_index)
    {
        uint32_t shortfall = count - (context->write_index - context->read_index);

        /* Note: We add one to the shortfall to account for integer division */
        ym2413_run_cycles (context, context->clock_rate, (shortfall + 1) * context->clock_rate / AUDIO_SAMPLE_RATE);
    }

    /* Take samples and pass them to the sound card */
    for (int i = 0; i < count; i++)
    {
        size_t sample_index = (context->read_index + i) & (YM2413_RING_SIZE - 1);

        /* Left, Right */
        stream [2 * i    ] += context->sample_ring [sample_index];
        stream [2 * i + 1] += context->sample_ring [sample_index];
    }

    context->read_index += count;
}


/*
 * Initialise a new YM2413 context.
 */
YM2413_Context *ym2413_init (void)
{
    static bool first = true;

    if (first)
    {
        /* Once-off initialisations */
        first = false;
        ym2413_populate_exp_table ();
        ym2413_populate_log_sin_table ();
        ym2413_populate_am_table ();
    }

    YM2413_Context *context = calloc (1, sizeof (YM2413_Context));
    pthread_mutex_init (&context->mutex, NULL); /* TODO: mutex_destroy */

    context->clock_rate = NTSC_COLOURBURST_FREQ;

    context->state.sd_lfsr = 0x000001;
    context->state.hh_lfsr = 0x000003;

    for (uint32_t channel = 0; channel < 9; channel++)
    {
        context->state.modulator [channel].eg_level = 127;
        context->state.carrier [channel].eg_level = 127;
    }

    return context;
}


/*
 * Export YM2413 state.
 */
void ym2413_state_save (YM2413_Context *context)
{
    YM2413_State ym2413_state_be = {
        .addr_latch = context->state.addr_latch,
        .regs_custom = { .r00 = context->state.regs_custom.r00,
                         .r01 = context->state.regs_custom.r01,
                         .r02 = context->state.regs_custom.r02,
                         .r03 = context->state.regs_custom.r03,
                         .r04 = context->state.regs_custom.r04,
                         .r05 = context->state.regs_custom.r05,
                         .r06 = context->state.regs_custom.r06,
                         .r07 = context->state.regs_custom.r07 },
        .r0e_rhythm = context->state.r0e_rhythm,
        .r0f_test = context->state.r0f_test,
        .global_counter = util_hton32 (context->state.global_counter),
        .am_counter = util_hton16 (context->state.am_counter),
        .am_value = util_hton16 (context->state.am_value),
        .sd_lfsr = util_hton32 (context->state.sd_lfsr),
        .hh_lfsr = util_hton32 (context->state.hh_lfsr)
    };

    for (int channel = 0; channel < 9; channel++)
    {
        ym2413_state_be.r10_channel_params [channel].fnum               = context->state.r10_channel_params [channel].fnum;
        ym2413_state_be.r20_channel_params [channel].r20_channel_params = context->state.r20_channel_params [channel].r20_channel_params;
        ym2413_state_be.r30_channel_params [channel].r30_channel_params = context->state.r30_channel_params [channel].r30_channel_params;
        ym2413_state_be.feedback [channel] [0]                          = util_hton16 (context->state.feedback [channel] [0]);
        ym2413_state_be.feedback [channel] [1]                          = util_hton16 (context->state.feedback [channel] [1]);
        ym2413_state_be.modulator [channel].eg_state                    = util_hton32 (context->state.modulator [channel].eg_state);
        ym2413_state_be.modulator [channel].eg_level                    = context->state.modulator [channel].eg_level;
        ym2413_state_be.modulator [channel].phase                       = util_hton32 (context->state.modulator [channel].phase);
        ym2413_state_be.carrier [channel].eg_state                      = util_hton32 (context->state.carrier [channel].eg_state);
        ym2413_state_be.carrier [channel].eg_level                      = context->state.carrier [channel].eg_level;
        ym2413_state_be.carrier [channel].phase                         = util_hton32 (context->state.carrier [channel].phase);
    }

    save_state_section_add (SECTION_ID_YM2413, 1, sizeof (ym2413_state_be), &ym2413_state_be);
}


/*
 * Import YM2413 state.
 */
void ym2413_state_load (YM2413_Context *context, uint32_t version, uint32_t size, void *data)
{
    YM2413_State ym2413_state_be;

    if (size == sizeof (ym2413_state_be))
    {
        memcpy (&ym2413_state_be, data, sizeof (ym2413_state_be));

        context->state.addr_latch      = ym2413_state_be.addr_latch;
        context->state.regs_custom.r00 = ym2413_state_be.regs_custom.r00;
        context->state.regs_custom.r01 = ym2413_state_be.regs_custom.r01;
        context->state.regs_custom.r02 = ym2413_state_be.regs_custom.r02;
        context->state.regs_custom.r03 = ym2413_state_be.regs_custom.r03;
        context->state.regs_custom.r04 = ym2413_state_be.regs_custom.r04;
        context->state.regs_custom.r05 = ym2413_state_be.regs_custom.r05;
        context->state.regs_custom.r06 = ym2413_state_be.regs_custom.r06;
        context->state.regs_custom.r07 = ym2413_state_be.regs_custom.r07;
        context->state.r0e_rhythm      = ym2413_state_be.r0e_rhythm;
        context->state.r0f_test        = ym2413_state_be.r0f_test;

        context->state.global_counter  = util_ntoh32 (ym2413_state_be.global_counter);
        context->state.am_counter      = util_ntoh16 (ym2413_state_be.am_counter);
        context->state.am_value        = util_ntoh16 (ym2413_state_be.am_value);
        context->state.sd_lfsr         = util_ntoh32 (ym2413_state_be.sd_lfsr);
        context->state.hh_lfsr         = util_ntoh32 (ym2413_state_be.hh_lfsr);

        for (int channel = 0; channel < 9; channel++)
        {
            context->state.r10_channel_params [channel].fnum               = ym2413_state_be.r10_channel_params [channel].fnum;
            context->state.r20_channel_params [channel].r20_channel_params = ym2413_state_be.r20_channel_params [channel].r20_channel_params;
            context->state.r30_channel_params [channel].r30_channel_params = ym2413_state_be.r30_channel_params [channel].r30_channel_params;
            context->state.feedback [channel] [0]                          = util_ntoh16 (ym2413_state_be.feedback [channel] [0]);
            context->state.feedback [channel] [1]                          = util_ntoh16 (ym2413_state_be.feedback [channel] [1]);
            context->state.modulator [channel].eg_state                    = util_ntoh32 (ym2413_state_be.modulator [channel].eg_state);
            context->state.modulator [channel].eg_level                    = ym2413_state_be.modulator [channel].eg_level;
            context->state.modulator [channel].phase                       = util_ntoh32 (ym2413_state_be.modulator [channel].phase);
            context->state.carrier [channel].eg_state                      = util_ntoh32 (ym2413_state_be.carrier [channel].eg_state);
            context->state.carrier [channel].eg_level                      = ym2413_state_be.carrier [channel].eg_level;
            context->state.carrier [channel].phase                         = util_ntoh32 (ym2413_state_be.carrier [channel].phase);

            /* Calculate effective values */
            ym2413_handle_channel_update (context, channel);
        }
    }
    else
    {
        snepulator_error ("Error", "Save-state contains incorrect YM2413 size");
    }
}
