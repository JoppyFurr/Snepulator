
typedef struct SN79489_Regs_t {

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
    int16_t counter_0;
    int16_t counter_1;
    int16_t counter_2;
    int16_t counter_3;
    uint16_t lfsr;

    /* Outputs */
    int16_t output_0;
    int16_t output_1;
    int16_t output_2;
    int16_t output_3;
    int16_t output_lfsr;

} SN79489_Regs;

#define BASE_VOLUME 100
#define PSG_RING_SIZE 16386

void sn79489_init (void);
void sn79489_data_write (uint8_t data);
void sn79489_get_samples (int16_t *stream, int len);
void psg_run_cycles (uint64_t cycles);
