#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <arpa/inet.h>

#include "../util.h"
#include "../snepulator.h"
#include "../save_state.h"
#include "sn76489.h"
extern Snepulator_State state;

/* State */
SN76489_State sn76489_state;
pthread_mutex_t psg_mutex = PTHREAD_MUTEX_INITIALIZER;

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
 * Stores samples at the PSG internal clock rate (~334 kHz).
 * As the read/write index are 64-bit, they should never overflow.
 */
static int16_t sample_ring_left [PSG_RING_SIZE];
static int16_t sample_ring_right [PSG_RING_SIZE];
static uint64_t read_index = 0;
static uint64_t write_index = 0;


/*
 * Handle data writes sent to the PSG.
 */
void sn76489_data_write (uint8_t data)
{
    uint16_t data_low = data & 0x0f;
    uint16_t data_high = data << 4;


    if (data & 0x80) /* LATCH + LOW DATA */
    {
        sn76489_state.latch = data & 0x70;

        switch (sn76489_state.latch)
        {
            /* Channel 0 (tone) */
            case 0x00:
                sn76489_state.tone_0 = (sn76489_state.tone_0 & 0x03f0) | data_low;
                break;
            case 0x10:
                sn76489_state.vol_0 = data_low;
                break;

            /* Channel 1 (tone) */
            case 0x20:
                sn76489_state.tone_1 = (sn76489_state.tone_1 & 0x03f0) | data_low;
                break;
            case 0x30:
                sn76489_state.vol_1 = data_low;
                break;

            /* Channel 2 (tone) */
            case 0x40:
                sn76489_state.tone_2 = (sn76489_state.tone_2 & 0x03f0) | data_low;
                break;
            case 0x50:
                sn76489_state.vol_2 = data_low;
                break;

            /* Channel 3 (noise) */
            case 0x60:
                sn76489_state.noise = data_low;
                sn76489_state.lfsr  = 0x8000;
                break;
            case 0x70:
                sn76489_state.vol_3 = data_low;
            default:
                break;
        }
    }
    else /* HIGH DATA */
    {
        switch (sn76489_state.latch)
        {
            /* Channel 0 (tone) */
            case 0x00:
                sn76489_state.tone_0 = (sn76489_state.tone_0 & 0x000f) | data_high;
                break;
            case 0x10:
                sn76489_state.vol_0 = data_low;
                break;

            /* Channel 1 (tone) */
            case 0x20:
                sn76489_state.tone_1 = (sn76489_state.tone_1 & 0x000f) | data_high;
                break;
            case 0x30:
                sn76489_state.vol_1 = data_low;
                break;

            /* Channel 2 (tone) */
            case 0x40:
                sn76489_state.tone_2 = (sn76489_state.tone_2 & 0x000f) | data_high;
                break;
            case 0x50:
                sn76489_state.vol_2 = data_low;
                break;

            /* Channel 3 (noise) */
            case 0x60:
                sn76489_state.noise = data_low;
                sn76489_state.lfsr  = 0x8000;
                break;
            case 0x70:
                sn76489_state.vol_3 = data_low;
            default:
                break;
        }
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
}


/*
 * Run the PSG for a number of CPU clock cycles
 */
void _psg_run_cycles (uint64_t cycles)
{

    static uint32_t excess = 0;
    cycles += excess;

    /* Divide the system clock by 16, store the excess cycles for next time */
    uint32_t psg_cycles = cycles >> 4;
    excess = cycles - (psg_cycles << 4);

    /* Try to avoid having more than two sound-card buffers worth of sound.
     * The ring buffer can fit ~74 ms of sound.
     * The sound card is configured to ask for sound in ~21 ms blocks. */
    if (psg_cycles + (write_index - read_index) > PSG_RING_SIZE * 0.6)
    {
        if (psg_cycles > 1)
        {
            psg_cycles -= 1;
        }
    }

    /* Limit the number of cycles we run to what will fit in the ring */
    if (psg_cycles + (write_index - read_index) > PSG_RING_SIZE)
    {
        psg_cycles = PSG_RING_SIZE - (write_index - read_index);
    }

    /* The read_index points to the next sample that will be passed to the sound card.
     * Make sure that by the time we return, there is valid data at the read_index. */
    if (write_index + psg_cycles <= read_index)
    {
        psg_cycles = read_index - write_index + 1;
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
            sample_ring_left  [write_index % PSG_RING_SIZE] = (sn76489_state.gg_stereo & GG_CH0_LEFT
                                                               ? sn76489_state.output_0    * (0x0f - sn76489_state.vol_0) * BASE_VOLUME : 0)
                                                            + (sn76489_state.gg_stereo & GG_CH1_LEFT
                                                               ? sn76489_state.output_1    * (0x0f - sn76489_state.vol_1) * BASE_VOLUME : 0)
                                                            + (sn76489_state.gg_stereo & GG_CH2_LEFT
                                                               ? sn76489_state.output_2    * (0x0f - sn76489_state.vol_2) * BASE_VOLUME : 0)
                                                            + (sn76489_state.gg_stereo & GG_CH3_LEFT
                                                               ? sn76489_state.output_lfsr * (0x0f - sn76489_state.vol_3) * BASE_VOLUME : 0);

            sample_ring_right [write_index % PSG_RING_SIZE] = (sn76489_state.gg_stereo & GG_CH0_RIGHT
                                                               ? sn76489_state.output_0    * (0x0f - sn76489_state.vol_0) * BASE_VOLUME : 0)
                                                            + (sn76489_state.gg_stereo & GG_CH1_RIGHT
                                                               ? sn76489_state.output_1    * (0x0f - sn76489_state.vol_1) * BASE_VOLUME : 0)
                                                            + (sn76489_state.gg_stereo & GG_CH2_RIGHT
                                                               ? sn76489_state.output_2    * (0x0f - sn76489_state.vol_2) * BASE_VOLUME : 0)
                                                            + (sn76489_state.gg_stereo & GG_CH3_RIGHT
                                                               ? sn76489_state.output_lfsr * (0x0f - sn76489_state.vol_3) * BASE_VOLUME : 0);
        }
        else
        {
            sample_ring_left [write_index % PSG_RING_SIZE] = sn76489_state.output_0    * (0x0f - sn76489_state.vol_0) * BASE_VOLUME
                                                           + sn76489_state.output_1    * (0x0f - sn76489_state.vol_1) * BASE_VOLUME
                                                           + sn76489_state.output_2    * (0x0f - sn76489_state.vol_2) * BASE_VOLUME
                                                           + sn76489_state.output_lfsr * (0x0f - sn76489_state.vol_3) * BASE_VOLUME;
        }
        write_index++;

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
 * Assumes a sample-rate of 48 KHz.
 *
 * TODO: Proper sample-rate conversion.
 */
void sn76489_get_samples (int16_t *stream, int count)
{
    static uint64_t soundcard_sample_count = 0;
    static uint32_t clock_rate = 0;

    /* Reset the ring buffer if the clock rate changes */
    if (clock_rate != state.get_clock_rate ())
    {
        clock_rate = state.get_clock_rate ();
        soundcard_sample_count = 0;
        read_index = 0;
        write_index = 0;
    }

    /* Take samples from the PSG ring to pass to the sound card */
    for (int i = 0; i < count; i+= 2)
    {
        read_index = (soundcard_sample_count * (clock_rate >> 4)) / 48000;

        if (read_index >= write_index)
        {
            /* Generate samples until we can meet the read_index */
            psg_run_cycles (0);
        }

        /* Left, Right */
        if (state.console == CONSOLE_GAME_GEAR)
        {
            stream [i    ] = sample_ring_left  [read_index % PSG_RING_SIZE];
            stream [i + 1] = sample_ring_right [read_index % PSG_RING_SIZE];
        }
        else
        {
            stream [i    ] = sample_ring_left [read_index % PSG_RING_SIZE];
            stream [i + 1] = sample_ring_left [read_index % PSG_RING_SIZE];
        }

        soundcard_sample_count++;
    }
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
