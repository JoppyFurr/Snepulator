/*
 * API for the SN76489 PSG chip.
 */

typedef struct SN76489_State_s {

    union {
        struct {
            /* Four 4-bit volume registers */
            uint16_t vol_0;
            uint16_t vol_1;
            uint16_t vol_2;
            uint16_t vol_3;

            /* Three 10-bit tone registers */
            uint16_t tone_0;
            uint16_t tone_1;
            uint16_t tone_2;

            /* One 4-bit noise register */
            uint16_t noise;
        };
        struct {
            uint16_t vol_0_low:4;
            uint16_t vol_0_unused:12;
            uint16_t vol_1_low:4;
            uint16_t vol_1_unused:12;
            uint16_t vol_2_low:4;
            uint16_t vol_2_unused:12;
            uint16_t vol_3_low:4;
            uint16_t vol_3_unused:12;
            uint16_t tone_0_low:4;
            uint16_t tone_0_high:6;
            uint16_t tone_0_unused:6;
            uint16_t tone_1_low:4;
            uint16_t tone_1_high:6;
            uint16_t tone_1_unused:6;
            uint16_t tone_2_low:4;
            uint16_t tone_2_high:6;
            uint16_t tone_2_unused:6;
            uint16_t noise_low:4;
            uint16_t noise_unused:12;
        };
    };

    uint16_t counter_0;
    uint16_t counter_1;
    uint16_t counter_2;
    uint16_t counter_3;

    int16_t output_0;
    int16_t output_1;
    int16_t output_2;
    int16_t output_3;

    uint16_t latch;
    uint16_t lfsr;
    uint16_t output_lfsr;

    /* Extensions */
    uint16_t gg_stereo;

} SN76489_State;

/* Reset PSG to initial power-on state. */
void sn76489_init (void);

/* Handle data writes sent to the PSG. */
void sn76489_data_write (uint8_t data);

/* Retrieves a block of samples from the sample-ring. */
void sn76489_get_samples (int16_t *stream, int len);

/* Run the PSG for a number of CPU clock cycles. */
void psg_run_cycles (uint64_t cycles);

/* Export sn76489 state. */
void sn76489_state_save (void);

/* Import sn76489 state. */
void sn76489_state_load (uint32_t version, uint32_t size, void *data);
