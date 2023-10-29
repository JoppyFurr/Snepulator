/*
 * YM2413 implementation.
 *
 * Based on reverse engineering documents by Andete.
 */

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

extern Snepulator_State state;

#include "ym2413.h"

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

#define SAMPLE_RATE 48000
#define YM2413_RING_SIZE 4096
#define BASE_VOLUME 100

/* State */
pthread_mutex_t ym2413_mutex = PTHREAD_MUTEX_INITIALIZER;

/* TODO: Avoid globals */
static uint64_t write_index = 0;
static uint64_t read_index = 0;
static uint64_t completed_samples = 0; /* ym2413 samples, not sound card samples */
static uint32_t clock_rate;

static int16_t sample_ring [YM2413_RING_SIZE];

/* Use a special type definition to mark sign-magnitude numbers.
 * The most significant bit is used to indicate if the number is negative. */
#define SIGN_BIT 0x8000
#define MAG_BITS 0x7fff
typedef uint16_t signmag16_t;

/* TODO:
 * - Phase generator
 * - global counter
 * - exp lookup
 * - sin lookup
 * - Tremolo
 * - Vibrato
 * - Instrument ROM
 * - Key on / off
 * - Envelope generator (Damp, Attack, Decay, Sustain, Release)
 * - LFSR
 * - Rhythm
 * - Waveforms (full-sine / rectified sin)
 * - Key Scale Rate
 * - Key Scale Level
 * - Feedback
 * - Volume
 * - Investigate behaviour of +0 and -0 in the DAC.
 *   Should they be the same value?
 *   If different, is their delta the same as other number pairs?
 *
 * - SMS Interface:
 *   I/O Port 0xf2 - Read bit 0 to detect if YM2413 is present (according to McDonald document)
 *                 - Write bits 1:0 to configure muting:
 *                   0 - Only SN76489 enabled
 *                   1 - Only YM2413 enabled
 *                   2 - Both disabled
 *                   3 - Both enabled
 *                   Note that, muting the SN76489 only works on Japanese consoles.
 *                   Reading: 7:4 - counter bits 11, 7, and 3 (ticked by C-Sync)
 *                            3:2 - always 0
 *                            1:0 - Last written values, 0 by default
 *                  If no YM2413 is present, reading from the audio control port returns varying results.
 *                   * It always returns %10 in the lowermost two bits on a non-japanese SMS.
 *                   * On a Mark III without FM unit, it returns the input from port 0.
 *                   * Writing to the audio control port has no effect if no YM2413 is present.
 */

static uint32_t exp_table [256] = { };
static uint32_t log_sin_table [256] = { };

static uint8_t instrument_rom [15][8] = {
    { 0x71, 0x61, 0x1E, 0x17, 0xD0, 0x78, 0x00, 0x17 },
    { 0x13, 0x41, 0x1A, 0x0D, 0xD8, 0xF7, 0x23, 0x13 },
    { 0x13, 0x01, 0x99, 0x00, 0xF2, 0xC4, 0x11, 0x23 },
    { 0x31, 0x61, 0x0E, 0x07, 0xA8, 0x64, 0x70, 0x27 },
    { 0x32, 0x21, 0x1E, 0x06, 0xE0, 0x76, 0x00, 0x28 },
    { 0x31, 0x22, 0x16, 0x05, 0xE0, 0x71, 0x00, 0x18 },
    { 0x21, 0x61, 0x1D, 0x07, 0x82, 0x81, 0x10, 0x07 },
    { 0x23, 0x21, 0x2D, 0x14, 0xA2, 0x72, 0x00, 0x07 },
    { 0x61, 0x61, 0x1B, 0x06, 0x64, 0x65, 0x10, 0x17 },
    { 0x41, 0x61, 0x0B, 0x18, 0x85, 0xF7, 0x71, 0x07 },
    { 0x13, 0x01, 0x83, 0x11, 0xFA, 0xE4, 0x10, 0x04 },
    { 0x17, 0xC1, 0x24, 0x07, 0xF8, 0xF8, 0x22, 0x12 },
    { 0x61, 0x50, 0x0C, 0x05, 0xC2, 0xF5, 0x20, 0x42 },
    { 0x01, 0x01, 0x55, 0x03, 0xC9, 0x95, 0x03, 0x02 },
    { 0x61, 0x41, 0x89, 0x03, 0xF1, 0xE4, 0x40, 0x13 }
};


/*
 * Write data to the latched register address.
 */
void ym2413_data_write (YM2413_Context *context, uint8_t data)
{
    if (context->state.addr_latch <= 0x39)
    {
        ((uint8_t *) &context->state.r00_instrument_params) [context->state.addr_latch] = data;
    }
}


/*
 * Latch a register address.
 */
void ym2413_addr_write (YM2413_Context *context, uint8_t addr)
{
    /* Register mirroring */
    if ((addr <= 0x19 && addr >= 0x1f) ||
        (addr <= 0x29 && addr >= 0x2f) ||
        (addr <= 0x39 && addr >= 0x3f))
    {
        addr -= 0x09;
    }

    context->state.addr_latch = addr;
}


/*
 * Populate the exp () table.
 * Note that we keep the always-set bit-10.
 */
static void ym2413_populate_exp_table (void)
{
    for (int i = 0; i < 256; i++)
    {
        exp_table [i] = round (exp2 (i / 256.0) * 1024);
    }
}


/*
 * Lookup an entry using the exp table.
 * Input is fixed-point with 8 fractional bits.
 */
static signmag16_t ym2413_exp (signmag16_t val)
{
    /* Note that the index is inverted to account
     * for the log-sine table using -log2. */
    uint8_t fractional = ~(val & 0xff);
    uint16_t integral = (val & MAG_BITS) >> 8;

    int16_t result = exp_table [fractional] >> integral;

    /* Propagate the sign */
    result |= (val & SIGN_BIT);

    return result;
}


/*
 * Populate the log (sin ()) table.
 * Fixed-point with 8 fractional bits.
 */
static void ym2413_populate_log_sin_table (void)
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
static signmag16_t ym2413_sin (uint16_t phase)
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
 * Run the PSG for a number of CPU clock cycles
 */
void _ym2413_run_cycles (uint64_t cycles)
{
    /* The YM2413 takes 72 cycles to update all 18 operators */
    static uint32_t excess = 0;
    cycles += excess;
    uint32_t ym_samples = cycles / 72;
    excess = cycles - (ym_samples * 72);

    /* Reset the ring buffer if the clock rate changes */
    if (state.console_context != NULL &&
        clock_rate != state.get_clock_rate (state.console_context))
    {
        clock_rate = state.get_clock_rate (state.console_context);

        read_index = 0;
        write_index = 0;
        completed_samples = 0;
    }

    while (ym_samples--)
    {
        /* TODO: Simulate envelope generator */

        /* TODO: Simulate operators */

        /* TODO: Propagate new samples into ring buffer */
        sample_ring [write_index % YM2413_RING_SIZE] = 0;

        /* Map from the amount of time emulated (completed cycles / clock rate) to the sound card sample rate */
        completed_samples++;
        write_index = completed_samples * SAMPLE_RATE * 72 / clock_rate;
    }
}


/*
 * Run the PSG for a number of CPU clock cycles (mutex-wrapper)
 *
 * Allows two threads to request sound to be generated:
 *  1. The emulation loop, this is the usual case.
 *  2. Additional samples needed to keep the sound card from running out.
 */
void ym2413_run_cycles (uint64_t cycles)
{
    pthread_mutex_lock (&ym2413_mutex);
    _ym2413_run_cycles (cycles);
    pthread_mutex_unlock (&ym2413_mutex);
}


/*
 * Retrieves a block of samples from the sample-ring.
 * Assumes that the number of samples requested fits evenly into the ring buffer.
 */
void ym2413_get_samples (int16_t *stream, uint32_t count)
{
    if (read_index + count > write_index)
    {
        int shortfall = count - (write_index - read_index);

        /* Note: We add one to the shortfall to account for integer division */
        ym2413_run_cycles ((shortfall + 1) * clock_rate / SAMPLE_RATE);
    }

    /* Take samples and pass them to the sound card */
    uint32_t read_start = read_index % YM2413_RING_SIZE;

    for (int i = 0; i < count; i++)
    {
        /* Left, Right */
        stream [2 * i    ] = sample_ring [read_start + i];
        stream [2 * i + 1] = sample_ring [read_start + i];
    }

    read_index += count;
}


/*
 * Initialise a new YM2413 context.
 */
YM2413_Context *ym2413_init (void)
{
    static bool first = true;

    if (first)
    {
        /* Once-off initialisations */
        first = false;
        ym2413_populate_exp_table ();
        ym2413_populate_log_sin_table ();
    }

    YM2413_Context *context = calloc (1, sizeof (YM2413_Context));

    memset (sample_ring, 0, sizeof (sample_ring));

    completed_samples = 0;
    write_index = 0;
    read_index = 0;
    clock_rate = 0;

    return context;
}
