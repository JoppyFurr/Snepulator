#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "band_limit.h"

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

#define PHASE_COUNT 32
#define PHASE_SAMPLES 48
#define MASTER_SAMPLE_COUNT (PHASE_COUNT * PHASE_SAMPLES)

/* A single high-resolution step from -1.0 to +1.0 */
static double master_step [MASTER_SAMPLE_COUNT] = {};

/* A collection of sampled steps at different phase offsets.
 * Each step has a magnitude of +1.0 and is stored as differences. */
static double step [PHASE_COUNT] [PHASE_SAMPLES] = {};

/*
 * Calculate the band-limited master step.
 *
 * This step is a single transition from -1.0 -> 1.0.
 * A 50 Hz fundamental is used, as this allows a reasonable number of harmonics.
 */
static void calculate_master_step (int limit)
{
    int base_hz = 50;
    int harmonic = 0;

    memset (master_step, 0, sizeof (master_step));

    int frequency = base_hz;

    while (frequency < limit)
    {
        for (int i = 0; i < MASTER_SAMPLE_COUNT; i++)
        {
            /* t rages from -0.5 ms -- +0.5 ms, covering our 48 samples at 48 kHz. */
            double t = (i - MASTER_SAMPLE_COUNT / 2) * ((48.0 / 48000.0) / MASTER_SAMPLE_COUNT);

            /* Index [768] is the zero crossing */
            master_step [i] += sin (frequency * t * (2 * M_PI)) * (4 / M_PI) / (1 + (2 * harmonic));
        }

        harmonic++;
        frequency = base_hz * (1 + 2 * harmonic);
    }
}


/*
 * Sample the master step, which is a band-limited transition from -1.0 to +1.0.
 *  - Zero-crossing is at (MASTER_SAMPLE_COUNT / 2)
 *  - Any value left of the samples is -1.0
 *  - Any value right of the samples is +1.0
 *  - Signals taper towards the ends
 */
static double sample_master_step (int index)
{
    if (index < 0)
    {
        return -1.0;
    }
    if (index >= MASTER_SAMPLE_COUNT)
    {
        return +1.0;
    }

    double sample = master_step [index];

    /* Taper towards the ends */
    int taper_length = MASTER_SAMPLE_COUNT / 8;

    if (index < taper_length)
    {
        double ratio = index / (double) taper_length;
        sample = (sample * ratio) + (-1.0 * (1.0 - ratio));
    }

    if (index > (MASTER_SAMPLE_COUNT - taper_length))
    {
        double ratio = (MASTER_SAMPLE_COUNT - index) / (double) taper_length;
        sample = (sample * ratio) + (+1.0 * (1.0 - ratio));
    }

    return sample;
}


/*
 * Calculate the phased difference arrays.
 *
 * The each index into the phase adds 1/32 delay.
 */
void calculate_phase_steps (void)
{
    for (int phase = 0; phase < PHASE_COUNT; phase++)
    {
        for (int sample = 0; sample < PHASE_SAMPLES; sample++)
        {
            int offset = PHASE_COUNT - phase - 31;

            /* Note: The value is halved, as the master step is a transition from -1.0 to +1.0, where we need a transition of magnitude 1.0 */
            step [phase] [sample] = (sample_master_step (offset + sample * PHASE_COUNT) - sample_master_step (offset + (sample - 1) * PHASE_COUNT)) * 0.5;
        }
    }

    /* Ensure each transition has a total delta of 1.0 */
    for (int phase = 0; phase < PHASE_COUNT; phase++)
    {
        double phase_sum = 0.0;

        /* Add up the samples in the phase so we can account for drift */
        for (int sample = 0; sample < PHASE_SAMPLES; sample++)
        {
            phase_sum += step [phase] [sample];

        }

        /* Evenly counteract drift across all samples within the phase */
        for (int sample = 0; sample < PHASE_SAMPLES; sample++)
        {
            step [phase] [sample] += (1.0 - phase_sum) / PHASE_SAMPLES;
        }
    }
}


/*
 * Apply band-limited synthesis to non-limited input.
 *
 * Note: a 24 sample delay is introduced, as samples are affected by future input.
 *
 * TODO: For lower end devices, consider only using integers during emulation.
 *       eg, use uint32 phase steps from 0 -> 32768 and arithmetic right-shift the result.
 */
void band_limit_samples (Bandlimit_Context *context, int16_t *sample_buf, int16_t *phase_buf, int count)
{
    for (int i = 0; i < count; i++)
    {
        /* First, take any steps in our square-wave input and convert them to band-limited differences */
        double delta = sample_buf [i] - context->previous_input;
        context->previous_input = sample_buf [i];

        if (delta)
        {
            int phase = phase_buf [i];

            /* Apply the transition */
            for (int j = 0; j < PHASE_SAMPLES; j++)
            {
                context->diff_ring [(context->diff_ring_index + j) % DIFF_RING_SIZE] += step [phase] [j] * delta;
            }
        }

        /* Now, we have a sample that is ready for conversion from difference to value */
        context->output += context->diff_ring [context->diff_ring_index];
        sample_buf [i] = context->output;

        /* Now that the value has been used, zero it for the next trip around the ring */
        context->diff_ring [context->diff_ring_index] = 0.0;

        /* Advance to the next index in the diff ring */
        context->diff_ring_index = (context->diff_ring_index + 1) % DIFF_RING_SIZE;
    }
}


/*
 * Initialise data for band limited synthesis.
 */
Bandlimit_Context *band_limit_init (void)
{
    static bool first = true;

    if (first)
    {
        /* Once-off initializations */
        first = false;
        calculate_master_step (24000);
        calculate_phase_steps ();
    }

    Bandlimit_Context *context = calloc (1, sizeof (Bandlimit_Context));
    context->previous_input = 0;
    context->output = 0.0;

    return context;
}
