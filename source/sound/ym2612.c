/*
 * Snepulator
 * YM2612 FM synthesizer chip implementation.
 *
 * Initial implementation only to get basic DAC output working.
 *
 * TODO:
 *  - Operators
 *    - Special mode for channel 3
 *  - Modulation
 *    - Modulation sample delay pipeline delay (1-3-2-4)
 *    - Modulation delay for consecutive channels
 *  - Envelopes
 *    - SSG-EG
 *    - CSM
 *  - LFO / AMS / PMS
 *  - DAC quantization
 *
 * Quirks not implemented:
 *  - If attack-rate is changed to >= 62 after key-on, the envelope stalls.
 */


#include <math.h>
#include <stdlib.h>

#include "../snepulator.h"
#include "../util.h"

extern Snepulator_State state;

#include "ym2612.h"

/* Represents the level of a single melody channel at maximum volume */
#define BASE_VOLUME 4096

/* Use a special type definition to mark sign-magnitude numbers.
 * The most significant bit is used to indicate if the number is negative. */
#define SIGN_BIT 0x8000
#define MAG_BITS 0x7fff

typedef uint16_t signmag16_t;
static uint32_t exp_table [256] = { };
static uint32_t log_sin_table [256] = { };

/*
 * Retrieves a block of samples from the sample-ring.
 * Assumes that the number of samples requested fits evenly into the ring buffer.
 */
void ym2612_get_samples (YM2612_Context *context, int32_t *stream, uint32_t count)
{
    if (context->read_index + count > context->write_index)
    {
        uint32_t shortfall = count - (context->write_index - context->read_index);

        /* Note: We add one to the shortfall to account for integer division */
        ym2612_run_cycles (context, context->clock_rate, (shortfall + 1) * context->clock_rate / AUDIO_SAMPLE_RATE);
    }

    /* Take samples and pass them to the sound card */
    for (int i = 0; i < count; i++)
    {
        size_t sample_index = (context->read_index + i) & (YM2612_RING_SIZE - 1);

        /* Left, Right */
        stream [2 * i    ] += context->sample_ring_left [sample_index];
        stream [2 * i + 1] += context->sample_ring_right [sample_index];
    }

    context->read_index += count;
}


/*
 * Read the status register.
 *
 *  [7] - Busy flag
 *  [1] - Timer B
 *  [0] - Timer A
 */
uint8_t ym2612_status_read (YM2612_Context *context)
{
    uint8_t status = 0;

    if (context->state.timer_a_flag)
    {
        status |= BIT_0;
    }
    if (context->state.timer_b_flag)
    {
        status |= BIT_1;
    }

    /* TODO: Does anything rely on a non-zero busy flag? */

    return status;
}


/*
 * Latch a register address.
 */
void ym2612_addr1_write (YM2612_Context *context, uint8_t addr)
{
    context->state.group_latch = YM2612_GROUP_1;
    context->state.addr_latch = addr;
}


/*
 * Latch a register address.
 */
void ym2612_addr2_write (YM2612_Context *context, uint8_t addr)
{
    context->state.group_latch = YM2612_GROUP_2;
    context->state.addr_latch = addr;
}


/*
 * Write data to the latched register address.
 */
void ym2612_data_write (YM2612_Context *context, uint8_t data)
{
    uint8_t addr = context->state.addr_latch;
    uint32_t fnum;
    uint32_t block;
    uint32_t channel;
    /* TODO: What happens when the write is for an invalid channel? */
    const uint32_t key_on_map [8] = { 0, 1, 2, 0, 3, 4, 5, 0 };

    pthread_mutex_lock (&context->mutex);

    /* Global Registers */
    if (addr < 0x30 && context->state.group_latch == YM2612_GROUP_1)
    {
        switch (addr)
        {
            case 0x22:
                /* TODO: LFO */
                break;

            case 0x24: /* Timer A - High */
                context->state.timer_a_interval = (context->state.timer_a_interval & 0x0003) | data << 2;
                break;
            case 0x25: /* Timer A - Low */
                context->state.timer_a_interval = (context->state.timer_a_interval & 0x03fc) | (data & 0x03);
                break;
            case 0x26: /* Timer B */
                context->state.timer_b_interval = data;
                break;
            case 0x27: /* Timer Control */
                /* Load - Both enables the timer, and does an initial load of the interval value. */
                if (context->state.timer_a_load == false && (data & BIT_0))
                {
                    context->state.timer_a = context->state.timer_a_interval;
                }
                if (context->state.timer_b_load == false && (data & BIT_1))
                {
                    context->state.timer_b = context->state.timer_b_interval;
                }

                context->state.timer_mode = data;

                /* Reset - Clears flags */
                if (context->state.timer_a_reset)
                {
                    context->state.timer_a_flag = false;
                }
                if (context->state.timer_b_reset)
                {
                    context->state.timer_b_flag = false;
                }

                break;

            case 0x28:
                channel = key_on_map [data & 0x07];
                uint32_t first_operator = channel * 4;

                for (uint32_t operator_index = first_operator; operator_index < first_operator + 4; operator_index++)
                {
                    YM2612_Operator_State *operator = &context->state.operator [operator_index];
                    bool key_on = (data >> (operator_index - first_operator + 4)) & 1;

                    if (key_on == true && !operator->key_on)
                    {
                        /* TODO: At key-on, is the attenuation reset to max, or does it
                         *       continue from where it was with the previous note? */
                        operator->phase = 0;
                        operator->eg_state = YM2612_STATE_ATTACK;
                        operator->key_on = true;

                        /* Skip the attack phase if the rate is high enough */
                        if (operator->attack_rate >= 62)
                        {
                            operator->eg_state = YM2612_STATE_DECAY;
                            operator->eg_level = 0;
                        }
                    }

                    else if (key_on == false && operator->key_on)
                    {
                        operator->eg_state = YM2612_STATE_RELEASE;
                        operator->key_on = false;
                    }

                }

                break;

            case 0x2a:
                context->state.dac_output_reg = data;
                break;

            case 0x2b:
                context->state.dac_enable_reg = data;
                break;

            default:
                break;
        }
    }

    /* Operator Registers */
    else if (addr >= 0x30 && addr < 0x90)
    {
        YM2612_Operator_State *operator_group = &context->state.operator [(context->state.group_latch == YM2612_GROUP_1) ? 0 : 12];

        const size_t operator_mapping [16] = { 0, 4, 8, 0xff, 2, 6, 10, 0xff, 1, 5, 9, 0xff, 3, 7, 11, 0xff };
        size_t operator = operator_mapping [addr & 0x0f];

        if (operator < 12)
        {
            switch (addr & 0xf0)
            {
                /* Detune, Multiply */
                case 0x30:
                    /* Store double the configured multiply value so that ½ can be represented */
                    operator_group [operator].multiply = (data & 0x0f) ? (data & 0x0f) * 2 : 1;
                    operator_group [operator].detune = (data & 0x70) >> 4;
                    break;

                /* Total Level */
                case 0x40:
                    operator_group [operator].total_level = (data & 0x7f) << 3;
                    break;

                /* Rate Scale, Attack Rate */
                case 0x50:
                    operator_group [operator].attack_rate = (data & 0x1f) << 1;
                    operator_group [operator].rate_scale = data >> 6;
                    break;

                /* AM, Decay Rate */
                case 0x60:
                    operator_group [operator].decay_rate = (data & 0x1f) << 1;
                    break;

                /* Sustain Rate */
                case 0x70:
                    operator_group [operator].sustain_rate = (data & 0x1f) << 1;
                    break;

                /* Sustain Level, Release Rate */
                case 0x80:
                    operator_group [operator].release_rate = ((data & 0x0f) << 2) + 2;
                    operator_group [operator].sustain_level = ((data & 0xf0) == 0xf0) ? 992 : (data & 0xf0) << 1;
                    break;

                default:
                    break;
            }
        }
    }

    /* Channel Registers */
    else if (addr >= 0xa0 && addr < 0xc0)
    {
        YM2612_Channel_State *channel_group = &context->state.channel [(context->state.group_latch == YM2612_GROUP_1) ? 0 : 3];
        size_t channel = addr & 0x03;

        if (channel < 3)
        {
            switch (addr & 0xfc)
            {
                /* TODO: For now, doing initial sine output only, the block is used to calculate
                 *       the effective fnum, but itself is not saved outside of the latch. If it
                 *       is needed for another feature this may need to be revisited. */

                /* Fnum 7:0 */
                case 0xa0:
                    fnum = ((channel_group [channel].fnum_high_latch & 0x07) << 8) | data;
                    block = (channel_group [channel].fnum_high_latch & 0x38) >> 3;
                    channel_group [channel].fnum = (fnum << block) >> 1;
                    channel_group [channel].key_code = (block << 2) | ((fnum & BIT_10) >> 9) |
                        (fnum >= 0x48) | ((fnum & 0x38) == 0x38);
                    break;

                /* Block, Fnum 10:8 */
                case 0xa4:
                    channel_group [channel].fnum_high_latch = data;
                    break;

                /* Feedback, Algorithm */
                case 0xb0:
                    channel_group [channel].algorithm = data & 0x07;
                    channel_group [channel].feedback = (data & 0x38) >> 3;
                    break;

                /* Panning, PMS, AMS */
                case 0xb4:
                    channel_group [channel].pan_right = (data >> 6) & 1;
                    channel_group [channel].pan_left = data >> 7;
                    break;

                default:
                    break;
            }
        }
    }

    pthread_mutex_unlock (&context->mutex);
}


/*
 * Populate the exp () table.
 * Note that we keep the always-set bit-10.
 */
static void ym2612_populate_exp_table (void)
{
    for (int i = 0; i < 256; i++)
    {
        exp_table [i] = round (exp2 (i / 256.0) * 1024);
    }
}


/*
 * Lookup an entry using the exp table.
 * Input is fixed-point with 8 fractional bits.
 * Output range is ±8168.
 */
static signmag16_t ym2612_exp (signmag16_t val)
{
    /* Note that the index is inverted to account
     * for the log-sine table using -log2. */
    uint8_t fractional = ~(val & 0xff);
    uint16_t integral = (val & MAG_BITS) >> 8;

    int16_t result = (exp_table [fractional] << 2) >> integral;

    /* Propagate the sign */
    result |= (val & SIGN_BIT);

    return result;
}


/*
 * Populate the log (sin ()) table.
 * Fixed-point with 8 fractional bits.
 */
static void ym2612_populate_log_sin_table (void)
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
static signmag16_t ym2612_sin (uint16_t phase)
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


static const uint8_t eg_increment_table [64] [8] = {
    { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 1, 0, 1, 0, 1, 0, 1 }, { 0, 1, 0, 1, 0, 1, 0, 1 },
    { 0, 1, 0, 1, 0, 1, 0, 1 }, { 0, 1, 0, 1, 0, 1, 0, 1 }, { 0, 1, 1, 1, 0, 1, 1, 1 }, { 0, 1, 1, 1, 0, 1, 1, 1 },
    { 0, 1, 0, 1, 0, 1, 0, 1 }, { 0, 1, 0, 1, 1, 1, 0, 1 }, { 0, 1, 1, 1, 0, 1, 1, 1 }, { 0, 1, 1, 1, 1, 1, 1, 1 },
    { 0, 1, 0, 1, 0, 1, 0, 1 }, { 0, 1, 0, 1, 1, 1, 0, 1 }, { 0, 1, 1, 1, 0, 1, 1, 1 }, { 0, 1, 1, 1, 1, 1, 1, 1 },
    { 0, 1, 0, 1, 0, 1, 0, 1 }, { 0, 1, 0, 1, 1, 1, 0, 1 }, { 0, 1, 1, 1, 0, 1, 1, 1 }, { 0, 1, 1, 1, 1, 1, 1, 1 },
    { 0, 1, 0, 1, 0, 1, 0, 1 }, { 0, 1, 0, 1, 1, 1, 0, 1 }, { 0, 1, 1, 1, 0, 1, 1, 1 }, { 0, 1, 1, 1, 1, 1, 1, 1 },
    { 0, 1, 0, 1, 0, 1, 0, 1 }, { 0, 1, 0, 1, 1, 1, 0, 1 }, { 0, 1, 1, 1, 0, 1, 1, 1 }, { 0, 1, 1, 1, 1, 1, 1, 1 },
    { 0, 1, 0, 1, 0, 1, 0, 1 }, { 0, 1, 0, 1, 1, 1, 0, 1 }, { 0, 1, 1, 1, 0, 1, 1, 1 }, { 0, 1, 1, 1, 1, 1, 1, 1 },
    { 0, 1, 0, 1, 0, 1, 0, 1 }, { 0, 1, 0, 1, 1, 1, 0, 1 }, { 0, 1, 1, 1, 0, 1, 1, 1 }, { 0, 1, 1, 1, 1, 1, 1, 1 },
    { 0, 1, 0, 1, 0, 1, 0, 1 }, { 0, 1, 0, 1, 1, 1, 0, 1 }, { 0, 1, 1, 1, 0, 1, 1, 1 }, { 0, 1, 1, 1, 1, 1, 1, 1 },
    { 0, 1, 0, 1, 0, 1, 0, 1 }, { 0, 1, 0, 1, 1, 1, 0, 1 }, { 0, 1, 1, 1, 0, 1, 1, 1 }, { 0, 1, 1, 1, 1, 1, 1, 1 },
    { 0, 1, 0, 1, 0, 1, 0, 1 }, { 0, 1, 0, 1, 1, 1, 0, 1 }, { 0, 1, 1, 1, 0, 1, 1, 1 }, { 0, 1, 1, 1, 1, 1, 1, 1 },
    { 1, 1, 1, 1, 1, 1, 1, 1 }, { 1, 1, 1, 2, 1, 1, 1, 2 }, { 1, 2, 1, 2, 1, 2, 1, 2 }, { 1, 2, 2, 2, 1, 2, 2, 2 },
    { 2, 2, 2, 2, 2, 2, 2, 2 }, { 2, 2, 2, 4, 2, 2, 2, 4 }, { 2, 4, 2, 4, 2, 4, 2, 4 }, { 2, 4, 4, 4, 2, 4, 4, 4 },
    { 4, 4, 4, 4, 4, 4, 4, 4 }, { 4, 4, 4, 8, 4, 4, 4, 8 }, { 4, 8, 4, 8, 4, 8, 4, 8 }, { 4, 8, 8, 8, 4, 8, 8, 8 },
    { 8, 8, 8, 8, 8, 8, 8, 8 }, { 8, 8, 8, 8, 8, 8, 8, 8 }, { 8, 8, 8, 8, 8, 8, 8, 8 }, { 8, 8, 8, 8, 8, 8, 8, 8 }
};


/*
 * Calculate the number of eg_level steps to decay by.
 * Input:  6-bit effective decay rate.
 * Output: Number of steps to attenuate by.
 */
static uint16_t ym2612_decay (YM2612_Context *context, uint16_t rate)
{
    uint32_t global_counter = context->state.eg_global_counter;

    uint32_t shift = (rate >= 44) ? 0 : 11 - (rate / 4);

    /* Only perform a decay step if all bits shifted from the global counter are zeros */
    if ((global_counter & ((1 << shift) - 1)) == 0)
    {
        uint32_t update_cycle = (global_counter >> shift) & 0x07;
        return eg_increment_table [rate] [update_cycle];
    }

    /* Zero steps if the global counter didn't trigger an earlier return */
    return 0;
}


/*
 * Run the envelope generator for one update cycle of an operator.
 */
static void ym2612_envelope_cycle (YM2612_Context *context, YM2612_Operator_State *operator, uint32_t ksr)
{
    uint32_t rate = 0;

    switch (operator->eg_state)
    {
        case YM2612_STATE_ATTACK:
            rate = operator->attack_rate ? operator->attack_rate + ksr : 0;
            operator->eg_level -= ((ym2612_decay (context, rate) * (operator->eg_level + 1)) >> 4);
            if (operator->eg_level == 0)
            {
                operator->eg_state = YM2612_STATE_DECAY;
            }
            break;

        case YM2612_STATE_DECAY:
            rate = operator->decay_rate ? operator->decay_rate + ksr : 0;
            operator->eg_level += ym2612_decay (context, rate);
            if (operator->eg_level >= operator->sustain_level)
            {
                operator->eg_state = YM2612_STATE_SUSTAIN;
            }
            break;

        case YM2612_STATE_SUSTAIN:
            rate = operator->sustain_rate ? operator->sustain_rate + ksr : 0;
            operator->eg_level += ym2612_decay (context, rate);
            break;

        case YM2612_STATE_RELEASE:
            rate = operator->release_rate ? operator->release_rate + ksr : 0;
            operator->eg_level += ym2612_decay (context, rate);
            break;
    }

    ENFORCE_MAXIMUM (operator->eg_level, 1023);
}


/*
 * Detune table, used to give the operator frequency a small
 * nudge away from the channel frequency.
 */
static const uint32_t detune_table [32] [4] = {
    { 0,  0,  1,  2 }, { 0,  0,  1,  2 }, { 0,  0,  1,  2 }, { 0,  0,  1,  2 },
    { 0,  1,  2,  2 }, { 0,  1,  2,  3 }, { 0,  1,  2,  3 }, { 0,  1,  2,  3 },
    { 0,  1,  2,  4 }, { 0,  1,  3,  4 }, { 0,  1,  3,  4 }, { 0,  1,  3,  5 },
    { 0,  2,  4,  5 }, { 0,  2,  4,  6 }, { 0,  2,  4,  6 }, { 0,  2,  5,  7 },
    { 0,  2,  5,  8 }, { 0,  3,  6,  8 }, { 0,  3,  6,  9 }, { 0,  3,  7, 10 },
    { 0,  4,  8, 11 }, { 0,  4,  8, 12 }, { 0,  4,  9, 13 }, { 0,  5, 10, 14 },
    { 0,  5, 11, 16 }, { 0,  6, 12, 17 }, { 0,  6, 13, 19 }, { 0,  7, 14, 20 },
    { 0,  8, 16, 22 }, { 0,  8, 16, 22 }, { 0,  8, 16, 22 }, { 0,  8, 16, 22 }
};


/*
 * Indicates which operators within a channel act as a carrier
 * under the configured algorithm.
 */
static const bool algorithm_carriers [8] [4] = {
    { 0, 0, 0, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 1 },
    { 0, 1, 0, 1 }, { 0, 1, 1, 1 }, { 0, 1, 1, 1 }, { 1, 1, 1, 1 }
};


/*
 * Indicates which operators modulate a channel under the
 * configured algorithm. The three values per entry represent
 * operators 1, 2, and 3 as modulators.
 */
static const bool algorithm_modulators [8] [4] [3] = {
    { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
    { { 0, 0, 0 }, { 0, 0, 0 }, { 1, 1, 0 }, { 0, 0, 1 } },
    { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 1, 0 }, { 1, 0, 1 } },
    { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 0, 1, 1 } },
    { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 0, 0, 1 } },
    { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 0 }, { 1, 0, 0 } },
    { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } },
    { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } }
};


/*
 * Run one sample of a YM2612 channel.
 */
static int32_t ym2413_run_channel_sample (YM2612_Context *context, uint16_t channel_index)
{
    YM2612_Channel_State *channel = &context->state.channel [channel_index];
    uint32_t first_operator = channel_index * 4;

    /* TODO: Calculate effective parameters (fnum, block, etc) */

    /* Run envelope generator on every third sample */
    if (context->completed_samples % 3 == 0)
    {
        for (size_t i = first_operator; i < first_operator + 4; i++)
        {
            YM2612_Operator_State *operator = &context->state.operator [i];
            uint32_t ksr = channel->key_code >> (3 - operator->rate_scale);

            ym2612_envelope_cycle (context, operator, ksr);
        }
    }

    /* Run phase accumulation and output for each operator in the channel */
    int32_t channel_output = 0;
    for (size_t i = 0; i < 4; i++)
    {
        YM2612_Operator_State *operator = &context->state.operator [first_operator + i];

        /* Update phase */
        uint32_t phase_increment = channel->fnum;
        if (operator->detune & BIT_3)
        {
            phase_increment -= detune_table [channel->key_code] [operator->detune & 0x03];
        }
        else
        {
            phase_increment += detune_table [channel->key_code] [operator->detune & 0x03];
        }
        phase_increment &= 0x1ffff; /* fnum + detune gives a 17-bit number */

        /* Multiply, right-shift to account for storing double the multiply
         * value, which is done to allow ½ to be represented. */
        phase_increment = (phase_increment * operator->multiply) >> 1;

        operator->phase += phase_increment;

        int32_t modulation = 0;
        const bool *modulators = algorithm_modulators [channel->algorithm] [i];
        modulation += (modulators [0]) ? context->state.operator [first_operator    ].output [0] : 0;
        modulation += (modulators [1]) ? context->state.operator [first_operator + 1].output [0] : 0;
        modulation += (modulators [2]) ? context->state.operator [first_operator + 2].output [0] : 0;

        /* Feedback */
        if (i == 0 && channel->feedback)
        {
            modulation += (operator->output [0] + operator->output [1]) >> (9 - channel->feedback);
        }

        /* Calculate operator output, fixed-precision with 8 fractional bits. */
        signmag16_t operator_log_value = (operator->total_level + operator->eg_level) << 2;
        ENFORCE_MAXIMUM (operator_log_value, 1023 << 2);
        operator_log_value += ym2612_sin ((operator->phase >> 10) + (modulation >> 1));
        signmag16_t operator_value = ym2612_exp (operator_log_value);
        int32_t operator_output = (operator_value & SIGN_BIT) ? -(operator_value & MAG_BITS) : operator_value;

        if (algorithm_carriers [channel->algorithm] [i])
        {
            channel_output += operator_output;
        }

        operator->output [1] = operator->output [0];
        operator->output [0] = operator_output;
    }

    return channel_output;
}


/*
 * Run the YM2612 for a number of CPU clock cycles.
 */
void _ym2612_run_cycles (YM2612_Context *context, uint32_t clock_rate, uint32_t cycles)
{
    /* The YM2612 takes 144 cycles to update all 6 channels:
     *  - Internally divides input clock by 6
     *  - Multiplexes between the 6 channels, switching every 4 cycles. */
    static uint32_t excess = 0;
    cycles += excess;
    uint32_t ym_samples = cycles / 144;
    excess = cycles - (ym_samples * 144);

    /* Reset the ring buffer if the clock rate changes */
    if (state.console_context != NULL &&
        clock_rate != context->clock_rate)
    {
        context->clock_rate = clock_rate;
        context->read_index = 0;
        context->write_index = 0;
        context->completed_samples = 0;
    }

    /* If we're about to overwrite samples that haven't been read yet,
     * skip the read_index forward to discard some of the backlog. */
    if (context->write_index + (ym_samples * AUDIO_SAMPLE_RATE * 144 / context->clock_rate) >= context->read_index + YM2612_RING_SIZE)
    {
        context->read_index += YM2612_RING_SIZE / 4;
    }

    while (ym_samples--)
    {
        /* Timers */
        if (context->state.timer_a_load)
        {
            context->state.timer_a += 1;
            if (context->state.timer_a >= 0x400)
            {
                /* TODO: Does the timer spend a cycle on 0x4000, or jump straight to interval from 0x3fff? */
                context->state.timer_a = context->state.timer_a_interval;
                if (context->state.timer_a_enable)
                {
                    context->state.timer_a_flag = true;
                }
            }
        }
        context->state.timer_b_divider += 1; /* Timer B ticks at 1/16 the rate of timer A */
        if (context->state.timer_b_load && (context->state.timer_b_divider & 0x0f) == 0)
        {
            context->state.timer_b += 1;
            if (context->state.timer_b >= 0x100)
            {
                context->state.timer_b = context->state.timer_b_interval;
                if (context->state.timer_b_enable)
                {
                    context->state.timer_b_flag = true;
                }
            }
        }

        /* EG Global counter - Increments once per three samples */
        context->state.eg_global_counter_divider += 1;
        if (context->state.eg_global_counter_divider == 3)
        {
            context->state.eg_global_counter += 1;
            context->state.eg_global_counter_divider = 0;
        }

        /* Synthesis */
        int32_t output_level_left = 0;
        int32_t output_level_right = 0;

        /* TODO: Consider breaking this out into a function ym2612_run_channel_sample */

        for (size_t channel_index = 0; channel_index < 6; channel_index++)
        {
            YM2612_Channel_State *channel = &context->state.channel [channel_index];
            int32_t channel_output = 0;

            if (channel_index == 5 && context->state.dac_enable_reg & 0x80)
            {
                channel_output = (context->state.dac_output_reg - 128) << 6;
            }
            else
            {
                channel_output = ym2413_run_channel_sample (context, channel_index);
            }

            if (channel->pan_left)
            {
                output_level_left += channel_output;
            }
            if (channel->pan_right)
            {
                output_level_right += channel_output;
            }
        }

        /* Propagate new samples into ring buffer.
         * Linear interpolation to get 48 kHz from 53.267… kHz */
        if (context->completed_samples * AUDIO_SAMPLE_RATE * 144 > context->write_index * context->clock_rate)
        {
            float portion = (float) ((context->write_index * context->clock_rate) % (AUDIO_SAMPLE_RATE * 144)) /
                            (float) (AUDIO_SAMPLE_RATE * 144);

            int32_t sample_left = roundf (portion * output_level_left + (1.0 - portion) * context->previous_output_level_left);
            int32_t sample_right = roundf (portion * output_level_right + (1.0 - portion) * context->previous_output_level_right);
            context->sample_ring_left [context->write_index % YM2612_RING_SIZE] = BASE_VOLUME * sample_left / 8168;
            context->sample_ring_right [context->write_index % YM2612_RING_SIZE] = BASE_VOLUME * sample_right / 8168;
            context->write_index++;
        }

        context->previous_output_level_left = output_level_left;
        context->previous_output_level_right = output_level_right;
        context->completed_samples++;
    }
}


/*
 * Run the YM2612 for a number of CPU clock cycles (mutex-wrapper)
 *
 * Allows two threads to request sound to be generated:
 *  1. The emulation loop, this is the usual case.
 *  2. Additional samples needed to keep the sound card from running out.
 */
void ym2612_run_cycles (YM2612_Context *context, uint32_t clock_rate, uint32_t cycles)
{
    pthread_mutex_lock (&context->mutex);
    _ym2612_run_cycles (context, clock_rate, cycles);
    pthread_mutex_unlock (&context->mutex);
}


/*
 * Initialise a new YM2612 context.
 */
YM2612_Context *ym2612_init (void)
{
    static bool first = true;

    if (first)
    {
        /* Once-off initialisations */
        first = false;
        ym2612_populate_exp_table ();
        ym2612_populate_log_sin_table ();
    }

    YM2612_Context *context = calloc (1, sizeof (YM2612_Context));
    pthread_mutex_init (&context->mutex, NULL); /* TODO: mutex_destroy */

    /* Initialize assuming NTSC Mega Drive - Will be updated when the run callback is made. */
    context->clock_rate = 7670453;

    /* Default Values */
    context->state.dac_output_reg = 0x80;
    for (int channel_index = 0; channel_index < 6; channel_index++)
    {
        YM2612_Channel_State *channel = &context->state.channel [channel_index];

        channel->pan_left = true;
        channel->pan_right = true;
    }
    for (int operator_index = 0; operator_index < 24; operator_index++)
    {
        YM2612_Operator_State *operator = &context->state.operator [operator_index];

        operator->total_level = 127;
        operator->eg_level = 1023;
    }

    return context;
}
