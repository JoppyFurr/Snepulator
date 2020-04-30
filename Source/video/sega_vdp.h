/*
 * SMS VDP API
 */

/* Enums */

typedef enum Vdp_Palette_t {
    VDP_PALETTE_BACKGROUND = 0,
    VDP_PALETTE_SPRITE = 16,
} Vdp_Palette;

/* Bits of control register 0 */
#define SMS_VDP_CTRL_0_MODE_4           BIT_2
#define SMS_VDP_CTRL_0_EC               BIT_3
#define SMS_VDP_CTRL_0_LINE_INT_EN      BIT_4
#define SMS_VDP_CTRL_0_MASK_COL_1       BIT_5
#define SMS_VDP_CTRL_0_LOCK_ROW_0_1     BIT_6
#define SMS_VDP_CTRL_0_LOCK_COL_24_31   BIT_7

/* Structs */
typedef struct Vdp_Mode4_Pattern_t {
    uint8_t data[32];
} Vdp_Mode4_Pattern;

/* Read one byte from the VDP data port. */
uint8_t vdp_data_read ();

/* Write one byte to the VDP data port. */
void vdp_data_write (uint8_t value);

/* Read one byte from the VDP control (status) port. */
uint8_t vdp_status_read ();

/* Write one byte to the VDP control port. */
void vdp_control_write (uint8_t value);

/* Read the 8-bit v-counter. */
uint8_t vdp_get_v_counter (void);

/* Reset the VDP registers and memory to power-on defaults. */
void vdp_init (void);

/* Run one scanline on the VDP. */
void vdp_run_one_scanline (void);

/* Copy the most recently rendered frame into the texture buffer. */
void vdp_copy_latest_frame (void);

/* Check if the VDP is currently requesting an interrupt. */
bool vdp_get_interrupt (void);

