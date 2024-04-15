/*
 * Snepulator
 * TI SN76489 PSG implementation.
 */

#include <assert.h>
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
#include "../save_state.h"
#include "band_limit.h"
#include "sn76489.h"
extern Snepulator_State state;

/* Represents the level of a single channel at maximum volume */
#define BASE_VOLUME 1536

#define GG_CH0_RIGHT    BIT_0
#define GG_CH1_RIGHT    BIT_1
#define GG_CH2_RIGHT    BIT_2
#define GG_CH3_RIGHT    BIT_3
#define GG_CH0_LEFT     BIT_4
#define GG_CH1_LEFT     BIT_5
#define GG_CH2_LEFT     BIT_6
#define GG_CH3_LEFT     BIT_7

/* Volume table, indexed by the volume (attenuation) register. */
static int16_t volume_table [16] = { };


/*
 * Handle data writes sent to the PSG.
 */
void sn76489_data_write (SN76489_Context *context, uint8_t data)
{
    if (data & 0x80)
    {
        context->state.latch = data & 0x70;
    }

    switch (context->state.latch)
    {
        case 0x00: /* Channel 0 tone */
            if (data & 0x80)
            {
                context->state.tone_0_low = data;
            }
            else
            {
                context->state.tone_0_high = data;
            }
            break;

        case 0x10: /* Channel 0 volume */
            context->state.vol_0_low = data;
            break;

        case 0x20: /* Channel 1 tone */
            if (data & 0x80)
            {
                context->state.tone_1_low = data;
            }
            else
            {
                context->state.tone_1_high = data;
            }
            break;

        case 0x30: /* Channel 1 volume */
            context->state.vol_1_low = data;
            break;

        case 0x40: /* Channel 2 tone */
            if (data & 0x80)
            {
                context->state.tone_2_low = data;
            }
            else
            {
                context->state.tone_2_high = data;
            }
            break;

        case 0x50: /* Channel 2 volume */
            context->state.vol_2_low = data;
            break;

        case 0x60: /* Channel 3 noise */
            context->state.noise_low = data;
            context->state.lfsr  = 0x8000;
            break;

        case 0x70: /* Channel 3 volume */
            context->state.vol_3_low = data;
            break;

        default:
            break;
    }
}


/*
 * Populate the volume table with the output levels for a single channel.
 */
static void sn76489_populate_volume_table (void)
{
    /* Only populate 15 volume levels, level 16 is silent. */
    for (int i = 0; i < 15; i++)
    {
        /* 2 dB attenuation per step */
        volume_table [i] = round (BASE_VOLUME * pow (10.0, (-2.0 * i) / 20.0));
    }
}


/*
 * Reset PSG to initial power-on state.
 */
SN76489_Context *sn76489_init (void)
{
    static bool first = true;

    if (first)
    {
        /* Once-off initialisations */
        first = false;
        sn76489_populate_volume_table ();
    }

    SN76489_Context *context = calloc (1, sizeof (SN76489_Context));
    pthread_mutex_init (&context->mutex, NULL);

    context->state.vol_0 = 0x0f;
    context->state.vol_1 = 0x0f;
    context->state.vol_2 = 0x0f;
    context->state.vol_3 = 0x0f;

    context->state.output_0 = 1;
    context->state.output_1 = 1;
    context->state.output_2 = 1;
    context->state.output_3 = 1;

    context->state.gg_stereo = 0xff;

    context->bandlimit_context_l = band_limit_init ();
    context->bandlimit_context_r = band_limit_init ();

    return context;
}


/*
 * Run the PSG for a number of CPU clock cycles
 */
void _psg_run_cycles (SN76489_Context *context, uint64_t cycles)
{
    /* Divide the system clock by 16, store the excess cycles for next time */
    static uint32_t excess = 0;
    cycles += excess;
    uint32_t psg_cycles = cycles >> 4;
    excess = cycles - (psg_cycles << 4);

    /* Reset the ring buffer if the clock rate changes */
    if (state.console_context != NULL &&
        context->clock_rate != (state.get_clock_rate (state.console_context) >> 4))
    {
        context->clock_rate = state.get_clock_rate (state.console_context) >> 4;

        context->read_index = 0;
        context->write_index = 0;
        context->completed_cycles = 0;
    }

    /* If we're about to overwrite samples that haven't been read yet,
     * skip the read_index forward to discard some of the backlog. */
    if (context->write_index + (psg_cycles * AUDIO_SAMPLE_RATE / context->clock_rate) >= context->read_index + SN76489_RING_SIZE)
    {
        context->read_index += SN76489_RING_SIZE / 4;
    }

    while (psg_cycles--)
    {
        /* Decrement counters */
        if (context->state.counter_0) { context->state.counter_0--; }
        if (context->state.counter_1) { context->state.counter_1--; }
        if (context->state.counter_2) { context->state.counter_2--; }
        if (context->state.counter_3) { context->state.counter_3--; }

        /* Toggle outputs */
        if (context->state.counter_0 == 0)
        {
            context->state.counter_0 = context->state.tone_0;
            context->state.output_0 *= -1;
        }
        if (context->state.counter_1 == 0)
        {
            context->state.counter_1 = context->state.tone_1;
            context->state.output_1 *= -1;
        }
        if (context->state.counter_2 == 0)
        {
            context->state.counter_2 = context->state.tone_2;
            context->state.output_2 *= -1;
        }
        if (context->state.counter_3 == 0)
        {
            switch (context->state.noise & 0x03)
            {
                case 0x00:  context->state.counter_3 = 0x10;                    break;
                case 0x01:  context->state.counter_3 = 0x20;                    break;
                case 0x02:  context->state.counter_3 = 0x40;                    break;
                case 0x03:  context->state.counter_3 = context->state.tone_2;   break;
                default:    break;
            }
            context->state.output_3 *= -1;

            /* On transition from -1 to 1, shift the LFSR */
            if (context->state.output_3 == 1)
            {
                context->state.output_lfsr = (context->state.lfsr & 0x0001);

                if (context->state.noise & (1 << 2))
                {
                    /* White noise - Tap bits 0 and 3 */
                    context->state.lfsr >>= 1;
                    context->state.lfsr ^= (context->state.output_lfsr) ? 0x9000 : 0;
                }
                else
                {
                    /* Periodic noise - Tap bit 0 */
                    context->state.lfsr >>= 1;
                    context->state.lfsr ^= (context->state.output_lfsr) ? 0x8000 : 0;
                }
            }
        }

        /* Tone channels output +1 if their tone register is zero */
        if (context->state.tone_0 <= 1) { context->state.output_0 = 1; }
        if (context->state.tone_1 <= 1) { context->state.output_1 = 1; }
        if (context->state.tone_2 <= 1) { context->state.output_2 = 1; }

        /* Store this sample in the ring */
        if (state.console == CONSOLE_GAME_GEAR)
        {
            context->sample_ring_l [context->write_index % SN76489_RING_SIZE] =
                (context->state.gg_stereo & GG_CH0_LEFT ? context->state.output_0 * volume_table [context->state.vol_0] : 0)
              + (context->state.gg_stereo & GG_CH1_LEFT ? context->state.output_1 * volume_table [context->state.vol_1] : 0)
              + (context->state.gg_stereo & GG_CH2_LEFT ? context->state.output_2 * volume_table [context->state.vol_2] : 0)
              + (context->state.gg_stereo & GG_CH3_LEFT ? (context->state.output_lfsr ? 1 : -1) * volume_table [context->state.vol_3] : 0);

            if (context->sample_ring_l [context->write_index % SN76489_RING_SIZE] != context->previous_sample_l)
            {
                /* Phase ranges from 0 (no delay) to 31 (0.97 samples delay) */
                context->phase_ring_l [context->write_index % SN76489_RING_SIZE] =
                    (context->completed_cycles * AUDIO_SAMPLE_RATE * 32 / context->clock_rate) % 32;
                context->previous_sample_l = context->sample_ring_l [context->write_index % SN76489_RING_SIZE];
            }

            context->sample_ring_r [context->write_index % SN76489_RING_SIZE] =
                (context->state.gg_stereo & GG_CH0_RIGHT ? context->state.output_0 * volume_table [context->state.vol_0] : 0)
              + (context->state.gg_stereo & GG_CH1_RIGHT ? context->state.output_1 * volume_table [context->state.vol_1] : 0)
              + (context->state.gg_stereo & GG_CH2_RIGHT ? context->state.output_2 * volume_table [context->state.vol_2] : 0)
              + (context->state.gg_stereo & GG_CH3_RIGHT ? (context->state.output_lfsr ? 1 : -1) * volume_table [context->state.vol_3] : 0);

            if (context->sample_ring_r [context->write_index % SN76489_RING_SIZE] != context->previous_sample_r)
            {
                /* Phase ranges from 0 (no delay) to 31 (0.97 samples delay) */
                context->phase_ring_r [context->write_index % SN76489_RING_SIZE] =
                    (context->completed_cycles * AUDIO_SAMPLE_RATE * 32 / context->clock_rate) % 32;
                context->previous_sample_r = context->sample_ring_r [context->write_index % SN76489_RING_SIZE];
            }

            /* If this is the final value for this sample, pass it to the band limiter */
            if ((context->completed_cycles + 1) * AUDIO_SAMPLE_RATE / context->clock_rate > context->write_index)
            {
                band_limit_samples (context->bandlimit_context_l, &context->sample_ring_l [context->write_index % SN76489_RING_SIZE],
                                                                  &context->phase_ring_l [context->write_index % SN76489_RING_SIZE], 1);
                band_limit_samples (context->bandlimit_context_r, &context->sample_ring_r [context->write_index % SN76489_RING_SIZE],
                                                                  &context->phase_ring_r [context->write_index % SN76489_RING_SIZE], 1);
            }
        }
        else
        {
            context->sample_ring_l [context->write_index % SN76489_RING_SIZE] =
                context->state.output_0 * volume_table [context->state.vol_0]
              + context->state.output_1 * volume_table [context->state.vol_1]
              + context->state.output_2 * volume_table [context->state.vol_2]
              + (context->state.output_lfsr ? 1 : -1) * volume_table [context->state.vol_3];

            if (context->sample_ring_l [context->write_index % SN76489_RING_SIZE] != context->previous_sample_l)
            {
                /* Phase ranges from 0 (no delay) to 31 (0.97 samples delay) */
                context->phase_ring_l [context->write_index % SN76489_RING_SIZE] =
                    (context->completed_cycles * AUDIO_SAMPLE_RATE * 32 / context->clock_rate) % 32;
                context->previous_sample_l = context->sample_ring_l [context->write_index % SN76489_RING_SIZE];
            }

            /* If this is the final value for this sample, pass it to the band limiter */
            if ((context->completed_cycles + 1) * AUDIO_SAMPLE_RATE / context->clock_rate > context->write_index)
            {
                band_limit_samples (context->bandlimit_context_l, &context->sample_ring_l [context->write_index % SN76489_RING_SIZE],
                                                                  &context->phase_ring_l [context->write_index % SN76489_RING_SIZE], 1);
            }
        }

        /* Map from the amount of time emulated (completed cycles / clock rate) to the sound card sample rate */
        context->completed_cycles++;
        context->write_index = context->completed_cycles * AUDIO_SAMPLE_RATE / context->clock_rate;
    }

#ifdef DEVELOPER_BUILD
    /* Update statistics (rolling average) */
    state.audio_ring_utilisation *= 0.9995;
    state.audio_ring_utilisation += 0.0005 * ((context->write_index - context->read_index) / (double) SN76489_RING_SIZE);
#endif
}


/*
 * Run the PSG for a number of CPU clock cycles (mutex-wrapper)
 *
 * Allows two threads to request sound to be generated:
 *  1. The emulation loop, this is the usual case.
 *  2. SDL may request additional samples to keep the sound card from running out.
 */
void psg_run_cycles (SN76489_Context *context, uint64_t cycles)
{
    pthread_mutex_lock (&context->mutex);
    _psg_run_cycles (context, cycles);
    pthread_mutex_unlock (&context->mutex);
}


/*
 * Retrieves a block of samples from the sample-ring.
 * Assumes that the number of samples requested fits evenly into the ring buffer.
 */
void sn76489_get_samples (SN76489_Context *context, int16_t *stream, uint32_t count)
{
    if (context->read_index + count > context->write_index)
    {
        int shortfall = count - (context->write_index - context->read_index);

        /* Note: We add one to the shortfall to account for integer division */
        psg_run_cycles (context, (shortfall + 1) * (context->clock_rate << 4) / AUDIO_SAMPLE_RATE);
    }

    /* Take samples and pass them to the sound card */
    for (int i = 0; i < count; i++)
    {
        size_t sample_index = (context->read_index + i) & (SN76489_RING_SIZE - 1);

        /* Left, Right */
        if (state.console == CONSOLE_GAME_GEAR)
        {
            stream [2 * i    ] += context->sample_ring_l [sample_index];
            stream [2 * i + 1] += context->sample_ring_r [sample_index];
        }
        else
        {
            stream [2 * i    ] += context->sample_ring_l [sample_index];
            stream [2 * i + 1] += context->sample_ring_l [sample_index];
        }
    }

    context->read_index += count;
}


/*
 * Export sn76489 state.
 */
void sn76489_state_save (SN76489_Context *context)
{
    SN76489_State sn76489_state_be = {
        .vol_0 =       util_hton16 (context->state.vol_0),
        .vol_1 =       util_hton16 (context->state.vol_1),
        .vol_2 =       util_hton16 (context->state.vol_2),
        .vol_3 =       util_hton16 (context->state.vol_3),
        .tone_0 =      util_hton16 (context->state.tone_0),
        .tone_1 =      util_hton16 (context->state.tone_1),
        .tone_2 =      util_hton16 (context->state.tone_2),
        .noise =       util_hton16 (context->state.noise),
        .counter_0 =   util_hton16 (context->state.counter_0),
        .counter_1 =   util_hton16 (context->state.counter_1),
        .counter_2 =   util_hton16 (context->state.counter_2),
        .counter_3 =   util_hton16 (context->state.counter_3),
        .output_0 =    util_hton16 (context->state.output_0),
        .output_1 =    util_hton16 (context->state.output_1),
        .output_2 =    util_hton16 (context->state.output_2),
        .output_3 =    util_hton16 (context->state.output_3),
        .latch =       util_hton16 (context->state.latch),
        .lfsr =        util_hton16 (context->state.lfsr),
        .output_lfsr = util_hton16 (context->state.output_lfsr),
        .gg_stereo =   util_hton16 (context->state.gg_stereo)
    };

    save_state_section_add (SECTION_ID_PSG, 1, sizeof (sn76489_state_be), &sn76489_state_be);
}


/*
 * Import sn76489 state.
 */
void sn76489_state_load (SN76489_Context *context, uint32_t version, uint32_t size, void *data)
{
    SN76489_State sn76489_state_be;

    if (size == sizeof (sn76489_state_be))
    {
        memcpy (&sn76489_state_be, data, sizeof (sn76489_state_be));

        context->state.vol_0 =       util_ntoh16 (sn76489_state_be.vol_0);
        context->state.vol_1 =       util_ntoh16 (sn76489_state_be.vol_1);
        context->state.vol_2 =       util_ntoh16 (sn76489_state_be.vol_2);
        context->state.vol_3 =       util_ntoh16 (sn76489_state_be.vol_3);
        context->state.tone_0 =      util_ntoh16 (sn76489_state_be.tone_0);
        context->state.tone_1 =      util_ntoh16 (sn76489_state_be.tone_1);
        context->state.tone_2 =      util_ntoh16 (sn76489_state_be.tone_2);
        context->state.noise =       util_ntoh16 (sn76489_state_be.noise);
        context->state.counter_0 =   util_ntoh16 (sn76489_state_be.counter_0);
        context->state.counter_1 =   util_ntoh16 (sn76489_state_be.counter_1);
        context->state.counter_2 =   util_ntoh16 (sn76489_state_be.counter_2);
        context->state.counter_3 =   util_ntoh16 (sn76489_state_be.counter_3);
        context->state.output_0 =    util_ntoh16 (sn76489_state_be.output_0);
        context->state.output_1 =    util_ntoh16 (sn76489_state_be.output_1);
        context->state.output_2 =    util_ntoh16 (sn76489_state_be.output_2);
        context->state.output_3 =    util_ntoh16 (sn76489_state_be.output_3);
        context->state.latch =       util_ntoh16 (sn76489_state_be.latch);
        context->state.lfsr =        util_ntoh16 (sn76489_state_be.lfsr);
        context->state.output_lfsr = util_ntoh16 (sn76489_state_be.output_lfsr);
        context->state.gg_stereo =   util_ntoh16 (sn76489_state_be.gg_stereo);
    }
    else
    {
        snepulator_error ("Error", "Save-state contains incorrect SN76489 size");
    }
}
