/*
 * API for the SN76489 PSG chip.
 */
#define SN76489_RING_SIZE 2048

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

typedef struct SN76489_Context_s {

    pthread_mutex_t mutex;
    SN76489_State state;

    /* Ring buffer */
    int16_t sample_ring_l [SN76489_RING_SIZE];
    int16_t sample_ring_r [SN76489_RING_SIZE];
    uint16_t previous_sample_l;
    uint16_t previous_sample_r;
    uint64_t write_index;
    uint64_t read_index;
    uint64_t completed_cycles;
    uint32_t clock_rate;

    /* Band limiting */
    Bandlimit_Context *bandlimit_context_l;
    Bandlimit_Context *bandlimit_context_r;
    int16_t phase_ring_l [SN76489_RING_SIZE];
    int16_t phase_ring_r [SN76489_RING_SIZE];

} SN76489_Context;

/* Reset PSG to initial power-on state. */
SN76489_Context *sn76489_init (void);

/* Handle data writes sent to the PSG. */
void sn76489_data_write (SN76489_Context *context, uint8_t data);

/* Retrieves a block of samples from the sample-ring. */
void sn76489_get_samples (SN76489_Context *context, int16_t *stream, uint32_t len);

/* Run the PSG for a number of CPU clock cycles. */
void psg_run_cycles (SN76489_Context *context, uint64_t cycles);

/* Export sn76489 state. */
void sn76489_state_save (SN76489_Context *context);

/* Import sn76489 state. */
void sn76489_state_load (SN76489_Context *context, uint32_t version, uint32_t size, void *data);
