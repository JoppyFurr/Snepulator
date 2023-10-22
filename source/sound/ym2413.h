/*
 * API for the YM2413 synth chip.
 */

typedef struct YM2413_State_s {

    uint8_t addr;
    /* TODO: Break registers down with unions */

    /* Custom Instrument registers */
    uint8_t ci_param_0;
    uint8_t ci_param_1;
    uint8_t ci_param_2;
    uint8_t ci_param_3;
    uint8_t ci_mod_ar_dr;
    uint8_t ci_car_ar_dr;
    uint8_t ci_mod_sl_rr;
    uint8_t ci_car_sl_rr;

    /* Channel Registers */
    uint8_t ch_fnum_0_7 [9];
    uint8_t ch_param [9];
    uint8_t ch_inst_vol [9];

    /* Other Registers */
    uint8_t rhythm;
    uint8_t test;

    /* Internal State */

} YM2413_State;

typedef struct YM2413_Context_s {
    YM2413_State state;
} YM2413_Context;

/* Latch a register address. */
void ym2413_addr_write (YM2413_Context *context, uint8_t addr);

/* Write data to the latched register address. */
void ym2413_data_write (YM2413_Context *context, uint8_t data);

/* Retrieves a block of samples from the sample-ring. */
void ym2413_get_samples (int16_t *stream, uint32_t count);

/* Run the PSG for a number of CPU clock cycles. */
void ym2413_run_cycles (uint64_t cycles);

/* Initialise a new YM2413 context. */
YM2413_Context *ym2413_init (void);
