
/* Enums */
typedef enum Vdp_Code_t {
    VDP_CODE_VRAM_READ  = 0x00,
    VDP_CODE_VRAM_WRITE = 0x40,
    VDP_CODE_REG_WRITE  = 0x80,
    VDP_CODE_CRAM_WRITE = 0xc0,
} Vdp_Code;

typedef enum Vdp_Palette_t {
    VDP_PALETTE_BACKGROUND,
    VDP_PALETTE_SPRITE,
} Vdp_Palette;

/* TODO: Consolidate helpers like this into a single file */
#define BIT_0               (1 << 0)
#define BIT_1               (1 << 1)
#define BIT_2               (1 << 2)
#define BIT_3               (1 << 3)
#define BIT_4               (1 << 4)
#define BIT_5               (1 << 5)
#define BIT_6               (1 << 6)
#define BIT_7               (1 << 7)
#define BIT_8               (1 << 8)
#define BIT_9               (1 << 9)
#define BIT_10              (1 << 10)


/* Structs */
typedef struct Vdp_Regs_t {
    uint8_t  mode_ctrl_1;
#define VDP_MODE_CTRL_1_NO_SYNC         BIT_0
#define VDP_MODE_CTRL_1_MODE_2          BIT_1
#define VDP_MODE_CTRL_1_MODE_4          BIT_2
#define VDP_MODE_CTRL_1_EC              BIT_3
#define VDP_MODE_CTRL_1_LINE_INT_EN     BIT_4
#define VDP_MODE_CTRL_1_MASK_COL_1      BIT_5
#define VDP_MODE_CTRL_1_SCROLL_DISABLE_ROW_0_1      BIT_6
#define VDP_MODE_CTRL_1_SCROLL_DISABLE_COL_24_31    BIT_7
    uint8_t  mode_ctrl_2;
#define VDP_MODE_CTRL_2_SPRITE_DOUBLE   BIT_0
#define VDP_MODE_CTRL_2_SPRITE_TALL     BIT_1
#define VDP_MODE_CTRL_2_MODE_3          BIT_3
#define VDP_MODE_CTRL_2_MODE_1          BIT_4
#define VDP_MODE_CTRL_2_FRAME_INT_EN    BIT_5
#define VDP_MODE_CTRL_2_BLANK           BIT_6
    uint8_t  name_table_addr;                   /* Bits 0x0e select name table base address. */
    uint8_t  colour_table_addr;                 /* Unused - Bits 0xff should be set. */
    uint8_t  background_pattern_generator_addr; /* Unused - Bits 0x07 should be set. */
    uint8_t  sprite_attr_table_addr;            /* Bits 0x7e select the sprite attribute table base address. */
    uint8_t  sprite_pattern_generator_addr;     /* Bit  0x04 selects sprite pattern generator base address. */
    uint8_t  background_colour;                 /* Bits 0x0f select the background from the sprite palette. */
    uint8_t  background_x_scroll;               /* Horizontal scroll */
    uint8_t  background_y_scroll;               /* Vertical scroll */
    uint8_t  line_counter;                      /* Line interrupt counter reset value */

    uint8_t  code;
    uint16_t address;
    uint8_t  read_buffer;
    uint8_t  status;
#define VDP_STATUS_INT                  BIT_7
    uint8_t  line_interrupt_counter;            /* Line interrupt counter current value */
    bool     line_interrupt;                    /* Line interrupt pending */
    uint8_t v_counter;                          /* 8-bit line counter */
} Vdp_Regs;

typedef struct Vdp_Pattern_t {
    uint8_t data[32];
} Vdp_Pattern;

typedef struct Vdp_V_Counter_Range_s {
    uint16_t first;
    uint16_t last;
} Vdp_V_Counter_Range;

typedef struct Vdp_Display_Mode_s {
    uint32_t lines_active;
    uint32_t lines_total;
    Vdp_V_Counter_Range v_counter_map[3];
} Vdp_Display_Mode;

/* I/O Functions */
uint8_t vdp_data_read ();
void vdp_data_write (uint8_t value);
uint8_t vdp_status_read ();
void vdp_control_write (uint8_t value);
uint8_t vdp_get_v_counter (void);

void vdp_init (void);
void vdp_run_one_scanline (void);
void vdp_copy_latest_frame (void);
bool vdp_get_interrupt (void);
const char *vdp_get_mode_name (void);
