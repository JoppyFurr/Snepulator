#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "z80.h"
#include "z80_names.h"

#define SWAP(TYPE, X, Y) { TYPE tmp = X; X = Y; Y = tmp; }

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

/* TODO: A better name than z80_cycle */

uint8_t instructions_before_interrupts = 0;

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
                                        (((((int16_t)(int8_t)z80_regs.a) + ((int16_t)(int8_t)X)) >  127 || \
                                          (((int16_t)(int8_t)z80_regs.a) + ((int16_t)(int8_t)X)) < -128) ? Z80_FLAG_OVERFLOW : 0) | \
                                        (((z80_regs.a & 0x0f) + (X & 0x0f)) & 0x10             ? Z80_FLAG_HALF     : 0) | \
                                        (((z80_regs.a + X) & 0xff) == 0x00                     ? Z80_FLAG_ZERO     : 0) | \
                                        ((z80_regs.a + X) & 0x80                               ? Z80_FLAG_SIGN     : 0); }

#define SET_FLAGS_SUB(X) { z80_regs.f = (((uint16_t)z80_regs.a - (uint16_t)X) & 0x100          ? Z80_FLAG_CARRY    : 0) | \
                                        (                                                       Z80_FLAG_SUB         ) | \
                                        (((((int16_t)(int8_t)z80_regs.a) - ((int16_t)(int8_t)X)) >  127 || \
                                          (((int16_t)(int8_t)z80_regs.a) - ((int16_t)(int8_t)X)) < -128) ? Z80_FLAG_OVERFLOW : 0) | \
                                        (((z80_regs.a & 0x0f) - (X & 0x0f)) & 0x10            ? Z80_FLAG_HALF     : 0) | \
                                        ((z80_regs.a == X)                                    ? Z80_FLAG_ZERO     : 0) | \
                                        ((z80_regs.a - X) & 0x80                              ? Z80_FLAG_SIGN     : 0); }

#define CARRY_BIT (z80_regs.f & Z80_FLAG_CARRY)

#define SET_FLAGS_ADC(X) { z80_regs.f = (((uint16_t)z80_regs.a + (uint16_t)X + CARRY_BIT) & 0x100  ? Z80_FLAG_CARRY    : 0) | \
                                        (((((int16_t)(int8_t)z80_regs.a) + ((int16_t)(int8_t)X) + CARRY_BIT) >  127 || \
                                          (((int16_t)(int8_t)z80_regs.a) + ((int16_t)(int8_t)X) + CARRY_BIT) < -128) ? Z80_FLAG_OVERFLOW : 0) | \
                                        (((z80_regs.a & 0x0f) + (X & 0x0f) + CARRY_BIT) & 0x10     ? Z80_FLAG_HALF     : 0) | \
                                        (((z80_regs.a + X + CARRY_BIT) & 0xff) == 0x00             ? Z80_FLAG_ZERO     : 0) | \
                                        ((z80_regs.a + X + CARRY_BIT) & 0x80                       ? Z80_FLAG_SIGN     : 0); }

#define SET_FLAGS_SBC(X) { z80_regs.f = (((uint16_t)z80_regs.a - (uint16_t)X - CARRY_BIT) & 0x100  ? Z80_FLAG_CARRY    : 0) | \
                                        (                                                            Z80_FLAG_SUB         ) | \
                                        (((((int16_t)(int8_t)z80_regs.a) - ((int16_t)(int8_t)X) - CARRY_BIT) >  127 || \
                                          (((int16_t)(int8_t)z80_regs.a) - ((int16_t)(int8_t)X) - CARRY_BIT) < -128) ? Z80_FLAG_OVERFLOW : 0) | \
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

#define SET_FLAGS_CPL { z80_regs.f |= Z80_FLAG_HALF | Z80_FLAG_SUB; }

#define SET_FLAGS_ADD_16(Y,X) { z80_regs.f = (z80_regs.f &              (Z80_FLAG_OVERFLOW | Z80_FLAG_ZERO | Z80_FLAG_SIGN))     | \
                                             ((((uint32_t)Y + (uint32_t)X) & 0x10000)                      ? Z80_FLAG_CARRY : 0) | \
                                             ((((uint32_t)(Y & 0x0fff) + (uint32_t)(X & 0x0fff)) & 0x1000) ? Z80_FLAG_HALF  : 0); }

#define SET_FLAGS_SUB_16(Y,X) { z80_regs.f = (z80_regs.f &              (Z80_FLAG_OVERFLOW | Z80_FLAG_ZERO | Z80_FLAG_SIGN)    ) | \
                                             (                                                               Z80_FLAG_SUB      ) | \
                                             ((((uint32_t)Y - (uint32_t)X) & 0x10000)                      ? Z80_FLAG_CARRY : 0) | \
                                             ((((uint32_t)(Y & 0x0fff) - (uint32_t)(X & 0x0fff)) & 0x1000) ? Z80_FLAG_HALF  : 0); }

#define SET_FLAGS_ADC_16(X) { z80_regs.f = (((uint32_t)z80_regs.hl + (uint32_t)X + CARRY_BIT) & 0x10000  ? Z80_FLAG_CARRY    : 0) | \
                                        (((((int32_t)(int16_t)z80_regs.hl) + ((int32_t)(int16_t)X) + CARRY_BIT) >  32767 || \
                                          (((int32_t)(int16_t)z80_regs.hl) + ((int32_t)(int16_t)X) + CARRY_BIT) < -32768)  ? Z80_FLAG_OVERFLOW : 0) | \
                                        (((z80_regs.hl & 0xfff) + (X & 0xfff) + CARRY_BIT) & 0x1000      ? Z80_FLAG_HALF     : 0) | \
                                        (((z80_regs.hl + X + CARRY_BIT) & 0xffff) == 0x0000              ? Z80_FLAG_ZERO     : 0) | \
                                        ((z80_regs.hl + X + CARRY_BIT) & 0x8000                          ? Z80_FLAG_SIGN     : 0); }

#define SET_FLAGS_SBC_16(X) { z80_regs.f = (((uint32_t)z80_regs.hl - (uint32_t)X - CARRY_BIT) & 0x10000    ? Z80_FLAG_CARRY    : 0) | \
                                        (                                                                  Z80_FLAG_SUB         ) | \
                                        (((((int32_t)(int16_t)z80_regs.hl) - ((int32_t)(int16_t)X) - CARRY_BIT) >  32767 || \
                                          (((int32_t)(int16_t)z80_regs.hl) - ((int32_t)(int16_t)X) - CARRY_BIT) < -32768)  ? Z80_FLAG_OVERFLOW : 0) | \
                                        (((z80_regs.hl & 0x0fff) - (X & 0x0fff) - CARRY_BIT) & 0x1000    ? Z80_FLAG_HALF     : 0) | \
                                        (((z80_regs.hl - X - CARRY_BIT) & 0xffff) == 0x0000              ? Z80_FLAG_ZERO     : 0) | \
                                        ((z80_regs.hl - X - CARRY_BIT) & 0x8000                          ? Z80_FLAG_SIGN     : 0); }

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
    uint16_t temp_16;

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
            break;
        case 2:
            param.l = memory_read (z80_regs.pc++);
            break;
        default:
            break;
    }

    switch (instruction)
    {
        case 0x41: /* OUT (C),B  */ io_write (z80_regs.c, z80_regs.b); z80_cycle += 12; break;
        case 0x42: /* SBC HL,BC  */ temp_16 = z80_regs.bc + CARRY_BIT;
                                    SET_FLAGS_SBC_16 (z80_regs.bc); z80_regs.hl -= temp_16; z80_cycle += 15; break;
        case 0x43: /* LD (**),BC */ memory_write (param.w,     z80_regs.c);
                                    memory_write (param.w + 1, z80_regs.b); z80_cycle += 20; break;
        case 0x44: /* NEG        */ temp_1 = z80_regs.a;
                                    z80_regs.a = 0 - (int8_t)z80_regs.a;
                                    z80_regs.f = (temp_1 != 0                  ? Z80_FLAG_CARRY    : 0) |
                                                 (                               Z80_FLAG_SUB         ) |
                                                 (temp_1 == 0x80               ? Z80_FLAG_OVERFLOW : 0) |
                                                 ((0 - (temp_1 & 0x0f)) & 0x10 ? Z80_FLAG_HALF     : 0) |
                                                 (z80_regs.a == 0              ? Z80_FLAG_ZERO     : 0) |
                                                 (z80_regs.a & 0x80            ? Z80_FLAG_SIGN     : 0);
                                    z80_cycle += 8; break;
        case 0x45: /* RETN       */ z80_regs.pc_l = memory_read (z80_regs.sp++);
                                    z80_regs.pc_h = memory_read (z80_regs.sp++);
                                    z80_regs.iff1 = z80_regs.iff2;
                                    break;
        case 0x4a: /* ADC HL,BC  */ temp_16 = z80_regs.bc + CARRY_BIT;
                                    SET_FLAGS_ADC_16 (z80_regs.bc); z80_regs.hl += temp_16; break;
        case 0x4b: /* LD BC,(**) */ z80_regs.c = memory_read (param.w);
                                    z80_regs.b = memory_read (param.w + 1); z80_cycle += 20; break;
        case 0x4d: /* RETI       */ z80_regs.pc_l = memory_read (z80_regs.sp++);
                                    z80_regs.pc_h = memory_read (z80_regs.sp++);
                                    break; /* TODO: Signals the IO device that the interrupt is handled? */

        case 0x51: /* OUT (C),D  */ io_write (z80_regs.c, z80_regs.d); z80_cycle += 12; break;
        case 0x52: /* SBC HL,DE  */ temp_16 = z80_regs.de + CARRY_BIT;
                                    SET_FLAGS_SBC_16 (z80_regs.de); z80_regs.hl -= temp_16; z80_cycle += 15; break;
        case 0x53: /* LD (**),DE */ memory_write (param.w,     z80_regs.e);
                                    memory_write (param.w + 1, z80_regs.d); z80_cycle += 20; break;
        case 0x56: /* IM 1       */ fprintf (stdout, "[DEBUG]: Interrupt mode = 1.\n"); z80_regs.im = 1; z80_cycle += 8; break;
        case 0x59: /* OUT (C),E  */ io_write (z80_regs.c, z80_regs.e); z80_cycle += 12; break;
        case 0x5a: /* ADC HL,DE  */ temp_16 = z80_regs.de + CARRY_BIT;
                                    SET_FLAGS_ADC_16 (z80_regs.de); z80_regs.hl += temp_16; break;
        case 0x5b: /* LD DE,(**) */ z80_regs.e = memory_read (param.w);
                                    z80_regs.d = memory_read (param.w + 1); z80_cycle += 20; break;

        case 0x61: /* OUT (C),H  */ io_write (z80_regs.c, z80_regs.h); z80_cycle += 12; break;
        case 0x62: /* SBC HL,HL  */ temp_16 = z80_regs.hl + CARRY_BIT;
                                    SET_FLAGS_SBC_16 (z80_regs.hl); z80_regs.hl -= temp_16; z80_cycle += 15; break;
        case 0x67: /* RRD        */ temp_1 = memory_read (z80_regs.hl);
                                    temp_2 = z80_regs.a;
                                    z80_regs.a &= 0xf0; z80_regs.a |= (temp_1 & 0x0f);
                                    temp_1 >>= 4; temp_1 |= (temp_2 << 4);
                                    memory_write (z80_regs.hl, temp_1);
                                    SET_FLAGS_RRD_RLD;
                                    break;
        case 0x69: /* OUT (C),L  */ io_write (z80_regs.c, z80_regs.l); z80_cycle += 12; break;
        case 0x6a: /* ADC HL,HL  */ temp_16 = z80_regs.hl + CARRY_BIT;
                                    SET_FLAGS_ADC_16 (z80_regs.hl); z80_regs.hl += temp_16; break;
        case 0x6f: /* RLD        */ temp_1 = memory_read (z80_regs.hl);
                                    temp_2 = z80_regs.a;
                                    z80_regs.a &= 0xf0; z80_regs.a |= (temp_1 >> 4);
                                    temp_1 <<= 4; temp_1 |= (temp_2 & 0x0f);
                                    memory_write (z80_regs.hl, temp_1);
                                    SET_FLAGS_RRD_RLD;
                                    break;

        case 0x71: /* OUT (C),0  */ io_write (z80_regs.c, 0); break;
        case 0x72: /* SBC HL,SP  */ temp_16 = z80_regs.sp + CARRY_BIT;
                                    SET_FLAGS_SBC_16 (z80_regs.sp); z80_regs.hl -= temp_16; z80_cycle += 15; break;
        case 0x73: /* LD (**),SP */ memory_write (param.w,     z80_regs.sp_l);
                                    memory_write (param.w + 1, z80_regs.sp_h); z80_cycle += 20; break;
        case 0x7a: /* ADC HL,SP  */ temp_16 = z80_regs.sp + CARRY_BIT;
                                    SET_FLAGS_ADC_16 (z80_regs.sp); z80_regs.hl += temp_16; break;
        case 0x79: /* OUT (C),A  */ io_write (z80_regs.c, z80_regs.a); z80_cycle += 12; break;
        case 0x7b: /* LD SP,(**) */ z80_regs.sp_l = memory_read (param.w);
                                    z80_regs.sp_h = memory_read (param.w + 1); z80_cycle += 20; break;

        case 0xa0: /* LDI        */ memory_write (z80_regs.de, memory_read (z80_regs.hl));
                                    z80_regs.hl++; z80_regs.de++; z80_regs.bc--;
                                    z80_regs.f &= (Z80_FLAG_CARRY | Z80_FLAG_ZERO | Z80_FLAG_SIGN);
                                    z80_regs.f |= (z80_regs.bc ? Z80_FLAG_OVERFLOW : 0);
                                    z80_cycle += 16; break;
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
                                    } z80_cycle += 16; break;
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
                                    z80_cycle += z80_regs.bc ? 21 : 16;
                                    break;
        case 0xb1: /* CPIR       */ temp_1 = memory_read (z80_regs.hl);
                                    z80_regs.hl++;
                                    z80_regs.bc--;
                                    z80_regs.pc -= (z80_regs.bc == 0 || z80_regs.a == temp_1) ? 0 : 2;
                                    SET_FLAGS_CPD_CPI (temp_1);
                                    break;
        case 0xb3: /* OTIR       */ io_write (z80_regs.c, memory_read(z80_regs.hl)),
                                    z80_regs.hl++; z80_regs.b--;
                                    z80_regs.pc -= z80_regs.b ? 2 : 0;
                                    z80_regs.f = (z80_regs.f & Z80_FLAG_CARRY) |
                                                 (Z80_FLAG_SUB | Z80_FLAG_ZERO);
                                    z80_cycle += z80_regs.b ? 21 : 16; break;
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
        fprintf (stderr, "Unknown extended instruction: \"%s\" (%02x).\n",
                 z80_instruction_name_extended[instruction], instruction);
        _abort_ = true;
    }

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
        case 0x00: /* RLC (ix+*) */ data = (data << 1) | ((data & 0x80) ? 0x01 : 0x00); SET_FLAGS_RLC (data);
                                    z80_cycle += 23; break;
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
            write_data = false;
            z80_cycle += 20; break;
        case 0x80: case 0x88: case 0x90: case 0x98: /* RES */
        case 0xa0: case 0xa8: case 0xb0: case 0xb8:
            data &= ~bit;
            z80_cycle += 23; break;
        case 0xc0: case 0xc8: case 0xd0: case 0xd8: /* SET */
        case 0xe0: case 0xe8: case 0xf0: case 0xf8:
            data |= bit;
            z80_cycle += 23; break;
        default:
            fprintf (stderr, "Unknown ix/iy bit instruction: \"%s\" (%02x).\n",
                     z80_instruction_name_bits[instruction], instruction);
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

        case 0x21: /* LD IX,**     */ reg_ix_iy.w = param.w; z80_cycle += 14; break;
        case 0x22: /* LD (**),IX   */ memory_write (param.w,     reg_ix_iy.l);
                                      memory_write (param.w + 1, reg_ix_iy.h); z80_cycle += 20; break;
        case 0x23: /* INC IX       */ reg_ix_iy.w++; z80_cycle += 10; break;
        case 0x24: /* INC IXH      */ reg_ix_iy.h++; SET_FLAGS_INC (reg_ix_iy.h) break;
        case 0x25: /* DEC IXH      */ reg_ix_iy.h--; SET_FLAGS_DEC (reg_ix_iy.h) break;
        case 0x26: /* LD IXH,*     */ reg_ix_iy.h = param.l; break;
        case 0x29: /* ADD IX,IX    */ SET_FLAGS_ADD_16 (reg_ix_iy.w, reg_ix_iy.w); reg_ix_iy.w += reg_ix_iy.w; break;
        case 0x2a: /* LD IX,(**)   */ reg_ix_iy.l = memory_read (param.w);
                                      reg_ix_iy.h = memory_read (param.w + 1); z80_cycle += 20; break;
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
        case 0x36: /* LD (IX+*),*  */ memory_write (reg_ix_iy.w + (int8_t) param.l, param.h); z80_cycle += 19; break;
        case 0x39: /* ADD IX,SP    */ SET_FLAGS_ADD_16 (reg_ix_iy.w, z80_regs.sp); reg_ix_iy.w += z80_regs.sp; break;

        case 0x40: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x41: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x42: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x43: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x44: /* LD B,IXH     */ z80_regs.b = reg_ix_iy.h; break;
        case 0x45: /* LD B,IXL     */ z80_regs.b = reg_ix_iy.l; break;
        case 0x46: /* LD B,(IX+*)  */ z80_regs.b = memory_read (reg_ix_iy.w + (int8_t) param.l); z80_cycle += 19; break;
        case 0x47: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x48: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x49: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x4a: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x4b: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x4c: /* LD C,IXH     */ z80_regs.c = reg_ix_iy.h; break;
        case 0x4d: /* LD C,IXL     */ z80_regs.c = reg_ix_iy.l; break;
        case 0x4e: /* LD C,(IX+*)  */ z80_regs.c = memory_read (reg_ix_iy.w + (int8_t) param.l); z80_cycle += 19; break;
        case 0x4f: /* -            */ z80_regs.pc--; z80_instruction (); break;

        case 0x50: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x51: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x52: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x53: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x54: /* LD D,IXH     */ z80_regs.d = reg_ix_iy.h; break;
        case 0x55: /* LD D,IXL     */ z80_regs.d = reg_ix_iy.l; break;
        case 0x56: /* LD D,(IX+*)  */ z80_regs.d = memory_read (reg_ix_iy.w + (int8_t) param.l); z80_cycle += 19; break;
        case 0x57: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x58: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x59: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x5a: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x5b: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x5c: /* LD E,IXH     */ z80_regs.e = reg_ix_iy.h; break;
        case 0x5d: /* LD E,IXL     */ z80_regs.e = reg_ix_iy.l; break;
        case 0x5e: /* LD E,(IX+*)  */ z80_regs.e = memory_read (reg_ix_iy.w + (int8_t) param.l); z80_cycle += 19; break;
        case 0x5f: /* -            */ z80_regs.pc--; z80_instruction (); break;

        case 0x60: /* LD IXH,B     */ reg_ix_iy.h = z80_regs.b; break;
        case 0x61: /* LD IXH,C     */ reg_ix_iy.h = z80_regs.c; break;
        case 0x62: /* LD IXH,D     */ reg_ix_iy.h = z80_regs.d; break;
        case 0x63: /* LD IXH,E     */ reg_ix_iy.h = z80_regs.e; break;
        case 0x64: /* LD IXH,IXH   */ reg_ix_iy.h = reg_ix_iy.h; break;
        case 0x65: /* LD IXH,IXL   */ reg_ix_iy.h = reg_ix_iy.l; break;
        case 0x66: /* LD H,(IX+*)  */ z80_regs.h = memory_read (reg_ix_iy.w + (int8_t) param.l); z80_cycle += 19; break;
        case 0x67: /* LD IXH,A     */ reg_ix_iy.h = z80_regs.a; break;
        case 0x68: /* LD IXH,B     */ reg_ix_iy.l = z80_regs.b; break;
        case 0x69: /* LD IXH,C     */ reg_ix_iy.l = z80_regs.c; break;
        case 0x6a: /* LD IXH,D     */ reg_ix_iy.l = z80_regs.d; break;
        case 0x6b: /* LD IXH,E     */ reg_ix_iy.l = z80_regs.e; break;
        case 0x6c: /* LD IXL,IXH   */ reg_ix_iy.l = reg_ix_iy.h; break;
        case 0x6d: /* LD IXL,IXL   */ reg_ix_iy.l = reg_ix_iy.l; break;
        case 0x6e: /* LD L,(IX+*)  */ z80_regs.l = memory_read (reg_ix_iy.w + (int8_t) param.l); z80_cycle += 19; break;
        case 0x6f: /* LD IXL,A     */ reg_ix_iy.l = z80_regs.a; break;

        case 0x70: /* LD (IX+*),B  */ memory_write (reg_ix_iy.w + (int8_t) param.l, z80_regs.b); z80_cycle += 19; break;
        case 0x71: /* LD (IX+*),C  */ memory_write (reg_ix_iy.w + (int8_t) param.l, z80_regs.c); z80_cycle += 19; break;
        case 0x72: /* LD (IX+*),D  */ memory_write (reg_ix_iy.w + (int8_t) param.l, z80_regs.d); z80_cycle += 19; break;
        case 0x73: /* LD (IX+*),E  */ memory_write (reg_ix_iy.w + (int8_t) param.l, z80_regs.e); z80_cycle += 19; break;
        case 0x74: /* LD (IX+*),H  */ memory_write (reg_ix_iy.w + (int8_t) param.l, z80_regs.h); z80_cycle += 19; break;
        case 0x75: /* LD (IX+*),L  */ memory_write (reg_ix_iy.w + (int8_t) param.l, z80_regs.l); z80_cycle += 19; break;
        case 0x77: /* LD (IX+*),A  */ memory_write (reg_ix_iy.w + (int8_t) param.l, z80_regs.a); z80_cycle += 19; break;
        case 0x78: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x79: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x7a: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x7b: /* -            */ z80_regs.pc--; z80_instruction (); break;
        case 0x7c: /* LD A,IXH     */ z80_regs.a = reg_ix_iy.h; break;
        case 0x7d: /* LD A,IXL     */ z80_regs.a = reg_ix_iy.l; break;
        case 0x7e: /* LD A,(IX+*)  */ z80_regs.a = memory_read (reg_ix_iy.w + (int8_t) param.l); z80_cycle += 19; break;
        case 0x7f: /* -            */ z80_regs.pc--; z80_instruction (); break;

        case 0x84: /* ADD A,IXH    */ SET_FLAGS_ADD (reg_ix_iy.h); z80_regs.a += reg_ix_iy.h; break;
        case 0x85: /* ADD A,IXL    */ SET_FLAGS_ADD (reg_ix_iy.l); z80_regs.a += reg_ix_iy.l; break;
        case 0x86: /* ADD A,(IX+*) */ temp = memory_read (reg_ix_iy.w + (int8_t) param.l);
                                      SET_FLAGS_ADD (temp); z80_regs.a += temp; break;

        case 0x8c: /* ADC A,IXH    */ temp = reg_ix_iy.h + CARRY_BIT;
                                      SET_FLAGS_ADC (reg_ix_iy.h); z80_regs.a += temp; break;
        case 0x8d: /* ADC A,IXL    */ temp = reg_ix_iy.l + CARRY_BIT;
                                      SET_FLAGS_ADC (reg_ix_iy.l); z80_regs.a += temp; break;
        case 0x8e: /* ADC A,(IX+*) */ { uint8_t value_read = memory_read (reg_ix_iy.w + (int8_t) param.l);
                                        temp = value_read + CARRY_BIT;
                                        SET_FLAGS_ADC (value_read); z80_regs.a += temp; } break;

        case 0x94: /* SUB A,IXH    */ SET_FLAGS_SUB (reg_ix_iy.h); z80_regs.a -= reg_ix_iy.h; break;
        case 0x95: /* SUB A,IXL    */ SET_FLAGS_SUB (reg_ix_iy.l); z80_regs.a -= reg_ix_iy.l; break;
        case 0x96: /* SUB A,(IX+*) */ temp = memory_read (reg_ix_iy.w + (int8_t) param.l);
                                      SET_FLAGS_SUB (temp); z80_regs.a -= temp; break;
        case 0x9c: /* SBC A,IXH    */ temp = reg_ix_iy.h + CARRY_BIT;
                                      SET_FLAGS_SBC (reg_ix_iy.h); z80_regs.a -= temp; break;
        case 0x9d: /* SBC A,IXL    */ temp = reg_ix_iy.l + CARRY_BIT;
                                      SET_FLAGS_SBC (reg_ix_iy.l); z80_regs.a -= temp; break;
        case 0x9e: /* SBC A,(IX+*) */ { uint8_t value_read= memory_read (reg_ix_iy.w + (int8_t) param.l);
                                        temp = value_read + CARRY_BIT;
                                        SET_FLAGS_SBC (value_read); z80_regs.a -= temp; } break;

        case 0xa4: /* AND A,IXH    */ z80_regs.a &= reg_ix_iy.h; SET_FLAGS_AND; break;
        case 0xa5: /* AND A,IXL    */ z80_regs.a &= reg_ix_iy.l; SET_FLAGS_AND; break;
        case 0xa6: /* AND A,(IX+*) */ z80_regs.a &= memory_read (reg_ix_iy.w + (int8_t) param.l);
                                      SET_FLAGS_AND; break;
        case 0xac: /* XOR A,IXH    */ z80_regs.a ^= reg_ix_iy.h; SET_FLAGS_OR_XOR; break;
        case 0xad: /* XOR A,IXL    */ z80_regs.a ^= reg_ix_iy.l; SET_FLAGS_OR_XOR; break;
        case 0xae: /* XOR A,(IX+*) */ z80_regs.a ^= memory_read (reg_ix_iy.w + (int8_t) param.l);
                                      SET_FLAGS_OR_XOR; break;

        case 0xb4: /* OR A,IXH     */ z80_regs.a |= reg_ix_iy.h; SET_FLAGS_OR_XOR; break;
        case 0xb5: /* OR A,IXL     */ z80_regs.a |= reg_ix_iy.l; SET_FLAGS_OR_XOR; break;
        case 0xb6: /* OR A,(IX+*)  */ z80_regs.a |= memory_read (reg_ix_iy.w + (int8_t) param.l);
                                      SET_FLAGS_OR_XOR; break;
        case 0xbc: /* CP  A,IXH    */ SET_FLAGS_SUB (reg_ix_iy.h); break;
        case 0xbd: /* CP  A,IXL    */ SET_FLAGS_SUB (reg_ix_iy.l); break;
        case 0xbe: /* CP  A,(IX+*) */ temp = memory_read (reg_ix_iy.w + (int8_t) param.l);
                                      SET_FLAGS_SUB (temp); break;

        case 0xcb: /* IX Bit Instructions */ z80_ix_iy_bit_instruction (reg_ix_iy.w); break;
        case 0xe1: /* POP IX       */ reg_ix_iy.l = memory_read (z80_regs.sp++);
                                      reg_ix_iy.h = memory_read (z80_regs.sp++); z80_cycle += 14; break;
        case 0xe5: /* PUSH IX      */ memory_write (--z80_regs.sp, reg_ix_iy.h);
                                      memory_write (--z80_regs.sp, reg_ix_iy.l);
                                      z80_cycle += 15; break;
        default:
        fprintf (stderr, "Unknown ix/iy instruction: \"%s\" (%02x).\n",
                 z80_instruction_name_ix[instruction], instruction);
        _abort_ = true;
    }

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
                               z80_regs.f |= (temp & 0x01) ? Z80_FLAG_CARRY : 0;
                               z80_cycle += (instruction & 0x07) == 0x06 ? 15 : 8; break;

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
                               z80_regs.f |= (temp & 0x01) ? Z80_FLAG_CARRY : 0;
                               z80_cycle += (instruction & 0x07) == 0x06 ? 15 : 8; break;

        case 0x40: case 0x48: case 0x50: case 0x58: /* BIT */
        case 0x60: case 0x68: case 0x70: case 0x78:
            z80_regs.f = (z80_regs.f & Z80_FLAG_CARRY) |
                         ((bit & data) ? 0 : Z80_FLAG_PARITY) |
                         (Z80_FLAG_HALF) |
                         ((bit & data) ? 0 : Z80_FLAG_ZERO) |
                         (((bit == BIT_7) && (data & BIT_7)) ? Z80_FLAG_SIGN : 0);
            write_data = false;
            z80_cycle += (instruction & 0x07) == 0x06 ? 12 : 8; break;

        case 0x80: case 0x88: case 0x90: case 0x98: /* RES */
        case 0xa0: case 0xa8: case 0xb0: case 0xb8:
            data &= ~bit;
            z80_cycle += (instruction & 0x07) == 0x06 ? 15 : 8; break;

        case 0xc0: case 0xc8: case 0xd0: case 0xd8: /* SET */
        case 0xe0: case 0xe8: case 0xf0: case 0xf8:
            data |= bit;
            z80_cycle += (instruction & 0x07) == 0x06 ? 15 : 8; break;

        default:
            fprintf (stderr, "Unknown bit instruction: \"%s\" (%02x).\n",
                     z80_instruction_name_bits[instruction], instruction);
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

    return 0;
}

void z80_instruction_daa ()
{
    bool set_carry = false;
    bool set_half = false;
    uint8_t diff;

    /* Calculate diff to apply */
    switch (z80_regs.f & (Z80_FLAG_CARRY | Z80_FLAG_HALF))
    {
        case Z80_FLAG_NONE:
                 if ((z80_regs.a & 0xf0) < 0xa0 && (z80_regs.a & 0x0f) < 0x0a)      diff = 0x00;
            else if ((z80_regs.a & 0xf0) < 0x90 && (z80_regs.a & 0x0f) > 0x09)      diff = 0x06;
            else if ((z80_regs.a & 0xf0) > 0x90 && (z80_regs.a & 0x0f) < 0x0a)      diff = 0x60;
            else if ((z80_regs.a & 0xf0) > 0x80 && (z80_regs.a & 0x0f) > 0x09)      diff = 0x66;
            break;
        case Z80_FLAG_HALF:
                 if ((z80_regs.a & 0xf0) < 0xa0 && (z80_regs.a & 0x0f) < 0x0a)      diff = 0x06;
            else if ((z80_regs.a & 0xf0) < 0x90 && (z80_regs.a & 0x0f) > 0x09)      diff = 0x06;
            else if ((z80_regs.a & 0xf0) > 0x80 && (z80_regs.a & 0x0f) > 0x09)      diff = 0x66;
            else if ((z80_regs.a & 0xf0) > 0x90 && (z80_regs.a & 0x0f) < 0x0a)      diff = 0x66;
            break;
        case Z80_FLAG_CARRY:
                 if (                              (z80_regs.a & 0x0f) < 0x0a)      diff = 0x60;
            else if (                              (z80_regs.a & 0x0f) > 0x09)      diff = 0x66;
            break;
        case Z80_FLAG_CARRY | Z80_FLAG_HALF:
                                                                                    diff = 0x66;
            break;
    }

    /* Calculate carry out */
    if (((z80_regs.a & 0xf0) > 0x80 && (z80_regs.a & 0x0f) > 0x09) ||
        ((z80_regs.a & 0xf0) > 0x90 && (z80_regs.a & 0x0f) < 0x0a) ||
        (z80_regs.f & Z80_FLAG_CARRY))
        set_carry = true;

    /* Calculate half-carry out */
    if ( (!(z80_regs.f & Z80_FLAG_SUB) && (z80_regs.a & 0x0f) > 0x09) ||
         ( (z80_regs.f & Z80_FLAG_SUB) && (z80_regs.f & Z80_FLAG_HALF) && (z80_regs.a & 0x0f) < 0x06))
        set_half = true;

    /* Apply diff */
    if (z80_regs.f & Z80_FLAG_SUB)
        z80_regs.a -= diff;
    else
        z80_regs.a += diff;

    z80_regs.f = (z80_regs.f                    & Z80_FLAG_SUB       ) |
                 (uint8_even_parity[z80_regs.a] ? Z80_FLAG_PARITY : 0) |
                 (set_carry                     ? Z80_FLAG_CARRY  : 0) |
                 (set_half                      ? Z80_FLAG_HALF   : 0) |
                 (z80_regs.a == 0x00            ? Z80_FLAG_ZERO   : 0) |
                 (z80_regs.a & 0x80             ? Z80_FLAG_SIGN   : 0);
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
            break;
        case 2:
            param.l = memory_read (z80_regs.pc++);
            break;
        default:
            break;
    }

    switch (instruction)
    {
        case 0x00: /* NOP        */ z80_cycle += 4; break;
        case 0x01: /* LD BC,**   */ z80_regs.bc = param.w;
                                    z80_cycle += 10; break;
        case 0x02: /* LD (BC),A  */ memory_write (z80_regs.bc, z80_regs.a); z80_cycle += 7;  break;
        case 0x03: /* INC BC     */ z80_regs.bc++; z80_cycle += 6; break;
        case 0x04: /* INC B      */ z80_regs.b++; SET_FLAGS_INC (z80_regs.b); z80_cycle += 4; break;
        case 0x05: /* DEC B      */ z80_regs.b--; SET_FLAGS_DEC (z80_regs.b); z80_cycle += 4; break;
        case 0x06: /* LD B,*     */ z80_regs.b = param.l; z80_cycle += 7; break;
        case 0x07: /* RLCA       */ z80_regs.a = (z80_regs.a << 1) | ((z80_regs.a & 0x80) ? 1 : 0); SET_FLAGS_RLCA (z80_regs.a);
                                    z80_cycle += 4; break;
        case 0x08: /* EX AF AF'  */ SWAP (uint16_t, z80_regs.af, z80_regs.alt_af); z80_cycle += 4; break;
        case 0x09: /* ADD HL,BC  */ SET_FLAGS_ADD_16 (z80_regs.hl, z80_regs.bc); z80_regs.hl += z80_regs.bc; z80_cycle += 11; break;
        case 0x0a: /* LD A,(BC)  */ z80_regs.a = memory_read (z80_regs.bc); z80_cycle += 7; break;
        case 0x0b: /* DEC BC     */ z80_regs.bc--; z80_cycle += 6; break;
        case 0x0c: /* INC C      */ z80_regs.c++; SET_FLAGS_INC (z80_regs.c); z80_cycle += 4; break;
        case 0x0d: /* DEC C      */ z80_regs.c--; SET_FLAGS_DEC (z80_regs.c); z80_cycle += 4; break;
        case 0x0e: /* LD C,*     */ z80_regs.c = param.l; z80_cycle += 7; break;
        case 0x0f: /* RRCA       */ z80_regs.a = (z80_regs.a >> 1) | ((z80_regs.a & 0x01) ? 0x80 : 0);
                                    z80_regs.f = (z80_regs.f & (Z80_FLAG_PARITY | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) |
                                                 ((z80_regs.a & 0x80) ? Z80_FLAG_CARRY : 0); z80_cycle += 4; break;

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
        case 0x11: /* LD DE,**   */ z80_regs.de = param.w;
                                    z80_cycle += 10; break;
        case 0x12: /* LD (DE),A  */ memory_write (z80_regs.de, z80_regs.a); z80_cycle += 7; break;
        case 0x13: /* INC DE     */ z80_regs.de++; z80_cycle += 6; break;
        case 0x14: /* INC D      */ z80_regs.d++; SET_FLAGS_INC (z80_regs.d); z80_cycle += 4; break;
        case 0x15: /* DEC D      */ z80_regs.d--; SET_FLAGS_DEC (z80_regs.d); z80_cycle += 4; break;
        case 0x16: /* LD D,*     */ z80_regs.d = param.l; z80_cycle += 7; break;
        case 0x17: /* RLA        */ temp = z80_regs.a;
                                    z80_regs.a = (z80_regs.a << 1) + ((z80_regs.f & Z80_FLAG_CARRY) ? 0x01 : 0);
                                    z80_regs.f = (z80_regs.f & (Z80_FLAG_PARITY | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) |
                                                 ((temp & 0x80) ? Z80_FLAG_CARRY : 0); break;
        case 0x18: /* JR *       */ z80_regs.pc += (int8_t)param.l; z80_cycle += 12; break;
        case 0x19: /* ADD HL,DE  */ SET_FLAGS_ADD_16 (z80_regs.hl, z80_regs.de); z80_regs.hl += z80_regs.de; z80_cycle += 11; break;
        case 0x1a: /* LD A,(DE)  */ z80_regs.a = memory_read (z80_regs.de); z80_cycle += 7; break;
        case 0x1b: /* DEC DE     */ z80_regs.de--; z80_cycle += 6; break;
        case 0x1c: /* INC E      */ z80_regs.e++; SET_FLAGS_INC (z80_regs.e); z80_cycle += 4; break;
        case 0x1d: /* DEC E      */ z80_regs.e--; SET_FLAGS_DEC (z80_regs.e); z80_cycle += 4; break;
        case 0x1e: /* LD E,*     */ z80_regs.e = param.l; z80_cycle += 7; break;
        case 0x1f: /* RRA        */ temp = z80_regs.a;
                                    z80_regs.a = (z80_regs.a >> 1) + ((z80_regs.f & Z80_FLAG_CARRY) ? 0x80 : 0);
                                    z80_regs.f = (z80_regs.f & (Z80_FLAG_PARITY | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) |
                                                 ((temp & 0x01) ? Z80_FLAG_CARRY : 0); z80_cycle += 4; break;

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
        case 0x21: /* LD HL,**   */ z80_regs.hl = param.w;
                                    z80_cycle += 10; break;
        case 0x22: /* LD (**),HL */ memory_write (param.w,     z80_regs.l);
                                    memory_write (param.w + 1, z80_regs.h); z80_cycle += 16; break;
        case 0x23: /* INC HL     */ z80_regs.hl++; z80_cycle += 6; break;
        case 0x24: /* INC H      */ z80_regs.h++; SET_FLAGS_INC (z80_regs.h); z80_cycle += 4; break;
        case 0x25: /* DEC H      */ z80_regs.h--; SET_FLAGS_DEC (z80_regs.h); z80_cycle += 4; break;
        case 0x26: /* LD H,*     */ z80_regs.h = param.l; z80_cycle += 7; break;
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
        case 0x29: /* ADD HL,HL  */ SET_FLAGS_ADD_16 (z80_regs.hl, z80_regs.hl); z80_regs.hl += z80_regs.hl; z80_cycle += 11; break;
        case 0x2a: /* LD HL,(**) */ z80_regs.l = memory_read (param.w);
                                    z80_regs.h = memory_read (param.w + 1); z80_cycle += 16; break;
        case 0x2b: /* DEC HL     */ z80_regs.hl--; z80_cycle += 6; break;
        case 0x2c: /* INC L      */ z80_regs.l++; SET_FLAGS_INC (z80_regs.l); z80_cycle += 4; z80_cycle += 4; break;
        case 0x2d: /* DEC L      */ z80_regs.l--; SET_FLAGS_DEC (z80_regs.l); z80_cycle += 4; break;
        case 0x2e: /* LD L,*     */ z80_regs.l = param.l; z80_cycle += 7; break;
        case 0x2f: /* CPL        */ z80_regs.a = ~z80_regs.a; SET_FLAGS_CPL; z80_cycle += 4; break;

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
        case 0x31: /* LD SP,**   */ z80_regs.sp = param.w;
                                    z80_cycle += 10; break;
        case 0x32: /* LD (**),A  */ memory_write (param.w, z80_regs.a); z80_cycle += 13; break;
        case 0x33: /* INC SP     */ z80_regs.sp++; z80_cycle += 6; break;
        case 0x34: /* INC (HL)   */ temp = memory_read (z80_regs.hl);
                                    temp++;
                                    memory_write (z80_regs.hl, temp);
                                    SET_FLAGS_INC (temp); z80_cycle += 11; break;
        case 0x35: /* DEC (HL)   */ temp = memory_read (z80_regs.hl);
                                    temp--;
                                    memory_write (z80_regs.hl, temp);
                                    SET_FLAGS_DEC (temp); break;
        case 0x36: /* LD (HL),*  */ memory_write (z80_regs.hl, param.l);
                                    z80_cycle += 10; break;
        case 0x37: /* SCF        */ z80_regs.f = (z80_regs.f & (Z80_FLAG_SIGN | Z80_FLAG_ZERO | Z80_FLAG_OVERFLOW)) | Z80_FLAG_CARRY;
                                    break;
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
        case 0x39: /* ADD HL,SP  */ SET_FLAGS_ADD_16 (z80_regs.hl, z80_regs.sp); z80_regs.hl += z80_regs.sp; z80_cycle += 11; break;
        case 0x3a: /* LD A,(**)  */ z80_regs.a = memory_read (param.w); z80_cycle += 13; break;
        case 0x3b: /* DEC SP     */ z80_regs.sp--; z80_cycle += 6; break;
        case 0x3c: /* INC A      */ z80_regs.a++; SET_FLAGS_INC (z80_regs.a); z80_cycle += 4; break;
        case 0x3d: /* DEC A      */ z80_regs.a--; SET_FLAGS_DEC (z80_regs.a); z80_cycle += 4; break;
        case 0x3e: /* LD A,*     */ z80_regs.a = param.l; z80_cycle += 7; break;
        case 0x3f: /* CCF        */ z80_regs.f = (z80_regs.f & (Z80_FLAG_SIGN | Z80_FLAG_ZERO | Z80_FLAG_OVERFLOW)) |
                                                 (CARRY_BIT ? Z80_FLAG_HALF : Z80_FLAG_CARRY);
                                    break;

        case 0x40: /* LD B,B     */ z80_regs.b = z80_regs.b; z80_cycle += 4; break;
        case 0x41: /* LD B,C     */ z80_regs.b = z80_regs.c; z80_cycle += 4; break;
        case 0x42: /* LD B,D     */ z80_regs.b = z80_regs.d; z80_cycle += 4; break;
        case 0x43: /* LD B,E     */ z80_regs.b = z80_regs.e; z80_cycle += 4; break;
        case 0x44: /* LD B,H     */ z80_regs.b = z80_regs.h; z80_cycle += 4; break;
        case 0x45: /* LD B,L     */ z80_regs.b = z80_regs.l; z80_cycle += 4; break;
        case 0x46: /* LD B,(HL)  */ z80_regs.b = memory_read(z80_regs.hl); z80_cycle += 7; break;
        case 0x47: /* LD B,A     */ z80_regs.b = z80_regs.a; z80_cycle += 4; break;

        case 0x48: /* LD C,B     */ z80_regs.c = z80_regs.b; z80_cycle += 4; break;
        case 0x49: /* LD C,C     */ z80_regs.c = z80_regs.c; z80_cycle += 4; break;
        case 0x4a: /* LD C,D     */ z80_regs.c = z80_regs.d; z80_cycle += 4; break;
        case 0x4b: /* LD C,E     */ z80_regs.c = z80_regs.e; z80_cycle += 4; break;
        case 0x4c: /* LD C,H     */ z80_regs.c = z80_regs.h; z80_cycle += 4; break;
        case 0x4d: /* LD C,L     */ z80_regs.c = z80_regs.l; z80_cycle += 4; break;
        case 0x4e: /* LD C,(HL)  */ z80_regs.c = memory_read(z80_regs.hl); z80_cycle += 7; break;
        case 0x4f: /* LD C,A     */ z80_regs.c = z80_regs.a; z80_cycle += 4; break;

        case 0x50: /* LD D,B     */ z80_regs.d = z80_regs.b; z80_cycle += 4; break;
        case 0x51: /* LD D,C     */ z80_regs.d = z80_regs.c; z80_cycle += 4; break;
        case 0x52: /* LD D,D     */ z80_regs.d = z80_regs.d; z80_cycle += 4; break;
        case 0x53: /* LD D,E     */ z80_regs.d = z80_regs.e; z80_cycle += 4; break;
        case 0x54: /* LD D,H     */ z80_regs.d = z80_regs.h; z80_cycle += 4; break;
        case 0x55: /* LD D,L     */ z80_regs.d = z80_regs.l; z80_cycle += 4; break;
        case 0x56: /* LD D,(HL)  */ z80_regs.d = memory_read(z80_regs.hl); z80_cycle += 7; break;
        case 0x57: /* LD D,A     */ z80_regs.d = z80_regs.a; z80_cycle += 4; break;

        case 0x58: /* LD E,B     */ z80_regs.e = z80_regs.b; z80_cycle += 4; break;
        case 0x59: /* LD E,C     */ z80_regs.e = z80_regs.c; z80_cycle += 4; break;
        case 0x5a: /* LD E,D     */ z80_regs.e = z80_regs.d; z80_cycle += 4; break;
        case 0x5b: /* LD E,E     */ z80_regs.e = z80_regs.e; z80_cycle += 4; break;
        case 0x5c: /* LD E,H     */ z80_regs.e = z80_regs.h; z80_cycle += 4; break;
        case 0x5d: /* LD E,L     */ z80_regs.e = z80_regs.l; z80_cycle += 4; break;
        case 0x5e: /* LD E,(HL)  */ z80_regs.e = memory_read(z80_regs.hl); z80_cycle += 7; break;
        case 0x5f: /* LD E,A     */ z80_regs.e = z80_regs.a; z80_cycle += 4; break;

        case 0x60: /* LD H,B     */ z80_regs.h = z80_regs.b; z80_cycle += 4; break;
        case 0x61: /* LD H,C     */ z80_regs.h = z80_regs.c; z80_cycle += 4; break;
        case 0x62: /* LD H,D     */ z80_regs.h = z80_regs.d; z80_cycle += 4; break;
        case 0x63: /* LD H,E     */ z80_regs.h = z80_regs.e; z80_cycle += 4; break;
        case 0x64: /* LD H,H     */ z80_regs.h = z80_regs.h; z80_cycle += 4; break;
        case 0x65: /* LD H,L     */ z80_regs.h = z80_regs.l; z80_cycle += 4; break;
        case 0x66: /* LD H,(HL)  */ z80_regs.h = memory_read(z80_regs.hl); z80_cycle += 7; break;
        case 0x67: /* LD H,A     */ z80_regs.h = z80_regs.a; z80_cycle += 4; break;

        case 0x68: /* LD L,B     */ z80_regs.l = z80_regs.b; z80_cycle += 4; break;
        case 0x69: /* LD L,C     */ z80_regs.l = z80_regs.c; z80_cycle += 4; break;
        case 0x6a: /* LD L,D     */ z80_regs.l = z80_regs.d; z80_cycle += 4; break;
        case 0x6b: /* LD L,E     */ z80_regs.l = z80_regs.e; z80_cycle += 4; break;
        case 0x6c: /* LD L,H     */ z80_regs.l = z80_regs.h; z80_cycle += 4; break;
        case 0x6d: /* LD L,L     */ z80_regs.l = z80_regs.l; z80_cycle += 4; break;
        case 0x6e: /* LD L,(HL)  */ z80_regs.l = memory_read(z80_regs.hl); z80_cycle += 7; break;
        case 0x6f: /* LD L,A     */ z80_regs.l = z80_regs.a; z80_cycle += 4; break;

        case 0x70: /* LD (HL),B  */ memory_write (z80_regs.hl, z80_regs.b); z80_cycle += 7; break;
        case 0x71: /* LD (HL),C  */ memory_write (z80_regs.hl, z80_regs.c); z80_cycle += 7; break;
        case 0x72: /* LD (HL),D  */ memory_write (z80_regs.hl, z80_regs.d); z80_cycle += 7; break;
        case 0x73: /* LD (HL),E  */ memory_write (z80_regs.hl, z80_regs.e); z80_cycle += 7; break;
        case 0x74: /* LD (HL),H  */ memory_write (z80_regs.hl, z80_regs.h); z80_cycle += 7; break;
        case 0x75: /* LD (HL),L  */ memory_write (z80_regs.hl, z80_regs.l); z80_cycle += 7; break;
        case 0x77: /* LD (HL),A  */ memory_write (z80_regs.hl, z80_regs.a); z80_cycle += 7; break;

        case 0x78: /* LD A,B     */ z80_regs.a = z80_regs.b; z80_cycle += 4; break;
        case 0x79: /* LD A,C     */ z80_regs.a = z80_regs.c; z80_cycle += 4; break;
        case 0x7a: /* LD A,D     */ z80_regs.a = z80_regs.d; z80_cycle += 4; break;
        case 0x7b: /* LD A,E     */ z80_regs.a = z80_regs.e; z80_cycle += 4; break;
        case 0x7c: /* LD A,H     */ z80_regs.a = z80_regs.h; z80_cycle += 4; break;
        case 0x7d: /* LD A,L     */ z80_regs.a = z80_regs.l; z80_cycle += 4; break;
        case 0x7e: /* LD A,(HL)  */ z80_regs.a = memory_read(z80_regs.hl); z80_cycle += 7; break;
        case 0x7f: /* LD A,A     */ z80_regs.a = z80_regs.a; z80_cycle += 4; break;

        case 0x80: /* ADD A,B    */ SET_FLAGS_ADD (z80_regs.b); z80_regs.a += z80_regs.b; z80_cycle += 4; break;
        case 0x81: /* ADD A,C    */ SET_FLAGS_ADD (z80_regs.c); z80_regs.a += z80_regs.c; z80_cycle += 4; break;
        case 0x82: /* ADD A,D    */ SET_FLAGS_ADD (z80_regs.d); z80_regs.a += z80_regs.d; z80_cycle += 4; break;
        case 0x83: /* ADD A,E    */ SET_FLAGS_ADD (z80_regs.e); z80_regs.a += z80_regs.e; z80_cycle += 4; break;
        case 0x84: /* ADD A,H    */ SET_FLAGS_ADD (z80_regs.h); z80_regs.a += z80_regs.h; z80_cycle += 4; break;
        case 0x85: /* ADD A,L    */ SET_FLAGS_ADD (z80_regs.l); z80_regs.a += z80_regs.l; z80_cycle += 4; break;
        case 0x86: /* ADD A,(HL) */ temp = memory_read (z80_regs.hl);
                                    SET_FLAGS_ADD (temp); z80_regs.a += temp; z80_cycle += 7; break;
        case 0x87: /* ADD A,A    */ SET_FLAGS_ADD (z80_regs.a); z80_regs.a += z80_regs.a; z80_cycle += 4; break;

        case 0x88: /* ADC A,B    */ temp = z80_regs.b + CARRY_BIT; SET_FLAGS_ADC (z80_regs.b); z80_regs.a += temp; z80_cycle += 4; break;
        case 0x89: /* ADC A,C    */ temp = z80_regs.c + CARRY_BIT; SET_FLAGS_ADC (z80_regs.c); z80_regs.a += temp; z80_cycle += 4; break;
        case 0x8a: /* ADC A,D    */ temp = z80_regs.d + CARRY_BIT; SET_FLAGS_ADC (z80_regs.d); z80_regs.a += temp; z80_cycle += 4; break;
        case 0x8b: /* ADC A,E    */ temp = z80_regs.e + CARRY_BIT; SET_FLAGS_ADC (z80_regs.e); z80_regs.a += temp; z80_cycle += 4; break;
        case 0x8c: /* ADC A,H    */ temp = z80_regs.h + CARRY_BIT; SET_FLAGS_ADC (z80_regs.h); z80_regs.a += temp; z80_cycle += 4; break;
        case 0x8d: /* ADC A,L    */ temp = z80_regs.l + CARRY_BIT; SET_FLAGS_ADC (z80_regs.l); z80_regs.a += temp; z80_cycle += 4; break;
        case 0x8e: /* ADC A,(HL) */ { uint8_t value_read = memory_read (z80_regs.hl);
                                      temp = value_read + CARRY_BIT;
                                      SET_FLAGS_ADC (value_read); z80_regs.a += temp; } break;
        case 0x8f: /* ADC A,A    */ temp = z80_regs.a + CARRY_BIT; SET_FLAGS_ADC (z80_regs.a); z80_regs.a += temp; z80_cycle += 4; break;

        case 0x90: /* SUB A,B    */ SET_FLAGS_SUB (z80_regs.b); z80_regs.a -= z80_regs.b; z80_cycle += 4; break;
        case 0x91: /* SUB A,C    */ SET_FLAGS_SUB (z80_regs.c); z80_regs.a -= z80_regs.c; z80_cycle += 4; break;
        case 0x92: /* SUB A,D    */ SET_FLAGS_SUB (z80_regs.d); z80_regs.a -= z80_regs.d; z80_cycle += 4; break;
        case 0x93: /* SUB A,E    */ SET_FLAGS_SUB (z80_regs.e); z80_regs.a -= z80_regs.e; z80_cycle += 4; break;
        case 0x94: /* SUB A,H    */ SET_FLAGS_SUB (z80_regs.h); z80_regs.a -= z80_regs.h; z80_cycle += 4; break;
        case 0x95: /* SUB A,L    */ SET_FLAGS_SUB (z80_regs.l); z80_regs.a -= z80_regs.l; z80_cycle += 4; break;
        case 0x96: /* SUB A,(HL) */ temp = memory_read (z80_regs.hl);
                                    SET_FLAGS_SUB (temp); z80_regs.a -= temp; break;
        case 0x97: /* SUB A,A    */ SET_FLAGS_SUB (z80_regs.a); z80_regs.a -= z80_regs.a; z80_cycle += 4; break;

        case 0x98: /* SBC A,B    */ temp = z80_regs.b + CARRY_BIT; SET_FLAGS_SBC (z80_regs.b); z80_regs.a -= temp; z80_cycle += 4; break;
        case 0x99: /* SBC A,C    */ temp = z80_regs.c + CARRY_BIT; SET_FLAGS_SBC (z80_regs.c); z80_regs.a -= temp; z80_cycle += 4; break;
        case 0x9a: /* SBC A,D    */ temp = z80_regs.d + CARRY_BIT; SET_FLAGS_SBC (z80_regs.d); z80_regs.a -= temp; z80_cycle += 4; break;
        case 0x9b: /* SBC A,E    */ temp = z80_regs.e + CARRY_BIT; SET_FLAGS_SBC (z80_regs.e); z80_regs.a -= temp; z80_cycle += 4; break;
        case 0x9c: /* SBC A,H    */ temp = z80_regs.h + CARRY_BIT; SET_FLAGS_SBC (z80_regs.h); z80_regs.a -= temp; z80_cycle += 4; break;
        case 0x9d: /* SBC A,L    */ temp = z80_regs.l + CARRY_BIT; SET_FLAGS_SBC (z80_regs.l); z80_regs.a -= temp; z80_cycle += 4; break;
        case 0x9e: /* SBC A,(HL) */ { uint8_t value_read = memory_read (z80_regs.hl);
                                      temp = value_read + CARRY_BIT;
                                      SET_FLAGS_SBC (value_read); z80_regs.a -= temp; } break;
        case 0x9f: /* SBC A,A    */ temp = z80_regs.a + CARRY_BIT; SET_FLAGS_SBC (z80_regs.a); z80_regs.a -= temp; z80_cycle += 4; break;

        case 0xa0: /* AND A,B    */ z80_regs.a &= z80_regs.b; SET_FLAGS_AND; z80_cycle += 4; break;
        case 0xa1: /* AND A,C    */ z80_regs.a &= z80_regs.c; SET_FLAGS_AND; z80_cycle += 4; break;
        case 0xa2: /* AND A,D    */ z80_regs.a &= z80_regs.d; SET_FLAGS_AND; z80_cycle += 4; break;
        case 0xa3: /* AND A,E    */ z80_regs.a &= z80_regs.e; SET_FLAGS_AND; z80_cycle += 4; break;
        case 0xa4: /* AND A,H    */ z80_regs.a &= z80_regs.h; SET_FLAGS_AND; z80_cycle += 4; break;
        case 0xa5: /* AND A,L    */ z80_regs.a &= z80_regs.l; SET_FLAGS_AND; z80_cycle += 4; break;
        case 0xa6: /* AND A,(HL) */ z80_regs.a &= memory_read (z80_regs.hl); SET_FLAGS_AND; break;
        case 0xa7: /* AND A,A    */ z80_regs.a &= z80_regs.a; SET_FLAGS_AND; z80_cycle += 4; break;

        case 0xa8: /* XOR A,B    */ z80_regs.a ^= z80_regs.b; SET_FLAGS_OR_XOR; z80_cycle += 4; break;
        case 0xa9: /* XOR A,C    */ z80_regs.a ^= z80_regs.c; SET_FLAGS_OR_XOR; z80_cycle += 4; break;
        case 0xaa: /* XOR A,D    */ z80_regs.a ^= z80_regs.d; SET_FLAGS_OR_XOR; z80_cycle += 4; break;
        case 0xab: /* XOR A,E    */ z80_regs.a ^= z80_regs.e; SET_FLAGS_OR_XOR; z80_cycle += 4; break;
        case 0xac: /* XOR A,H    */ z80_regs.a ^= z80_regs.h; SET_FLAGS_OR_XOR; z80_cycle += 4; break;
        case 0xad: /* XOR A,L    */ z80_regs.a ^= z80_regs.l; SET_FLAGS_OR_XOR; z80_cycle += 4; break;
        case 0xae: /* XOR A,(HL) */ z80_regs.a ^= memory_read(z80_regs.hl); SET_FLAGS_OR_XOR; z80_cycle += 7; break;
        case 0xaf: /* XOR A,A    */ z80_regs.a ^= z80_regs.a; SET_FLAGS_OR_XOR; z80_cycle += 4; break;

        case 0xb0: /* OR  A,B    */ z80_regs.a |= z80_regs.b; SET_FLAGS_OR_XOR; z80_cycle += 4; break;
        case 0xb1: /* OR  A,C    */ z80_regs.a |= z80_regs.c; SET_FLAGS_OR_XOR; z80_cycle += 4; break;
        case 0xb2: /* OR  A,D    */ z80_regs.a |= z80_regs.d; SET_FLAGS_OR_XOR; z80_cycle += 4; break;
        case 0xb3: /* OR  A,E    */ z80_regs.a |= z80_regs.e; SET_FLAGS_OR_XOR; z80_cycle += 4; break;
        case 0xb4: /* OR  A,H    */ z80_regs.a |= z80_regs.h; SET_FLAGS_OR_XOR; z80_cycle += 4; break;
        case 0xb5: /* OR  A,L    */ z80_regs.a |= z80_regs.l; SET_FLAGS_OR_XOR; z80_cycle += 4; break;
        case 0xb6: /* OR (HL)    */ z80_regs.a |= memory_read (z80_regs.hl); SET_FLAGS_OR_XOR; break;
        case 0xb7: /* OR  A,A    */ z80_regs.a |= z80_regs.a; SET_FLAGS_OR_XOR; z80_cycle += 4; break;

        case 0xb8: /* CP A,B     */ SET_FLAGS_SUB (z80_regs.b); z80_cycle += 4; break;
        case 0xb9: /* CP A,C     */ SET_FLAGS_SUB (z80_regs.c); z80_cycle += 4; break;
        case 0xba: /* CP A,D     */ SET_FLAGS_SUB (z80_regs.d); z80_cycle += 4; break;
        case 0xbb: /* CP A,E     */ SET_FLAGS_SUB (z80_regs.e); z80_cycle += 4; break;
        case 0xbc: /* CP A,H     */ SET_FLAGS_SUB (z80_regs.h); z80_cycle += 4; break;
        case 0xbd: /* CP A,L     */ SET_FLAGS_SUB (z80_regs.l); z80_cycle += 4; break;
        case 0xbe: /* CP A,(HL)  */ temp = memory_read (z80_regs.hl); SET_FLAGS_SUB (temp); z80_cycle += 7; break;
        case 0xbf: /* CP A,A     */ SET_FLAGS_SUB (z80_regs.a); z80_cycle += 4; break;

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
                                    z80_regs.b = memory_read (z80_regs.sp++); z80_cycle += 10; break;
        case 0xc2: /* JP NZ,**   */ z80_regs.pc = (z80_regs.f & Z80_FLAG_ZERO) ? z80_regs.pc : param.w; z80_cycle += 10; break;
        case 0xc3: /* JP **      */ z80_regs.pc = param.w;
                                    z80_cycle += 10; break;
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
                                    memory_write (--z80_regs.sp, z80_regs.c); z80_cycle += 11; break;
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
                                    z80_regs.pc_h = memory_read (z80_regs.sp++); z80_cycle += 10; break;
        case 0xca: /* JP Z,**    */ z80_regs.pc = (z80_regs.f & Z80_FLAG_ZERO) ? param.w : z80_regs.pc; z80_cycle += 10; break;
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
        case 0xcd: /* CALL **    */ memory_write (--z80_regs.sp, z80_regs.pc_h);
                                    memory_write (--z80_regs.sp, z80_regs.pc_l);
                                    z80_regs.pc = param.w;
                                    z80_cycle += 17; break;
        case 0xce: /* ADC A,*    */ temp = param.l + CARRY_BIT;
                                    SET_FLAGS_ADC (param.l); z80_regs.a += temp; z80_cycle += 7; break;
        case 0xcf: /* RST 08h    */ memory_write (--z80_regs.sp, z80_regs.pc_h);
                                    memory_write (--z80_regs.sp, z80_regs.pc_l);
                                    z80_regs.pc = 0x08; z80_cycle += 11; break;

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
                                    z80_regs.d = memory_read (z80_regs.sp++); z80_cycle += 10; break;
        case 0xd2: /* JP NC,**   */ z80_regs.pc = (z80_regs.f & Z80_FLAG_CARRY) ? z80_regs.pc : param.w; z80_cycle += 10; break;
        case 0xd3: /* OUT (*),A  */ io_write (param.l, z80_regs.a); z80_cycle += 11; break;
        case 0xd5: /* PUSH DE    */ memory_write (--z80_regs.sp, z80_regs.d);
                                    memory_write (--z80_regs.sp, z80_regs.e); z80_cycle += 11; break;
        case 0xd6: /* SUB A,*    */ SET_FLAGS_SUB (param.l); z80_regs.a -= param.l; z80_cycle += 7; break;
        case 0xd7: /* RST 10h    */ memory_write (--z80_regs.sp, z80_regs.pc_h);
                                    memory_write (--z80_regs.sp, z80_regs.pc_l);
                                    z80_regs.pc = 0x10; z80_cycle += 11; break;
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
                                    SWAP (uint16_t, z80_regs.hl, z80_regs.alt_hl);
                                    z80_cycle += 4; break;
        case 0xda: /* JP C,**    */ z80_regs.pc = (z80_regs.f & Z80_FLAG_CARRY) ? param.w : z80_regs.pc; z80_cycle += 10; break;
        case 0xdb: /* IN A,(*)   */ z80_regs.a = io_read (param.l); z80_cycle += 11; break;
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
        case 0xde: /* SBC A,*    */ temp = param.l + CARRY_BIT;
                                    SET_FLAGS_SBC (param.l); z80_regs.a -= temp; break;
        case 0xdf: /* RST 18h    */ memory_write (--z80_regs.sp, z80_regs.pc_h);
                                    memory_write (--z80_regs.sp, z80_regs.pc_l);
                                    z80_regs.pc = 0x18; z80_cycle += 11; break;

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
                                    z80_regs.h = memory_read (z80_regs.sp++); z80_cycle += 10; break;
        case 0xe2: /* JP PO      */ z80_regs.pc = (z80_regs.f & Z80_FLAG_PARITY) ? z80_regs.pc : param.w; z80_cycle += 10; break;
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
                                    memory_write (--z80_regs.sp, z80_regs.l); z80_cycle += 11; break;
        case 0xe6: /* AND A,*    */ z80_regs.a &= param.l; SET_FLAGS_AND; z80_cycle += 7; break;
        case 0xe7: /* RST 20h    */ memory_write (--z80_regs.sp, z80_regs.pc_h);
                                    memory_write (--z80_regs.sp, z80_regs.pc_l);
                                    z80_regs.pc = 0x20; z80_cycle += 11; break;
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
        case 0xe9: /* JP (HL)    */ z80_regs.pc = param.w; z80_cycle += 4; break;
        case 0xea: /* JP PE,**   */ z80_regs.pc = (z80_regs.f & Z80_FLAG_PARITY) ? param.w : z80_regs.pc; z80_cycle += 10; break;
        case 0xeb: /* EX DE,HL   */ SWAP (uint16_t, z80_regs.de, z80_regs.hl); z80_cycle += 4; break;
        case 0xed: /* Extended Instructions */ z80_extended_instruction (); break;
        case 0xee: /* XOR A,*    */ z80_regs.a ^= param.l; SET_FLAGS_OR_XOR; z80_cycle += 7; break;
        case 0xef: /* RST 28h    */ memory_write (--z80_regs.sp, z80_regs.pc_h);
                                    memory_write (--z80_regs.sp, z80_regs.pc_l);
                                    z80_regs.pc = 0x28; z80_cycle += 11; break;

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
                                    z80_regs.a = memory_read (z80_regs.sp++); z80_cycle += 10; break;
        case 0xf2: /* JP P,**    */ z80_regs.pc = (z80_regs.f & Z80_FLAG_SIGN) ? z80_regs.pc : param.w; z80_cycle += 10; break;
        case 0xf3: /* DI         */ fprintf (stdout, "[DEBUG]: Interrupts disable.\n");
                                    z80_regs.iff1 = false; z80_regs.iff2 = false;
                                    z80_cycle += 4; break;
        case 0xf5: /* PUSH AF    */ memory_write (--z80_regs.sp, z80_regs.a);
                                    memory_write (--z80_regs.sp, z80_regs.f); z80_cycle += 11; break;
        case 0xf6: /* OR A,*     */ z80_regs.a |= param.l; SET_FLAGS_OR_XOR; z80_cycle += 7; break;
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
        case 0xf9: /* LD SP,HL   */ z80_regs.sp = z80_regs.hl; z80_cycle += 6; break;
        case 0xfa: /* JP M,**    */ z80_regs.pc = (z80_regs.f & Z80_FLAG_SIGN) ? param.w : z80_regs.pc; z80_cycle += 10; break;
        case 0xfb: /* EI         */ fprintf (stdout, "[DEBUG]: Interrupts enable.\n");
                                    z80_regs.iff1 = true; z80_regs.iff2 = true; instructions_before_interrupts = 2; z80_cycle += 4; break;
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

        case 0xfe: /* CP A,*     */ SET_FLAGS_SUB (param.l); z80_cycle += 7; break;
        case 0xff: /* RST 38h    */ memory_write (--z80_regs.sp, z80_regs.pc_h);
                                    memory_write (--z80_regs.sp, z80_regs.pc_l);
                                    z80_regs.pc = 0x38; z80_cycle += 11; break;

        default:
            fprintf (stderr, "Unknown instruction: \"%s\" (%02x).\n",
                     z80_instruction_name[instruction], instruction);
            _abort_ = true;
    }
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
        z80_instruction ();

        /* Time has passed, update the VDP state */
        vdp_clock_update (z80_cycle);

        /* Check for interrupts */
        if (instructions_before_interrupts)
            instructions_before_interrupts--;

        if (!instructions_before_interrupts && z80_regs.iff1 && vdp_get_interrupt ())
        {
            z80_regs.iff1 = false;
            z80_regs.iff2 = false;

            switch (z80_regs.im)
            {
                /* TODO: Cycle count? */
                case 1:
                    /* RST 0x38 */
                    printf ("INTERRUPT: RST 0x38  <--\n");
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
