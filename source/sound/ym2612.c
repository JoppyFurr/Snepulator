/*
 * Snepulator
 * YM2612 FM synthesizer chip implementation.
 *
 * Initial implementation only to get basic DAC output working.
 *
 * TODO:
 *  - Operators
 *    - Four per channel
 *    - Algorithms
 *    - Detune
 *    - Special mode for channel 3
 *    - Multiply
 *    - Key per-operator
 *  - Algorithms & Modulation
 *  - Modulation
 *    - Total level
 *    - Feedback
 *  - Envelopes
 *    - State machine
 *    - Attack
 *    - Decay
 *    - Sustain level & rate
 *    - Release
 *    - SSG-EG
 *    - CSM
 *  - LFO / AMS / PMS
 *  - Stereo
 */


#include <math.h>
#include <stdlib.h>

#include "../snepulator.h"

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
        stream [2 * i    ] += context->sample_ring [sample_index];
        stream [2 * i + 1] += context->sample_ring [sample_index];
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
    /* TODO: If switching to C23, then these can live inside the switch statement */
    uint32_t fnum;
    uint32_t block;
    uint32_t channel;
    /* TODO: What happens when the write is for an invalid channel? */
    const uint32_t key_on_map [8] = { 0, 1, 2, 0, 3, 4, 5, 0 };

    pthread_mutex_lock (&context->mutex);

    if (context->state.group_latch == YM2612_GROUP_1)
    {
        switch (addr)
        {
            /* Global Registers */
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
                /* TODO: Separate key-on per operator. For now just use operator four as representative. */
                channel = key_on_map [data & 0x07];
                if (context->state.key_on [channel] == false && data >> 7)
                {
                    context->state.operator [channel].phase = 0;
                }
                context->state.key_on [channel] = data >> 7;
                break;

            case 0x2a:
                context->state.dac_output_reg = data;
                break;

            case 0x2b:
                context->state.dac_enable_reg = data;
                break;

            /* Operator Registers */

            /* Channel Registers */
            /* TODO: For now, doing initial sine output only, the block is used to calculate
             *       the effective fnum, but itself is not saved outside of the latch. If it
             *       is needed for another feature this may need to be revisited. */
            case 0xa0:
                fnum = ((context->state.fnum_high_latch [0] & 0x07) << 8) | data;
                block = (context->state.fnum_high_latch [0] & 0x38) >> 3;
                context->state.fnum [0] = (fnum << block) >> 1;
                break;
            case 0xa1:
                fnum = ((context->state.fnum_high_latch [1] & 0x07) << 8) | data;
                block = (context->state.fnum_high_latch [1] & 0x38) >> 3;
                context->state.fnum [1] = (fnum << block) >> 1;
                break;
            case 0xa2:
                fnum = ((context->state.fnum_high_latch [2] & 0x07) << 8) | data;
                block = (context->state.fnum_high_latch [2] & 0x38) >> 3;
                context->state.fnum [2] = (fnum << block) >> 1;
                break;

            case 0xa4:
                context->state.fnum_high_latch [0] = data;
                break;
            case 0xa5:
                context->state.fnum_high_latch [1] = data;
                break;
            case 0xa6:
                context->state.fnum_high_latch [2] = data;
                break;

            default:
                break;
        }
    }
    else if (context->state.group_latch == YM2612_GROUP_2)
    {
        switch (addr)
        {
            /* Operator Registers */

            /* Channel Registers */
            case 0xa0:
                fnum = ((context->state.fnum_high_latch [3] & 0x07) << 8) | data;
                block = (context->state.fnum_high_latch [3] & 0x38) >> 3;
                context->state.fnum [3] = (fnum << block) >> 1;
                break;
            case 0xa1:
                fnum = ((context->state.fnum_high_latch [4] & 0x07) << 8) | data;
                block = (context->state.fnum_high_latch [4] & 0x38) >> 3;
                context->state.fnum [4] = (fnum << block) >> 1;
                break;
            case 0xa2:
                fnum = ((context->state.fnum_high_latch [5] & 0x07) << 8) | data;
                block = (context->state.fnum_high_latch [5] & 0x38) >> 3;
                context->state.fnum [5] = (fnum << block) >> 1;
                break;

            case 0xa4:
                context->state.fnum_high_latch [3] = data;
                break;
            case 0xa5:
                context->state.fnum_high_latch [4] = data;
                break;
            case 0xa6:
                context->state.fnum_high_latch [5] = data;
                break;

            default:
                break;
        }
    }

    pthread_mutex_unlock (&context->mutex);
}


/*
 * Populate the exp () table.
 * Note that we keep the always-set bit-10.
 *
 * TODO: This is based on the ym2413 implementation. Double check it's the same.
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
 * Output range is ±4084.
 *
 * TODO: This is based on the ym2413 implementation. Double check it's the same.
 */
static signmag16_t ym2612_exp (signmag16_t val)
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
 *
 * TODO: This is based on the ym2413 implementation. Double check it's the same.
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
 *
 * TODO: This is based on the ym2413 implementation. Double check it's the same.
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

        /* Synthesis */
        int16_t output_level = 0;

        for (uint32_t channel = 0; channel < 6; channel++)
        {
            if (channel == 5 && context->state.dac_enable_reg & 0x80)
            {
                /* TODO: DAC amplitude will need to be scaled appropriately. */
                output_level += (context->state.dac_output_reg - 128);
            }
            else
            {
                context->state.operator [channel].phase += context->state.fnum [channel];

                signmag16_t log_carrier_value = ym2612_sin (context->state.operator [channel].phase >> 10);
                /* TODO: Volume, envelope, etc. */
                log_carrier_value += 1536; /* Substitute for volume control */
                signmag16_t carrier_value = ym2612_exp (log_carrier_value);

                /* TODO: Shift was from YM2413 - May not be valid for 2612. */
                if (carrier_value & SIGN_BIT)
                {
                    output_level -= ((carrier_value & MAG_BITS) >> 1);
                }
                else
                {
                    output_level += ((carrier_value & MAG_BITS) >> 1);
                }
            }
        }

        /* Propagate new samples into ring buffer.
         * Linear interpolation to get 48 kHz from 53.267… kHz */
        if (context->completed_samples * AUDIO_SAMPLE_RATE * 144 > context->write_index * context->clock_rate)
        {
            float portion = (float) ((context->write_index * context->clock_rate) % (AUDIO_SAMPLE_RATE * 144)) /
                            (float) (AUDIO_SAMPLE_RATE * 144);

            int16_t sample = roundf (portion * output_level + (1.0 - portion) * context->previous_output_level);
            /* TODO: 128 constant is just the maximum amplitude of the DAC, once converted to signed,
             *       this will need sorting out once FM channels are added in. */
            context->sample_ring [context->write_index % YM2612_RING_SIZE] = BASE_VOLUME * sample / 128;
            context->write_index++;
        }

        context->previous_output_level = output_level;
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


    return context;
}
