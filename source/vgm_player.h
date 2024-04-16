/*
 * Snepulator
 * VGM Player header.
 */

typedef struct VGM_Player_Context_s {

    uint64_t time_ds; /* Remaining time to carry over to the next call of vgm_player_run, in deci-samples of 44.1 kHz */

    uint8_t *vgm;
    uint32_t vgm_size;
    uint32_t index;
    uint32_t delay; /* Number of 44.1 kHz samples to delay before processing the next command */

    uint32_t version;
    uint32_t vgm_start;
    uint32_t vgm_loop;

    SN76489_Context *sn76489_context;
    uint32_t sn76489_clock;
    uint64_t sn76489_millicycles; /* Remaining time to carry over to the next run of the chip. */

    YM2413_Context *ym2413_context;
    uint32_t ym2413_clock;
    uint64_t ym2413_millicycles; /* Remaining time to carry over to the next run of the chip. */

} VGM_Player_Context;

/* Initialize the VGM Player */
VGM_Player_Context *vgm_player_init (void);
