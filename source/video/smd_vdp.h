/*
 * Snepulator
 * Sega Mega Drive VDP header
 */

#define SMD_VDP_VRAM_SIZE SIZE_64K

typedef struct SMD_VDP_State_s {

    uint16_t line;

} SMD_VDP_State;

typedef struct SMD_VDP_Context_s {

    void *parent;
    SMD_VDP_State state;

    uint8_t vram [SMD_VDP_VRAM_SIZE];

    /* Video output */
    uint32_t video_width;
    uint32_t video_height;

    uint_pixel frame_buffer [VIDEO_BUFFER_WIDTH * VIDEO_BUFFER_LINES];
    void (* frame_done) (void *);

} SMD_VDP_Context;

/* Read the VDP status register. */
uint16_t smd_vdp_status_read (SMD_VDP_Context *context);

/* Run one scanline on the VDP. */
void smd_vdp_run_one_scanline (SMD_VDP_Context *context);

/* Create an SMD VDP context with power-on defaults. */
SMD_VDP_Context *smd_vdp_init (void *parent, void (* frame_done) (void *));
