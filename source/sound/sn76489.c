#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <arpa/inet.h>

#include "../snepulator_types.h"
#include "../snepulator.h"
#include "../save_state.h"
#include "band_limit.h"
#include "sn76489.h"
extern Snepulator_State state;

/* State */
SN76489_State sn76489_state;
pthread_mutex_t psg_mutex = PTHREAD_MUTEX_INITIALIZER;

#define SAMPLE_RATE 48000
#define PSG_RING_SIZE 4096
#define BASE_VOLUME 100

#define GG_CH0_RIGHT    BIT_0
#define GG_CH1_RIGHT    BIT_1
#define GG_CH2_RIGHT    BIT_2
#define GG_CH3_RIGHT    BIT_3
#define GG_CH0_LEFT     BIT_4
#define GG_CH1_LEFT     BIT_5
#define GG_CH2_LEFT     BIT_6
#define GG_CH3_LEFT     BIT_7

/*
 * PSG output ring buffer.
 * Stores samples and phase data with a sample rate of 48 kHz.
 * As the read/write index are 64-bit, they should never overflow.
 */
static uint64_t write_index = 0;
static uint64_t read_index = 0;
static uint64_t completed_cycles = 0;
static uint32_t clock_rate = 0;

static Bandlimit_Context *bandlimit_context_l = NULL;
static int16_t sample_ring_l [PSG_RING_SIZE];
static int16_t phase_ring_l [PSG_RING_SIZE];
static uint16_t previous_sample_l = 0.0;

static Bandlimit_Context *bandlimit_context_r = NULL;
static int16_t sample_ring_r [PSG_RING_SIZE];
static int16_t phase_ring_r [PSG_RING_SIZE];
static uint16_t previous_sample_r = 0.0;


/*
 * Handle data writes sent to the PSG.
 */
void sn76489_data_write (uint8_t data)
{
    if (data & 0x80)
    {
        sn76489_state.latch = data & 0x70;
    }

    switch (sn76489_state.latch)
    {
        case 0x00: /* Channel 0 tone */
            if (data & 0x80)
            {
                sn76489_state.tone_0_low = data;
            }
            else
            {
                sn76489_state.tone_0_high = data;
            }
            break;

        case 0x10: /* Channel 0 volume */
            sn76489_state.vol_0_low = data;
            break;

        case 0x20: /* Channel 1 tone */
            if (data & 0x80)
            {
                sn76489_state.tone_1_low = data;
            }
            else
            {
                sn76489_state.tone_1_high = data;
            }
            break;

        case 0x30: /* Channel 1 volume */
            sn76489_state.vol_1_low = data;
            break;

        case 0x40: /* Channel 2 tone */
            if (data & 0x80)
            {
                sn76489_state.tone_2_low = data;
            }
            else
            {
                sn76489_state.tone_2_high = data;
            }
            break;

        case 0x50: /* Channel 2 volume */
            sn76489_state.vol_2_low = data;
            break;

        case 0x60: /* Channel 3 noise */
            sn76489_state.noise_low = data;
            sn76489_state.lfsr  = 0x8000;
            break;

        case 0x70: /* Channel 3 volume */
            sn76489_state.vol_3_low = data;
            break;

        default:
            break;
    }
}


/*
 * Reset PSG to initial power-on state.
 */
void sn76489_init (void)
{
    memset (&sn76489_state, 0, sizeof (sn76489_state));
    sn76489_state.vol_0 = 0x0f;
    sn76489_state.vol_1 = 0x0f;
    sn76489_state.vol_2 = 0x0f;
    sn76489_state.vol_3 = 0x0f;

    sn76489_state.output_0 =  1;
    sn76489_state.output_1 = -1;
    sn76489_state.output_2 =  1;
    sn76489_state.output_3 = -1;

    sn76489_state.gg_stereo = 0xff;

    memset (sample_ring_l, 0, sizeof (sample_ring_l));
    memset (phase_ring_l, 0, sizeof (phase_ring_l));
    previous_sample_l = 0.0;
    if (bandlimit_context_l != NULL)
    {
        free (bandlimit_context_l);
    }
    bandlimit_context_l = band_limit_init ();

    memset (sample_ring_r, 0, sizeof (sample_ring_r));
    memset (phase_ring_r, 0, sizeof (phase_ring_r));
    previous_sample_r = 0.0;
    if (bandlimit_context_r != NULL)
    {
        free (bandlimit_context_r);
    }
    bandlimit_context_r = band_limit_init ();

    completed_cycles = 0;
    write_index = 0;
    read_index = 0;
    clock_rate = 0;
}


/*
 * Run the PSG for a number of CPU clock cycles
 */
void _psg_run_cycles (uint64_t cycles)
{
    /* Divide the system clock by 16, store the excess cycles for next time */
    static uint32_t excess = 0;
    cycles += excess;
    uint32_t psg_cycles = cycles >> 4;
    excess = cycles - (psg_cycles << 4);

    /* Reset the ring buffer if the clock rate changes */
    if (state.console_context != NULL &&
        clock_rate != (state.get_clock_rate (state.console_context) >> 4))
    {
        clock_rate = state.get_clock_rate (state.console_context) >> 4;

        read_index = 0;
        write_index = 0;
        completed_cycles = 0;
    }

    /* keep track of how much buffer is free */
    uint32_t used_buffer = write_index - read_index;

    /* Try to avoid having more than two sound-card buffers worth of sound.
     * The ring buffer can fit ~85 ms of sound.
     * The sound card is configured to ask for sound in ~21 ms blocks. */
    if ((psg_cycles * SAMPLE_RATE / clock_rate) + used_buffer > PSG_RING_SIZE * 0.6)
    {
        if (psg_cycles > 1)
        {
            psg_cycles -= 1;
        }
    }

    /* Limit the number of cycles we run to what will fit in the ring */
    if ((psg_cycles * SAMPLE_RATE / clock_rate) + used_buffer > PSG_RING_SIZE)
    {
        psg_cycles = (PSG_RING_SIZE - used_buffer) * clock_rate / SAMPLE_RATE;
    }

    /* The read_index points to the next sample that will be passed to the sound card.
     * Make sure that by the time we return, there is valid data at the read_index. */
    if (write_index + (psg_cycles * SAMPLE_RATE / clock_rate) <= read_index)
    {
        /* An extra cycle is added to account for integer division losses */
        psg_cycles = (read_index - write_index + 1) * clock_rate / SAMPLE_RATE + 1;
    }

    while (psg_cycles--)
    {
        /* Decrement counters */
        if (sn76489_state.counter_0) { sn76489_state.counter_0--; }
        if (sn76489_state.counter_1) { sn76489_state.counter_1--; }
        if (sn76489_state.counter_2) { sn76489_state.counter_2--; }
        if (sn76489_state.counter_3) { sn76489_state.counter_3--; }

        /* Toggle outputs */
        if (sn76489_state.counter_0 == 0)
        {
            sn76489_state.counter_0 = sn76489_state.tone_0;
            sn76489_state.output_0 *= -1;
        }
        if (sn76489_state.counter_1 == 0)
        {
            sn76489_state.counter_1 = sn76489_state.tone_1;
            sn76489_state.output_1 *= -1;
        }
        if (sn76489_state.counter_2 == 0)
        {
            sn76489_state.counter_2 = sn76489_state.tone_2;
            sn76489_state.output_2 *= -1;
        }

        /* Tone channels output +1 if their tone register is zero */
        if (sn76489_state.tone_0 <= 1) { sn76489_state.output_0 = 1; }
        if (sn76489_state.tone_1 <= 1) { sn76489_state.output_1 = 1; }
        if (sn76489_state.tone_2 <= 1) { sn76489_state.output_2 = 1; }

        if (sn76489_state.counter_3 == 0)
        {
            switch (sn76489_state.noise & 0x03)
            {
                case 0x00:  sn76489_state.counter_3 = 0x10;              break;
                case 0x01:  sn76489_state.counter_3 = 0x20;              break;
                case 0x02:  sn76489_state.counter_3 = 0x40;              break;
                case 0x03:  sn76489_state.counter_3 = sn76489_state.tone_2;   break;
                default:    break;
            }
            sn76489_state.output_3 *= -1;

            /* On transition from -1 to 1, shift the LFSR */
            if (sn76489_state.output_3 == 1)
            {
                sn76489_state.output_lfsr = (sn76489_state.lfsr & 0x0001);

                if (sn76489_state.noise & (1 << 2))
                {
                    /* White noise - Tap bits 0 and 3 */
                    sn76489_state.lfsr = (sn76489_state.lfsr >> 1) |
                                    (((sn76489_state.lfsr & (1 << 0)) ? 0x8000 : 0) ^ ((sn76489_state.lfsr & (1 << 3)) ? 0x8000 : 0));

                }
                else
                {
                    /* Periodic noise  - Tap bit 0 */
                    sn76489_state.lfsr = (sn76489_state.lfsr >> 1) |
                                    ((sn76489_state.lfsr & (1 << 0)) ? 0x8000 : 0);
                }
            }
        }

        /* Store this sample in the ring */
        if (state.console == CONSOLE_GAME_GEAR)
        {
            sample_ring_l [write_index % PSG_RING_SIZE] =
                (sn76489_state.gg_stereo & GG_CH0_LEFT ? sn76489_state.output_0    * (0x0f - sn76489_state.vol_0) * BASE_VOLUME : 0)
              + (sn76489_state.gg_stereo & GG_CH1_LEFT ? sn76489_state.output_1    * (0x0f - sn76489_state.vol_1) * BASE_VOLUME : 0)
              + (sn76489_state.gg_stereo & GG_CH2_LEFT ? sn76489_state.output_2    * (0x0f - sn76489_state.vol_2) * BASE_VOLUME : 0)
              + (sn76489_state.gg_stereo & GG_CH3_LEFT ? sn76489_state.output_lfsr * (0x0f - sn76489_state.vol_3) * BASE_VOLUME : 0);

            if (sample_ring_l [write_index % PSG_RING_SIZE] != previous_sample_l)
            {
                /* Phase ranges from 0 (no delay) to 31 (0.97 samples delay) */
                phase_ring_l [write_index % PSG_RING_SIZE] = (completed_cycles * SAMPLE_RATE * 32 / clock_rate) % 32;
                previous_sample_l = sample_ring_l [write_index % PSG_RING_SIZE];
            }

            sample_ring_r [write_index % PSG_RING_SIZE] =
                (sn76489_state.gg_stereo & GG_CH0_RIGHT ? sn76489_state.output_0    * (0x0f - sn76489_state.vol_0) * BASE_VOLUME : 0)
              + (sn76489_state.gg_stereo & GG_CH1_RIGHT ? sn76489_state.output_1    * (0x0f - sn76489_state.vol_1) * BASE_VOLUME : 0)
              + (sn76489_state.gg_stereo & GG_CH2_RIGHT ? sn76489_state.output_2    * (0x0f - sn76489_state.vol_2) * BASE_VOLUME : 0)
              + (sn76489_state.gg_stereo & GG_CH3_RIGHT ? sn76489_state.output_lfsr * (0x0f - sn76489_state.vol_3) * BASE_VOLUME : 0);

            if (sample_ring_r [write_index % PSG_RING_SIZE] != previous_sample_r)
            {
                /* Phase ranges from 0 (no delay) to 31 (0.97 samples delay) */
                phase_ring_r [write_index % PSG_RING_SIZE] = (completed_cycles * SAMPLE_RATE * 32 / clock_rate) % 32;
                previous_sample_r = sample_ring_r [write_index % PSG_RING_SIZE];
            }

            /* If this is the final value for this sample, pass it to the band limiter */
            if ((completed_cycles + 1) * SAMPLE_RATE / clock_rate > write_index)
            {
                band_limit_samples (bandlimit_context_l, &sample_ring_l [write_index % PSG_RING_SIZE], &phase_ring_r [write_index % PSG_RING_SIZE], 1);
                band_limit_samples (bandlimit_context_r, &sample_ring_r [write_index % PSG_RING_SIZE], &phase_ring_r [write_index % PSG_RING_SIZE], 1);
            }
        }
        else
        {
            sample_ring_l [write_index % PSG_RING_SIZE] = sn76489_state.output_0    * (0x0f - sn76489_state.vol_0) * BASE_VOLUME
                                                        + sn76489_state.output_1    * (0x0f - sn76489_state.vol_1) * BASE_VOLUME
                                                        + sn76489_state.output_2    * (0x0f - sn76489_state.vol_2) * BASE_VOLUME
                                                        + sn76489_state.output_lfsr * (0x0f - sn76489_state.vol_3) * BASE_VOLUME;

            if (sample_ring_l [write_index % PSG_RING_SIZE] != previous_sample_l)
            {
                /* Phase ranges from 0 (no delay) to 31 (0.97 samples delay) */
                phase_ring_l [write_index % PSG_RING_SIZE] = (completed_cycles * SAMPLE_RATE * 32 / clock_rate) % 32;
                previous_sample_l = sample_ring_l [write_index % PSG_RING_SIZE];
            }

            /* If this is the final value for this sample, pass it to the band limiter */
            if ((completed_cycles + 1) * SAMPLE_RATE / clock_rate > write_index)
            {
                band_limit_samples (bandlimit_context_l, &sample_ring_l [write_index % PSG_RING_SIZE], &phase_ring_l [write_index % PSG_RING_SIZE], 1);
            }
        }

        /* Map from the amount of time emulated (completed cycles / clock rate) to the sound card sample rate */
        completed_cycles++;
        write_index = completed_cycles * SAMPLE_RATE / clock_rate;
    }

    /* Update statistics (rolling average) */
    state.audio_ring_utilisation *= 0.9995;
    state.audio_ring_utilisation += 0.0005 * ((write_index - read_index) / (double) PSG_RING_SIZE);
}


/*
 * Run the PSG for a number of CPU clock cycles (mutex-wrapper)
 *
 * Allows two threads to request sound to be generated:
 *  1. The emulation loop, this is the usual case.
 *  2. SDL may request additional samples to keep the sound card from running out.
 */
void psg_run_cycles (uint64_t cycles)
{
    pthread_mutex_lock (&psg_mutex);
    _psg_run_cycles (cycles);
    pthread_mutex_unlock (&psg_mutex);
}


/*
 * Retrieves a block of samples from the sample-ring.
 * Assumes that the number of samples requested fits evenly into the ring buffer.
 */
void sn76489_get_samples (int16_t *stream, int count)
{
    int sample_count = count >> 1;

    if (read_index + sample_count > write_index)
    {
        int shortfall = sample_count - (write_index - read_index);

        /* Note: We add one to the shortfall to account for integer division */
        psg_run_cycles ((shortfall + 1) * (clock_rate << 4) / SAMPLE_RATE);
    }

    /* Take samples and pass them to the sound card */
    uint32_t read_start = read_index % PSG_RING_SIZE;

    for (int i = 0; i < sample_count; i++)
    {
        /* Left, Right */
        if (state.console == CONSOLE_GAME_GEAR)
        {
            stream [2 * i    ] = sample_ring_l [read_start + i];
            stream [2 * i + 1] = sample_ring_r [read_start + i];
        }
        else
        {
            stream [2 * i    ] = sample_ring_l [read_start + i];
            stream [2 * i + 1] = sample_ring_l [read_start + i];
        }
    }

    read_index += sample_count;
}


/*
 * Export sn76489 state.
 */
void sn76489_state_save (void)
{
    SN76489_State sn76489_state_be = {
        .vol_0 =       htons (sn76489_state.vol_0),
        .vol_1 =       htons (sn76489_state.vol_1),
        .vol_2 =       htons (sn76489_state.vol_2),
        .vol_3 =       htons (sn76489_state.vol_3),
        .tone_0 =      htons (sn76489_state.tone_0),
        .tone_1 =      htons (sn76489_state.tone_1),
        .tone_2 =      htons (sn76489_state.tone_2),
        .noise =       htons (sn76489_state.noise),
        .counter_0 =   htons (sn76489_state.counter_0),
        .counter_1 =   htons (sn76489_state.counter_1),
        .counter_2 =   htons (sn76489_state.counter_2),
        .counter_3 =   htons (sn76489_state.counter_3),
        .output_0 =    htons (sn76489_state.output_0),
        .output_1 =    htons (sn76489_state.output_1),
        .output_2 =    htons (sn76489_state.output_2),
        .output_3 =    htons (sn76489_state.output_3),
        .latch =       htons (sn76489_state.latch),
        .lfsr =        htons (sn76489_state.lfsr),
        .output_lfsr = htons (sn76489_state.output_lfsr),
        .gg_stereo =   htons (sn76489_state.gg_stereo)
    };

    save_state_section_add (SECTION_ID_PSG, 1, sizeof (sn76489_state_be), &sn76489_state_be);
}


/*
 * Import sn76489 state.
 */
void sn76489_state_load (uint32_t version, uint32_t size, void *data)
{
    SN76489_State sn76489_state_be;

    if (size == sizeof (sn76489_state_be))
    {
        memcpy (&sn76489_state_be, data, sizeof (sn76489_state_be));

        sn76489_state.vol_0 =       ntohs (sn76489_state_be.vol_0);
        sn76489_state.vol_1 =       ntohs (sn76489_state_be.vol_1);
        sn76489_state.vol_2 =       ntohs (sn76489_state_be.vol_2);
        sn76489_state.vol_3 =       ntohs (sn76489_state_be.vol_3);
        sn76489_state.tone_0 =      ntohs (sn76489_state_be.tone_0);
        sn76489_state.tone_1 =      ntohs (sn76489_state_be.tone_1);
        sn76489_state.tone_2 =      ntohs (sn76489_state_be.tone_2);
        sn76489_state.noise =       ntohs (sn76489_state_be.noise);
        sn76489_state.counter_0 =   ntohs (sn76489_state_be.counter_0);
        sn76489_state.counter_1 =   ntohs (sn76489_state_be.counter_1);
        sn76489_state.counter_2 =   ntohs (sn76489_state_be.counter_2);
        sn76489_state.counter_3 =   ntohs (sn76489_state_be.counter_3);
        sn76489_state.output_0 =    ntohs (sn76489_state_be.output_0);
        sn76489_state.output_1 =    ntohs (sn76489_state_be.output_1);
        sn76489_state.output_2 =    ntohs (sn76489_state_be.output_2);
        sn76489_state.output_3 =    ntohs (sn76489_state_be.output_3);
        sn76489_state.latch =       ntohs (sn76489_state_be.latch);
        sn76489_state.lfsr =        ntohs (sn76489_state_be.lfsr);
        sn76489_state.output_lfsr = ntohs (sn76489_state_be.output_lfsr);
        sn76489_state.gg_stereo =   ntohs (sn76489_state_be.gg_stereo);

    }
    else
    {
        snepulator_error ("Error", "Save-state contains incorrect Z80 size");
    }
}
