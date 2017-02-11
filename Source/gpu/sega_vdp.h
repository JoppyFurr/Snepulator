
/* Enums */
typedef enum Vdp_Port_t {
    VDP_PORT_V_COUNTER,
    VDP_PORT_H_COUNTER,
    VDP_PORT_DATA,
    VDP_PORT_CONTROL
} Vdp_Port;

typedef enum Vdp_Operation_t {
    VDP_OPERATION_WRITE,
    VDP_OPERATION_READ,
} Vdp_Operation;

typedef enum Vdp_Code_t {
    VDP_CODE_VRAM_READ  = 0x00,
    VDP_CODE_VRAM_WRITE = 0x01,
    VDP_CODE_REG_WRITE  = 0x02,
    VDP_CODE_CRAM_WRITE = 0x03,
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
void vdp_init (void);
void vdp_dump (void);
void vdp_render (void);
uint32_t vdp_access (uint8_t value, Vdp_Port port, Vdp_Operation operation);
