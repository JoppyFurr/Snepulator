/*
 * Z80 API
 */

/* Structs */
/* TODO: Big-endian support */
typedef struct Z80_State_s {

    /* Main Register Set */
    union {
        struct {
            uint16_t af;
            uint16_t bc;
            uint16_t de;
            uint16_t hl;
        };
        struct {
            uint8_t f;
            uint8_t a;
            uint8_t c;
            uint8_t b;
            uint8_t e;
            uint8_t d;
            uint8_t l;
            uint8_t h;
        };
    };

    /* Alternate Register Set */
    union {
        struct {
            uint16_t af_alt;
            uint16_t bc_alt;
            uint16_t de_alt;
            uint16_t hl_alt;
        };
        struct {
            uint8_t f_alt;
            uint8_t a_alt;
            uint8_t c_alt;
            uint8_t b_alt;
            uint8_t e_alt;
            uint8_t d_alt;
            uint8_t l_alt;
            uint8_t h_alt;
        };
    };

    /* Special Purpose Registers */
    union {
        struct {
            uint16_t ir;
            uint16_t ix;
            uint16_t iy;
            uint16_t sp;
            uint16_t pc;
        };
        struct {
            uint8_t i;
            uint8_t r;
            uint8_t ix_l;
            uint8_t ix_h;
            uint8_t iy_l;
            uint8_t iy_h;
            uint8_t sp_l;
            uint8_t sp_h;
            uint8_t pc_l;
            uint8_t pc_h;
        };
    };

    /* Interrupts */
    uint8_t im;
    uint8_t iff1;
    uint8_t iff2;
    uint8_t halt;

} Z80_State;

/* Z80 FLAGS */
#define Z80_FLAG_CARRY      BIT_0
#define Z80_FLAG_SUB        BIT_1
#define Z80_FLAG_PARITY     BIT_2
#define Z80_FLAG_OVERFLOW   BIT_2
#define Z80_FLAG_X          BIT_3
#define Z80_FLAG_HALF       BIT_4
#define Z80_FLAG_Y          BIT_5
#define Z80_FLAG_ZERO       BIT_6
#define Z80_FLAG_SIGN       BIT_7
#define Z80_FLAG_NONE       0x00

/* Reset the Z80 registers to power-on defaults. */
void z80_init (uint8_t (* _memory_read)  (uint16_t),
               void    (* _memory_write) (uint16_t, uint8_t),
               uint8_t (* _io_read)      (uint8_t),
               void    (* _io_write)     (uint8_t, uint8_t));

/* Execute a single Z80 instruction. */
void z80_run_instruction (void);

/* Simulate the Z80 for the specified number of clock cycles. */
void z80_run_cycles (uint64_t cycles);
