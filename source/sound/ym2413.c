/*
 * YM2413 implementation.
 *
 * Based on reverse engineering documents by Andete,
 * and some experiments of my own with a ym2413 chip.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include "../snepulator_types.h"
#include "../snepulator.h"
#include "../util.h"

extern Snepulator_State state;

#include "ym2413.h"

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

#define SAMPLE_RATE 48000
#define YM2413_RING_SIZE 4096
#define BASE_VOLUME 100

/* State */
pthread_mutex_t ym2413_mutex = PTHREAD_MUTEX_INITIALIZER;

/* TODO: Avoid globals, move these into the context. */
static uint64_t write_index = 0;
static uint64_t read_index = 0;
static uint64_t completed_samples = 0; /* ym2413 samples, not sound card samples */
static uint32_t clock_rate;

static int16_t sample_ring [YM2413_RING_SIZE];
static int16_t previous_output_level = 0;

/* Use a special type definition to mark sign-magnitude numbers.
 * The most significant bit is used to indicate if the number is negative. */
#define SIGN_BIT 0x8000
#define MAG_BITS 0x7fff
typedef uint16_t signmag16_t;

/* TODO:
 * - Investigate behaviour of +0 and -0 in the DAC.
 *   Should they be the same value?
 *   If different, is their delta the same as other number pairs?
 *
 * - SMS Interface:
 *   I/O Port 0xf2 - Read bit 0 to detect if YM2413 is present (according to McDonald document)
 *                 - Write bits 1:0 to configure muting:
 *                   0 - Only SN76489 enabled
 *                   1 - Only YM2413 enabled
 *                   2 - Both disabled
 *                   3 - Both enabled
 *                   Note that, muting the SN76489 only works on Japanese consoles.
 *                   Reading: 7:4 - counter bits 11, 7, and 3 (ticked by C-Sync)
 *                            3:2 - always 0
 *                            1:0 - Last written values, 0 by default
 *                  If no YM2413 is present, reading from the audio control port returns varying results.
 *                   * It always returns %10 in the lowermost two bits on a non-japanese SMS.
 *                   * On a Mark III without FM unit, it returns the input from port 0.
 *                   * Writing to the audio control port has no effect if no YM2413 is present.
 */

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
 * Handle key-on / key-off events for the melody channels.
 */
static void ym2413_handle_melody_keys (YM2413_Context *context)
{
    uint32_t melody_channels = (context->state.rhythm_mode) ? 6 : 9;

    for (uint32_t channel = 0; channel < melody_channels; channel++)
    {
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
            /* Note that only the carrier is put into the release state. */
            carrier->eg_state = YM2413_STATE_RELEASE;
        }
    }
}


/*
 * Handle key-on / key-off events for rhythm instruments.
 */
static void ym2413_handle_rhythm_keys (YM2413_Context *context)
{
    if (context->state.rhythm_mode)
    {
        /* Bass Drum */
        if (context->state.rhythm_key_bd && context->state.carrier [YM2413_BASS_DRUM_CH].eg_state == YM2413_STATE_RELEASE)
        {
             context->state.modulator [YM2413_BASS_DRUM_CH].eg_state = YM2413_STATE_DAMP;
             context->state.carrier [YM2413_BASS_DRUM_CH].eg_state = YM2413_STATE_DAMP;
        }
        else if (!context->state.rhythm_key_bd)
        {
             context->state.carrier [YM2413_BASS_DRUM_CH].eg_state = YM2413_STATE_RELEASE;
        }

        /* High Hat */
        if (context->state.rhythm_key_hh && context->state.modulator [YM2413_HIGH_HAT_CH].eg_state == YM2413_STATE_RELEASE)
        {
            context->state.modulator [YM2413_HIGH_HAT_CH].eg_state = YM2413_STATE_DAMP;
        }
        else if (!context->state.rhythm_key_hh)
        {
            context->state.modulator [YM2413_HIGH_HAT_CH].eg_state = YM2413_STATE_RELEASE;
        }

        /* Snare Drum */
        if (context->state.rhythm_key_sd && context->state.carrier [YM2413_SNARE_DRUM_CH].eg_state == YM2413_STATE_RELEASE)
        {
            context->state.carrier [YM2413_SNARE_DRUM_CH].eg_state = YM2413_STATE_DAMP;
        }
        else if (!context->state.rhythm_key_sd)
        {
            context->state.carrier [YM2413_SNARE_DRUM_CH].eg_state = YM2413_STATE_RELEASE;
        }

        /* Tom Tom */
        if (context->state.rhythm_key_tt && context->state.modulator [YM2413_TOM_TOM_CH].eg_state == YM2413_STATE_RELEASE)
        {
            context->state.modulator [YM2413_TOM_TOM_CH].eg_state = YM2413_STATE_DAMP;
        }
        else if (!context->state.rhythm_key_tt)
        {
            context->state.modulator [YM2413_TOM_TOM_CH].eg_state = YM2413_STATE_RELEASE;
        }

        /* Top Cymbal */
        if (context->state.rhythm_key_tc && context->state.carrier [YM2413_TOP_CYMBAL_CH].eg_state == YM2413_STATE_RELEASE)
        {
            context->state.carrier [YM2413_TOP_CYMBAL_CH].eg_state = YM2413_STATE_DAMP;
        }
        else if (!context->state.rhythm_key_tc)
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

    if (addr >= 0x00 && addr <= 0x07)
    {
        ((uint8_t *) &context->state.regs_custom.r00) [addr] = data;
    }
    else if (addr == 0x0e)
    {
        /* Some of the operators don't normally experience release,
         * so put them into release when entering rhythm mode. */
        if (context->state.rhythm_mode == false && data & 0x20)
        {
            context->state.modulator [YM2413_HIGH_HAT_CH].eg_state = YM2413_STATE_RELEASE;
            context->state.modulator [YM2413_HIGH_HAT_CH].eg_level = 127;
            context->state.modulator [YM2413_TOM_TOM_CH].eg_state = YM2413_STATE_RELEASE;
            context->state.modulator [YM2413_TOM_TOM_CH].eg_level = 127;
        }

        context->state.r0e_rhythm = data;
        ym2413_handle_rhythm_keys (context);
    }
    else if (addr == 0x0f)
    {
        context->state.r0f_test = data;
    }
    else if (addr >= 0x10 && addr <= 0x19)
    {
        ((uint8_t *) &context->state.r10_channel_params) [addr - 0x10] = data;
    }
    else if (addr >= 0x20 && addr <= 0x29)
    {
        ((uint8_t *) &context->state.r20_channel_params) [addr - 0x20] = data;
        ym2413_handle_melody_keys (context);
    }
    else if (addr >= 0x30 && addr <= 0x39)
    {
        ((uint8_t *) &context->state.r30_channel_params) [addr - 0x30] = data;
    }
}


/*
 * Latch a register address.
 */
void ym2413_addr_write (YM2413_Context *context, uint8_t addr)
{
    /* Register mirroring */
    if ((addr <= 0x19 && addr >= 0x1f) ||
        (addr <= 0x29 && addr >= 0x2f) ||
        (addr <= 0x39 && addr >= 0x3f))
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
 * Run the sustained-tone envelope generator for one sample.
 */
static void ym2413_sustained_envelope_cycle (YM2413_Context *context, YM2413_Operator_State *operator,
                                               uint8_t attack_rate, uint8_t decay_rate, uint8_t sustain_level,
                                               uint8_t release_rate, uint8_t key_scale_rate)
{
    switch (operator->eg_state)
    {
        case YM2413_STATE_DAMP:
            operator->eg_level += ym2413_decay (context, 48 + key_scale_rate);
            break;

        case YM2413_STATE_ATTACK:
            operator->eg_level = ym2413_attack (context, operator->eg_level, (attack_rate << 2) + key_scale_rate);
            if (operator->eg_level == 0)
            {
                operator->eg_state = YM2413_STATE_DECAY;
            }
            break;

        case YM2413_STATE_DECAY:
            if (decay_rate)
            {
                operator->eg_level += ym2413_decay (context, (decay_rate << 2) + key_scale_rate);
            }
            if (operator->eg_level >= (sustain_level << 3))
            {
                operator->eg_state = YM2413_STATE_SUSTAIN;
            }
            break;

        case YM2413_STATE_SUSTAIN:
            /* Sustain until the key is released. */
            break;

        case YM2413_STATE_RELEASE:
            if (release_rate)
            {
                operator->eg_level += ym2413_decay (context, (release_rate << 2) + key_scale_rate);
            }
            break;
    }

    ENFORCE_MAXIMUM (operator->eg_level, 127);
}


/*
 * Run the percussive-tone envelope generator for one sample.
 */
static void ym2413_percussive_envelope_cycle (YM2413_Context *context, YM2413_Operator_State *operator,
                                   uint8_t attack_rate, uint8_t decay_rate, uint8_t sustain_level,
                                   uint8_t release_rate_1, uint8_t release_rate_2, uint8_t key_scale_rate)
{
    switch (operator->eg_state)
    {
        case YM2413_STATE_DAMP:
            operator->eg_level += ym2413_decay (context, 48 + key_scale_rate);
            break;

        case YM2413_STATE_ATTACK:
            operator->eg_level = ym2413_attack (context, operator->eg_level, (attack_rate << 2) + key_scale_rate);
            if (operator->eg_level == 0)
            {
                operator->eg_state = YM2413_STATE_DECAY;
            }
            break;

        case YM2413_STATE_DECAY:
            if (decay_rate)
            {
                operator->eg_level += ym2413_decay (context, (decay_rate << 2) + key_scale_rate);
            }
            if (operator->eg_level >= (sustain_level << 3))
            {
                operator->eg_state = YM2413_STATE_SUSTAIN;
            }
            break;

        case YM2413_STATE_SUSTAIN:
            if (release_rate_1)
            {
                operator->eg_level += ym2413_decay (context, (release_rate_1 << 2) + key_scale_rate);
            }
            break;

        case YM2413_STATE_RELEASE:
            operator->eg_level += ym2413_decay (context, (release_rate_2 << 2) + key_scale_rate);
            break;
    }

    ENFORCE_MAXIMUM (operator->eg_level, 127);
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
    bool     sustain = context->state.r20_channel_params [channel].sustain;

    int16_t fm = vibrato_table [fnum >> 6] [(context->state.global_counter >> 10) & 0x07];

    /* Key Scale Rate */
    uint16_t modulator_key_scale_rate = context->state.r20_channel_params [channel].r20_channel_params & 0x0f;
    if (instrument->modulator_key_scale_rate == 0)
    {
        modulator_key_scale_rate >>= 2;
    }
    uint16_t carrier_key_scale_rate = context->state.r20_channel_params [channel].r20_channel_params & 0x0f;
    if (instrument->carrier_key_scale_rate == 0)
    {
        carrier_key_scale_rate >>= 2;
    }

    /* Damp->Attack Transition */
    if (modulator->eg_state == YM2413_STATE_DAMP && modulator->eg_level >= 120)
    {
        modulator->eg_state = YM2413_STATE_ATTACK;

        /* Skip the attack phase if the rate is high enough */
        if ((instrument->modulator_attack_rate << 2) + modulator_key_scale_rate >= 60)
        {
            modulator->eg_state = YM2413_STATE_DECAY;
            modulator->eg_level = 0;
        }
    }
    if (carrier->eg_state == YM2413_STATE_DAMP && carrier->eg_level >= 120)
    {
        carrier->eg_state = YM2413_STATE_ATTACK;
        carrier->phase = 0;
        modulator->phase = 0;

        /* Skip the attack phase if the rate is high enough */
        if ((instrument->carrier_attack_rate << 2) + carrier_key_scale_rate >= 60)
        {
            carrier->eg_state = YM2413_STATE_DECAY;
            carrier->eg_level = 0;
        }
    }

    /* Modulator Envelope - Only runs when the key is pressed */
    if (key_on)
    {
        if (instrument->modulator_envelope_type == 1)
        {
            ym2413_sustained_envelope_cycle (context, modulator, instrument->modulator_attack_rate,
                                             instrument->modulator_decay_rate, instrument->modulator_sustain_level,
                                             (sustain ? 5 : instrument->modulator_release_rate), modulator_key_scale_rate);
        }
        else
        {
            ym2413_percussive_envelope_cycle (context, modulator, instrument->modulator_attack_rate,
                                              instrument->modulator_decay_rate, instrument->modulator_sustain_level,
                                              instrument->modulator_release_rate, (sustain ? 5 : 7), modulator_key_scale_rate);
        }
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

    /* TODO: Check if historic feedback is stored before or after waveform flattening */
    context->state.feedback [channel] [context->state.global_counter % 2] = modulator_value;

    /* Carrier Envelope */
    if (instrument->carrier_envelope_type == 1)
    {
        ym2413_sustained_envelope_cycle (context, carrier, instrument->carrier_attack_rate,
                                         instrument->carrier_decay_rate, instrument->carrier_sustain_level,
                                         (sustain ? 5 : instrument->carrier_release_rate), carrier_key_scale_rate);
    }
    else
    {
        ym2413_percussive_envelope_cycle (context, carrier, instrument->carrier_attack_rate,
                                          instrument->carrier_decay_rate, instrument->carrier_sustain_level,
                                          instrument->carrier_release_rate, (sustain ? 5 : 7), carrier_key_scale_rate);
    }

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
 * Run the YM2413 for a number of CPU clock cycles.
 */
void _ym2413_run_cycles (YM2413_Context *context, uint64_t cycles)
{
    /* The YM2413 takes 72 cycles to update all 18 operators */
    static uint32_t excess = 0;
    cycles += excess;
    uint32_t ym_samples = cycles / 72;
    excess = cycles - (ym_samples * 72);

    /* Reset the ring buffer if the clock rate changes */
    if (state.console_context != NULL &&
        clock_rate != state.get_clock_rate (state.console_context))
    {
        clock_rate = state.get_clock_rate (state.console_context);

        read_index = 0;
        write_index = 0;
        completed_samples = 0;
    }

    uint32_t melody_channels = (context->state.rhythm_mode) ? 6 : 9;

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
            YM2413_Instrument *sd_hh_instrument = (YM2413_Instrument *) rhythm_rom [1];
            YM2413_Instrument *tt_tc_instrument = (YM2413_Instrument *) rhythm_rom [2];
            uint16_t sd_hh_fnum    = context->state.r10_channel_params [YM2413_SNARE_DRUM_CH].fnum |
                        (((uint16_t) context->state.r20_channel_params [YM2413_SNARE_DRUM_CH].fnum_9) << 8);
            uint16_t sd_hh_block   = context->state.r20_channel_params [YM2413_SNARE_DRUM_CH].block;
            uint16_t tt_tc_fnum    = context->state.r10_channel_params [YM2413_TOM_TOM_CH].fnum |
                        (((uint16_t) context->state.r20_channel_params [YM2413_TOM_TOM_CH].fnum_9) << 8);
            uint16_t tt_tc_block   = context->state.r20_channel_params [YM2413_TOM_TOM_CH].block;

            /* Bass Drum */
            output_level += ym2413_run_channel_sample (context, YM2413_BASS_DRUM_CH, (YM2413_Instrument *) rhythm_rom [0],
                                                       context->state.rhythm_key_bd, context->state.rhythm_volume_bd) << 1;

            /* Single-operator Instruments */
            YM2413_Operator_State *high_hat = &context->state.modulator [YM2413_HIGH_HAT_CH];
            YM2413_Operator_State *snare_drum = &context->state.carrier [YM2413_SNARE_DRUM_CH];
            YM2413_Operator_State *tom_tom = &context->state.modulator [YM2413_TOM_TOM_CH];
            YM2413_Operator_State *top_cymbal = &context->state.carrier [YM2413_TOP_CYMBAL_CH];


            /* High Hat */
            uint16_t hh_ksr = (context->state.r20_channel_params [YM2413_HIGH_HAT_CH].r20_channel_params & 0x0f) >> 2;
            uint16_t hh_factor = factor_table [sd_hh_instrument->modulator_multiplication_factor];

            if (high_hat->eg_state == YM2413_STATE_DAMP && high_hat->eg_level >= 120)
            {
                high_hat->eg_state = YM2413_STATE_ATTACK;
                high_hat->phase = 0;

                /* Skip the attack phase if the rate is high enough */
                if ((sd_hh_instrument->modulator_attack_rate << 2) + hh_ksr >= 60)
                {
                    high_hat->eg_state = YM2413_STATE_DECAY;
                    high_hat->eg_level = 0;
                }
            }

            ym2413_percussive_envelope_cycle (context, high_hat, sd_hh_instrument->modulator_attack_rate,
                                              sd_hh_instrument->modulator_decay_rate, sd_hh_instrument->modulator_sustain_level,
                                              sd_hh_instrument->modulator_release_rate, 7, hh_ksr);

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
            uint16_t sd_ksr = (context->state.r20_channel_params [YM2413_SNARE_DRUM_CH].r20_channel_params & 0x0f) >> 2;
            uint16_t sd_factor = factor_table [sd_hh_instrument->carrier_multiplication_factor];

            if (snare_drum->eg_state == YM2413_STATE_DAMP && snare_drum->eg_level >= 120)
            {
                snare_drum->eg_state = YM2413_STATE_ATTACK;
                snare_drum->phase = 0;

                /* Skip the attack phase if the rate is high enough */
                if ((sd_hh_instrument->carrier_attack_rate << 2) + sd_ksr >= 60)
                {
                    snare_drum->eg_state = YM2413_STATE_DECAY;
                    snare_drum->eg_level = 0;
                }
            }

            ym2413_percussive_envelope_cycle (context, snare_drum, sd_hh_instrument->carrier_attack_rate,
                                              sd_hh_instrument->carrier_decay_rate, sd_hh_instrument->carrier_sustain_level,
                                              sd_hh_instrument->carrier_release_rate, 7, sd_ksr);

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
            uint16_t tt_ksr = (context->state.r20_channel_params [YM2413_TOM_TOM_CH].r20_channel_params & 0x0f) >> 2;
            uint16_t tt_factor = factor_table [tt_tc_instrument->modulator_multiplication_factor];

            if (tom_tom->eg_state == YM2413_STATE_DAMP && tom_tom->eg_level >= 120)
            {
                tom_tom->eg_state = YM2413_STATE_DECAY;
                tom_tom->eg_level = 0;
                tom_tom->phase = 0;
            }
            ym2413_percussive_envelope_cycle (context, tom_tom, tt_tc_instrument->modulator_attack_rate,
                                              tt_tc_instrument->modulator_decay_rate, tt_tc_instrument->modulator_sustain_level,
                                              tt_tc_instrument->modulator_release_rate, tt_tc_instrument->modulator_release_rate, tt_ksr);

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
            uint16_t tc_ksr = (context->state.r20_channel_params [YM2413_TOP_CYMBAL_CH].r20_channel_params & 0x0f) >> 2;
            uint16_t tc_factor = factor_table [tt_tc_instrument->carrier_multiplication_factor];

            if (top_cymbal->eg_state == YM2413_STATE_DAMP && top_cymbal->eg_level >= 120)
            {
                top_cymbal->eg_state = YM2413_STATE_ATTACK;
                top_cymbal->phase = 0;

                /* Skip the attack phase if the rate is high enough */
                if ((tt_tc_instrument->carrier_attack_rate << 2) + sd_ksr >= 60)
                {
                    top_cymbal->eg_state = YM2413_STATE_DECAY;
                    top_cymbal->eg_level = 0;
                }
            }

            ym2413_percussive_envelope_cycle (context, top_cymbal, tt_tc_instrument->carrier_attack_rate,
                                              tt_tc_instrument->carrier_decay_rate, tt_tc_instrument->carrier_sustain_level,
                                              tt_tc_instrument->carrier_release_rate, 7, tc_ksr);

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
        }

        /* Propagate new samples into ring buffer */
        /* TODO: Sort out proper volume levels. */

        /* Linear interpolation to get 48 kHz from 49.7… kHz */
        if (completed_samples * SAMPLE_RATE * 72 > write_index * clock_rate)
        {
            float portion = (float) ((write_index * clock_rate) % (SAMPLE_RATE * 72)) /
                            (float) (SAMPLE_RATE * 72);

            int16_t sample = roundf (portion * output_level + (1.0 - portion) * previous_output_level);
            sample_ring [write_index % YM2413_RING_SIZE] = sample * 3;
            write_index++;
        }

        previous_output_level = output_level;
        completed_samples++;
    }
}


/*
 * Run the YM2413 for a number of CPU clock cycles (mutex-wrapper)
 *
 * Allows two threads to request sound to be generated:
 *  1. The emulation loop, this is the usual case.
 *  2. Additional samples needed to keep the sound card from running out.
 */
void ym2413_run_cycles (YM2413_Context *context, uint64_t cycles)
{
    pthread_mutex_lock (&ym2413_mutex);
    _ym2413_run_cycles (context, cycles);
    pthread_mutex_unlock (&ym2413_mutex);
}


/*
 * Retrieves a block of samples from the sample-ring.
 * Assumes that the number of samples requested fits evenly into the ring buffer.
 */
void ym2413_get_samples (YM2413_Context *context, int16_t *stream, uint32_t count)
{
    if (read_index + count > write_index)
    {
        int shortfall = count - (write_index - read_index);

        /* Note: We add one to the shortfall to account for integer division */
        ym2413_run_cycles (context, (shortfall + 1) * clock_rate / SAMPLE_RATE);
    }

    /* Take samples and pass them to the sound card */
    uint32_t read_start = read_index % YM2413_RING_SIZE;

    for (int i = 0; i < count; i++)
    {
        /* Left, Right */
        stream [2 * i    ] = sample_ring [read_start + i];
        stream [2 * i + 1] = sample_ring [read_start + i];
    }

    read_index += count;
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
    context->state.sd_lfsr = 0x000001;
    context->state.hh_lfsr = 0x000003;

    memset (sample_ring, 0, sizeof (sample_ring));

    completed_samples = 0;
    write_index = 0;
    read_index = 0;
    clock_rate = 0;

    return context;
}
