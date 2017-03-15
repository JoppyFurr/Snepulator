
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

/* Structs */
typedef struct Vdp_Regs_t {
    uint8_t mode_ctrl_1;
    uint8_t mode_ctrl_2;
    uint8_t name_table_addr;
    uint8_t colour_table_addr;          /* Unused - Bits 0xff should be set. */
    uint8_t background_pattern_addr;    /* Unused - Bits 0x07 should be set. */
    uint8_t sprite_attr_table_addr;
    uint8_t sprite_pattern_table_addr;
    uint8_t background_colour;          /* Bits 0x0f select the background from the sprite palette. */
    uint8_t background_x_scroll;
    uint8_t background_y_scroll;
    uint8_t line_counter;
    uint8_t code;
    uint16_t address;
} Vdp_Regs;

typedef struct Vdp_Pattern_t {
    uint8_t data[32];
} Vdp_Pattern;

/* Defines */
#define VDP_OVERSCAN_X 8
#define VDP_OVERSCAN_Y 16

/* Register bits */
#define VDP_MODE_CTRL_1_MODE_4  (1 << 2)

/* Functions */
uint8_t vdp_data_read ();
void vdp_data_write (uint8_t value);
uint8_t vdp_status_read ();
void vdp_control_write (uint8_t value);

void vdp_init (void);
void vdp_dump (void);
void vdp_render (void);
