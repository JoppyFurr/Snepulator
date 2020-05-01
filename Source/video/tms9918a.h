/*
 * TMS9918A video chip.
 */

#define TMS9918A_VRAM_SIZE (16 << 10)

enum {
    TMS9918A_COLOUR_TRANSPARENT = 0,
    TMS9918A_COLOUR_BLACK = 1,
    TMS9918A_COLOUR_MEDIUM_GREEN = 2,
    TMS9918A_COLOUR_LIGHT_GREEN = 3,
    TMS9918A_COLOUR_DARK_BLUE = 4,
    TMS9918A_COLOUR_LIGHT_BLUE = 5,
    TMS9918A_COLOUR_DARK_RED = 6,
    TMS9918A_COLOUR_CYAN = 7,
    TMS9918A_COLOUR_MEDIUM_RED = 8,
    TMS9918A_COLOUR_LIGHT_RED = 9,
    TMS9918A_COLOUR_DARK_YELLOW = 10,
    TMS9918A_COLOUR_LIGHT_YELLOW = 11,
    TMS9918A_COLOUR_DARK_GREEN = 12,
    TMS9918A_COLOUR_MAGENTA = 13,
    TMS9918A_COLOUR_GREY = 14,
    TMS9918A_COLOUR_WHITE = 15
};

typedef enum TMS9918A_Mode_e {
    TMS9918A_MODE_0      =  0, /* Graphics I Mode  */
    TMS9918A_MODE_1      =  1, /* Text Mode        */
    TMS9918A_MODE_2      =  2, /* Graphics II Mode */
    TMS9918A_MODE_2_1    =  3,
    TMS9918A_MODE_3      =  4, /* Multicolour Mode */
    TMS9918A_MODE_3_1    =  5,
    TMS9918A_MODE_3_2    =  6,
    TMS9918A_MODE_3_2_1  =  7,
    SMS_VDP_MODE_4       =  8, /* SMS Mode4 */
    SMS_VDP_MODE_4_1     =  9,
    SMS_VDP_MODE_4_2     = 10,
    SMS_VDP_MODE_4_224   = 11, /* SMS Mode4 - 224 lines */
    SMS_VDP_MODE_4_3     = 12,
    SMS_VDP_MODE_4_3_1   = 13,
    SMS_VDP_MODE_4_240   = 14, /* SMS Mode4 - 240 lines */
    SMS_VDP_MODE_4_3_2_1 = 15
} TMS9918A_Mode;

typedef enum TMS9918A_Code_t {
    TMS9918A_CODE_VRAM_READ  = 0x00,
    TMS9918A_CODE_VRAM_WRITE = 0x40,
    TMS9918A_CODE_REG_WRITE  = 0x80,
    SMS_VDP_CODE_CRAM_WRITE  = 0xc0,
} TMS9918A_Code;

/* Bits of control register 0 */
#define TMS9918A_CTRL_0_NO_SYNC         BIT_0
#define TMS9918A_CTRL_0_MODE_2          BIT_1

/* Bits of control register 1 */
#define TMS9918A_CTRL_1_SPRITE_MAG      BIT_0
#define TMS9918A_CTRL_1_SPRITE_SIZE     BIT_1
#define TMS9918A_CTRL_1_MODE_3          BIT_3
#define TMS9918A_CTRL_1_MODE_1          BIT_4
#define TMS9918A_CTRL_1_FRAME_INT_EN    BIT_5
#define TMS9918A_CTRL_1_BLANK           BIT_6

/* Bits of the status register */
#define TMS9918A_SPRITE_COLLISION       BIT_5
#define TMS9918A_STATUS_INT             BIT_7

typedef struct TMS9918A_Registers_s {
    uint8_t ctrl_0;
    uint8_t ctrl_1;
    uint8_t name_table_base;        /* Bits [0:3] select bits [10:13] of the name table base address */
    uint8_t colour_table_base;      /* Bits [0:7] select bits [ 6:13] of the colour table base address */
    uint8_t background_pg_base;     /* Bits [0:2] select bits [11:13] of the background pattern-generator table base address */
    uint8_t sprite_attr_table_base; /* Bits [0:6] select bits [ 7:13] of the sprite attribute table base address */
    uint8_t sprite_pg_base;         /* Bits [0:2] select bits [11:13] of the sprite pattern-generator table base address */
    uint8_t background_colour;      /* Bits [0:3] select the background colour. Bits [4:7] selects the text colour. */

    /* SMS VDP extensions */
    uint8_t bg_scroll_x;            /* Horizontal scroll */
    uint8_t bg_scroll_y;            /* Vertical scroll */
    uint8_t line_counter_reset;     /* Line interrupt counter reset value */
} TMS9918A_Registers;

typedef struct TMS9918A_State_s {
    TMS9918A_Registers regs;
    uint8_t vram [TMS9918A_VRAM_SIZE];
    uint8_t  code;
    uint16_t address;
    uint8_t  read_buffer;
    uint8_t  status;

    /* SMS VDP extensions */
    uint8_t  line_interrupt_counter;    /* Line interrupt counter current value */
    bool     line_interrupt;            /* Line interrupt pending */
    uint8_t  v_counter;                 /* 8-bit line counter */

    /* Frame buffer */
    float_Colour frame_current  [VIDEO_BUFFER_WIDTH * VIDEO_BUFFER_LINES];
} TMS9918A_State;

/* SMS - Range of 8-bit values to map onto the 16-bit v-counter */
typedef struct SMS_VDP_V_Counter_Range_s {
    uint16_t first;
    uint16_t last;
} SMS_VDP_V_Counter_Range;

/* TMS9918A / SMS-VDP common configuration structure */
typedef struct TMS9918A_Config_s {
    TMS9918A_Mode mode;
    uint16_t lines_active;
    uint16_t lines_total;
    float_Colour palette [16]; /* Palette to use for TMS9918A modes */
    SMS_VDP_V_Counter_Range v_counter_map [3]; /* SMS VDP v-counter mapping */
} TMS9918A_Config;

/* Each byte of the pattern represents a row of eight pixels. */
typedef struct TMS9918A_Pattern_t {
    uint8_t data [8];
} TMS9918A_Pattern;

/* Sprite attribute table entry format */
typedef struct TMS9918A_Sprite_t {
    uint8_t y;
    uint8_t x;
    uint8_t pattern;
    uint8_t colour_ec;
} TMS9918A_Sprite;

/* Supply a human-readable string describing the specified mode. */
const char *tms9918a_mode_name_get (TMS9918A_Mode mode);

/* Render one line of a mode2 8x8 pattern. */
void tms9918a_render_mode2_pattern_line (const TMS9918A_Config *config, uint16_t line, TMS9918A_Pattern *pattern_base,
                                         uint8_t tile_colours, int32_Point_2D offset, bool sprite);

/* Render one line of sprites for mode0 / mode2 / mode3. */
void tms9918a_render_sprites_line (const TMS9918A_Config *config, uint16_t line);

/* Render one line of the mode2 background layer. */
void tms9918a_render_mode2_background_line (const TMS9918A_Config *config, uint16_t line);

/* Run one scanline on the tms9918a. */
void tms9918a_run_one_scanline (void);

/* Check if the tms9918a is currently requesting an interrupt. */
bool tms9918a_get_interrupt (void);

/* Reset the tms9918a registers and memory to power-on defaults. */
void tms9918a_init (void);
