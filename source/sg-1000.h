/*
 * Sega SG-1000
 */

#define SG_1000_CLOCK_RATE_PAL  3546893
#define SG_1000_CLOCK_RATE_NTSC 3579545

#define SG_1000_RAM_SIZE (1 << 10)
#define SG_1000_SRAM_SIZE (8 << 10)

typedef enum SG_Mapper_e {
    SG_MAPPER_UNKNOWN = 0,
    SG_MAPPER_NONE,
    SG_MAPPER_SEGA,
    SG_MAPPER_GRAPHIC_BOARD,
} SG_Mapper;

/* Console hardware state */
typedef struct SG_1000_HW_State_s {
    uint8_t mapper;
    uint8_t mapper_bank [3];
} SG_1000_HW_State;

typedef struct SG_1000_Context_s {

    Z80_Context *z80_context;
    TMS9928A_Context *vdp_context;
    SG_1000_HW_State hw_state;
    uint64_t millicycles;

    /* Settings */
    Video_Format format;
    uint32_t overclock;

    uint8_t ram [SG_1000_RAM_SIZE];
    uint8_t sram [SG_1000_SRAM_SIZE];
    bool sram_used;

    uint8_t *rom;
    uint32_t rom_size;
    uint32_t rom_mask;
    uint8_t  rom_hash [HASH_LENGTH];
    uint16_t rom_hints;

    /* State of Sega Graphic Board */
    uint8_t graphic_board_axis;

} SG_1000_Context;

/* Reset the SG-1000 and load a new cartridge ROM. */
SG_1000_Context *sg_1000_init (void);
