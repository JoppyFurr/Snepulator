#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "z80.h"
#include "z80_names.h"

#define SWAP(TYPE, X, Y) { TYPE tmp = X; X = Y; Y = tmp; }

#define DEBUG

/* State */
Z80_Regs z80_regs;
uint64_t z80_cycle = 0;

/* Function pointers for accessing the rest of the system */
uint8_t (* memory_read) (uint16_t) = NULL;
void    (* memory_write)(uint16_t, uint8_t) = NULL;
uint8_t (* io_read)     (uint8_t) = NULL;
void    (* io_write)    (uint8_t, uint8_t) = NULL;

/* TODO: For IX/IY, can we do a SWAP (IX,HL) before/after the instruction? */

/* TODO: Note: Interrupts should not be accepted until after the instruction following EI */

/* TODO: Cycle counts for interrupt response */

/* DIAG */
uint64_t instruction_count = 0;
bool debug_instruction = false;

#define E 1 /* Extended */
#define U 0 /* Unused */
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

/* TODO: Implement variable cycle-lengths */
#define V 0 /* Variable */
static const uint8_t z80_instruction_cycles[256] = {
     4, 10,  7,  6,  4,  4,  7,  4,  4, 11,  7,  6,  4,  4,  7,  4,
     V, 10,  7,  6,  4,  4,  7,  4, 12, 11,  7,  6,  4,  4,  7,  4,
     V, 10, 16,  6,  4,  4,  7,  4,  V, 11, 16,  6,  4,  4,  7,  4,
     V, 10, 13,  6, 11, 11, 10,  4,  V, 11, 13,  6,  4,  4,  7,  4,
     4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
     4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
     4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
     7,  7,  7,  7,  7,  7,  4,  7,  4,  4,  4,  4,  4,  4,  7,  4,
     4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
     4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
     4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
     4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
     V, 10, 10, 10,  V, 11,  7, 11,  V, 10, 10,  V,  V, 17,  7, 11,
     V, 10, 10, 11,  V, 11,  7, 11,  V,  4, 10, 11,  V,  V,  7, 11,
     V, 10, 10, 19,  V, 11,  7, 11,  V,  4, 10,  4,  V,  V,  7, 11,
     V, 10, 10,  4,  V, 11,  7, 11,  V,  6, 10,  4,  V,  V,  7, 11,
};

/* TODO: Implement cycle lengths for extended instructions */
static const uint8_t z80_instruction_size_extended[256] = {
    U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    1, 1, 1, 3, 1, 1, 1, 1, 1, 1, 1, 3, 1, 1, 1, 1,
    1, 1, 1, 3, 1, 1, 1, 1, 1, 1, 1, 3, 1, 1, 1, 1,
    1, 1, 1, 3, 1, 1, 1, 1, 1, 1, 1, 3, 1, 1, 1, 1,
    1, 1, 1, 3, 1, 1, 1, U, 1, 1, 1, 3, 1, 1, 1, U,
    U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    1, 1, 1, 1, U, U, U, U, 1, 1, 1, 1, U, U, U, U,
    1, 1, 1, 1, U, U, U, U, 1, 1, 1, 1, U, U, U, U,
    U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
};

/* TODO: Implement cycle lengths for IX/IY and bit instructions */
static const uint8_t z80_instruction_size_ix[256] = {
    U, U, U, U, U, U, U, U, U, 1, U, U, U, U, U, U,
    U, U, U, U, U, U, U, U, U, 1, U, U, U, U, U, U,
    U, 3, 3, 1, 1, 1, 2, U, U, 1, 3, 1, 1, 1, 2, U,
    U, U, U, U, 2, 2, 3, U, U, 1, U, U, U, U, U, U,
    U, U, U, U, 1, 1, 2, U, U, U, U, U, 1, 1, 2, U,
    U, U, U, U, 1, 1, 2, U, U, U, U, U, 1, 1, 2, U,
    1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1,
    2, 2, 2, 2, 2, 2, U, 2, U, U, U, U, 1, 1, 2, U,

    U, U, U, U, 1, 1, 2, U, U, U, U, U, 1, 1, 2, U,
    U, U, U, U, 1, 1, 2, U, U, U, U, U, 1, 1, 2, U,
    U, U, U, U, 1, 1, 2, U, U, U, U, U, 1, 1, 2, U,
    U, U, U, U, 1, 1, 2, U, U, U, U, U, 1, 1, 2, U,
    U, U, U, U, U, U, U, U, U, U, U, E, U, U, U, U,
    U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    U, 1, U, 1, U, 1, U, U, U, 1, U, U, U, U, U, U,
    U, U, U, U, U, U, U, U, U, 1, U, U, U, U, U, U,
};

static const uint8_t uint8_even_parity[256] = {
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
};

uint32_t z80_init (uint8_t (* _memory_read) (uint16_t),
                   void    (* _memory_write)(uint16_t, uint8_t),
                   uint8_t (* _io_read)     (uint8_t),
                   void    (* _io_write)    (uint8_t, uint8_t))
{
    memory_read  = _memory_read;
    memory_write = _memory_write;
    io_read      = _io_read;
    io_write     = _io_write;

    /* Reset values */
    memset (&z80_regs, 0, sizeof (Z80_Regs));
    z80_regs.af = 0xffff;
    z80_regs.sp = 0xffff;
}

#define SET_FLAGS_AND { z80_regs.f = (uint8_even_parity[z80_regs.a] ? Z80_FLAG_PARITY : 0) | \
                                     (                                Z80_FLAG_HALF      ) | \
                                     (z80_regs.a == 0x00            ? Z80_FLAG_ZERO   : 0) | \
                                     ((z80_regs.a & 0x80)           ? Z80_FLAG_SIGN   : 0); }

#define SET_FLAGS_OR_XOR { z80_regs.f = (uint8_even_parity[z80_regs.a] ? Z80_FLAG_PARITY : 0) | \
                                        (z80_regs.a == 0x00            ? Z80_FLAG_ZERO   : 0) | \
                                        ((z80_regs.a & 0x80)           ? Z80_FLAG_SIGN   : 0); }

#define SET_FLAGS_ADD(X) { z80_regs.f = (((uint16_t)z80_regs.a + (uint16_t)X) & 0x100          ? Z80_FLAG_CARRY    : 0) | \
                                        (((((int8_t)z80_regs.a) + ((int8_t)X)) >  127 || \
                                          (((int8_t)z80_regs.a) + ((int8_t)X)) < -128)         ? Z80_FLAG_OVERFLOW : 0) | \
                                        (((z80_regs.a & 0x0f) + (X & 0x0f)) & 0x10             ? Z80_FLAG_HALF     : 0) | \
                                        (((z80_regs.a + X) & 0xff) == 0x00                     ? Z80_FLAG_ZERO     : 0) | \
                                        ((z80_regs.a + X) & 0x80                               ? Z80_FLAG_SIGN     : 0); }

#define SET_FLAGS_SUB(X) { z80_regs.f = (((uint16_t)z80_regs.a - (uint16_t)X) & 0x100          ? Z80_FLAG_CARRY    : 0) | \
                                        (                                                       Z80_FLAG_SUB         ) | \
                                        (((((int8_t)z80_regs.a) - ((int8_t)X)) >  127 || \
                                          (((int8_t)z80_regs.a) - ((int8_t)X)) < -128)        ? Z80_FLAG_OVERFLOW : 0) | \
                                        (((z80_regs.a & 0x0f) - (X & 0x0f)) & 0x10            ? Z80_FLAG_HALF     : 0) | \
                                        ((z80_regs.a == X)                                    ? Z80_FLAG_ZERO     : 0) | \
                                        ((z80_regs.a - X) & 0x80                              ? Z80_FLAG_SIGN     : 0); }

#define CARRY_BIT (z80_regs.f & Z80_FLAG_CARRY)

#define SET_FLAGS_ADC(X) { z80_regs.f = (((uint16_t)z80_regs.a + (uint16_t)X + CARRY_BIT) & 0x100  ? Z80_FLAG_CARRY    : 0) | \
                                        (((((int8_t)z80_regs.a) + ((int8_t)X) + CARRY_BIT) >  127 || \
                                          (((int8_t)z80_regs.a) + ((int8_t)X) + CARRY_BIT) < -128) ? Z80_FLAG_OVERFLOW : 0) | \
                                        (((z80_regs.a & 0x0f) + (X & 0x0f) + CARRY_BIT) & 0x10     ? Z80_FLAG_HALF     : 0) | \
                                        (((z80_regs.a + X + CARRY_BIT) & 0xff) == 0x00             ? Z80_FLAG_ZERO     : 0) | \
                                        ((z80_regs.a + X + CARRY_BIT) & 0x80                       ? Z80_FLAG_SIGN     : 0); }

#define SET_FLAGS_SBC(X) { z80_regs.f = (((uint16_t)z80_regs.a - (uint16_t)X - CARRY_BIT) & 0x100  ? Z80_FLAG_CARRY    : 0) | \
                                        (                                                            Z80_FLAG_SUB         ) | \
                                        (((((int8_t)z80_regs.a) - ((int8_t)X) - CARRY_BIT) >  127 || \
                                          (((int8_t)z80_regs.a) - ((int8_t)X) - CARRY_BIT) < -128) ? Z80_FLAG_OVERFLOW : 0) | \
                                        (((z80_regs.a & 0x0f) - (X & 0x0f) - CARRY_BIT) & 0x10     ? Z80_FLAG_HALF     : 0) | \
                                        (((z80_regs.a - X - CARRY_BIT) & 0xff) == 0x00             ? Z80_FLAG_ZERO     : 0) | \
                                        ((z80_regs.a - X - CARRY_BIT) & 0x80                       ? Z80_FLAG_SIGN     : 0); }

#define SET_FLAGS_INC(X) { z80_regs.f = (z80_regs.f         & Z80_FLAG_CARRY       ) | \
                                        (X == 0x80          ? Z80_FLAG_OVERFLOW : 0) | \
                                        ((X & 0x0f) == 0x00 ? Z80_FLAG_HALF     : 0) | \
                                        (X == 0x00          ? Z80_FLAG_ZERO     : 0) | \
                                        ((X  & 0x80)        ? Z80_FLAG_SIGN     : 0); }

#define SET_FLAGS_DEC(X) { z80_regs.f = (z80_regs.f         & Z80_FLAG_CARRY       ) | \
                                        (                     Z80_FLAG_SUB         ) | \
                                        (X == 0x7f          ? Z80_FLAG_OVERFLOW : 0) | \
                                        ((X & 0x0f) == 0x0f ? Z80_FLAG_HALF     : 0) | \
                                        (X == 0x00          ? Z80_FLAG_ZERO     : 0) | \
                                        ((X & 0x80)         ? Z80_FLAG_SIGN     : 0); }

#define SET_FLAGS_CPL { z80_regs.f = z80_regs.f | Z80_FLAG_HALF | Z80_FLAG_SUB; }

#define SET_FLAGS_ADD_16(Y,X) { z80_regs.f = (z80_regs.f &              (Z80_FLAG_OVERFLOW | Z80_FLAG_ZERO | Z80_FLAG_SIGN))     | \
                                             ((((uint32_t)Y + (uint32_t)X) & 0x10000)                      ? Z80_FLAG_CARRY : 0) | \
                                             ((((uint32_t)(Y & 0x0fff) + (uint32_t)(X & 0x0fff)) & 0x1000) ? Z80_FLAG_HALF  : 0); }

#define SET_FLAGS_SUB_16(Y,X) { z80_regs.f = (z80_regs.f &              (Z80_FLAG_OVERFLOW | Z80_FLAG_ZERO | Z80_FLAG_SIGN)    ) | \
                                             (                                                               Z80_FLAG_SUB      ) | \
                                             ((((uint32_t)Y - (uint32_t)X) & 0x10000)                      ? Z80_FLAG_CARRY : 0) | \
                                             ((((uint32_t)(Y & 0x0fff) - (uint32_t)(X & 0x0fff)) & 0x1000) ? Z80_FLAG_HALF  : 0); }

#define SET_FLAGS_CPD_CPI(X) { z80_regs.f = (z80_regs.f                            & Z80_FLAG_CARRY       ) | \
                                        (                                            Z80_FLAG_SUB         ) | \
                                        ((z80_regs.bc)                             ? Z80_FLAG_OVERFLOW : 0) | \
                                        (((z80_regs.a & 0x0f) - (X & 0x0f)) & 0x10 ? Z80_FLAG_HALF     : 0) | \
                                        ((z80_regs.a == X)                         ? Z80_FLAG_ZERO     : 0) | \
                                        ((z80_regs.a - X) & 0x80                   ? Z80_FLAG_SIGN     : 0); }

#define SET_FLAGS_RLCA(X) { z80_regs.f = (z80_regs.f & (Z80_FLAG_PARITY | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) | \
                                         ((X & 0x01)                             ? Z80_FLAG_CARRY : 0); }

#define SET_FLAGS_RLC(X) { z80_regs.f = (uint8_even_parity[X]                    ? Z80_FLAG_PARITY : 0) | \
                                         ((X & 0x01)                             ? Z80_FLAG_CARRY  : 0) | \
                                         (X == 0x00                              ? Z80_FLAG_ZERO   : 0) | \
                                         ((X & 0x80)                             ? Z80_FLAG_SIGN   : 0); }

#define SET_FLAGS_RRC(X) { z80_regs.f = (uint8_even_parity[X]                    ? Z80_FLAG_PARITY : 0) | \
                                         ((X & 0x80)                             ? Z80_FLAG_CARRY  : 0) | \
                                         (X == 0x00                              ? Z80_FLAG_ZERO   : 0) | \
                                         ((X & 0x80)                             ? Z80_FLAG_SIGN   : 0); }

#define SET_FLAGS_RL(X) { z80_regs.f = (uint8_even_parity[X]                    ? Z80_FLAG_PARITY : 0) | \
                                       (X == 0x00                               ? Z80_FLAG_ZERO   : 0) | \
                                       ((X & 0x80)                              ? Z80_FLAG_SIGN   : 0); }

#define SET_FLAGS_RR(X) { z80_regs.f = (uint8_even_parity[X]                    ? Z80_FLAG_PARITY : 0) | \
                                       (X == 0x00                               ? Z80_FLAG_ZERO   : 0) | \
                                       ((X & 0x80)                              ? Z80_FLAG_SIGN   : 0); }

#define SET_FLAGS_RRD_RLD { z80_regs.f = (z80_regs.f                            & Z80_FLAG_CARRY)      | \
                                     (uint8_even_parity[z80_regs.a]             ? Z80_FLAG_PARITY : 0) | \
                                     (z80_regs.a == 0x00                        ? Z80_FLAG_ZERO   : 0) | \
                                     (z80_regs.a & 0x80                         ? Z80_FLAG_SIGN   : 0); }


/* TEMPORORY */
#include "SDL2/SDL.h"
extern SDL_Renderer *renderer;
void vdp_render (void);
static bool _abort_ = false;

uint32_t z80_extended_instruction ()
{
    uint8_t instruction = memory_read (z80_regs.pc++);
    uint8_t temp_1;
    uint8_t temp_2;

    union {
        uint16_t w;
        struct {
            uint8_t l;
            uint8_t h;
        };
    } param;

    switch (z80_instruction_size_extended[instruction])
    {
        case 3:
            param.l = memory_read (z80_regs.pc++);
            param.h = memory_read (z80_regs.pc++);

            if (debug_instruction) fprintf (stdout, "[DEBUG]:           INST=%02x %02x %02x,  %-12s\n",
                 instruction, param.l, param.h,
                 z80_instruction_name_extended[instruction]);
            break;
        case 2:
            param.l = memory_read (z80_regs.pc++);

            if (debug_instruction) fprintf (stdout, "[DEBUG]:           INST=%02x %02x,     %-12s\n",
                 instruction, param.l,
                 z80_instruction_name_extended[instruction]);
            break;
        default:
            if (debug_instruction) fprintf (stdout, "[DEBUG]:           INST=%02x,        %-12s\n",
                 instruction,
                 z80_instruction_name_extended[instruction]);
            break;
    }

    switch (instruction)
    {
        case 0x41: /* OUT (C),B  */ io_write (z80_regs.c, z80_regs.b); break;
        case 0x42: /* SBC HL,BC  */ SET_FLAGS_SUB_16 (z80_regs.hl, (z80_regs.bc + (z80_regs.f & Z80_FLAG_CARRY ? 1 : 0)));
                                    z80_regs.hl -=                 (z80_regs.bc + (z80_regs.f & Z80_FLAG_CARRY ? 1 : 0)); break;
        case 0x43: /* LD (**),BC */ memory_write (param.w,     z80_regs.c);
                                    memory_write (param.w + 1, z80_regs.b); break;
        case 0x44: /* NEG        */ temp_1 = z80_regs.a;
                                    z80_regs.a = 0 - (int8_t)z80_regs.a;
                                    /* TODO: Consolidate flags */
                                    z80_regs.f = (temp_1 != 0                  ? Z80_FLAG_CARRY    : 0) |
                                                 (                               Z80_FLAG_SUB         ) |
                                                 (temp_1 == 0x80               ? Z80_FLAG_OVERFLOW : 0) |
                                                 ((0 - (temp_1 & 0x0f)) & 0x10 ? Z80_FLAG_HALF     : 0) |
                                                 (z80_regs.a == 0              ? Z80_FLAG_ZERO     : 0) |
                                                 (z80_regs.a & 0x80            ? Z80_FLAG_SIGN     : 0);
                                    break;
        case 0x45: /* RETN       */ z80_regs.pc_l = memory_read (z80_regs.sp++);
                                    z80_regs.pc_h = memory_read (z80_regs.sp++);
                                    z80_regs.iff1 = z80_regs.iff2;
                                    break;
        case 0x4a: /* ADC HL,BC  */ SET_FLAGS_ADD_16 (z80_regs.hl, (z80_regs.bc + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                    z80_regs.hl +=   z80_regs.bc + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
        case 0x4b: /* LD BC,(**) */ z80_regs.c = memory_read (param.w);
                                    z80_regs.b = memory_read (param.w + 1); break;
        case 0x4d: /* RETI       */ z80_regs.pc_l = memory_read (z80_regs.sp++);
                                    z80_regs.pc_h = memory_read (z80_regs.sp++);
                                    break; /* TODO: Signals the IO device that the interrupt is handled? */

        case 0x51: /* OUT (C),D  */ io_write (z80_regs.c, z80_regs.d); break;
        case 0x52: /* SBC HL,DE  */ SET_FLAGS_SUB_16 (z80_regs.hl, (z80_regs.de + (z80_regs.f & Z80_FLAG_CARRY ? 1 : 0)));
                                    z80_regs.hl -=                 (z80_regs.de + (z80_regs.f & Z80_FLAG_CARRY ? 1 : 0)); break;
        case 0x53: /* LD (**),DE */ memory_write (param.w,     z80_regs.e);
                                    memory_write (param.w + 1, z80_regs.d); break;
        case 0x56: /* IM 1       */ fprintf (stdout, "[DEBUG]: Interrupt mode = 1.\n"); z80_regs.im = 1; break;
        case 0x59: /* OUT (C),E  */ io_write (z80_regs.c, z80_regs.e); break;
        case 0x5a: /* ADC HL,DE  */ SET_FLAGS_ADD_16 (z80_regs.hl, (z80_regs.de + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                    z80_regs.hl +=   z80_regs.de + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
        case 0x5b: /* LD DE,(**) */ z80_regs.e = memory_read (param.w);
                                    z80_regs.d = memory_read (param.w + 1); break;

        case 0x61: /* OUT (C),H  */ io_write (z80_regs.c, z80_regs.h); break;
        case 0x62: /* SBC HL,HL  */ SET_FLAGS_SUB_16 (z80_regs.hl, (z80_regs.hl + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                    z80_regs.hl -=   z80_regs.hl + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
        case 0x67: /* RRD        */ temp_1 = memory_read (z80_regs.hl);
                                    temp_2 = z80_regs.a;
                                    z80_regs.a &= 0xf0; z80_regs.a |= (temp_1 & 0x0f);
                                    temp_1 >>= 4; temp_1 |= (temp_2 << 4);
                                    memory_write (z80_regs.hl, temp_1);
                                    SET_FLAGS_RRD_RLD;
                                    break;
        case 0x69: /* OUT (C),L  */ io_write (z80_regs.c, z80_regs.l); break;
        case 0x6a: /* ADC HL,HL  */ SET_FLAGS_ADD_16 (z80_regs.hl, (z80_regs.hl + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                    z80_regs.hl +=   z80_regs.hl + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
        case 0x6f: /* RLD        */ temp_1 = memory_read (z80_regs.hl);
                                    temp_2 = z80_regs.a;
                                    z80_regs.a &= 0xf0; z80_regs.a |= (temp_1 >> 4);
                                    temp_1 <<= 4; temp_1 |= (temp_2 & 0x0f);
                                    memory_write (z80_regs.hl, temp_1);
                                    SET_FLAGS_RRD_RLD;
                                    break;

        case 0x71: /* OUT (C),0  */ io_write (z80_regs.c, 0); break;
        case 0x72: /* SBC HL,SP  */ SET_FLAGS_SUB_16 (z80_regs.hl, (z80_regs.sp + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                    z80_regs.hl -=   z80_regs.sp + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
        case 0x73: /* LD (**),SP */ memory_write (param.w,     z80_regs.sp_l);
                                    memory_write (param.w + 1, z80_regs.sp_h); break;
        case 0x7a: /* ADC HL,SP  */ SET_FLAGS_ADD_16 (z80_regs.hl, (z80_regs.sp + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                    z80_regs.hl +=   z80_regs.sp + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
        case 0x79: /* OUT (C),A  */ io_write (z80_regs.c, z80_regs.a); break;
        case 0x7b: /* LD SP,(**) */ z80_regs.sp_l = memory_read (param.w);
                                    z80_regs.sp_h = memory_read (param.w + 1); break;

        case 0xa0: /* LDI        */ memory_write (z80_regs.de, memory_read (z80_regs.hl));
                                    z80_regs.hl++; z80_regs.de++; z80_regs.bc--;
                                    z80_regs.f &= (Z80_FLAG_CARRY | Z80_FLAG_ZERO | Z80_FLAG_SIGN);
                                    z80_regs.f |= (z80_regs.bc ? Z80_FLAG_OVERFLOW : 0); break;
        case 0xa1: /* CPI        */ temp_1 = memory_read (z80_regs.hl);
                                    z80_regs.hl++;
                                    z80_regs.bc--;
                                    SET_FLAGS_CPD_CPI (temp_1);
                                    break;
        case 0xa3: /* OUTI       */ { io_write (z80_regs.c, memory_read(z80_regs.hl)),
                                      z80_regs.hl++; z80_regs.b--;
                                      z80_regs.f = (z80_regs.f & Z80_FLAG_CARRY) |
                                                  (Z80_FLAG_SUB) |
                                                  (z80_regs.b == 0 ? Z80_FLAG_ZERO : 0);
                                    } break;
        case 0xa8: /* LDD        */ memory_write (z80_regs.de, memory_read (z80_regs.hl));
                                    z80_regs.hl--; z80_regs.de--; z80_regs.bc--;
                                    z80_regs.f &= (Z80_FLAG_CARRY | Z80_FLAG_ZERO | Z80_FLAG_SIGN);
                                    z80_regs.f |= (z80_regs.bc ? Z80_FLAG_OVERFLOW : 0); break;
        case 0xa9: /* CPD        */ temp_1 = memory_read (z80_regs.hl);
                                    z80_regs.hl--;
                                    z80_regs.bc--;
                                    SET_FLAGS_CPD_CPI (temp_1);
                                    break;

        case 0xb0: /* LDIR       */ memory_write (z80_regs.de, memory_read (z80_regs.hl));
                                    z80_regs.hl++; z80_regs.de++;
                                    z80_regs.bc--;
                                    z80_regs.pc -= z80_regs.bc ? 2 : 0;
                                    z80_regs.f = (z80_regs.f & (Z80_FLAG_CARRY |
                                                                Z80_FLAG_ZERO  |
                                                                Z80_FLAG_SIGN)) |
                                                 (z80_regs.bc ? Z80_FLAG_OVERFLOW : 0);
                                    break;
        case 0xb1: /* CPIR       */ temp_1 = memory_read (z80_regs.hl);
                                    z80_regs.hl++;
                                    z80_regs.bc--;
                                    z80_regs.pc -= (z80_regs.bc == 0 || z80_regs.a == temp_1) ? 0 : 2;
                                    SET_FLAGS_CPD_CPI (temp_1);
                                    break;
        case 0xb3: /* OUTR       */ io_write (z80_regs.c, memory_read(z80_regs.hl)),
                                    z80_regs.hl++; z80_regs.b--;
                                    z80_regs.pc -= z80_regs.b ? 2 : 0;
                                    z80_regs.f = (z80_regs.f & Z80_FLAG_CARRY) |
                                                 (Z80_FLAG_SUB | Z80_FLAG_ZERO); break;
        case 0xb8: /* LDDR       */ memory_write (z80_regs.de, memory_read (z80_regs.hl));
                                    z80_regs.hl--; z80_regs.de--; z80_regs.bc--;
                                    z80_regs.f = (z80_regs.f & (Z80_FLAG_CARRY |
                                                                Z80_FLAG_ZERO  |
                                                                Z80_FLAG_SIGN)) |
                                                 (z80_regs.bc ? Z80_FLAG_OVERFLOW : 0);
                                    z80_regs.pc -= z80_regs.bc ? 2 : 0; break;
        case 0xb9: /* CPDR       */ temp_1 = memory_read (z80_regs.hl);
                                    z80_regs.hl--;
                                    z80_regs.bc--;
                                    z80_regs.pc -= (z80_regs.bc == 0 || z80_regs.a == temp_1) ? 0 : 2;
                                    SET_FLAGS_CPD_CPI (temp_1);
                                    break;

        default:
        fprintf (stderr, "Unknown extended instruction: \"%s\" (%02x). %" PRIu64 " instructions have been run.\n",
                 z80_instruction_name_extended[instruction], instruction, instruction_count);
        _abort_ = true;
    }

    /* TODO: Fill this out properly. Most instructions take longer */
    z80_cycle += 8;

    return 0;
}

uint32_t z80_ix_iy_bit_instruction (uint16_t reg_ix_iy_w)
{
    /* Note: The displacement comes first, then the instruction */
    uint8_t displacement = memory_read (z80_regs.pc++);
    uint8_t instruction = memory_read (z80_regs.pc++);
    uint8_t data;
    uint8_t bit;
    bool write_data = true;
    uint8_t temp;


    /* All IX/IY bit instructions take one parameter */

    /* Read data */
    data = memory_read (reg_ix_iy_w + (int8_t) displacement);

    /* For bit/res/set, determine the bit to operate on */
    bit = 1 << ((instruction >> 3) & 0x07);

    switch (instruction & 0xf8)
    {
        case 0x00: /* RLC (ix+*) */ data = (data << 1) | ((data & 0x80) ? 0x01 : 0x00); SET_FLAGS_RLC (data); break;
        case 0x08: /* RRC (ix+*) */ data = (data >> 1) | (data << 7); SET_FLAGS_RRC (data); break;
        case 0x10: /* RL  (ix+*) */ temp = data;
                                    data = (data << 1) | ((z80_regs.f & Z80_FLAG_CARRY) ? 0x01 : 0x00); SET_FLAGS_RL (data);
                                    z80_regs.f |= (temp & 0x80) ? Z80_FLAG_CARRY : 0; break;
        case 0x18: /* RR  (ix+*) */ temp = data;
                                    data = (data >> 1) | ((z80_regs.f & Z80_FLAG_CARRY) ? 0x80 : 0x00); SET_FLAGS_RR (data);
                                    z80_regs.f |= (temp & 0x01) ? Z80_FLAG_CARRY : 0; break;

        case 0x20: /* SLA (ix+*) */ temp = data;
                                    data = (data << 1); SET_FLAGS_RL (data);
                                    z80_regs.f |= (temp & 0x80) ? Z80_FLAG_CARRY : 0; break;
        case 0x28: /* SRA (ix+*) */ temp = data;
                                    data = (data >> 1) | (data & 0x80); SET_FLAGS_RR (data);
                                    z80_regs.f |= (temp & 0x01) ? Z80_FLAG_CARRY : 0; break;

        case 0x30: /* SLL (ix+*) */ temp = data;
                                    data = (data << 1) | 0x01; SET_FLAGS_RL (data);
                                    z80_regs.f |= (temp & 0x80) ? Z80_FLAG_CARRY : 0; break;
        case 0x38: /* SRL (ix+*) */ temp = data;
                                    data = (data >> 1); SET_FLAGS_RR (data);
                                    z80_regs.f |= (temp & 0x01) ? Z80_FLAG_CARRY : 0; break;
        case 0x40: case 0x48: case 0x50: case 0x58: /* BIT */
        case 0x60: case 0x68: case 0x70: case 0x78:
            z80_regs.f = (z80_regs.f & Z80_FLAG_CARRY) |
                         ((bit & data) ? 0 : Z80_FLAG_PARITY) |
                         (Z80_FLAG_HALF) |
                         ((bit & data) ? 0 : Z80_FLAG_ZERO) |
                         (((bit == BIT_7) && (data & BIT_7)) ? Z80_FLAG_SIGN : 0);
            write_data = false; break;
        case 0x80: case 0x88: case 0x90: case 0x98: /* RES */
        case 0xa0: case 0xa8: case 0xb0: case 0xb8:
            data &= ~bit; break;
        case 0xc0: case 0xc8: case 0xd0: case 0xd8: /* SET */
        case 0xe0: case 0xe8: case 0xf0: case 0xf8:
            data |= bit; break;
        default:
            fprintf (stderr, "Unknown ix/iy bit instruction: \"%s\" (%02x). %" PRIu64 " instructions have been run.\n",
                     z80_instruction_name_bits[instruction], instruction, instruction_count);
            write_data = false;
            _abort_ = true;
    }

    /* Write data */
    if (write_data)
    {
        memory_write (reg_ix_iy_w + (int8_t) displacement, data);

        switch (instruction & 0x07)
        {
            case 0x00: z80_regs.b = data; break;
            case 0x01: z80_regs.c = data; break;
            case 0x02: z80_regs.d = data; break;
            case 0x03: z80_regs.e = data; break;
            case 0x04: z80_regs.h = data; break;
            case 0x05: z80_regs.l = data; break;
            case 0x07: z80_regs.a = data; break;
            default: break;
        }
    }

    /* TODO: Fill this out properly. Most instructions take longer */
    z80_cycle += 10;

    return 0;
}

uint32_t z80_instruction (void);

uint16_t z80_ix_iy_instruction (uint16_t reg_ix_iy_in)
{
    uint8_t instruction = memory_read (z80_regs.pc++);

    union {
        uint16_t w;
        struct {
            uint8_t l;
            uint8_t h;
        };
    } reg_ix_iy;

    union {
        uint16_t w;
        struct {
            uint8_t l;
            uint8_t h;
        };
    } param;

    uint8_t temp;

    reg_ix_iy.w = reg_ix_iy_in;

    switch (z80_instruction_size_ix[instruction])
    {
        case 3:
            param.l = memory_read (z80_regs.pc++);
            param.h = memory_read (z80_regs.pc++);
            break;
        case 2:
            param.l = memory_read (z80_regs.pc++);
            break;
        default:
            break;
    }

    switch (instruction)
    {
        case 0x09: /* ADD IX,BC    */ SET_FLAGS_ADD_16 (reg_ix_iy.w, z80_regs.bc); reg_ix_iy.w += z80_regs.bc; break;

        case 0x19: /* ADD IX,DE    */ SET_FLAGS_ADD_16 (reg_ix_iy.w, z80_regs.de); reg_ix_iy.w += z80_regs.de; break;

        case 0x21: /* LD IX,**     */ reg_ix_iy.w = param.w; break;
        case 0x22: /* LD (**),IX   */ memory_write (param.w,     reg_ix_iy.l);
                                      memory_write (param.w + 1, reg_ix_iy.h); break;
        case 0x23: /* INC IX       */ reg_ix_iy.w++; break;
        case 0x24: /* INC IXH      */ reg_ix_iy.h++; SET_FLAGS_INC (reg_ix_iy.h) break;
        case 0x25: /* DEC IXH      */ reg_ix_iy.h--; SET_FLAGS_DEC (reg_ix_iy.h) break;
        case 0x26: /* LD IXH,*     */ reg_ix_iy.h = param.l; break;
        case 0x29: /* ADD IX,IX    */ SET_FLAGS_ADD_16 (reg_ix_iy.w, reg_ix_iy.w); reg_ix_iy.w += reg_ix_iy.w; break;
        case 0x2a: /* LD IX,(**)   */ reg_ix_iy.l = memory_read (param.w);
                                      reg_ix_iy.h = memory_read (param.w + 1); break;
        case 0x2b: /* DEC IX       */ reg_ix_iy.w--; break;
        case 0x2c: /* INC IXL      */ reg_ix_iy.l++; SET_FLAGS_INC (reg_ix_iy.l) break;
        case 0x2d: /* DEC IXL      */ reg_ix_iy.l--; SET_FLAGS_DEC (reg_ix_iy.l) break;
        case 0x2e: /* LD IXL,*     */ reg_ix_iy.l = param.l; break;

        case 0x34: /* INC (IX+*)   */ temp = memory_read (reg_ix_iy.w + (int8_t) param.l);
                                      temp++; SET_FLAGS_INC (temp);
                                      memory_write (reg_ix_iy.w + (int8_t) param.l, temp); break;
        case 0x35: /* DEC (IX+*)   */ temp = memory_read (reg_ix_iy.w + (int8_t) param.l);
                                      temp--; SET_FLAGS_DEC (temp);
                                      memory_write (reg_ix_iy.w + (int8_t) param.l, temp); break;
        case 0x36: /* LD (IX+*),*  */ memory_write (reg_ix_iy.w + (int8_t) param.l, param.h); break;
        case 0x39: /* ADD IX,SP    */ SET_FLAGS_ADD_16 (reg_ix_iy.w, z80_regs.sp); reg_ix_iy.w += z80_regs.sp; break;

        case 0x40: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x41: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x42: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x43: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x44: /* LD B,IXH     */ z80_regs.b = reg_ix_iy.h; break;
        case 0x45: /* LD B,IXL     */ z80_regs.b = reg_ix_iy.l; break;
        case 0x46: /* LD B,(IX+*)  */ z80_regs.b = memory_read (reg_ix_iy.w + (int8_t) param.l); break;
        case 0x47: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x48: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x49: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x4a: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x4b: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x4c: /* LD C,IXH     */ z80_regs.c = reg_ix_iy.h; break;
        case 0x4d: /* LD C,IXL     */ z80_regs.c = reg_ix_iy.l; break;
        case 0x4e: /* LD C,(IX+*)  */ z80_regs.c = memory_read (reg_ix_iy.w + (int8_t) param.l); break;
        case 0x4f: /* -            */ z80_regs.pc--; z80_instruction (); break;

        case 0x50: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x51: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x52: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x53: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x54: /* LD D,IXH     */ z80_regs.d = reg_ix_iy.h; break;
        case 0x55: /* LD D,IXL     */ z80_regs.d = reg_ix_iy.l; break;
        case 0x56: /* LD D,(IX+*)  */ z80_regs.d = memory_read (reg_ix_iy.w + (int8_t) param.l); break;
        case 0x57: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x58: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x59: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x5a: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x5b: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x5c: /* LD E,IXH     */ z80_regs.e = reg_ix_iy.h; break;
        case 0x5d: /* LD E,IXL     */ z80_regs.e = reg_ix_iy.l; break;
        case 0x5e: /* LD E,(IX+*)  */ z80_regs.e = memory_read (reg_ix_iy.w + (int8_t) param.l); break;
        case 0x5f: /* -            */ z80_regs.pc--; z80_instruction (); break;

        case 0x60: /* LD IXH,B     */ reg_ix_iy.h = z80_regs.b; break;
        case 0x61: /* LD IXH,C     */ reg_ix_iy.h = z80_regs.c; break;
        case 0x62: /* LD IXH,D     */ reg_ix_iy.h = z80_regs.d; break;
        case 0x63: /* LD IXH,E     */ reg_ix_iy.h = z80_regs.e; break;
        case 0x64: /* LD IXH,IXH   */ reg_ix_iy.h = reg_ix_iy.h; break;
        case 0x65: /* LD IXH,IXL   */ reg_ix_iy.h = reg_ix_iy.l; break;
        case 0x66: /* LD H,(IX+*)  */ z80_regs.h = memory_read (reg_ix_iy.w + (int8_t) param.l); break;
        case 0x67: /* LD IXH,A     */ reg_ix_iy.h = z80_regs.a; break;
        case 0x68: /* LD IXH,B     */ reg_ix_iy.l = z80_regs.b; break;
        case 0x69: /* LD IXH,C     */ reg_ix_iy.l = z80_regs.c; break;
        case 0x6a: /* LD IXH,D     */ reg_ix_iy.l = z80_regs.d; break;
        case 0x6b: /* LD IXH,E     */ reg_ix_iy.l = z80_regs.e; break;
        case 0x6c: /* LD IXL,IXH   */ reg_ix_iy.l = reg_ix_iy.h; break;
        case 0x6d: /* LD IXL,IXL   */ reg_ix_iy.l = reg_ix_iy.l; break;
        case 0x6e: /* LD L,(IX+*)  */ z80_regs.l = memory_read (reg_ix_iy.w + (int8_t) param.l); break;
        case 0x6f: /* LD IXL,A     */ reg_ix_iy.l = z80_regs.a; break;

        case 0x70: /* LD (IX+*),B  */ memory_write (reg_ix_iy.w + (int8_t) param.l, z80_regs.b); break;
        case 0x71: /* LD (IX+*),C  */ memory_write (reg_ix_iy.w + (int8_t) param.l, z80_regs.c); break;
        case 0x72: /* LD (IX+*),D  */ memory_write (reg_ix_iy.w + (int8_t) param.l, z80_regs.d); break;
        case 0x73: /* LD (IX+*),E  */ memory_write (reg_ix_iy.w + (int8_t) param.l, z80_regs.e); break;
        case 0x74: /* LD (IX+*),H  */ memory_write (reg_ix_iy.w + (int8_t) param.l, z80_regs.h); break;
        case 0x75: /* LD (IX+*),L  */ memory_write (reg_ix_iy.w + (int8_t) param.l, z80_regs.l); break;
        case 0x77: /* LD (IX+*),A  */ memory_write (reg_ix_iy.w + (int8_t) param.l, z80_regs.a); break;
        case 0x78: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x79: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x7a: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x7b: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x7c: /* LD A,IXH     */ z80_regs.a = reg_ix_iy.h; break;
        case 0x7d: /* LD A,IXL     */ z80_regs.a = reg_ix_iy.l; break;
        case 0x7e: /* LD A,(IX+*)  */ z80_regs.a = memory_read (reg_ix_iy.w + (int8_t) param.l); break;
        case 0x7f: /* -            */ z80_regs.pc--; z80_instruction (); break;

        case 0x84: /* ADD A,IXH    */ SET_FLAGS_ADD (reg_ix_iy.h); z80_regs.a += reg_ix_iy.h; break;
        case 0x85: /* ADD A,IXL    */ SET_FLAGS_ADD (reg_ix_iy.l); z80_regs.a += reg_ix_iy.l; break;
        case 0x86: /* ADD A,(IX+*) */ temp = memory_read (reg_ix_iy.w + (int8_t) param.l);
                                      SET_FLAGS_ADD (temp); z80_regs.a += temp; break;

        case 0x8c: /* ADC A,IXH    */ SET_FLAGS_ADC (reg_ix_iy.h);
                                      z80_regs.a +=  reg_ix_iy.h + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
        case 0x8d: /* ADC A,IXL    */ SET_FLAGS_ADC (reg_ix_iy.l);
                                      z80_regs.a +=  reg_ix_iy.l + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
        case 0x8e: /* ADC A,(IX+*) */ temp = memory_read (reg_ix_iy.w + (int8_t) param.l);
                                      SET_FLAGS_ADC (temp);
                                      z80_regs.a +=  temp + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;

        case 0x94: /* SUB A,IXH    */ SET_FLAGS_SUB (reg_ix_iy.h); z80_regs.a -= reg_ix_iy.h; break;
        case 0x95: /* SUB A,IXL    */ SET_FLAGS_SUB (reg_ix_iy.l); z80_regs.a -= reg_ix_iy.l; break;
        case 0x96: /* SUB A,(IX+*) */ temp = memory_read (reg_ix_iy.w + (int8_t) param.l);
                                      SET_FLAGS_SUB (temp); z80_regs.a -= temp; break;
        case 0x9c: /* SBC A,IXH    */ SET_FLAGS_SBC (reg_ix_iy.h);
                                      z80_regs.a -=  reg_ix_iy.h + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
        case 0x9d: /* SBC A,IXL    */ SET_FLAGS_SBC (reg_ix_iy.l);
                                      z80_regs.a -=  reg_ix_iy.l + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
        case 0x9e: /* SBC A,(IX+*) */ temp = memory_read (reg_ix_iy.w + (int8_t) param.l);
                                      SET_FLAGS_SBC (temp);
                                      z80_regs.a -=  temp + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;

        case 0xa4: /* AND A,IXH    */ z80_regs.a &= reg_ix_iy.h; SET_FLAGS_AND; break;
        case 0xa5: /* AND A,IXL    */ z80_regs.a &= reg_ix_iy.l; SET_FLAGS_AND; break;
        case 0xa6: /* AND A,(IX+*) */ z80_regs.a &= memory_read (reg_ix_iy.w + (int8_t) param.l);
                                      SET_FLAGS_AND; break;
        case 0xac: /* XOR A,IXH    */ z80_regs.a ^= reg_ix_iy.h; SET_FLAGS_OR_XOR; break;
        case 0xad: /* XOR A,IXL    */ z80_regs.a ^= reg_ix_iy.l; SET_FLAGS_OR_XOR; break;
        case 0xae: /* XOR A,(IX+*) */ z80_regs.a ^= memory_read (reg_ix_iy.w + (int8_t) param.l);
                                      SET_FLAGS_OR_XOR; break;

        case 0xb4: /* OR  A,IXH    */ z80_regs.a |= reg_ix_iy.h; SET_FLAGS_OR_XOR; break;
        case 0xb5: /* OR  A,IXL    */ z80_regs.a |= reg_ix_iy.l; SET_FLAGS_OR_XOR; break;
        case 0xb6: /* OR  A,(IX+*) */ z80_regs.a |= memory_read (reg_ix_iy.w + (int8_t) param.l);
                                      SET_FLAGS_OR_XOR; break;
        case 0xbc: /* CP  A,IXH    */ SET_FLAGS_SUB (reg_ix_iy.h); break;
        case 0xbd: /* CP  A,IXL    */ SET_FLAGS_SUB (reg_ix_iy.l); break;
        case 0xbe: /* CP  A,(IX+*) */ temp = memory_read (reg_ix_iy.w + (int8_t) param.l);
                                      SET_FLAGS_SUB (temp); break;

        case 0xcb: /* IX Bit Instructions */ z80_ix_iy_bit_instruction (reg_ix_iy.w); break;
        case 0xe1: /* POP IX       */ reg_ix_iy.l = memory_read (z80_regs.sp++);
                                      reg_ix_iy.h = memory_read (z80_regs.sp++); break;
        case 0xe5: /* PUSH IX      */ memory_write (--z80_regs.sp, reg_ix_iy.h);
                                      memory_write (--z80_regs.sp, reg_ix_iy.l); break;
        default:
        fprintf (stderr, "Unknown ix/iy instruction: \"%s\" (%02x). %" PRIu64 " instructions have been run.\n",
                 z80_instruction_name_ix[instruction], instruction, instruction_count);
        _abort_ = true;
    }

    /* TODO: Fill this out correctly. Most instructions take more time */
    z80_cycle += 10;

    return reg_ix_iy.w;
}

uint32_t z80_bit_instruction ()
{
    uint8_t instruction = memory_read (z80_regs.pc++);
    uint8_t data;
    uint8_t temp;
    uint8_t bit;
    bool write_data = true;

    /* Read data */
    switch (instruction & 0x07)
    {
        case 0x00: data = z80_regs.b; break;
        case 0x01: data = z80_regs.c; break;
        case 0x02: data = z80_regs.d; break;
        case 0x03: data = z80_regs.e; break;
        case 0x04: data = z80_regs.h; break;
        case 0x05: data = z80_regs.l; break;
        case 0x06: data = memory_read (z80_regs.hl); break;
        case 0x07: data = z80_regs.a; break;
    }

    /* For bit/res/set, determine the bit to operate on */
    bit = 1 << ((instruction >> 3) & 0x07);

    switch (instruction & 0xf8)
    {
        case 0x00: /* RLC X */ data = (data << 1) | (data >> 7); SET_FLAGS_RLC (data); break;
        case 0x08: /* RRC X */ data = (data >> 1) | (data << 7); SET_FLAGS_RRC (data); break;

        case 0x10: /* RL  X */ temp = data;
                               data = (data << 1) | ((z80_regs.f & Z80_FLAG_CARRY) ? 0x01 : 0x00); SET_FLAGS_RL (data);
                               z80_regs.f |= (temp & 0x80) ? Z80_FLAG_CARRY : 0; break;
        case 0x18: /* RR  X */ temp = data;
                               data = (data >> 1) | ((z80_regs.f & Z80_FLAG_CARRY) ? 0x80 : 0x00); SET_FLAGS_RR (data);
                               z80_regs.f |= (temp & 0x01) ? Z80_FLAG_CARRY : 0; break;

        case 0x20: /* SLA X */ temp = data;
                               data = (data << 1); SET_FLAGS_RL (data);
                               z80_regs.f |= (temp & 0x80) ? Z80_FLAG_CARRY : 0; break;
        case 0x28: /* SRA X */ temp = data;
                               data = (data >> 1) | (data & 0x80); SET_FLAGS_RR (data);
                               z80_regs.f |= (temp & 0x01) ? Z80_FLAG_CARRY : 0; break;

        case 0x30: /* SLL X */ temp = data;
                               data = (data << 1) | 0x01; SET_FLAGS_RL (data);
                               z80_regs.f |= (temp & 0x80) ? Z80_FLAG_CARRY : 0; break;
        case 0x38: /* SRL X */ temp = data;
                               data = (data >> 1); SET_FLAGS_RR (data);
                               z80_regs.f |= (temp & 0x01) ? Z80_FLAG_CARRY : 0; break;

        case 0x40: case 0x48: case 0x50: case 0x58: /* BIT */
        case 0x60: case 0x68: case 0x70: case 0x78:
            z80_regs.f = (z80_regs.f & Z80_FLAG_CARRY) |
                         ((bit & data) ? 0 : Z80_FLAG_PARITY) |
                         (Z80_FLAG_HALF) |
                         ((bit & data) ? 0 : Z80_FLAG_ZERO) |
                         (((bit == BIT_7) && (data & BIT_7)) ? Z80_FLAG_SIGN : 0);
            write_data = false;
            break;

        case 0x80: case 0x88: case 0x90: case 0x98: /* RES */
        case 0xa0: case 0xa8: case 0xb0: case 0xb8:
            data &= ~bit; break;

        case 0xc0: case 0xc8: case 0xd0: case 0xd8: /* SET */
        case 0xe0: case 0xe8: case 0xf0: case 0xf8:
            data |= bit; break;

        default:
            fprintf (stderr, "Unknown bit instruction: \"%s\" (%02x). %" PRIu64 " instructions have been run.\n",
                     z80_instruction_name_bits[instruction], instruction, instruction_count);
            _abort_ = true;
    }

    /* Write data */
    if (write_data)
    {
        switch (instruction & 0x07)
        {
            case 0x00: z80_regs.b = data; break;
            case 0x01: z80_regs.c = data; break;
            case 0x02: z80_regs.d = data; break;
            case 0x03: z80_regs.e = data; break;
            case 0x04: z80_regs.h = data; break;
            case 0x05: z80_regs.l = data; break;
            case 0x06: memory_write (z80_regs.hl, data); break;
            case 0x07: z80_regs.a = data; break;
        }
    }

    /* TODO: Fill this out properly. (hl) instructions take longer */
    z80_cycle += 8;

    return 0;
}

void z80_instruction_daa ()
{   /* TODO: Set flags */
    if (!(z80_regs.f & Z80_FLAG_SUB))
    {
        if      (!(z80_regs.f & Z80_FLAG_CARRY) && (z80_regs.a & 0xf0 <= 0x90) && !(z80_regs.f & Z80_FLAG_HALF) && (0x0f <= 0x09))
        {
            /* Nothing to do */
        }
        else if (!(z80_regs.f & Z80_FLAG_CARRY) && (z80_regs.a & 0xf0 <= 0x80) && !(z80_regs.f & Z80_FLAG_HALF) && (0x0f >= 0x0A))
        {
            z80_regs.a += 0x06;
        }
        else if (!(z80_regs.f & Z80_FLAG_CARRY) && (z80_regs.a & 0xf0 <= 0x90) &&  (z80_regs.f & Z80_FLAG_HALF) && (0x0f <= 0x03))
        {
            z80_regs.a += 0x06;
        }
        else if (!(z80_regs.f & Z80_FLAG_CARRY) && (z80_regs.a & 0xf0 >= 0xA0) && !(z80_regs.f & Z80_FLAG_HALF) && (0x0f <= 0x09))
        {
            z80_regs.a += 0x60;
            z80_regs.f |= Z80_FLAG_CARRY;
        }
        else if (!(z80_regs.f & Z80_FLAG_CARRY) && (z80_regs.a & 0xf0 >= 0x90) && !(z80_regs.f & Z80_FLAG_HALF) && (0x0f >= 0x0A))
        {
            z80_regs.a += 0x66;
            z80_regs.f |= Z80_FLAG_CARRY;
        }
        else if (!(z80_regs.f & Z80_FLAG_CARRY) && (z80_regs.a & 0xf0 >= 0xA0) &&  (z80_regs.f & Z80_FLAG_HALF) && (0x0f <= 0x03))
        {
            z80_regs.a += 0x66;
            z80_regs.f |= Z80_FLAG_CARRY;
        }
        else if ( (z80_regs.f & Z80_FLAG_CARRY) && (z80_regs.a & 0xf0 <= 0x20) && !(z80_regs.f & Z80_FLAG_HALF) && (0x0f <= 0x09))
        {
            z80_regs.a += 0x60;
            z80_regs.f |= Z80_FLAG_CARRY;
        }
        else if ( (z80_regs.f & Z80_FLAG_CARRY) && (z80_regs.a & 0xf0 <= 0x20) && !(z80_regs.f & Z80_FLAG_HALF) && (0x0f >= 0x0A))
        {
            z80_regs.a += 0x66;
            z80_regs.f |= Z80_FLAG_CARRY;
        }
        else if ( (z80_regs.f & Z80_FLAG_CARRY) && (z80_regs.a & 0xf0 <= 0x30) &&  (z80_regs.f & Z80_FLAG_HALF) && (0x0f <= 0x03))
        {
            z80_regs.a += 0x66;
            z80_regs.f |= Z80_FLAG_CARRY;
        }
    }
    else
    {
        if      (!(z80_regs.f & Z80_FLAG_CARRY) && (z80_regs.a & 0xf0 <= 0x90) && !(z80_regs.f & Z80_FLAG_HALF) && (0x0f <= 0x09))
        {
            /* Nothing to do */
        }
        else if (!(z80_regs.f & Z80_FLAG_CARRY) && (z80_regs.a & 0xf0 <= 0x80) &&  (z80_regs.f & Z80_FLAG_HALF) && (0x0f >= 0x06))
        {
            z80_regs.a += 0xfa;
        }
        else if ( (z80_regs.f & Z80_FLAG_CARRY) && (z80_regs.a & 0xf0 >= 0x70) && !(z80_regs.f & Z80_FLAG_HALF) && (0x0f <= 0x09))
        {
            z80_regs.a += 0xa0;
            z80_regs.f |= Z80_FLAG_CARRY;
        }
        else if ( (z80_regs.f & Z80_FLAG_CARRY) && ((z80_regs.a & 0xf0 == 0x60) ||
                                                  (z80_regs.a & 0xf0 == 0x70)) &&  (z80_regs.f & Z80_FLAG_HALF) && (0x0f >= 0x06))
        {
            z80_regs.a += 0x9a;
            z80_regs.f |= Z80_FLAG_CARRY;
        }
    }
}

uint32_t z80_instruction ()
{
    uint8_t instruction;
    uint8_t temp;

    union {
        uint16_t w;
        struct {
            uint8_t l;
            uint8_t h;
        };
    } param;

    /* Fetch */
    instruction = memory_read (z80_regs.pc++);

    switch (z80_instruction_size[instruction])
    {
        case 3:
            param.l = memory_read (z80_regs.pc++);
            param.h = memory_read (z80_regs.pc++);
#ifdef DEBUG
            if (debug_instruction) fprintf (stdout, "[DEBUG]: PC=%04x,  INST=%02x %02x %02x,  %-12s"
                                                            " [af=%04x, bc=%04x, de=%04x, hl=%04x]\n",
                 z80_regs.pc-3, instruction, param.l, param.h,
                 z80_instruction_name[instruction],
                 z80_regs.af, z80_regs.bc, z80_regs.de, z80_regs.hl);
#endif
            break;
        case 2:
            param.l = memory_read (z80_regs.pc++);
#ifdef DEBUG
            if (debug_instruction) fprintf (stdout, "[DEBUG]: PC=%04x,  INST=%02x %02x,     %-12s"
                                                            " [af=%04x, bc=%04x, de=%04x, hl=%04x]\n",
                 z80_regs.pc-2, instruction, param.l,
                 z80_instruction_name[instruction],
                 z80_regs.af, z80_regs.bc, z80_regs.de, z80_regs.hl);
#endif
            break;
        default:
#ifdef DEBUG
            if (debug_instruction) fprintf (stdout, "[DEBUG]: PC=%04x,  INST=%02x,        %-12s"
                                                            " [af=%04x, bc=%04x, de=%04x, hl=%04x]\n",
                 z80_regs.pc-1, instruction,
                 z80_instruction_name[instruction],
                 z80_regs.af, z80_regs.bc, z80_regs.de, z80_regs.hl);
#endif
            break;
    }

    switch (instruction)
    {
        case 0x00: /* NOP        */ break;
        case 0x01: /* LD BC,**   */ z80_regs.bc = param.w; break;
        case 0x02: /* LD (BC),A  */ memory_write (z80_regs.bc, z80_regs.a); break;
        case 0x03: /* INC BC     */ z80_regs.bc++; break;
        case 0x04: /* INC B      */ z80_regs.b++; SET_FLAGS_INC (z80_regs.b); break;
        case 0x05: /* DEC B      */ z80_regs.b--; SET_FLAGS_DEC (z80_regs.b); break;
        case 0x06: /* LD B,*     */ z80_regs.b = param.l; break;
        case 0x07: /* RLCA       */ z80_regs.a = (z80_regs.a << 1) | ((z80_regs.a & 0x80) ? 1 : 0); SET_FLAGS_RLCA (z80_regs.a); break;
        case 0x08: /* EX AF AF'  */ SWAP (uint16_t, z80_regs.af, z80_regs.alt_af); break;
        case 0x09: /* ADD HL,BC  */ SET_FLAGS_ADD_16 (z80_regs.hl, z80_regs.bc); z80_regs.hl += z80_regs.bc; break;
        case 0x0a: /* LD A,(BC)  */ z80_regs.a = memory_read (z80_regs.bc); break;
        case 0x0b: /* DEC BC     */ z80_regs.bc--; break;
        case 0x0c: /* INC C      */ z80_regs.c++; SET_FLAGS_INC (z80_regs.c); break;
        case 0x0d: /* DEC C      */ z80_regs.c--; SET_FLAGS_DEC (z80_regs.c); break;
        case 0x0e: /* LD C,*     */ z80_regs.c = param.l; break;
        case 0x0f: /* RRCA       */ z80_regs.a = (z80_regs.a >> 1) | ((z80_regs.a & 0x01) ? 0x80 : 0);
                                    z80_regs.f = (z80_regs.f & (Z80_FLAG_PARITY | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) |
                                                 ((z80_regs.a & 0x80) ? Z80_FLAG_CARRY : 0); break;

        case 0x10: /* DJNZ       */ if (--z80_regs.b)
                                    {
                                        z80_regs.pc += (int8_t) param.l;
                                        z80_cycle += 13;
                                    }
                                    else
                                    {
                                        z80_cycle += 8;
                                    }
                                    break;
        case 0x11: /* LD DE,**   */ z80_regs.de = param.w; break;
        case 0x12: /* LD (DE),a  */ memory_write (z80_regs.de, z80_regs.a); break;
        case 0x13: /* INC DE     */ z80_regs.de++; break;
        case 0x14: /* INC D      */ z80_regs.d++; SET_FLAGS_INC (z80_regs.d); break;
        case 0x15: /* DEC D      */ z80_regs.d--; SET_FLAGS_DEC (z80_regs.d); break;
        case 0x16: /* LD D,*     */ z80_regs.d = param.l; break;
        case 0x17: /* RLA        */ temp = z80_regs.a;
                                    z80_regs.a = (z80_regs.a << 1) + ((z80_regs.f & Z80_FLAG_CARRY) ? 0x01 : 0);
                                    z80_regs.f = (z80_regs.f & (Z80_FLAG_PARITY | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) |
                                                 ((temp & 0x80) ? Z80_FLAG_CARRY : 0); break;
        case 0x18: /* JR         */ z80_regs.pc += (int8_t)param.l; break;
        case 0x19: /* ADD HL,DE  */ SET_FLAGS_ADD_16 (z80_regs.hl, z80_regs.de); z80_regs.hl += z80_regs.de; break;
        case 0x1a: /* LD A,(DE)  */ z80_regs.a = memory_read (z80_regs.de); break;
        case 0x1b: /* DEC DE     */ z80_regs.de--; break;
        case 0x1c: /* INC E      */ z80_regs.e++; SET_FLAGS_INC (z80_regs.e); break;
        case 0x1d: /* DEC E      */ z80_regs.e--; SET_FLAGS_DEC (z80_regs.e); break;
        case 0x1e: /* LD E,*     */ z80_regs.e = param.l; break;
        case 0x1f: /* RRA        */ temp = z80_regs.a;
                                    z80_regs.a = (z80_regs.a >> 1) + ((z80_regs.f & Z80_FLAG_CARRY) ? 0x80 : 0);
                                    z80_regs.f = (z80_regs.f & (Z80_FLAG_PARITY | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) |
                                                 ((temp & 0x01) ? Z80_FLAG_CARRY : 0); break;

        case 0x20: /* JR NZ      */ if (z80_regs.f & Z80_FLAG_ZERO)
                                    {
                                        z80_cycle += 7;
                                    }
                                    else
                                    {
                                        z80_regs.pc += (int8_t) param.l;
                                        z80_cycle += 12;
                                    }
                                    break;
        case 0x21: /* LD HL,**   */ z80_regs.hl = param.w; break;
        case 0x22: /* LD (**),HL */ memory_write (param.w,     z80_regs.l);
                                    memory_write (param.w + 1, z80_regs.h); break;
        case 0x23: /* INC HL     */ z80_regs.hl++; break;
        case 0x24: /* INC H      */ z80_regs.h++; SET_FLAGS_INC (z80_regs.h); break;
        case 0x25: /* DEC H      */ z80_regs.h--; SET_FLAGS_DEC (z80_regs.h); break;
        case 0x26: /* LD H,*     */ z80_regs.h = param.l; break;
        case 0x27: /* DAA        */ z80_instruction_daa (); break;
        case 0x28: /* JR Z       */ if (z80_regs.f & Z80_FLAG_ZERO)
                                    {
                                        z80_regs.pc += (int8_t) param.l;
                                        z80_cycle += 12;
                                    }
                                    else
                                    {
                                        z80_cycle += 7;
                                    }
                                    break;
        case 0x29: /* ADD HL,HL  */ SET_FLAGS_ADD_16 (z80_regs.hl, z80_regs.hl); z80_regs.hl += z80_regs.hl; break;
        case 0x2a: /* LD HL,(**) */ z80_regs.l = memory_read (param.w);
                                    z80_regs.h = memory_read (param.w + 1); break;
        case 0x2b: /* DEC HL     */ z80_regs.hl--; break;
        case 0x2c: /* INC L      */ z80_regs.l++; SET_FLAGS_INC (z80_regs.l); break;
        case 0x2d: /* DEC L      */ z80_regs.l--; SET_FLAGS_DEC (z80_regs.l); break;
        case 0x2e: /* LD L,*     */ z80_regs.l = param.l; break;
        case 0x2f: /* CPL        */ z80_regs.a = ~z80_regs.a; SET_FLAGS_CPL; break;

        case 0x30: /* JR NC      */ if (z80_regs.f & Z80_FLAG_CARRY)
                                    {
                                        z80_cycle += 7;
                                    }
                                    else
                                    {
                                        z80_regs.pc += (int8_t) param.l;
                                        z80_cycle += 12;
                                    }
                                    break;
        case 0x31: /* LD SP,**   */ z80_regs.sp = param.w; break;
        case 0x32: /* LD (**),A  */ memory_write (param.w, z80_regs.a); break;
        case 0x33: /* INC SP     */ z80_regs.sp++; break;
        case 0x34: /* INC (HL)   */ temp = memory_read (z80_regs.hl);
                                    temp++;
                                    memory_write (z80_regs.hl, temp);
                                    SET_FLAGS_INC (temp); break;
        case 0x35: /* DEC (HL)   */ temp = memory_read (z80_regs.hl);
                                    temp--;
                                    memory_write (z80_regs.hl, temp);
                                    SET_FLAGS_DEC (temp); break;
        case 0x36: /* LD (HL),*  */ memory_write (z80_regs.hl, param.l); break;
        case 0x37: /* SCF        */ z80_regs.f |= Z80_FLAG_CARRY; break;
        case 0x38: /* JR C,*     */ if (z80_regs.f & Z80_FLAG_CARRY)
                                    {
                                        z80_regs.pc += (int8_t) param.l;
                                        z80_cycle += 12;
                                    }
                                    else
                                    {
                                        z80_cycle += 7;
                                    }
                                    break;
        case 0x39: /* ADD HL,SP  */ SET_FLAGS_ADD_16 (z80_regs.hl, z80_regs.sp); z80_regs.hl += z80_regs.sp; break;
        case 0x3a: /* LD A,(**)  */ z80_regs.a = memory_read (param.w); break;
        case 0x3b: /* DEC SP     */ z80_regs.sp--; break;
        case 0x3c: /* INC A      */ z80_regs.a++; SET_FLAGS_INC (z80_regs.a); break;
        case 0x3d: /* DEC A      */ z80_regs.a--; SET_FLAGS_DEC (z80_regs.a); break;
        case 0x3e: /* LD A,*     */ z80_regs.a = param.l; break;
        case 0x3f: /* CCF        */ z80_regs.f = (z80_regs.f & (Z80_FLAG_SIGN | Z80_FLAG_ZERO | Z80_FLAG_PARITY)) |
                                                 ((z80_regs.f & Z80_FLAG_CARRY) ? Z80_FLAG_HALF : Z80_FLAG_CARRY);
                                    break;

        case 0x40: /* LD B,B     */ z80_regs.b = z80_regs.b; break;
        case 0x41: /* LD B,C     */ z80_regs.b = z80_regs.c; break;
        case 0x42: /* LD B,D     */ z80_regs.b = z80_regs.d; break;
        case 0x43: /* LD B,E     */ z80_regs.b = z80_regs.e; break;
        case 0x44: /* LD B,H     */ z80_regs.b = z80_regs.h; break;
        case 0x45: /* LD B,L     */ z80_regs.b = z80_regs.l; break;
        case 0x46: /* LD B,(HL)  */ z80_regs.b = memory_read(z80_regs.hl); break;
        case 0x47: /* LD B,A     */ z80_regs.b = z80_regs.a; break;

        case 0x48: /* LD C,B     */ z80_regs.c = z80_regs.b; break;
        case 0x49: /* LD C,C     */ z80_regs.c = z80_regs.c; break;
        case 0x4a: /* LD C,D     */ z80_regs.c = z80_regs.d; break;
        case 0x4b: /* LD C,E     */ z80_regs.c = z80_regs.e; break;
        case 0x4c: /* LD C,H     */ z80_regs.c = z80_regs.h; break;
        case 0x4d: /* LD C,L     */ z80_regs.c = z80_regs.l; break;
        case 0x4e: /* LD C,(HL)  */ z80_regs.c = memory_read(z80_regs.hl); break;
        case 0x4f: /* LD C,A     */ z80_regs.c = z80_regs.a; break;

        case 0x50: /* LD D,B     */ z80_regs.d = z80_regs.b; break;
        case 0x51: /* LD D,C     */ z80_regs.d = z80_regs.c; break;
        case 0x52: /* LD D,D     */ z80_regs.d = z80_regs.d; break;
        case 0x53: /* LD D,E     */ z80_regs.d = z80_regs.e; break;
        case 0x54: /* LD D,H     */ z80_regs.d = z80_regs.h; break;
        case 0x55: /* LD D,L     */ z80_regs.d = z80_regs.l; break;
        case 0x56: /* LD D,(HL)  */ z80_regs.d = memory_read(z80_regs.hl); break;
        case 0x57: /* LD D,A     */ z80_regs.d = z80_regs.a; break;

        case 0x58: /* LD E,B     */ z80_regs.e = z80_regs.b; break;
        case 0x59: /* LD E,C     */ z80_regs.e = z80_regs.c; break;
        case 0x5a: /* LD E,D     */ z80_regs.e = z80_regs.d; break;
        case 0x5b: /* LD E,E     */ z80_regs.e = z80_regs.e; break;
        case 0x5c: /* LD E,H     */ z80_regs.e = z80_regs.h; break;
        case 0x5d: /* LD E,L     */ z80_regs.e = z80_regs.l; break;
        case 0x5e: /* LD E,(HL)  */ z80_regs.e = memory_read(z80_regs.hl); break;
        case 0x5f: /* LD E,A     */ z80_regs.e = z80_regs.a; break;

        case 0x60: /* LD H,B     */ z80_regs.h = z80_regs.b; break;
        case 0x61: /* LD H,C     */ z80_regs.h = z80_regs.c; break;
        case 0x62: /* LD H,D     */ z80_regs.h = z80_regs.d; break;
        case 0x63: /* LD H,E     */ z80_regs.h = z80_regs.e; break;
        case 0x64: /* LD H,H     */ z80_regs.h = z80_regs.h; break;
        case 0x65: /* LD H,L     */ z80_regs.h = z80_regs.l; break;
        case 0x66: /* LD H,(HL)  */ z80_regs.h = memory_read(z80_regs.hl); break;
        case 0x67: /* LD H,A     */ z80_regs.h = z80_regs.a; break;

        case 0x68: /* LD L,B     */ z80_regs.l = z80_regs.b; break;
        case 0x69: /* LD L,C     */ z80_regs.l = z80_regs.c; break;
        case 0x6a: /* LD L,D     */ z80_regs.l = z80_regs.d; break;
        case 0x6b: /* LD L,E     */ z80_regs.l = z80_regs.e; break;
        case 0x6c: /* LD L,H     */ z80_regs.l = z80_regs.h; break;
        case 0x6d: /* LD L,L     */ z80_regs.l = z80_regs.l; break;
        case 0x6e: /* LD L,(HL)  */ z80_regs.l = memory_read(z80_regs.hl); break;
        case 0x6f: /* LD L,A     */ z80_regs.l = z80_regs.a; break;

        case 0x70: /* LD (HL),B  */ memory_write (z80_regs.hl, z80_regs.b); break;
        case 0x71: /* LD (HL),C  */ memory_write (z80_regs.hl, z80_regs.c); break;
        case 0x72: /* LD (HL),D  */ memory_write (z80_regs.hl, z80_regs.d); break;
        case 0x73: /* LD (HL),E  */ memory_write (z80_regs.hl, z80_regs.e); break;
        case 0x74: /* LD (HL),H  */ memory_write (z80_regs.hl, z80_regs.h); break;
        case 0x75: /* LD (HL),L  */ memory_write (z80_regs.hl, z80_regs.l); break;
        case 0x77: /* LD (HL),A  */ memory_write (z80_regs.hl, z80_regs.a); break;

        case 0x78: /* LD A,B     */ z80_regs.a = z80_regs.b; break;
        case 0x79: /* LD A,C     */ z80_regs.a = z80_regs.c; break;
        case 0x7a: /* LD A,D     */ z80_regs.a = z80_regs.d; break;
        case 0x7b: /* LD A,E     */ z80_regs.a = z80_regs.e; break;
        case 0x7c: /* LD A,H     */ z80_regs.a = z80_regs.h; break;
        case 0x7d: /* LD A,L     */ z80_regs.a = z80_regs.l; break;
        case 0x7e: /* LD A,(HL)  */ z80_regs.a = memory_read(z80_regs.hl); break;
        case 0x7f: /* LD A,A     */ z80_regs.a = z80_regs.a; break;

        case 0x80: /* ADD A,B    */ SET_FLAGS_ADD (z80_regs.b); z80_regs.a += z80_regs.b; break;
        case 0x81: /* ADD A,C    */ SET_FLAGS_ADD (z80_regs.c); z80_regs.a += z80_regs.c; break;
        case 0x82: /* ADD A,D    */ SET_FLAGS_ADD (z80_regs.d); z80_regs.a += z80_regs.d; break;
        case 0x83: /* ADD A,E    */ SET_FLAGS_ADD (z80_regs.e); z80_regs.a += z80_regs.e; break;
        case 0x84: /* ADD A,H    */ SET_FLAGS_ADD (z80_regs.h); z80_regs.a += z80_regs.h; break;
        case 0x85: /* ADD A,L    */ SET_FLAGS_ADD (z80_regs.l); z80_regs.a += z80_regs.l; break;
        case 0x86: /* ADD A,(HL) */ temp = memory_read (z80_regs.hl);
                                    SET_FLAGS_ADD (temp); z80_regs.a += temp; break;
        case 0x87: /* ADD A,A    */ SET_FLAGS_ADD (z80_regs.a); z80_regs.a += z80_regs.a; break;

        case 0x88: /* ADC A,B    */ SET_FLAGS_ADC (z80_regs.b); z80_regs.a += z80_regs.b + CARRY_BIT; break;
        case 0x89: /* ADC A,C    */ SET_FLAGS_ADC (z80_regs.c); z80_regs.a += z80_regs.c + CARRY_BIT; break;
        case 0x8a: /* ADC A,D    */ SET_FLAGS_ADC (z80_regs.d); z80_regs.a += z80_regs.d + CARRY_BIT; break;
        case 0x8b: /* ADC A,E    */ SET_FLAGS_ADC (z80_regs.e); z80_regs.a += z80_regs.e + CARRY_BIT; break;
        case 0x8c: /* ADC A,H    */ SET_FLAGS_ADC (z80_regs.h); z80_regs.a += z80_regs.h + CARRY_BIT; break;
        case 0x8d: /* ADC A,L    */ SET_FLAGS_ADC (z80_regs.l); z80_regs.a += z80_regs.l + CARRY_BIT; break;
        case 0x8e: /* ADC A,(HL) */ temp = memory_read (z80_regs.hl);
                                    SET_FLAGS_ADC (temp); z80_regs.a += temp + CARRY_BIT; break;
        case 0x8f: /* ADC A,A    */ SET_FLAGS_ADC (z80_regs.a); z80_regs.a += z80_regs.a + CARRY_BIT; break;

        case 0x90: /* SUB A,B    */ SET_FLAGS_SUB (z80_regs.b); z80_regs.a -= z80_regs.b; break;
        case 0x91: /* SUB A,C    */ SET_FLAGS_SUB (z80_regs.c); z80_regs.a -= z80_regs.c; break;
        case 0x92: /* SUB A,D    */ SET_FLAGS_SUB (z80_regs.d); z80_regs.a -= z80_regs.d; break;
        case 0x93: /* SUB A,E    */ SET_FLAGS_SUB (z80_regs.e); z80_regs.a -= z80_regs.e; break;
        case 0x94: /* SUB A,H    */ SET_FLAGS_SUB (z80_regs.h); z80_regs.a -= z80_regs.h; break;
        case 0x95: /* SUB A,L    */ SET_FLAGS_SUB (z80_regs.l); z80_regs.a -= z80_regs.l; break;
        case 0x96: /* SUB A,(HL) */ temp = memory_read (z80_regs.hl);
                                    SET_FLAGS_SUB (temp); z80_regs.a -= temp; break;
        case 0x97: /* SUB A,A    */ SET_FLAGS_SUB (z80_regs.a); z80_regs.a -= z80_regs.a; break;

        case 0x98: /* SBC A,B    */ SET_FLAGS_SBC (z80_regs.b); z80_regs.a -= z80_regs.b + CARRY_BIT; break;
        case 0x99: /* SBC A,C    */ SET_FLAGS_SBC (z80_regs.c); z80_regs.a -= z80_regs.c + CARRY_BIT; break;
        case 0x9a: /* SBC A,D    */ SET_FLAGS_SBC (z80_regs.d); z80_regs.a -= z80_regs.d + CARRY_BIT; break;
        case 0x9b: /* SBC A,E    */ SET_FLAGS_SBC (z80_regs.e); z80_regs.a -= z80_regs.e + CARRY_BIT; break;
        case 0x9c: /* SBC A,H    */ SET_FLAGS_SBC (z80_regs.h); z80_regs.a -= z80_regs.h + CARRY_BIT; break;
        case 0x9d: /* SBC A,L    */ SET_FLAGS_SBC (z80_regs.l); z80_regs.a -= z80_regs.l + CARRY_BIT; break;
        case 0x9e: /* SBC A,(HL) */ temp = memory_read (z80_regs.hl);
                                    SET_FLAGS_SBC (temp); z80_regs.a -= temp + CARRY_BIT; break;
        case 0x9f: /* SBC A,A    */ SET_FLAGS_SBC (z80_regs.a); z80_regs.a -= z80_regs.a + CARRY_BIT; break;

        case 0xa0: /* AND A,B    */ z80_regs.a &= z80_regs.b; SET_FLAGS_AND; break;
        case 0xa1: /* AND A,C    */ z80_regs.a &= z80_regs.c; SET_FLAGS_AND; break;
        case 0xa2: /* AND A,D    */ z80_regs.a &= z80_regs.d; SET_FLAGS_AND; break;
        case 0xa3: /* AND A,E    */ z80_regs.a &= z80_regs.e; SET_FLAGS_AND; break;
        case 0xa4: /* AND A,H    */ z80_regs.a &= z80_regs.h; SET_FLAGS_AND; break;
        case 0xa5: /* AND A,L    */ z80_regs.a &= z80_regs.l; SET_FLAGS_AND; break;
        case 0xa6: /* AND A,(HL) */ z80_regs.a &= memory_read (z80_regs.hl); SET_FLAGS_AND; break;
        case 0xa7: /* AND A,A    */ z80_regs.a &= z80_regs.a; SET_FLAGS_AND; break;

        case 0xa8: /* XOR A,B    */ z80_regs.a ^= z80_regs.b; SET_FLAGS_OR_XOR; break;
        case 0xa9: /* XOR A,C    */ z80_regs.a ^= z80_regs.c; SET_FLAGS_OR_XOR; break;
        case 0xaa: /* XOR A,D    */ z80_regs.a ^= z80_regs.d; SET_FLAGS_OR_XOR; break;
        case 0xab: /* XOR A,E    */ z80_regs.a ^= z80_regs.e; SET_FLAGS_OR_XOR; break;
        case 0xac: /* XOR A,H    */ z80_regs.a ^= z80_regs.h; SET_FLAGS_OR_XOR; break;
        case 0xad: /* XOR A,L    */ z80_regs.a ^= z80_regs.l; SET_FLAGS_OR_XOR; break;
        case 0xae: /* XOR A,(HL) */ z80_regs.a ^= memory_read(z80_regs.hl); SET_FLAGS_OR_XOR; break;
        case 0xaf: /* XOR A,A    */ z80_regs.a ^= z80_regs.a; SET_FLAGS_OR_XOR; break;

        case 0xb0: /* OR  A,B    */ z80_regs.a |= z80_regs.b; SET_FLAGS_OR_XOR; break;
        case 0xb1: /* OR  A,C    */ z80_regs.a |= z80_regs.c; SET_FLAGS_OR_XOR; break;
        case 0xb2: /* OR  A,D    */ z80_regs.a |= z80_regs.d; SET_FLAGS_OR_XOR; break;
        case 0xb3: /* OR  A,E    */ z80_regs.a |= z80_regs.e; SET_FLAGS_OR_XOR; break;
        case 0xb4: /* OR  A,H    */ z80_regs.a |= z80_regs.h; SET_FLAGS_OR_XOR; break;
        case 0xb5: /* OR  A,L    */ z80_regs.a |= z80_regs.l; SET_FLAGS_OR_XOR; break;
        case 0xb6: /* OR (HL)    */ z80_regs.a |= memory_read (z80_regs.hl); SET_FLAGS_OR_XOR; break;
        case 0xb7: /* OR  A,A    */ z80_regs.a |= z80_regs.a; SET_FLAGS_OR_XOR; break;

        case 0xb8: /* CP A,B     */ SET_FLAGS_SUB (z80_regs.b); break;
        case 0xb9: /* CP A,C     */ SET_FLAGS_SUB (z80_regs.c); break;
        case 0xba: /* CP A,D     */ SET_FLAGS_SUB (z80_regs.d); break;
        case 0xbb: /* CP A,E     */ SET_FLAGS_SUB (z80_regs.e); break;
        case 0xbc: /* CP A,H     */ SET_FLAGS_SUB (z80_regs.h); break;
        case 0xbd: /* CP A,L     */ SET_FLAGS_SUB (z80_regs.l); break;
        case 0xbe: /* CP A,(HL)  */ temp = memory_read (z80_regs.hl); SET_FLAGS_SUB (temp); break;
        case 0xbf: /* CP A,A     */ SET_FLAGS_SUB (z80_regs.a); break;

        case 0xc0: /* RET NZ     */ if (z80_regs.f & Z80_FLAG_ZERO)
                                    {
                                        z80_cycle += 5;
                                    }
                                    else
                                    {
                                        z80_regs.pc_l = memory_read (z80_regs.sp++);
                                        z80_regs.pc_h = memory_read (z80_regs.sp++);
                                        z80_cycle += 11;
                                    }
                                    break;
        case 0xc1: /* POP BC     */ z80_regs.c = memory_read (z80_regs.sp++);
                                    z80_regs.b = memory_read (z80_regs.sp++); break;
        case 0xc2: /* JP NZ,**   */ z80_regs.pc = (z80_regs.f & Z80_FLAG_ZERO) ? z80_regs.pc : param.w; break;
        case 0xc3: /* JP **      */ z80_regs.pc = param.w; break;
        case 0xc4: /* CALL NZ,** */ if (z80_regs.f & Z80_FLAG_ZERO)
                                    {
                                        z80_cycle += 10;
                                    }
                                    else
                                    {
                                        memory_write (--z80_regs.sp, z80_regs.pc_h);
                                        memory_write (--z80_regs.sp, z80_regs.pc_l);
                                        z80_regs.pc = param.w;
                                        z80_cycle += 17;
                                    }
                                    break;
        case 0xc5: /* PUSH BC    */ memory_write (--z80_regs.sp, z80_regs.b);
                                    memory_write (--z80_regs.sp, z80_regs.c); break;
        case 0xc6: /* ADD A,*    */ SET_FLAGS_ADD (param.l); z80_regs.a += param.l; break;
        case 0xc8: /* RET Z      */ if (z80_regs.f & Z80_FLAG_ZERO)
                                    {
                                        z80_regs.pc_l = memory_read (z80_regs.sp++);
                                        z80_regs.pc_h = memory_read (z80_regs.sp++);
                                        z80_cycle += 11;
                                    }
                                    else
                                    {
                                        z80_cycle += 5;
                                    }
                                    break;
        case 0xc9: /* RET        */ z80_regs.pc_l = memory_read (z80_regs.sp++);
                                    z80_regs.pc_h = memory_read (z80_regs.sp++); break;
        case 0xca: /* JP Z,**    */ z80_regs.pc = (z80_regs.f & Z80_FLAG_ZERO) ? param.w : z80_regs.pc; break;
        case 0xcb: /* Bit Instruction */ z80_bit_instruction (); break;
        case 0xcc: /* CALL Z,**  */ if (z80_regs.f & Z80_FLAG_ZERO)
                                    {
                                        memory_write (--z80_regs.sp, z80_regs.pc_h);
                                        memory_write (--z80_regs.sp, z80_regs.pc_l);
                                        z80_regs.pc = param.w;
                                        z80_cycle += 17;
                                    }
                                    else
                                    {
                                        z80_cycle += 10;
                                    }
                                    break;
        case 0xcd: /* CALL       */ memory_write (--z80_regs.sp, z80_regs.pc_h);
                                    memory_write (--z80_regs.sp, z80_regs.pc_l);
                                    z80_regs.pc = param.w; break;
        case 0xce: /* ADC A,*    */ SET_FLAGS_ADC (param.l); z80_regs.a += param.l + CARRY_BIT; break;
        case 0xcf: /* RST 08h    */ memory_write (--z80_regs.sp, z80_regs.pc_h);
                                    memory_write (--z80_regs.sp, z80_regs.pc_l);
                                    z80_regs.pc = 0x08; break;

        case 0xd0: /* RET NC     */ if (z80_regs.f & Z80_FLAG_CARRY)
                                    {
                                        z80_cycle += 5;
                                    }
                                    else
                                    {
                                        z80_regs.pc_l = memory_read (z80_regs.sp++);
                                        z80_regs.pc_h = memory_read (z80_regs.sp++);
                                        z80_cycle += 11;
                                    }
                                    break;
        case 0xd1: /* POP DE     */ z80_regs.e = memory_read (z80_regs.sp++);
                                    z80_regs.d = memory_read (z80_regs.sp++); break;
        case 0xd2: /* JP NC,**   */ z80_regs.pc = (z80_regs.f & Z80_FLAG_CARRY) ? z80_regs.pc : param.w; break;
        case 0xd3: /* OUT (*),A  */ io_write (param.l, z80_regs.a); break;
        case 0xd5: /* PUSH DE    */ memory_write (--z80_regs.sp, z80_regs.d);
                                    memory_write (--z80_regs.sp, z80_regs.e); break;
        case 0xd6: /* SUB A,*    */ SET_FLAGS_SUB (param.l); z80_regs.a -= param.l; break;
        case 0xd7: /* RST 10h    */ memory_write (--z80_regs.sp, z80_regs.pc_h);
                                    memory_write (--z80_regs.sp, z80_regs.pc_l);
                                    z80_regs.pc = 0x10; break;
        case 0xd8: /* RET C      */ if (z80_regs.f & Z80_FLAG_CARRY)
                                    {
                                        z80_regs.pc_l = memory_read (z80_regs.sp++);
                                        z80_regs.pc_h = memory_read (z80_regs.sp++);
                                        z80_cycle += 11;
                                    }
                                    else
                                    {
                                        z80_cycle += 5;
                                    }
                                    break;
        case 0xd9: /* EXX        */ SWAP (uint16_t, z80_regs.bc, z80_regs.alt_bc);
                                    SWAP (uint16_t, z80_regs.de, z80_regs.alt_de);
                                    SWAP (uint16_t, z80_regs.hl, z80_regs.alt_hl); break;
        case 0xda: /* JP C,**    */ z80_regs.pc = (z80_regs.f & Z80_FLAG_CARRY) ? param.w : z80_regs.pc; break;
        case 0xdb: /* IN (*),A   */ z80_regs.a = io_read (param.l); break;
        case 0xdc: /* CALL C,**  */ if (z80_regs.f & Z80_FLAG_CARRY)
                                    {
                                        memory_write (--z80_regs.sp, z80_regs.pc_h);
                                        memory_write (--z80_regs.sp, z80_regs.pc_l);
                                        z80_regs.pc = param.w;
                                        z80_cycle += 17;
                                    }
                                    else
                                    {
                                        z80_cycle += 10;
                                    }
                                    break;

        case 0xdd: /* IX         */ z80_regs.ix = z80_ix_iy_instruction (z80_regs.ix); break;
        case 0xde: /* SBC A,*    */ SET_FLAGS_SBC (param.l); z80_regs.a -=  param.l + CARRY_BIT; break;
        case 0xdf: /* RST 18h    */ memory_write (--z80_regs.sp, z80_regs.pc_h);
                                    memory_write (--z80_regs.sp, z80_regs.pc_l);
                                    z80_regs.pc = 0x18; break;

        case 0xe0: /* RET PO     */ if (z80_regs.f & Z80_FLAG_PARITY)
                                    {
                                        z80_cycle += 5;
                                    }
                                    else
                                    {
                                        z80_regs.pc_l = memory_read (z80_regs.sp++);
                                        z80_regs.pc_h = memory_read (z80_regs.sp++);
                                        z80_cycle += 11;
                                    }
                                    break;
        case 0xe1: /* POP HL     */ z80_regs.l = memory_read (z80_regs.sp++);
                                    z80_regs.h = memory_read (z80_regs.sp++); break;
        case 0xe2: /* JP PO      */ z80_regs.pc = (z80_regs.f & Z80_FLAG_PARITY) ? z80_regs.pc : param.w; break;
        case 0xe3: /* EX (SP),HL */ temp = z80_regs.l;
                                    z80_regs.l = memory_read (z80_regs.sp);
                                    memory_write (z80_regs.sp, temp);
                                    temp = z80_regs.h;
                                    z80_regs.h = memory_read (z80_regs.sp + 1);
                                    memory_write (z80_regs.sp + 1, temp); break;
        case 0xe4: /* CALL PO,** */ if (z80_regs.f & Z80_FLAG_PARITY)
                                    {
                                        z80_cycle += 10;
                                    }
                                    else
                                    {
                                        memory_write (--z80_regs.sp, z80_regs.pc_h);
                                        memory_write (--z80_regs.sp, z80_regs.pc_l);
                                        z80_regs.pc = param.w;
                                        z80_cycle += 17;
                                    }
                                    break;
        case 0xe5: /* PUSH HL    */ memory_write (--z80_regs.sp, z80_regs.h);
                                    memory_write (--z80_regs.sp, z80_regs.l); break;
        case 0xe6: /* AND A,*    */ z80_regs.a &= param.l; SET_FLAGS_AND; break;
        case 0xe7: /* RST 20h    */ memory_write (--z80_regs.sp, z80_regs.pc_h);
                                    memory_write (--z80_regs.sp, z80_regs.pc_l);
                                    z80_regs.pc = 0x20; break;
        case 0xe8: /* RET PE     */ if (z80_regs.f & Z80_FLAG_PARITY)
                                    {
                                        z80_regs.pc_l = memory_read (z80_regs.sp++);
                                        z80_regs.pc_h = memory_read (z80_regs.sp++);
                                        z80_cycle += 11;
                                    }
                                    else
                                    {
                                        z80_cycle += 5;
                                    }
                                    break;
        case 0xe9: /* JP (HL)    */ z80_regs.pc = param.w; break;
        case 0xea: /* JP PE,**   */ z80_regs.pc = (z80_regs.f & Z80_FLAG_PARITY) ? param.w : z80_regs.pc; break;
        case 0xeb: /* EX DE,HL   */ SWAP (uint16_t, z80_regs.de, z80_regs.hl); break;
        case 0xed: /* Extended Instructions */ z80_extended_instruction (); break;
        case 0xee: /* XOR A,*    */ z80_regs.a ^= param.l; SET_FLAGS_OR_XOR; break;
        case 0xef: /* RST 28h    */ memory_write (--z80_regs.sp, z80_regs.pc_h);
                                    memory_write (--z80_regs.sp, z80_regs.pc_l);
                                    z80_regs.pc = 0x28; break;

        case 0xf0: /* RET P      */ if (z80_regs.f & Z80_FLAG_SIGN)
                                    {
                                        z80_cycle += 5;
                                    }
                                    else
                                    {
                                        z80_regs.pc_l = memory_read (z80_regs.sp++);
                                        z80_regs.pc_h = memory_read (z80_regs.sp++);
                                        z80_cycle += 11;
                                    }
                                    break;
        case 0xf1: /* POP AF     */ z80_regs.f = memory_read (z80_regs.sp++);
                                    z80_regs.a = memory_read (z80_regs.sp++); break;
        case 0xf2: /* JP P,**    */ z80_regs.pc = (z80_regs.f & Z80_FLAG_SIGN) ? z80_regs.pc : param.w; break;
        case 0xf3: /* DI         */ z80_regs.iff1 = false; z80_regs.iff2 = false; break;
        case 0xf5: /* PUSH AF    */ memory_write (--z80_regs.sp, z80_regs.a);
                                    memory_write (--z80_regs.sp, z80_regs.f); break;
        case 0xf6: /* OR  A,*    */ z80_regs.a |= param.l; SET_FLAGS_OR_XOR; break;
        case 0xf8: /* RET M      */ if (z80_regs.f & Z80_FLAG_SIGN)
                                    {
                                        z80_regs.pc_l = memory_read (z80_regs.sp++);
                                        z80_regs.pc_h = memory_read (z80_regs.sp++);
                                        z80_cycle += 11;
                                    }
                                    else
                                    {
                                        z80_cycle += 5;
                                    }
                                    break;
        case 0xf9: /* LD SP,HL   */ z80_regs.sp = z80_regs.hl; break;
        case 0xfa: /* JP M,**    */ z80_regs.pc = (z80_regs.f & Z80_FLAG_SIGN) ? param.w : z80_regs.pc; break;
        case 0xfb: /* EI         */ fprintf (stdout, "[DEBUG]: Interrupts enable.\n");
                                    z80_regs.iff1 = true; z80_regs.iff2 = true; break;
        case 0xfc: /* CALL M,**  */ if (z80_regs.f & Z80_FLAG_SIGN)
                                    {
                                        memory_write (--z80_regs.sp, z80_regs.pc_h);
                                        memory_write (--z80_regs.sp, z80_regs.pc_l);
                                        z80_regs.pc = param.w;
                                        z80_cycle += 17;
                                    }
                                    else
                                    {
                                        z80_cycle += 10;
                                    }
                                    break;
        case 0xfd: /* IY         */ z80_regs.iy = z80_ix_iy_instruction (z80_regs.iy); break;

        case 0xfe: /* CP A,*     */ SET_FLAGS_SUB (param.l); break;
        case 0xff: /* RST 38h    */ memory_write (--z80_regs.sp, z80_regs.pc_h);
                                    memory_write (--z80_regs.sp, z80_regs.pc_l);
                                    z80_regs.pc = 0x38; break;

        default:
            fprintf (stderr, "Unknown instruction: \"%s\" (%02x). %" PRIu64 " instructions have been run.\n",
                     z80_instruction_name[instruction], instruction, instruction_count);
            _abort_ = true;
    }

    z80_cycle += z80_instruction_cycles [instruction];
}

/* TODO: Move these somewhere SMS-specific */
extern void vdp_clock_update (uint64_t cycles);
extern bool vdp_get_interrupt (void);
#define SMS_CLOCK_RATE_PAL  3546895
#define SMS_CLOCK_RATE_NTSC 3579545

uint32_t z80_run ()
{
    uint64_t next_frame_cycle = 0;
    uint64_t frame_number = 0;
    for (;;)
    {

#if 0
        if (instruction_count >= 0000ull && instruction_count < 1000ull)
        {
            debug_instruction = true;
        }
        else
        {
            debug_instruction = false;
        }
#endif

#if 0
        if (instruction_count == 20000)
        {
            fprintf (stdout, "[DEBUG(z80)]: Reached instruction goal.\n");
            _abort_ = true;
        }
#endif
        z80_instruction ();
        instruction_count++;

        /* Time has passed, update the VDP state */
        vdp_clock_update (z80_cycle);

        /* Check for interrupts */
        if (z80_regs.iff1 && vdp_get_interrupt ())
        {
            z80_regs.iff1 = false;
            z80_regs.iff2 = false;

            switch (z80_regs.im)
            {
                case 1:
                    /* RST 0x38 */
                    memory_write (--z80_regs.sp, z80_regs.pc_h);
                    memory_write (--z80_regs.sp, z80_regs.pc_l);
                    z80_regs.pc = 0x38;
                    break;
                default:
                    fprintf (stderr, "Unknown interrupt mode %d.\n", z80_regs.im);
                    _abort_ = true;
            }
        }

        /* TODO: Rather than SMS cycles, we should update the display based on host VSYNC */
        if (z80_cycle >= next_frame_cycle)
        {
            /* Check input */
            SDL_Event event;

            while (SDL_PollEvent (&event))
            {
                if (event.type == SDL_QUIT)
                {
                    _abort_ = true;
                }
            }

            frame_number++;

            vdp_render ();
            SDL_RenderPresent (renderer);
            SDL_Delay (10);

            if ((frame_number % 50) == 0)
            {
                printf ("-- %02" PRId64 " seconds have passed --\n", frame_number / 50);
            }

            next_frame_cycle = z80_cycle + (SMS_CLOCK_RATE_PAL / 50);
        }

        if (_abort_)
        {
            fprintf (stderr, "[DEBUG]: _abort_ set. Terminating emulation.\n");
            return EXIT_FAILURE;
        }
    }
}
