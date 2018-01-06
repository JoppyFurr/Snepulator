#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../sms.h"
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
                psg_regs.lfsr  = 0x0001;
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
                psg_regs.lfsr  = 0x0001;
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

    psg_regs.output_0 =  1;
    psg_regs.output_1 = -1;
    psg_regs.output_2 =  1;
    psg_regs.output_3 = -1;

}

static int16_t psg_sample_ring[PSG_RING_SIZE];
static uint64_t read_index = 0;
static uint64_t write_index = 0;

void psg_run_cycles (uint64_t cycles)
{
    static int excess = 0;
    cycles += excess;

    /* Divide the system clock by 16, store the excess cycles for next time */
    int psg_cycles = cycles >> 4;
    excess = cycles - (psg_cycles << 4);

    /* Make sure we don't overflow the ring, if psg_cycles would overflow it, just
     * use half of the remaining space instead. */
    if (write_index + cycles > read_index + PSG_RING_SIZE)
        psg_cycles = (PSG_RING_SIZE - (write_index - read_index)) / 2;

    while (psg_cycles--)
    {
        psg_regs.counter_0--;
        psg_regs.counter_1--;
        psg_regs.counter_2--;
        psg_regs.counter_3--;

        if (psg_regs.counter_0 <= 0)
        {
            psg_regs.counter_0 = psg_regs.tone_0;
            psg_regs.output_0 *= -1;
        }

        if (psg_regs.counter_1 <= 0)
        {
            psg_regs.counter_1 = psg_regs.tone_1;
            psg_regs.output_1 *= -1;
        }

        if (psg_regs.counter_2 <= 0)
        {
            psg_regs.counter_2 = psg_regs.tone_2;
            psg_regs.output_2 *= -1;
        }

        if (psg_regs.counter_3 <= 0)
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
        psg_sample_ring[write_index++ % PSG_RING_SIZE] = psg_regs.output_0    * (0x0f - psg_regs.vol_0) * BASE_VOLUME
                                                       + psg_regs.output_1    * (0x0f - psg_regs.vol_1) * BASE_VOLUME
                                                       + psg_regs.output_2    * (0x0f - psg_regs.vol_2) * BASE_VOLUME
                                                       + psg_regs.output_lfsr * (0x0f - psg_regs.vol_3) * BASE_VOLUME;

    }
}

/* TODO: Currently assuming 48 KHz for the sound card */
void sn79489_get_samples (int16_t *stream, int count)
{
    static uint64_t soundcard_sample_count = 0;

    /* Take samples from the PSG ring to pass to the sound card */
    for (int i = 0; i < count; i++)
    {
        read_index = (soundcard_sample_count * (sms_get_clock_rate() >> 4)) / 48000;

        if (read_index >= write_index)
        {
            /* Generate samples until we can meet the read_index */
            psg_run_cycles ((read_index - write_index + 1) << 4);
        }

        stream[i] = psg_sample_ring [read_index % PSG_RING_SIZE];

        soundcard_sample_count++;
    }
}
