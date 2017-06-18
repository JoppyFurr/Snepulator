#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "sn79489.h"

/* State */
SN79489_Regs psg_regs;


void sn79489_data_write (uint8_t data)
{
    static uint8_t latch = 0x00;

    if (data & 0x80) /* LATCH + DATA */
    {
        latch = data & 0x70;

        switch (latch)
        {
            /* Tone 0 */
            case 0x00:
                psg_regs.tone_0 = (psg_regs.tone_0 & 0x03f0) | (data & 0x0f);
                break;
            case 0x10:
                psg_regs.vol_0 = data & 0x0f;
                break;

            /* Tone 1 */
            case 0x20:
                psg_regs.tone_1 = (psg_regs.tone_1 & 0x03f0) | (data & 0x0f);
                break;
            case 0x30:
                psg_regs.vol_1 = data & 0x0f;
                break;

            /* Tone 2 */
            case 0x40:
                psg_regs.tone_2 = (psg_regs.tone_2 & 0x03f0) | (data & 0x0f);
                break;
            case 0x50:
                psg_regs.vol_2 = data & 0x0f;
                break;

            /* Noise */
            case 0x60:
                psg_regs.noise = data & 0x0f;
                break;
            case 0x70:
                psg_regs.vol_3 = data & 0x0f;
            default:
                break;
        }
    }
    else /* DATA */
    {
        switch (latch)
        {
            /* Tone 0 */
            case 0x00:
                psg_regs.tone_0 = (psg_regs.tone_0 & 0x000f) | (((uint16_t) data & 0x3f) << 4);
                break;
            case 0x10:
                psg_regs.vol_0 = data & 0x0f;
                break;

            /* Tone 1 */
            case 0x20:
                psg_regs.tone_1 = (psg_regs.tone_1 & 0x000f) | (((uint16_t) data & 0x3f) << 4);
                break;
            case 0x30:
                psg_regs.vol_1 = data & 0x0f;
                break;

            /* Tone 2 */
            case 0x40:
                psg_regs.tone_2 = (psg_regs.tone_2 & 0x000f) | (((uint16_t) data & 0x3f) << 4);
                break;
            case 0x50:
                psg_regs.vol_2 = data & 0x0f;
                break;

            /* Noise */
            case 0x60:
                psg_regs.noise = data & 0x0f;
                break;
            case 0x70:
                psg_regs.vol_3 = data & 0x0f;
            default:
                break;
        }
    }
}

void sn79489_init (void)
{
    memset (&psg_regs, 0, sizeof (psg_regs));
    psg_regs.vol_0 = 0x0f;
    psg_regs.vol_1 = 0x0f;
    psg_regs.vol_2 = 0x0f;
    psg_regs.vol_3 = 0x0f;

    /* Net zero */
    psg_regs.output_0 =  1;
    psg_regs.output_1 = -1;
    psg_regs.output_2 =  1;
    psg_regs.output_3 = -1;

}

#define PSG_CLOCK_RATE_PAL  (3546895 >> 4)
#define PSG_RING_SIZE 16386
#define BASE_VOLUME 50

/* TODO: Fix assumption of 48000 samples per sec */
/* TODO: Fix assumption of PAL */
/* TODO: Implement the noise channel */
/* TODO: Generate sound as the CPU ticks, rather than only at callback-time */
void sn79489_get_samples (int16_t *stream, int count)
{
    static uint64_t psg_sample_count = 0;
    static uint64_t soundcard_sample_count = 0;

    static int16_t psg_sample_ring[PSG_RING_SIZE];

    printf ("DEBUG: vol= (%x  %x  %x)\n", psg_regs.vol_0, psg_regs.vol_1, psg_regs.vol_2);

    /* Generate samples in PSG time */
    while (psg_sample_count < (((soundcard_sample_count + count) * PSG_CLOCK_RATE_PAL) / 48000))
    {
        psg_regs.counter_0--;
        psg_regs.counter_1--;
        psg_regs.counter_2--;

        if (psg_regs.counter_0 < 0)
        {
            psg_regs.counter_0 = psg_regs.tone_0;
            psg_regs.output_0 *= -1;
        }

        if (psg_regs.counter_1 < 0)
        {
            psg_regs.counter_1 = psg_regs.tone_1;
            psg_regs.output_1 *= -1;
        }

        if (psg_regs.counter_2 < 0)
        {
            psg_regs.counter_2 = psg_regs.tone_2;
            psg_regs.output_2 *= -1;
        }

        psg_sample_ring[psg_sample_count % PSG_RING_SIZE] = 0;
        psg_sample_ring[psg_sample_count % PSG_RING_SIZE] += psg_regs.output_0 * (0x0f - psg_regs.vol_0) * BASE_VOLUME;
        psg_sample_ring[psg_sample_count % PSG_RING_SIZE] += psg_regs.output_1 * (0x0f - psg_regs.vol_1) * BASE_VOLUME;
        psg_sample_ring[psg_sample_count % PSG_RING_SIZE] += psg_regs.output_2 * (0x0f - psg_regs.vol_2) * BASE_VOLUME;

        psg_sample_count++;
    }

    /* Take samples from the PSG ring to pass to the sound card */
    for (int i = 0; i < count; i++)
    {
        stream[i] = 0;

        stream[i] += psg_sample_ring [ ((soundcard_sample_count * PSG_CLOCK_RATE_PAL) / 48000) % PSG_RING_SIZE ];

        soundcard_sample_count++;
    }
}
