/*
 * Snepulator
 * YM2612 FM synthesizer chip header.
 */

#define YM2612_RING_SIZE 2048

typedef struct YM2612_State_s {
    /* TEMP: For the initial spin, only the
     *       bare minimum for DAC output. */
    uint8_t addr_latch;
    uint8_t dac_output_reg;
    uint8_t dac_enable_reg;

} YM2612_State;

typedef struct YM2612_Context_s {

    pthread_mutex_t mutex;
    YM2612_State state;

    /* Ring buffer */
    int16_t sample_ring [YM2612_RING_SIZE];
    int16_t previous_output_level; /* For linear interpolation */
    uint64_t write_index;
    uint64_t read_index;
    uint64_t completed_samples; /* YM2612 samples, not sound card samples */
    uint32_t clock_rate;

} YM2612_Context;

/* Latch a register address. */
void ym2612_addr1_write (YM2612_Context *context, uint8_t addr);

/* Write data to the latched register address. */
void ym2612_data_write (YM2612_Context *context, uint8_t data);

/* Retrieves a block of samples from the sample-ring. */
void ym2612_get_samples (YM2612_Context *context, int32_t *stream, uint32_t count);

/* Run the PSG for a number of CPU clock cycles. */
void ym2612_run_cycles (YM2612_Context *context, uint32_t clock_rate, uint32_t cycles);

/* Initialise a new YM2612 context. */
YM2612_Context *ym2612_init (void);
