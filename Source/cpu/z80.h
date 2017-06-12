/* Structs */
/* TODO: Do we need to do anything extra to tell the compiler to pack the structs? */
/* Note: Assuming little endian for now */
typedef struct Z80_Regs_t {
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
            uint16_t alt_af;
            uint16_t alt_bc;
            uint16_t alt_de;
            uint16_t alt_hl;
        };
        struct {
            uint8_t alt_f;
            uint8_t alt_a;
            uint8_t alt_c;
            uint8_t alt_b;
            uint8_t alt_e;
            uint8_t alt_d;
            uint8_t alt_l;
            uint8_t alt_h;
        };
    };
    /* Special Purpose Registers */
    union {
        struct {
            uint16_t ir;
            uint16_t ix;    /* Index registers */
            uint16_t iy;
            uint16_t sp;    /* Stack Pointer */
            uint16_t pc;    /* Program Counter */
        };
        struct {
            uint8_t i;      /* Interrupt Vector */
            uint8_t r;      /* Memory Refresh */
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
    uint8_t im;
    bool iff1;
    bool iff2;
} Z80_Regs;

#define BIT_0               (1 << 0)
#define BIT_1               (1 << 1)
#define BIT_2               (1 << 2)
#define BIT_3               (1 << 3)
#define BIT_4               (1 << 4)
#define BIT_5               (1 << 5)
#define BIT_6               (1 << 6)
#define BIT_7               (1 << 7)

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

/* Function declarations */
void z80_init (uint8_t (* _memory_read) (uint16_t),
               void    (* _memory_write)(uint16_t, uint8_t),
               uint8_t (* _io_read)     (uint8_t),
               void    (* _io_write)    (uint8_t, uint8_t));

void z80_instruction (void);
void z80_run_until_cycle (uint64_t run_until);
