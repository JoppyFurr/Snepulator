/*
 * Snepulator
 * Sega Mega Drive VDP header
 */

#define SMD_VDP_VRAM_SIZE SIZE_64K

#define ADDRESS_CODE_DMA 0x20

typedef struct SMD_VDP_State_s {

    uint16_t line;
    uint16_t address;
    uint8_t code;
    bool second_half_pending;
    bool fill_pending;

    /* TODO: A method of listing configured features that are not yet implemented */
    union {
        uint8_t regs [24];
#pragma pack (1)
        struct {
            /* Mode 1 */
            uint8_t mode_1;

            /* Mode 2 */
            uint8_t mode_2_unused_2_0:3;
            uint8_t mode_2_v30:1;
            uint8_t mode_2_dma_en:1;
            uint8_t mode_2_vertical_int_en:1;
            uint8_t mode_2_blank:1;
            uint8_t mode_2_unused_7:1;

            uint8_t plane_a_nt_base;
            uint8_t window_nt_base;
            uint8_t plane_b_nt_base;
            uint8_t sprite_table_base;
            uint8_t unused_6; /* 128 KB only */
            uint8_t background_colour;
            uint8_t unused_8; /* SMS H-Scroll */
            uint8_t unused_9; /* SMS V-Scroll */
            uint8_t line_counter_reset;
            uint8_t mode_3;
            uint8_t mode_4;
            uint8_t h_scroll_data_base;
            uint8_t unused_e; /* 128 KB only */
            uint8_t auto_increment;
            uint8_t plane_size;
            uint8_t window_h_pos;
            uint8_t window_v_pos;
            uint16_t dma_length;
            uint32_t dma_source:22;
            uint32_t dma_operation:2;
        };
#pragma pack ()
    };

    uint_pixel_t cram [64];
    uint16_t vsram [40];

} SMD_VDP_State;

typedef struct SMD_VDP_Context_s {

    void *parent;
    SMD_VDP_State state;

    uint8_t vram [SMD_VDP_VRAM_SIZE];

    /* Video output */
    uint32_t video_width;
    uint32_t video_height;

    Video_Frame frame_buffer;
    void (* frame_done) (void *);

} SMD_VDP_Context;

/* Read the VDP status register. */
uint16_t smd_vdp_status_read (SMD_VDP_Context *context);

/* Write to the VDP control port. */
void smd_vdp_control_write (SMD_VDP_Context *context, uint16_t data);

/* Read from the VDP data port. */
uint16_t smd_vdp_data_read (SMD_VDP_Context *context);

/* Write to the VDP data port. */
void smd_vdp_data_write (SMD_VDP_Context *context, uint16_t data);

/* Run one scanline on the VDP. */
void smd_vdp_run_one_scanline (SMD_VDP_Context *context);

/* Create an SMD VDP context with power-on defaults. */
SMD_VDP_Context *smd_vdp_init (void *parent, void (* frame_done) (void *));
