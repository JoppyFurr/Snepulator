#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* Structs */
/* TODO: Do we need to do anything extra to tell the compiler to pack the structs? */
/* Note: Assuming little endian for now */
typedef struct Z80_Regs_t {
    /* Special Purpose Registers */
    uint8_t  i;  /* Interrupt Vector */
    uint8_t  r;  /* Memory Refresh */
    uint16_t ix; /* Index register */
    uint16_t iy; /* Index register */
    uint16_t sp; /* Stack Pointer */
    union {
        uint16_t pc; /* Program Counter */
        struct {
            uint8_t pc_l;
            uint8_t pc_h;
        };
    };
    /* Main Register Set */
    uint8_t a;
    uint8_t f;
    union {
        struct {
            uint16_t bc;
            uint16_t de;
            uint16_t hl;
        };
        struct {
            uint8_t c;
            uint8_t b;
            uint8_t e;
            uint8_t d;
            uint8_t l;
            uint8_t h;
        };
    };
    /* Alternate Register Set */
    uint8_t alt_a;
    uint8_t alt_f;
    union {
        struct {
            uint16_t alt_bc;
            uint16_t alt_de;
            uint16_t alt_hl;
        };
        struct {
            uint8_t alt_c;
            uint8_t alt_b;
            uint8_t alt_e;
            uint8_t alt_d;
            uint8_t alt_l;
            uint8_t alt_h;
        };
    };
} Z80_Regs;

#define Z80_FLAG_C  (1 << 0)
#define Z80_FLAG_H  (1 << 1) /* TODO: Confirm which bit is H and which bit is N */
#define Z80_FLAG_PV (1 << 2)
#define Z80_FLAG_N  (1 << 4)
#define Z80_FLAG_Z  (1 << 6)
#define Z80_FLAG_S  (1 << 7)

/* The CPU interacts with the rest of the world by reading and writing to addresses,
 * so we need to construct an interface to allow this */

Z80_Regs z80_regs;

#define E 1
static const uint8_t z80_instruction_size[256] = {
    1, 3, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1,
    2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1,
    2, 3, 3, 1, 1, 1, 2, 1, 2, 1, 3, 1, 1, 1, 2, 1,
    2, 3, 3, 1, 1, 1, 2, 1, 2, 1, 3, 1, 1, 1, 2, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 3, 3, 3, 1, 2, 1, 1, 1, 3, E, 3, 3, 2, 1,
    1, 1, 3, 2, 3, 1, 2, 1, 1, 1, 3, 2, 3, E, 2, 1,
    1, 1, 3, 1, 3, 1, 2, 1, 1, 1, 3, 1, 3, E, 2, 1,
    1, 1, 3, 1, 3, 1, 2, 1, 1, 1, 3, 1, 3, E, 2, 1,
};

void z80_reset ()
{
    memset (&z80_regs, 0, sizeof (Z80_Regs));
}

uint32_t z80_run (uint8_t (* memory_read) (uint16_t),
                  void    (* memory_write)(uint16_t, uint8_t),
                  uint8_t (* io_read)     (uint16_t),
                  void    (* io_write)    (uint16_t, uint8_t))
{
    uint8_t instruction;
    uint8_t param_l;
    uint8_t param_h;
    uint8_t interrupt_mode;
    bool    interrupt_enable = true;

    for (;;)
    {
        /* Fetch */
        instruction = memory_read (z80_regs.pc++);

        switch (z80_instruction_size[instruction])
        {
            case 3:
                param_l = memory_read (z80_regs.pc++);
                param_h = memory_read (z80_regs.pc++);
                break;
            case 2:
                param_l = memory_read (z80_regs.pc++);
                break;
            default:
                break;
        }

        switch (instruction)
        {
            case 0x00: /* NOP */ break;
            case 0x06: /* LD B  */ z80_regs.b = param_l; break;
            case 0x0e: /* LD C  */ z80_regs.c = param_l; break;

            case 0x21: /* LD HL */ z80_regs.h = param_h; z80_regs.l = param_l; break;
            case 0x2c: /* INC L */ z80_regs.l++; break; /* TODO, set flags */

            case 0x31: /* LD SP */ z80_regs.sp = (uint16_t) param_l + ((uint16_t) param_h << 8); break;
            case 0x36: /* LD (HL) */ memory_write (z80_regs.hl, param_l); break;

            case 0x80: /* ADD B */ z80_regs.a += z80_regs.b; break; /* TODO, set flags */
            case 0x81: /* ADD C */ z80_regs.a += z80_regs.c; break; /* TODO, set flags */
            case 0x82: /* ADD D */ z80_regs.a += z80_regs.d; break; /* TODO, set flags */
            case 0x83: /* ADD E */ z80_regs.a += z80_regs.e; break; /* TODO, set flags */
            case 0x84: /* ADD H */ z80_regs.a += z80_regs.h; break; /* TODO, set flags */
            case 0x85: /* ADD L */ z80_regs.a += z80_regs.l; break; /* TODO, set flags */

            case 0x90: /* SUB B */ z80_regs.a -= z80_regs.b; break; /* TODO, set flags */
            case 0x91: /* SUB C */ z80_regs.a -= z80_regs.c; break; /* TODO, set flags */
            case 0x92: /* SUB D */ z80_regs.a -= z80_regs.d; break; /* TODO, set flags */
            case 0x93: /* SUB E */ z80_regs.a -= z80_regs.e; break; /* TODO, set flags */
            case 0x94: /* SUB H */ z80_regs.a -= z80_regs.h; break; /* TODO, set flags */
            case 0x95: /* SUB L */ z80_regs.a -= z80_regs.l; break; /* TODO, set flags */

            case 0xa0: /* AND B */ z80_regs.a &= z80_regs.b; break; /* TODO, set flags */
            case 0xa1: /* AND C */ z80_regs.a &= z80_regs.c; break; /* TODO, set flags */
            case 0xa2: /* AND D */ z80_regs.a &= z80_regs.d; break; /* TODO, set flags */
            case 0xa3: /* AND E */ z80_regs.a &= z80_regs.e; break; /* TODO, set flags */
            case 0xa4: /* AND H */ z80_regs.a &= z80_regs.h; break; /* TODO, set flags */
            case 0xa5: /* AND L */ z80_regs.a &= z80_regs.l; break; /* TODO, set flags */

            case 0xb0: /* OR  B */ z80_regs.a |= z80_regs.b; break; /* TODO, set flags */
            case 0xb1: /* OR  C */ z80_regs.a |= z80_regs.c; break; /* TODO, set flags */
            case 0xb2: /* OR  D */ z80_regs.a |= z80_regs.d; break; /* TODO, set flags */
            case 0xb3: /* OR  E */ z80_regs.a |= z80_regs.e; break; /* TODO, set flags */
            case 0xb4: /* OR  H */ z80_regs.a |= z80_regs.h; break; /* TODO, set flags */
            case 0xb5: /* OR  L */ z80_regs.a |= z80_regs.l; break; /* TODO, set flags */

            case 0xc3: /* JP */ z80_regs.pc = (uint16_t) param_l + ((uint16_t) param_h << 8); break;
            case 0xcd: /* CALL */ memory_write (z80_regs.sp--, z80_regs.pc_h);
                                  memory_write (z80_regs.sp--, z80_regs.pc_l);
                                  z80_regs.pc = (uint16_t) param_l + ((uint16_t) param_h << 8); break;
            case 0xd9: /* EXX */ { uint16_t temp_bc = z80_regs.bc; /* TODO: Write SWAP() macro, this is three lines */
                                   uint16_t temp_de = z80_regs.de;
                                   uint16_t temp_hl = z80_regs.hl;
                                   z80_regs.bc = z80_regs.alt_bc;
                                   z80_regs.de = z80_regs.alt_de;
                                   z80_regs.hl = z80_regs.alt_hl;
                                   z80_regs.alt_bc = temp_bc;
                                   z80_regs.alt_de = temp_de;
                                   z80_regs.alt_hl = temp_hl; } break;
            case 0xed: /* Extended instructions */
                       /* TODO: A second size array */
                instruction = memory_read (z80_regs.pc++);
                switch (instruction)
                {
                    case 0x56: /* IM 1 */ interrupt_mode = 1; break;

                    default:
                    fprintf (stderr, "Unknown extended instruction: %02x.\n", instruction);
                    return EXIT_FAILURE;
                }
                break;

            case 0xf3: /* DI */ interrupt_enable = false; break;

            default:
                fprintf (stderr, "Unknown instruction: %02x.\n", instruction);
                return EXIT_FAILURE;
        }
    }
}
