#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include "../util.h"
#include "../snepulator.h"
#include "sn76489.h"
extern Snepulator_State state;

/* State */
SN76489_Regs psg_regs;
pthread_mutex_t psg_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * PSG output ring buffer.
 * Stores samples at the PSG internal clock rate (~334 kHz).
 * As the read/write index are 64-bit, they should never overflow.
 */
static int16_t psg_sample_ring [PSG_RING_SIZE];
static uint64_t read_index = 0;
static uint64_t write_index = 0;


/*
 * Handle data writes sent to the PSG.
 */
void sn76489_data_write (uint8_t data)
{
    static uint8_t latch = 0x00;
    uint16_t data_low = data & 0x0f;
    uint16_t data_high = data << 4;


    if (data & 0x80) /* LATCH + LOW DATA */
    {
        latch = data & 0x70;

        switch (latch)
        {
            /* Channel 0 (tone) */
            case 0x00:
                psg_regs.tone_0 = (psg_regs.tone_0 & 0x03f0) | data_low;
                break;
            case 0x10:
                psg_regs.vol_0 = data_low;
                break;

            /* Channel 1 (tone) */
            case 0x20:
                psg_regs.tone_1 = (psg_regs.tone_1 & 0x03f0) | data_low;
                break;
            case 0x30:
                psg_regs.vol_1 = data_low;
                break;

            /* Channel 2 (tone) */
            case 0x40:
                psg_regs.tone_2 = (psg_regs.tone_2 & 0x03f0) | data_low;
                break;
            case 0x50:
                psg_regs.vol_2 = data_low;
                break;

            /* Channel 3 (noise) */
            case 0x60:
                psg_regs.noise = data_low;
                psg_regs.lfsr  = 0x0001;
                break;
            case 0x70:
                psg_regs.vol_3 = data_low;
            default:
                break;
        }
    }
    else /* HIGH DATA */
    {
        switch (latch)
        {
            /* Channel 0 (tone) */
            case 0x00:
                psg_regs.tone_0 = (psg_regs.tone_0 & 0x000f) | data_high;
                break;
            case 0x10:
                psg_regs.vol_0 = data_low;
                break;

            /* Channel 1 (tone) */
            case 0x20:
                psg_regs.tone_1 = (psg_regs.tone_1 & 0x000f) | data_high;
                break;
            case 0x30:
                psg_regs.vol_1 = data_low;
                break;

            /* Channel 2 (tone) */
            case 0x40:
                psg_regs.tone_2 = (psg_regs.tone_2 & 0x000f) | data_high;
                break;
            case 0x50:
                psg_regs.vol_2 = data_low;
                break;

            /* Channel 3 (noise) */
            case 0x60:
                psg_regs.noise = data_low;
                psg_regs.lfsr  = 0x0001;
                break;
            case 0x70:
                psg_regs.vol_3 = data_low;
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
    memset (&psg_regs, 0, sizeof (psg_regs));
    psg_regs.vol_0 = 0x0f;
    psg_regs.vol_1 = 0x0f;
    psg_regs.vol_2 = 0x0f;
    psg_regs.vol_3 = 0x0f;

    psg_regs.output_0 =  1;
    psg_regs.output_1 = -1;
    psg_regs.output_2 =  1;
    psg_regs.output_3 = -1;

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
        if (psg_regs.counter_0) { psg_regs.counter_0--; }
        if (psg_regs.counter_1) { psg_regs.counter_1--; }
        if (psg_regs.counter_2) { psg_regs.counter_2--; }
        if (psg_regs.counter_3) { psg_regs.counter_3--; }

        /* Toggle outputs */
        if (psg_regs.counter_0 == 0)
        {
            psg_regs.counter_0 = psg_regs.tone_0;
            psg_regs.output_0 *= -1;
        }
        if (psg_regs.counter_1 == 0)
        {
            psg_regs.counter_1 = psg_regs.tone_1;
            psg_regs.output_1 *= -1;
        }
        if (psg_regs.counter_2 == 0)
        {
            psg_regs.counter_2 = psg_regs.tone_2;
            psg_regs.output_2 *= -1;
        }

        /* Tone channels output +1 if their tone register is zero */
        if (psg_regs.tone_0 == 0) { psg_regs.output_0 = 1; }
        if (psg_regs.tone_1 == 0) { psg_regs.output_1 = 1; }
        if (psg_regs.tone_2 == 0) { psg_regs.output_2 = 1; }

        if (psg_regs.counter_3 == 0)
        {
            switch (psg_regs.noise & 0x03)
            {
                case 0x00:  psg_regs.counter_3 = 0x10;              break;
                case 0x01:  psg_regs.counter_3 = 0x20;              break;
                case 0x02:  psg_regs.counter_3 = 0x40;              break;
                case 0x03:  psg_regs.counter_3 = psg_regs.tone_2;   break;
                default:    break;
            }
            psg_regs.output_3 *= -1;

            /* On transition from -1 to 1, shift the LFSR */
            if (psg_regs.output_3 == 1)
            {
                psg_regs.output_lfsr = (psg_regs.lfsr & 0x0001);

                if (psg_regs.noise & (1 << 2))
                {
                    /* White noise - Tap bits 0 and 3 */
                    psg_regs.lfsr = (psg_regs.lfsr >> 1) |
                                    (((psg_regs.lfsr & (1 << 0)) ? 0x8000 : 0) ^ ((psg_regs.lfsr & (1 << 3)) ? 0x8000 : 0));

                }
                else
                {
                    /* Periodic noise  - Tap bit 0 */
                    psg_regs.lfsr = (psg_regs.lfsr >> 1) |
                                    ((psg_regs.lfsr & (1 << 0)) ? 0x8000 : 0);
                }
            }
        }

        /* Store this sample in the ring */
        psg_sample_ring [write_index++ % PSG_RING_SIZE] = psg_regs.output_0    * (0x0f - psg_regs.vol_0) * BASE_VOLUME
                                                        + psg_regs.output_1    * (0x0f - psg_regs.vol_1) * BASE_VOLUME
                                                        + psg_regs.output_2    * (0x0f - psg_regs.vol_2) * BASE_VOLUME
                                                        + psg_regs.output_lfsr * (0x0f - psg_regs.vol_3) * BASE_VOLUME;

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
    for (int i = 0; i < count; i++)
    {
        read_index = (soundcard_sample_count * (clock_rate >> 4)) / 48000;

        if (read_index >= write_index)
        {
            /* Generate samples until we can meet the read_index */
            psg_run_cycles (0);
        }

        stream [i] = psg_sample_ring [read_index % PSG_RING_SIZE];

        soundcard_sample_count++;
    }
}
