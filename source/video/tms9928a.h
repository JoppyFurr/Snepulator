/*
 * TMS9928A / TMS9928A video chip.
 */

#define TMS9928A_VRAM_SIZE (16 << 10)

enum {
    TMS9928A_COLOUR_TRANSPARENT = 0,
    TMS9928A_COLOUR_BLACK = 1,
    TMS9928A_COLOUR_MEDIUM_GREEN = 2,
    TMS9928A_COLOUR_LIGHT_GREEN = 3,
    TMS9928A_COLOUR_DARK_BLUE = 4,
    TMS9928A_COLOUR_LIGHT_BLUE = 5,
    TMS9928A_COLOUR_DARK_RED = 6,
    TMS9928A_COLOUR_CYAN = 7,
    TMS9928A_COLOUR_MEDIUM_RED = 8,
    TMS9928A_COLOUR_LIGHT_RED = 9,
    TMS9928A_COLOUR_DARK_YELLOW = 10,
    TMS9928A_COLOUR_LIGHT_YELLOW = 11,
    TMS9928A_COLOUR_DARK_GREEN = 12,
    TMS9928A_COLOUR_MAGENTA = 13,
    TMS9928A_COLOUR_GREY = 14,
    TMS9928A_COLOUR_WHITE = 15
};

/* Note:
 * The TI 9900 datasheet labels the mode bit in register 0 as 'M3', with 'M2' in register 1.
 * Most other (unofficial) documents label the mode bit in register 0 as 'M2', with 'M3' in register 1.
 * It is this second naming scheme that is used in Snepulator. */

typedef enum TMS9928A_Mode_e {
    TMS9928A_MODE_0      =  0, /* Graphics I Mode  */
    TMS9928A_MODE_1      =  1, /* Text Mode (not implemented) */
    TMS9928A_MODE_2      =  2, /* Graphics II Mode */
    TMS9928A_MODE_2_1    =  3,
    TMS9928A_MODE_3      =  4, /* Multicolour Mode (not implemented) */
    TMS9928A_MODE_3_1    =  5,
    TMS9928A_MODE_3_2    =  6,
    TMS9928A_MODE_3_2_1  =  7,
    SMS_VDP_MODE_4       =  8, /* SMS Mode4 */
    SMS_VDP_MODE_4_1     =  9,
    SMS_VDP_MODE_4_2     = 10,
    SMS_VDP_MODE_4_224   = 11, /* SMS Mode4 - 224 lines */
    SMS_VDP_MODE_4_3     = 12,
    SMS_VDP_MODE_4_3_1   = 13,
    SMS_VDP_MODE_4_240   = 14, /* SMS Mode4 - 240 lines */
    SMS_VDP_MODE_4_3_2_1 = 15
} TMS9928A_Mode;

typedef enum TMS9928A_Code_e {
    TMS9928A_CODE_VRAM_READ  = 0x00,
    TMS9928A_CODE_VRAM_WRITE = 0x40,
    TMS9928A_CODE_REG_WRITE  = 0x80,
    SMS_VDP_CODE_CRAM_WRITE  = 0xc0,
} TMS9928A_Code;

/* Bits of control register 0 */
#define TMS9928A_CTRL_0_EXTERNAL_SYNC   BIT_0
#define TMS9928A_CTRL_0_MODE_2          BIT_1

/* Bits of control register 1 */
#define TMS9928A_CTRL_1_SPRITE_MAG      BIT_0
#define TMS9928A_CTRL_1_SPRITE_SIZE     BIT_1
#define TMS9928A_CTRL_1_MODE_3          BIT_3
#define TMS9928A_CTRL_1_MODE_1          BIT_4
#define TMS9928A_CTRL_1_FRAME_INT_EN    BIT_5
#define TMS9928A_CTRL_1_BLANK           BIT_6

/* Bits of the status register */
#define TMS9928A_SPRITE_COLLISION       BIT_5
#define TMS9928A_SPRITE_OVERFLOW        BIT_6
#define TMS9928A_STATUS_INT             BIT_7

typedef struct TMS9928A_Registers_s {
    union {
        uint8_t ctrl_0;
        struct {
            uint8_t ctrl_0_external_sync:1;  /* External sync for overlaying video */
            uint8_t ctrl_0_mode_2:1;         /* 'Graphics II' mode */
            uint8_t ctrl_0_mode_4:1;         /* SMS - Enable Mode 4 */
            uint8_t ctrl_0_ec:1;             /* SMS - Shifts all sprites eight pixels left */
            uint8_t ctrl_0_line_int_en:1;    /* SMS - Line interrupts */
            uint8_t ctrl_0_mask_col_1:1;     /* SMS - Blank the leftmost column of background tiles */
            uint8_t ctrl_0_lock_row_0_1:1;   /* SMS - Disable scrolling for the top two rows */
            uint8_t ctrl_0_lock_col_24_31:1; /* SMS - Disable scrolling for the rightmost eight columns */
        };
    };
    union {
        uint8_t ctrl_1;
        struct {
            uint8_t ctrl_1_sprite_mag:1;    /* Draw sprite pixels at double size */
            uint8_t ctrl_1_sprite_size:1;   /* Use 8×16 sprites */
            uint8_t ctrl_1_bit_2:1;         /* Unused */
            uint8_t ctrl_1_mode_3:1;        /* 'Multicolour' mode */
            uint8_t ctrl_1_mode_1:1;        /* 'Text' mode */
            uint8_t ctrl_1_frame_int_en:1;  /* Enable frame interrupt */
            uint8_t ctrl_1_blank:1;         /* Output a blank screen */
            uint8_t ctrl_1_bit_7:1;         /* Unused */
        };
    };
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
} TMS9928A_Registers;

typedef struct TMS9928A_State_s {
    TMS9928A_Registers regs;
    TMS9928A_Registers regs_buffer;
    uint16_t line;
    uint16_t address;
    uint8_t  first_byte_received;
    uint8_t  code;
    uint8_t  read_buffer;
    uint8_t  status;
    uint8_t  collision_buffer [256];

    /* SMS VDP extensions */
    uint_pixel cram [32];               /* Conversion to uint_pixel is done when writing to cram */
    uint8_t  line_interrupt_counter;    /* Line interrupt counter current value */
    uint8_t  line_interrupt;            /* Line interrupt pending */
    uint8_t  h_counter;                 /* 8-bit horizontal counter */
    uint8_t  v_counter;                 /* 8-bit line counter */

    /* Game Gear VDP extensions */
    uint8_t cram_latch;
} TMS9928A_State;

typedef struct TMS9928A_Context_s {

    void *parent;
    TMS9928A_State state;
    Video_Format format;
    bool is_game_gear;
    bool sms1_vdp_hint;

    /* Hacks */
    bool remove_sprite_limit;
    bool disable_blanking;

    /* Video output */
    uint8_t vram [TMS9928A_VRAM_SIZE];
    uint32_t render_start_x;
    uint32_t render_start_y;
    uint32_t video_start_x;
    uint32_t video_start_y;
    uint32_t video_width;
    uint32_t video_height;
    uint32_t video_blank_left;
    uint_pixel *palette;

    uint_pixel frame_buffer [VIDEO_BUFFER_WIDTH * VIDEO_BUFFER_LINES];
    void (* frame_done) (void *);

} TMS9928A_Context;

/* SMS - Range of 8-bit values to map onto the 16-bit v-counter */
typedef struct SMS_VDP_V_Counter_Range_s {
    uint16_t first;
    uint16_t last;
} SMS_VDP_V_Counter_Range;

/* TMS9928A / SMS-VDP common configuration structure */
typedef struct TMS9928A_Config_s {
    TMS9928A_Mode mode;
    uint16_t lines_active;
    uint16_t lines_total;
    SMS_VDP_V_Counter_Range v_counter_map [3]; /* SMS VDP v-counter mapping */
} TMS9928A_Config;

/* Each byte of the pattern represents a row of eight pixels. */
typedef struct TMS9928A_Pattern_t {
    uint8_t data [8];
} TMS9928A_Pattern;

/* Sprite attribute table entry format */
typedef struct TMS9928A_Sprite_t {
    uint8_t y;
    uint8_t x;
    uint8_t pattern;
    uint8_t colour_ec;
} TMS9928A_Sprite;

extern uint_pixel tms9928a_palette [16];
extern uint_pixel tms9928a_palette_uncorrected [16];

/* Supply a human-readable string describing the specified mode. */
const char *tms9928a_mode_name_get (TMS9928A_Mode mode);

/* Read one byte from the tms9928a data port. */
uint8_t tms9928a_data_read (TMS9928A_Context *context);

/* Write one byte to the tms9928a data port. */
void tms9928a_data_write (TMS9928A_Context *context, uint8_t value);

/* Read one byte from the tms9928a control (status) port. */
uint8_t tms9928a_status_read (TMS9928A_Context *context);

/* Write one byte to the tms9928a control port. */
void tms9928a_control_write (TMS9928A_Context *context, uint8_t value);

/* Render one line of sprites for mode0 / mode2 / mode3. */
void tms9928a_draw_sprites (TMS9928A_Context *context, const TMS9928A_Config *config, uint16_t line);

/* Render one line of the mode2 background layer. */
void tms9928a_mode2_draw_background (TMS9928A_Context *context, const TMS9928A_Config *config, uint16_t line);

/* Run one scanline on the tms9928a. */
void tms9928a_run_one_scanline (TMS9928A_Context *context);

/* Check if the tms9928a is currently requesting an interrupt. */
bool tms9928a_get_interrupt (TMS9928A_Context *context);

/* Reset the tms9928a registers and memory to power-on defaults. */
TMS9928A_Context *tms9928a_init (void *parent, void (* frame_done) (void *));

/* Export tms9928a state. */
void tms9928a_state_save (TMS9928A_Context *context);

/* Import tms9928a state. */
void tms9928a_state_load (TMS9928A_Context *context, uint32_t version, uint32_t size, void *data);
