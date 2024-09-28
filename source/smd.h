/*
 * Snepulator
 * Sega Mega Drive header.
 */

#define SMD_NTSC_MASTER_CLOCK   53693175
#define SMD_PAL_MASTER_CLOCK    53203424

#define SMD_RAM_SIZE        SIZE_64K
#define SMD_Z80_RAM_SIZE    SIZE_8K


typedef struct SMD_State_s {

    uint8_t port1_ctrl;
    uint8_t port2_ctrl;
    uint8_t ext_ctrl;

    bool z80_reset_n;
    bool z80_busreq;

} SMD_State;

typedef struct SMD_Context_s {

    SMD_State state;
    M68000_Context *m68k_context;
    Z80_Context *z80_context;
    SMD_VDP_Context *vdp_context;
    SN76489_Context *psg_context;
    uint64_t pending_cycles;

    /* Settings */
    Video_Format format;
    Console_Region region;

    uint8_t ram [SMD_RAM_SIZE];
    uint8_t z80_ram [SMD_Z80_RAM_SIZE];

    uint8_t *rom;
    uint32_t rom_size;
    uint32_t rom_mask;
    uint8_t  rom_hash [HASH_LENGTH];

} SMD_Context;

/* Reset the Mega Drive and load a new ROM. */
SMD_Context *smd_init (void);
