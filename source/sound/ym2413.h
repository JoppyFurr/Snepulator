/*
 * API for the YM2413 synth chip.
 */

typedef struct YM2413_State_s {

    uint8_t addr_latch;

    /* Custom Instrument registers */
    union {
        uint8_t r00_instrument_params;
        struct {
            uint8_t custom_modulator_multiplication_factor:4;
            uint8_t custom_modulator_key_scale_rate:1;
            uint8_t custom_modulator_envelope_type:1;
            uint8_t custom_modulator_vibrato:1;
            uint8_t custom_modulator_tremolo:1;
        };
    };
    union {
        uint8_t r01_instrument_params;
        struct {
            uint8_t custom_carrier_multiplication_factor:4;
            uint8_t custom_carrier_key_scale_rate:1;
            uint8_t custom_carrier_envelope_type:1;
            uint8_t custom_carrier_vibrato:1;
            uint8_t custom_carrier_tremolo:1;
        };
    };
    union {
        uint8_t r02_instrument_params;
        struct {
            uint8_t custom_modulator_total_level:6;
            uint8_t custom_modulator_key_scale_level:2;
        };
    };
    union {
        uint8_t r03_instrument_params;
        struct {
            uint8_t custom_modulator_feedback_level:3;
            uint8_t custom_modulator_waveform:1;
            uint8_t custom_carrier_waveform:1;
            uint8_t r03_unused:1;
            uint8_t custom_carrier_key_scale_level:2;
        };
    };
    union {
        uint8_t r04_instrument_params;
        struct {
            uint8_t custom_modulator_decay_rate:4;
            uint8_t custom_modulator_attack_level:4;
        };
    };
    union {
        uint8_t r05_instrument_params;
        struct {
            uint8_t custom_carrier_decay_rate:4;
            uint8_t custom_carrier_attack_level:4;
        };
    };
    union {
        uint8_t r06_instrument_params;
        struct {
            uint8_t custom_modulator_release_rate:4;
            uint8_t custom_modulator_sustain_level:4;
        };
    };
    union {
        uint8_t r07_instrument_params;
        struct {
            uint8_t custom_carrier_release_rate:4;
            uint8_t custom_carrier_sustain_level:4;
        };
    };

    uint8_t r08_unused [6];

    /* Rhythm register */
    union {
        uint8_t r0e_rhythm;
        struct {
            uint8_t rhythm_hat:1;
            uint8_t rhythm_cymbal:1;
            uint8_t rhythm_tom:1;
            uint8_t rhythm_snare:1;
            uint8_t rhythm_bass:1;
            uint8_t rhythm_mode:1;
            uint8_t r0e_unused:2;
        };
    };

    /* Test register */
    uint8_t r0f_test;

    /* Channel Registers */
    struct {
        uint8_t fnum;
    } r10_channel_params [9];

    uint8_t r19_unused [7];

    union {
        uint8_t r20_channel_params;
        struct {
            uint8_t fnum_9:1;
            uint8_t block:3;
            uint8_t key_on:1;
            uint8_t sustain:1;
            uint8_t unused:2;
        };
    } r20_channel_params [9];

    uint8_t r29_unused [7];

    union {
        uint8_t r30_channel_params;
        struct {
            uint8_t volume:4;
            uint8_t instrument:4;
        };
    } r30_channel_params [9];

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
