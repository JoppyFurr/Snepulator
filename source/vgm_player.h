/*
 * Snepulator
 * VGM Player header.
 */

typedef struct VGM_Player_Context_s {

    uint8_t *vgm;
    uint32_t vgm_size;
    uint32_t index;
    uint32_t delay; /* Number of 44.1 kHz samples to delay before processing the next command */
    uint32_t current_sample;

    /* Values read from VGM header */
    uint32_t version;
    uint32_t vgm_start;
    uint32_t vgm_loop;
    uint32_t total_samples;
    uint32_t loop_samples;

    SN76489_Context *sn76489_context;
    uint32_t sn76489_clock;
    uint64_t sn76489_millicycles; /* Remaining time to carry over to the next run of the chip. */

    YM2413_Context *ym2413_context;
    uint32_t ym2413_clock;
    uint64_t ym2413_millicycles; /* Remaining time to carry over to the next run of the chip. */

    /* Visualisation */
    uint32_t frame_sample_counter; /* Time for updating the visualisation */
    Video_Frame frame_buffer;

} VGM_Player_Context;

/* Initialize the VGM Player */
VGM_Player_Context *vgm_player_init (void);
