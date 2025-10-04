/*
 * Snepulator
 * Sega Master System VDP header
 */

/* Enums */

typedef enum SMS_VDP_Palette_e {
    SMS_VDP_PALETTE_BACKGROUND = 0,
    SMS_VDP_PALETTE_SPRITE = 16,
} SMS_VDP_Palette;

/* Bits of control register 0 */
#define SMS_VDP_CTRL_0_MODE_4           BIT_2
#define SMS_VDP_CTRL_0_EC               BIT_3
#define SMS_VDP_CTRL_0_LINE_INT_EN      BIT_4
#define SMS_VDP_CTRL_0_MASK_COL_1       BIT_5
#define SMS_VDP_CTRL_0_LOCK_ROW_0_1     BIT_6
#define SMS_VDP_CTRL_0_LOCK_COL_24_31   BIT_7

#define SMS_PHASER_RADIUS 10

extern uint_pixel_t sms_vdp_legacy_palette [16];

/* Structs */
typedef struct SMS_VDP_Mode4_Pattern_t {
    uint8_t data[32];
} SMS_VDP_Mode4_Pattern;

/* Write one byte to the VDP control port. */
void sms_vdp_control_write (TMS9928A_Context *context, uint8_t value);

/* Read one byte from the VDP data port. */
uint8_t sms_vdp_data_read (TMS9928A_Context *context);

/* Write one byte to the VDP data port. */
void sms_vdp_data_write (TMS9928A_Context *context, uint8_t value);

/* Read the 8-bit h-counter. */
uint8_t sms_vdp_get_h_counter (TMS9928A_Context *context);

/* Check if the VDP is currently requesting an interrupt. */
bool sms_vdp_get_interrupt (TMS9928A_Context *context);

/* Assemble the four mode-bits. */
uint8_t sms_vdp_get_mode (TMS9928A_Context *context);

/* Check if the light phaser is receiving light */
bool sms_vdp_get_phaser_th (TMS9928A_Context *context, uint64_t z80_cycle);

/* Read the 8-bit v-counter. */
uint8_t sms_vdp_get_v_counter (TMS9928A_Context *context);

/* Create a SMS VDP context with power-on defaults. */
TMS9928A_Context *sms_vdp_init (void *parent, void (* frame_done) (void *), Console console);

/* Read one byte from the VDP control (status) port. */
uint8_t sms_vdp_status_read (TMS9928A_Context *context);

/* Run one scanline on the VDP. */
void sms_vdp_run_one_scanline (TMS9928A_Context *context);

/* Update the latched h_counter value. */
void sms_vdp_update_h_counter (TMS9928A_Context *context, uint64_t cycle_count);

/* Check the line counter and update the line interrupt. */
void sms_vdp_update_line_interrupt (TMS9928A_Context *context);

/* Latch the scroll register. */
void sms_vdp_update_x_scroll_latch (TMS9928A_Context *context);
