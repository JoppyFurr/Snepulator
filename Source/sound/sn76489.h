/*
 * API for the SN76489 PSG chip.
 */

typedef struct SN76489_Regs_s {

    /* 4-bit volume registers */
    uint8_t vol_0;
    uint8_t vol_1;
    uint8_t vol_2;
    uint8_t vol_3;

    /* 10-bit tone registers */
    uint16_t tone_0;
    uint16_t tone_1;
    uint16_t tone_2;

    /* 4-bit noise register */
    uint8_t noise;

    /* Counters */
    uint16_t counter_0;
    uint16_t counter_1;
    uint16_t counter_2;
    uint16_t counter_3;
    uint16_t lfsr;

    /* Outputs */
    int16_t output_0;
    int16_t output_1;
    int16_t output_2;
    int16_t output_3;
    int16_t output_lfsr;

} SN76489_Regs;

#define BASE_VOLUME 100
#define PSG_RING_SIZE 32768

/* Reset PSG to initial power-on state. */
void sn76489_init (void);

/* Handle data writes sent to the PSG. */
void sn76489_data_write (uint8_t data);

/* Retrieves a block of samples from the sample-ring. */
void sn76489_get_samples (int16_t *stream, int len);

/* Run the PSG for a number of CPU clock cycles. */
void psg_run_cycles (uint64_t cycles);
