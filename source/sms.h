/*
 * Sega Master System
 */

#define SMS_CLOCK_RATE_PAL  3546895
#define SMS_CLOCK_RATE_NTSC 3579545

#define SMS_RAM_SIZE        SIZE_8K
#define SMS_SRAM_SIZE       SIZE_32K
#define SMS_SRAM_SIZE_MIN   SIZE_128
#define SMS_SRAM_BANK_SIZE  SIZE_16K
#define SMS_SRAM_BANK_MASK  (SMS_SRAM_BANK_SIZE - 1)

typedef enum SMS_Mapper_e {
    SMS_MAPPER_UNKNOWN = 0,
    SMS_MAPPER_NONE,
    SMS_MAPPER_SEGA,
    SMS_MAPPER_CODEMASTERS,
    SMS_MAPPER_KOREAN,
} SMS_Mapper;

typedef enum SMS_3D_Field_e {
    SMS_3D_FIELD_NONE = 0,
    SMS_3D_FIELD_LEFT,
    SMS_3D_FIELD_RIGHT
} SMS_3D_Field;

/* Console hardware state */
typedef struct SMS_HW_State_s {
    uint8_t memory_control;
    uint8_t io_control;
    uint8_t mapper;
    uint8_t mapper_bank [3];
    uint16_t sram_bank;
    bool sram_enable;
} SMS_HW_State;

typedef struct SMS_Context_s {

    SMS_HW_State hw_state;
    Console console;
    Z80_Context *z80_context;
    TMS9928A_Context *vdp_context;
    SMS_3D_Field video_3d_field;
    bool export_paddle;

    /* Settings */
    Video_Format format;
    uint32_t overclock;
    Console_Region region;


    uint8_t ram [SMS_RAM_SIZE];
    uint8_t sram [SMS_SRAM_SIZE];
    uint16_t sram_used;

    uint8_t *rom;
    uint32_t rom_size;
    uint32_t rom_mask;
    uint8_t  rom_hash [HASH_LENGTH];
    uint8_t  rom_hints;

    uint8_t *bios;
    uint32_t bios_size;
    uint32_t bios_mask;

} SMS_Context;

/* Reset the SMS and load a new BIOS and/or cartridge ROM. */
SMS_Context *sms_init (void);
