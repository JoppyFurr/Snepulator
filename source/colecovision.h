/*
 * Sega SG-1000
 */

#define COLECOVISION_CLOCK_RATE_PAL  3546895
#define COLECOVISION_CLOCK_RATE_NTSC 3579545

#define COLECOVISION_RAM_SIZE (1 << 10)

/* Stores the controller interface state */
typedef enum ColecoVision_Input_Mode_e {
    COLECOVISION_INPUT_MODE_JOYSTICK,
    COLECOVISION_INPUT_MODE_KEYPAD,
} ColecoVision_Input_Mode;


typedef struct ColecoVision_HW_State_s {
    uint8_t input_mode;
} ColecoVision_HW_State;


typedef struct ColecoVision_Context_s {

    Z80_Context *z80_context;
    TMS9928A_Context *vdp_context;
    SN76489_Context *psg_context;
    ColecoVision_HW_State hw_state;
    uint64_t millicycles;

    /* Settings */
    Video_Format format;
    uint32_t overclock;

    uint8_t ram [COLECOVISION_RAM_SIZE];

    uint8_t *rom;
    uint32_t rom_size;
    uint32_t rom_mask;
    uint8_t  rom_hash [HASH_LENGTH];

    uint8_t *bios;
    uint32_t bios_size;
    uint32_t bios_mask;

} ColecoVision_Context;

/* Reset the ColecoVision and load a new cartridge ROM. */
ColecoVision_Context *colecovision_init (void);
