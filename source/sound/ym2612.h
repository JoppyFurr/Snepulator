/*
 * Snepulator
 * YM2612 FM synthesizer chip header.
 */

#define YM2612_RING_SIZE 2048

#define YM2612_GROUP_1 0
#define YM2612_GROUP_2 1

typedef enum YM2612_Envelope_State_e {
    YM2612_STATE_ATTACK = 0,
    YM2612_STATE_DECAY,
    YM2612_STATE_SUSTAIN,
    YM2612_STATE_RELEASE
} YM2612_Envelope_State;


typedef struct YM2612_Operator_State_s {

    bool key_on;

    /* Configured values */
    /* TODO: detune, multiply, AM, rate scale, SSG-EG */
    uint32_t multiply;
    uint32_t total_level;
    uint32_t attack_rate;
    uint32_t decay_rate;
    uint32_t sustain_rate;
    uint32_t release_rate;
    uint32_t sustain_level;

    /* Envelope Generators */
    uint32_t effective_attack;
    uint32_t effective_decay;
    uint32_t effective_sustain;
    uint32_t effective_release;
    YM2612_Envelope_State eg_state;
    uint32_t eg_level;

    /* Fixed-point phase accumulators - 10.10 bits */
    uint32_t phase;

    /* Most recent output, stored for use with modulation */
    int32_t output;

} YM2612_Operator_State;

typedef struct YM2612_Channel_State_s {

    uint32_t algorithm;
    uint32_t fnum_high_latch;
    uint32_t fnum;

} YM2612_Channel_State;

typedef struct YM2612_State_s {

    uint8_t addr_latch;
    uint8_t group_latch;

    uint16_t eg_global_counter_divider;
    uint16_t eg_global_counter;

    /* Timers & channel-3 mode */
    uint16_t timer_a;
    bool     timer_a_flag;
    uint16_t timer_a_interval;
    uint16_t timer_b;
    bool     timer_b_flag;
    uint8_t  timer_b_interval;
    uint8_t  timer_b_divider;
    union {
        uint8_t timer_mode;
        struct {
            uint8_t timer_a_load:1;
            uint8_t timer_b_load:1;
            uint8_t timer_a_enable:1;
            uint8_t timer_b_enable:1;
            uint8_t timer_a_reset:1;
            uint8_t timer_b_reset:1;
            uint8_t ch3_mode:2;
        };
    };

    /* DAC */
    uint8_t dac_output_reg;
    uint8_t dac_enable_reg;

    YM2612_Operator_State operator [24];
    YM2612_Channel_State channel [6];

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


/* Read the status register. */
uint8_t ym2612_status_read (YM2612_Context *context);

/* Latch a register address. */
void ym2612_addr1_write (YM2612_Context *context, uint8_t addr);

/* Latch a register address. */
void ym2612_addr2_write (YM2612_Context *context, uint8_t addr);

/* Write data to the latched register address. */
void ym2612_data_write (YM2612_Context *context, uint8_t data);

/* Retrieves a block of samples from the sample-ring. */
void ym2612_get_samples (YM2612_Context *context, int32_t *stream, uint32_t count);

/* Run the PSG for a number of CPU clock cycles. */
void ym2612_run_cycles (YM2612_Context *context, uint32_t clock_rate, uint32_t cycles);

/* Initialise a new YM2612 context. */
YM2612_Context *ym2612_init (void);
