/*
 * Snepulator
 * Sega Master System header.
 */

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
    SMS_MAPPER_MSX,
    SMS_MAPPER_NEMESIS,
    SMS_MAPPER_4PAK,
    SMS_MAPPER_JANGGUN,
} SMS_Mapper;

typedef enum SMS_3D_Field_e {
    SMS_3D_FIELD_NONE = 0,
    SMS_3D_FIELD_LEFT,
    SMS_3D_FIELD_RIGHT
} SMS_3D_Field;

/* Console hardware state */
typedef struct SMS_HW_State_s {
    uint8_t memory_control;
    union {
        uint8_t io_control;
        struct {
            uint8_t io_tr_a_direction:1;
            uint8_t io_th_a_direction:1;
            uint8_t io_tr_b_direction:1;
            uint8_t io_th_b_direction:1;
            uint8_t io_tr_a_value:1;
            uint8_t io_th_a_value:1;
            uint8_t io_tr_b_value:1;
            uint8_t io_th_b_value:1;
        };
    };
    uint8_t mapper;
    uint8_t mapper_bank [4];
    uint16_t sram_bank;
    bool sram_enable;
} SMS_HW_State;

typedef struct SMS_Context_s {

    SMS_HW_State hw_state;
    Console console;
    Z80_Context *z80_context;
    TMS9928A_Context *vdp_context;
    SN76489_Context *psg_context;
    YM2413_Context *ym2413_context;
    SMS_3D_Field video_3d_field;
    bool export_paddle;
    bool reset_button;
    uint32_t reset_button_timeout;
    uint64_t pending_cycles;

    /* TODO: Move the audio_control register into the state when updating the format.
     *       for now it lives in the context to avoid changing the size of the state. */
    uint8_t audio_control;

    /* Settings */
    Video_Format format;
    uint32_t overclock;
    Console_Region region;

    uint8_t ram [SMS_RAM_SIZE];
    uint8_t sram [SMS_SRAM_SIZE];
    uint8_t sram_last_write [SMS_SRAM_SIZE];
    uint16_t sram_used;

    uint8_t *rom;
    uint32_t rom_size;
    uint32_t rom_mask;
    uint8_t  rom_hash [HASH_LENGTH];
    uint16_t rom_hints;

    uint8_t *bios;
    uint32_t bios_size;
    uint32_t bios_mask;

} SMS_Context;

/* Reset the SMS and load a new BIOS and/or cartridge ROM. */
SMS_Context *sms_init (void);
