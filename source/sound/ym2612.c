/*
 * Snepulator
 * YM2612 FM synthesizer chip implementation.
 *
 * Initial implementation only to get basic DAC output working.
 *
 * TODO:
 *  - Operators
 *  - Algorithms & Modulation
 *  - Feedback
 *  - Envelopes
 *  - LFO
 *  - Stereo
 */


#include <math.h>
#include <stdlib.h>

#include "../snepulator.h"

extern Snepulator_State state;

#include "ym2612.h"

/* Represents the level of a single melody channel at maximum volume */
#define BASE_VOLUME 4096


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
 * Latch a register address.
 */
void ym2612_addr1_write (YM2612_Context *context, uint8_t addr)
{
    context->state.addr_latch = addr;
}


/*
 * Write data to the latched register address.
 */
void ym2612_data_write (YM2612_Context *context, uint8_t data)
{
    uint8_t addr = context->state.addr_latch;

    pthread_mutex_lock (&context->mutex);

    switch (addr)
    {
        case 0x2a:
            context->state.dac_output_reg = data;
            break;
        case 0x2b:
            context->state.dac_enable_reg = data;
            break;
        default:
            break;
    }

    pthread_mutex_unlock (&context->mutex);
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
        int16_t output_level = 0;

        if (context->state.dac_enable_reg & 0x80)
        {
            /* TODO: Once FM channels are implemented, the DAC amplitude will need to be
             *       scaled appropriately. For now though the DAC is the only ym2612 feature
             *       that makes sound. */
            output_level += (context->state.dac_output_reg - 128);
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
    YM2612_Context *context = calloc (1, sizeof (YM2612_Context));
    pthread_mutex_init (&context->mutex, NULL); /* TODO: mutex_destroy */

    /* Initialize assuming NTSC Mega Drive - Will be updated when the run callback is made. */
    context->clock_rate = 7670453;


    return context;
}
