/* Structs */
/* TODO: Do we need to do anything extra to tell the compiler to pack the structs? */
/* Note: Assuming little endian for now */
typedef struct Z80_Regs_t {
    /* Special Purpose Registers */
    uint8_t  i;  /* Interrupt Vector */
    uint8_t  r;  /* Memory Refresh */
    uint16_t sp; /* Stack Pointer */
    union {
        uint16_t pc; /* Program Counter */
        struct {
            uint8_t pc_l;
            uint8_t pc_h;
        };
    };
    /* Main Register Set */
    union {
        struct {
            uint16_t af;
            uint16_t bc;
            uint16_t de;
            uint16_t hl;
            uint16_t ix;
            uint16_t iy;
        };
        struct {
            uint8_t a;
            uint8_t f;
            uint8_t c;
            uint8_t b;
            uint8_t e;
            uint8_t d;
            uint8_t l;
            uint8_t h;
            uint8_t ixl;
            uint8_t ixh;
            uint8_t iyl;
            uint8_t iyh;
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
            uint8_t alt_a;
            uint8_t alt_f;
            uint8_t alt_c;
            uint8_t alt_b;
            uint8_t alt_e;
            uint8_t alt_d;
            uint8_t alt_l;
            uint8_t alt_h;
        };
    };
} Z80_Regs;

#define Z80_FLAG_CARRY           (1 << 0)
#define Z80_FLAG_SUB             (1 << 1)
#define Z80_FLAG_PARITY          (1 << 2)
#define Z80_FLAG_OVERFLOW        (1 << 2)
#define Z80_FLAG_HALF            (1 << 4)
#define Z80_FLAG_ZERO            (1 << 6)
#define Z80_FLAG_SIGN            (1 << 7)

/* Function declarations */
void z80_reset ();

uint32_t z80_run (uint8_t (* memory_read) (uint16_t),
              void    (* memory_write)(uint16_t, uint8_t),
              uint8_t (* io_read)     (uint8_t),
              void    (* io_write)    (uint8_t, uint8_t));
