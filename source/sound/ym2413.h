/*
 * API for the YM2413 synth chip.
 */

typedef enum YM2413_Envelope_State_e {
    YM2413_STATE_DAMP = 0,
    YM2413_STATE_ATTACK,
    YM2413_STATE_DECAY,
    YM2413_STATE_SUSTAIN,
    YM2413_STATE_RELEASE
} YM2413_Envelope_State;

typedef struct YM2413_Instrument_s {
    union {
        uint8_t r00;
        struct {
            uint8_t modulator_multiplication_factor:4;
            uint8_t modulator_key_scale_rate:1;
            uint8_t modulator_envelope_type:1;
            uint8_t modulator_vibrato:1;
            uint8_t modulator_tremolo:1;
        };
    };
    union {
        uint8_t r01;
        struct {
            uint8_t carrier_multiplication_factor:4;
            uint8_t carrier_key_scale_rate:1;
            uint8_t carrier_envelope_type:1;
            uint8_t carrier_vibrato:1;
            uint8_t carrier_tremolo:1;
        };
    };
    union {
        uint8_t r02;
        struct {
            uint8_t modulator_total_level:6;
            uint8_t modulator_key_scale_level:2;
        };
    };
    union {
        uint8_t r03;
        struct {
            uint8_t modulator_feedback_level:3;
            uint8_t modulator_waveform:1;
            uint8_t carrier_waveform:1;
            uint8_t r03_unused:1;
            uint8_t carrier_key_scale_level:2;
        };
    };
    union {
        uint8_t r04;
        struct {
            uint8_t modulator_decay_rate:4;
            uint8_t modulator_attack_rate:4;
        };
    };
    union {
        uint8_t r05;
        struct {
            uint8_t carrier_decay_rate:4;
            uint8_t carrier_attack_rate:4;
        };
    };
    union {
        uint8_t r06;
        struct {
            uint8_t modulator_release_rate:4;
            uint8_t modulator_sustain_level:4;
        };
    };
    union {
        uint8_t r07;
        struct {
            uint8_t carrier_release_rate:4;
            uint8_t carrier_sustain_level:4;
        };
    };
} YM2413_Instrument;

typedef struct YM2413_Operator_State_s {

    /* Envelope Generators */
    YM2413_Envelope_State eg_state;
    uint8_t eg_level;

    /* Fixed-point phase accumulators - 10.9 bits */
    uint32_t phase;

} YM2413_Operator_State;

typedef struct YM2413_State_s {

    uint8_t addr_latch;

    /* Custom Instrument registers */
    YM2413_Instrument regs_custom;

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

    union {
        uint8_t r30_channel_params;
        struct {
            uint8_t volume:4;
            uint8_t instrument:4;
        };
    } r30_channel_params [9];

    /* Internal State */
    uint32_t global_counter;
    int16_t feedback [9] [2];
    YM2413_Operator_State modulator [9];
    YM2413_Operator_State carrier [9];

} YM2413_State;

typedef struct YM2413_Context_s {
    YM2413_State state;
} YM2413_Context;

/* Latch a register address. */
void ym2413_addr_write (YM2413_Context *context, uint8_t addr);

/* Write data to the latched register address. */
void ym2413_data_write (YM2413_Context *context, uint8_t data);

/* Retrieves a block of samples from the sample-ring. */
void ym2413_get_samples (YM2413_Context *context, int16_t *stream, uint32_t count);

/* Run the PSG for a number of CPU clock cycles. */
void ym2413_run_cycles (YM2413_Context *context, uint64_t cycles);

/* Initialise a new YM2413 context. */
YM2413_Context *ym2413_init (void);
