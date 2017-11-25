#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../snepulator.h"
extern Snepulator snepulator;

#include "z80.h"
#include "z80_names.h"

#define SWAP(TYPE, X, Y) { TYPE tmp = X; X = Y; Y = tmp; }


/* State */
Z80_Regs z80_regs;

/* 8-bit register access */
#define I z80_regs.i
#define R z80_regs.r
#define A z80_regs.a
#define F z80_regs.f
#define B z80_regs.b
#define C z80_regs.c
#define D z80_regs.d
#define E z80_regs.e
#define H z80_regs.h
#define L z80_regs.l

/* 16-bit register access */
#define AF z80_regs.af
#define BC z80_regs.bc
#define DE z80_regs.de
#define HL z80_regs.hl
#define IX z80_regs.ix
#define IY z80_regs.iy
#define SP z80_regs.sp
#define PC z80_regs.pc

#define IFF1 z80_regs.iff1
#define IFF2 z80_regs.iff2

/* Immediate values */
#define N  param.l
#define NN param.w

/* Cycle count */
/* TODO: At some point this will wrap around… */
uint64_t z80_cycle = 0;
#define CYCLES(X) { z80_cycle += (X); }

/* Function pointers for accessing the rest of the system */
uint8_t (* memory_read) (uint16_t) = NULL;
void    (* memory_write)(uint16_t, uint8_t) = NULL;
uint8_t (* io_read)     (uint8_t) = NULL;
void    (* io_write)    (uint8_t, uint8_t) = NULL;

/* TODO: For IX/IY, can we do a SWAP (IX,HL) before/after the instruction? */

/* TODO: Note: Interrupts should not be accepted until after the instruction following EI */

/* TODO: Consider the accuracy of the R register */

uint8_t instructions_before_interrupts = 0;

#define X 1 /* Extended */
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
    1, 1, 3, 3, 3, 1, 2, 1, 1, 1, 3, X, 3, 3, 2, 1,
    1, 1, 3, 2, 3, 1, 2, 1, 1, 1, 3, 2, 3, X, 2, 1,
    1, 1, 3, 1, 3, 1, 2, 1, 1, 1, 3, 1, 3, X, 2, 1,
    1, 1, 3, 1, 3, 1, 2, 1, 1, 1, 3, 1, 3, X, 2, 1,
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
    U, U, U, U, U, U, U, U, U, U, U, X, U, U, U, U,
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

void z80_init (uint8_t (* _memory_read) (uint16_t),
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
    AF = 0xffff;
    SP = 0xffff;
}

#define SET_FLAGS_AND { F = (uint8_even_parity[z80_regs.a] ? Z80_FLAG_PARITY : 0) | \
                            (                                Z80_FLAG_HALF      ) | \
                            (A == 0x00            ? Z80_FLAG_ZERO   : 0) | \
                            ((A & 0x80)           ? Z80_FLAG_SIGN   : 0); }

#define SET_FLAGS_OR_XOR { F = (uint8_even_parity[z80_regs.a] ? Z80_FLAG_PARITY : 0) | \
                               (A == 0x00                     ? Z80_FLAG_ZERO   : 0) | \
                               ((A & 0x80)                    ? Z80_FLAG_SIGN   : 0); }

#define SET_FLAGS_ADD(X,Y) { F = (((uint16_t)X + (uint16_t)Y) & 0x100                    ? Z80_FLAG_CARRY    : 0) | \
                                 (((((int16_t)(int8_t)X) + ((int16_t)(int8_t)Y)) >  127 ||                          \
                                   (((int16_t)(int8_t)X) + ((int16_t)(int8_t)Y)) < -128) ? Z80_FLAG_OVERFLOW : 0) | \
                                 (((X & 0x0f) + (Y & 0x0f)) & 0x10                       ? Z80_FLAG_HALF     : 0) | \
                                 (((X + Y) & 0xff) == 0x00                               ? Z80_FLAG_ZERO     : 0) | \
                                 ((X + Y) & 0x80                                         ? Z80_FLAG_SIGN     : 0); }

#define SET_FLAGS_SUB(X,Y) { F = (((uint16_t)X - (uint16_t)Y) & 0x100          ? Z80_FLAG_CARRY    : 0) | \
                                 (                                               Z80_FLAG_SUB         ) | \
                                 (((((int16_t)(int8_t)X) - ((int16_t)(int8_t)Y)) >  127 ||                \
                                   (((int16_t)(int8_t)X) - ((int16_t)(int8_t)Y)) < -128) ? Z80_FLAG_OVERFLOW : 0) | \
                                 (((X & 0x0f) - (Y & 0x0f)) & 0x10             ? Z80_FLAG_HALF     : 0) | \
                                 ((X == Y)                                     ? Z80_FLAG_ZERO     : 0) | \
                                 ((X - Y) & 0x80                               ? Z80_FLAG_SIGN     : 0); }

#define CARRY_BIT (F & Z80_FLAG_CARRY)

#define SET_FLAGS_ADC(X) { F = (((uint16_t)A + (uint16_t)X + CARRY_BIT) & 0x100  ? Z80_FLAG_CARRY    : 0) | \
                               (((((int16_t)(int8_t)z80_regs.a) + ((int16_t)(int8_t)X) + CARRY_BIT) >  127 || \
                                 (((int16_t)(int8_t)z80_regs.a) + ((int16_t)(int8_t)X) + CARRY_BIT) < -128) ? Z80_FLAG_OVERFLOW : 0) | \
                               (((A & 0x0f) + (X & 0x0f) + CARRY_BIT) & 0x10     ? Z80_FLAG_HALF     : 0) | \
                               (((A + X + CARRY_BIT) & 0xff) == 0x00             ? Z80_FLAG_ZERO     : 0) | \
                               ((A + X + CARRY_BIT) & 0x80                       ? Z80_FLAG_SIGN     : 0); }

#define SET_FLAGS_SBC(X) { F = (((uint16_t)A - (uint16_t)X - CARRY_BIT) & 0x100  ? Z80_FLAG_CARRY    : 0) | \
                               (                                                            Z80_FLAG_SUB         ) | \
                               (((((int16_t)(int8_t)z80_regs.a) - ((int16_t)(int8_t)X) - CARRY_BIT) >  127 || \
                                 (((int16_t)(int8_t)z80_regs.a) - ((int16_t)(int8_t)X) - CARRY_BIT) < -128) ? Z80_FLAG_OVERFLOW : 0) | \
                               (((A & 0x0f) - (X & 0x0f) - CARRY_BIT) & 0x10     ? Z80_FLAG_HALF     : 0) | \
                               (((A - X - CARRY_BIT) & 0xff) == 0x00             ? Z80_FLAG_ZERO     : 0) | \
                               ((A - X - CARRY_BIT) & 0x80                       ? Z80_FLAG_SIGN     : 0); }

#define SET_FLAGS_INC(X) { F = (F                  & Z80_FLAG_CARRY       ) | \
                               (X == 0x80          ? Z80_FLAG_OVERFLOW : 0) | \
                               ((X & 0x0f) == 0x00 ? Z80_FLAG_HALF     : 0) | \
                               (X == 0x00          ? Z80_FLAG_ZERO     : 0) | \
                               ((X  & 0x80)        ? Z80_FLAG_SIGN     : 0); }

#define SET_FLAGS_DEC(X) { F = (F                  & Z80_FLAG_CARRY       ) | \
                               (                     Z80_FLAG_SUB         ) | \
                               (X == 0x7f          ? Z80_FLAG_OVERFLOW : 0) | \
                               ((X & 0x0f) == 0x0f ? Z80_FLAG_HALF     : 0) | \
                               (X == 0x00          ? Z80_FLAG_ZERO     : 0) | \
                               ((X & 0x80)         ? Z80_FLAG_SIGN     : 0); }

#define SET_FLAGS_CPL { F |= Z80_FLAG_HALF | Z80_FLAG_SUB; }

#define SET_FLAGS_ADD_16(Y,X) { F = (F &                       (Z80_FLAG_OVERFLOW | Z80_FLAG_ZERO | Z80_FLAG_SIGN))     | \
                                    ((((uint32_t)Y + (uint32_t)X) & 0x10000)                      ? Z80_FLAG_CARRY : 0) | \
                                    ((((uint32_t)(Y & 0x0fff) + (uint32_t)(X & 0x0fff)) & 0x1000) ? Z80_FLAG_HALF  : 0); }

#define SET_FLAGS_SUB_16(Y,X) { F = (F &                       (Z80_FLAG_OVERFLOW | Z80_FLAG_ZERO | Z80_FLAG_SIGN)    ) | \
                                    (                                                               Z80_FLAG_SUB      ) | \
                                    ((((uint32_t)Y - (uint32_t)X) & 0x10000)                      ? Z80_FLAG_CARRY : 0) | \
                                    ((((uint32_t)(Y & 0x0fff) - (uint32_t)(X & 0x0fff)) & 0x1000) ? Z80_FLAG_HALF  : 0); }

#define SET_FLAGS_ADC_16(X) { F = (((uint32_t)HL + (uint32_t)X + CARRY_BIT) & 0x10000  ? Z80_FLAG_CARRY    : 0) | \
                                  (((((int32_t)(int16_t)HL) + ((int32_t)(int16_t)X) + CARRY_BIT) >  32767 || \
                                    (((int32_t)(int16_t)HL) + ((int32_t)(int16_t)X) + CARRY_BIT) < -32768)  ? Z80_FLAG_OVERFLOW : 0) | \
                                  (((HL & 0xfff) + (X & 0xfff) + CARRY_BIT) & 0x1000      ? Z80_FLAG_HALF     : 0) | \
                                  (((HL + X + CARRY_BIT) & 0xffff) == 0x0000              ? Z80_FLAG_ZERO     : 0) | \
                                  ((HL + X + CARRY_BIT) & 0x8000                          ? Z80_FLAG_SIGN     : 0); }

#define SET_FLAGS_SBC_16(X) { F = (((uint32_t)HL - (uint32_t)X - CARRY_BIT) & 0x10000    ? Z80_FLAG_CARRY    : 0) | \
                                  (                                                                  Z80_FLAG_SUB         ) | \
                                  (((((int32_t)(int16_t)HL) - ((int32_t)(int16_t)X) - CARRY_BIT) >  32767 || \
                                    (((int32_t)(int16_t)HL) - ((int32_t)(int16_t)X) - CARRY_BIT) < -32768)  ? Z80_FLAG_OVERFLOW : 0) | \
                                  (((HL & 0x0fff) - (X & 0x0fff) - CARRY_BIT) & 0x1000    ? Z80_FLAG_HALF     : 0) | \
                                  (((HL - X - CARRY_BIT) & 0xffff) == 0x0000              ? Z80_FLAG_ZERO     : 0) | \
                                  ((HL - X - CARRY_BIT) & 0x8000                          ? Z80_FLAG_SIGN     : 0); }

#define SET_FLAGS_CPD_CPI(X) { F = (F                                & Z80_FLAG_CARRY       ) | \
                                   (                                   Z80_FLAG_SUB         ) | \
                                   ((BC)                             ? Z80_FLAG_OVERFLOW : 0) | \
                                   (((A & 0x0f) - (X & 0x0f)) & 0x10 ? Z80_FLAG_HALF     : 0) | \
                                   ((A == X)                         ? Z80_FLAG_ZERO     : 0) | \
                                   ((A - X) & 0x80                   ? Z80_FLAG_SIGN     : 0); }

#define SET_FLAGS_RLCA(X) { F = (F & (Z80_FLAG_PARITY | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) | \
                                ((X & 0x01)                           ? Z80_FLAG_CARRY : 0); }

#define SET_FLAGS_RLC(X) { F = (uint8_even_parity[X]                   ? Z80_FLAG_PARITY : 0) | \
                               ((X & 0x01)                             ? Z80_FLAG_CARRY  : 0) | \
                               (X == 0x00                              ? Z80_FLAG_ZERO   : 0) | \
                               ((X & 0x80)                             ? Z80_FLAG_SIGN   : 0); }

#define SET_FLAGS_RRC(X) { F = (uint8_even_parity[X]                   ? Z80_FLAG_PARITY : 0) | \
                               ((X & 0x80)                             ? Z80_FLAG_CARRY  : 0) | \
                               (X == 0x00                              ? Z80_FLAG_ZERO   : 0) | \
                               ((X & 0x80)                             ? Z80_FLAG_SIGN   : 0); }

#define SET_FLAGS_RL(X) { F = (uint8_even_parity[X]                    ? Z80_FLAG_PARITY : 0) | \
                              (X == 0x00                               ? Z80_FLAG_ZERO   : 0) | \
                              ((X & 0x80)                              ? Z80_FLAG_SIGN   : 0); }

#define SET_FLAGS_RR(X) { F = (uint8_even_parity[X]                    ? Z80_FLAG_PARITY : 0) | \
                              (X == 0x00                               ? Z80_FLAG_ZERO   : 0) | \
                              ((X & 0x80)                              ? Z80_FLAG_SIGN   : 0); }

#define SET_FLAGS_RRD_RLD { F = (F                                & Z80_FLAG_CARRY)      | \
                                (uint8_even_parity[z80_regs.a]    ? Z80_FLAG_PARITY : 0) | \
                                (A == 0x00                        ? Z80_FLAG_ZERO   : 0) | \
                                (A & 0x80                         ? Z80_FLAG_SIGN   : 0); }


#define IN_C(X) { X = io_read (C);                                   \
                  F = (F                    & Z80_FLAG_CARRY     ) | \
                      (X & 0x80             ? Z80_FLAG_SIGN   : 0) | \
                      (X == 0               ? Z80_FLAG_ZERO   : 0) | \
                      (uint8_even_parity[X] ? Z80_FLAG_PARITY : 0); }
uint32_t z80_extended_instruction ()
{
    uint8_t instruction = memory_read (PC++);
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
            param.l = memory_read (PC++);
            param.h = memory_read (PC++);
            break;
        case 2:
            param.l = memory_read (PC++);
            break;
        default:
            break;
    }

    switch (instruction)
    {
        case 0x40: /* IN B,(C)   */ IN_C (B);                   CYCLES (12);    break;
        case 0x41: /* OUT (C),B  */ io_write (C, B);            CYCLES (12);    break;
        case 0x42: /* SBC HL,BC  */ temp_16 = BC + CARRY_BIT;
                                    SET_FLAGS_SBC_16 (BC); HL -= temp_16;
                                                                CYCLES (15);    break;
        case 0x43: /* LD (**),BC */ memory_write (NN,     C);
                                    memory_write (NN + 1, B);   CYCLES (20);    break;
        case 0x44: /* NEG        */ temp_1 = A;
                                    A = 0 - (int8_t)A;
                                    F = (temp_1 != 0                           ? Z80_FLAG_CARRY    : 0) |
                                                 (                               Z80_FLAG_SUB         ) |
                                                 (temp_1 == 0x80               ? Z80_FLAG_OVERFLOW : 0) |
                                                 ((0 - (temp_1 & 0x0f)) & 0x10 ? Z80_FLAG_HALF     : 0) |
                                                 (A == 0                       ? Z80_FLAG_ZERO     : 0) |
                                                 (A & 0x80                     ? Z80_FLAG_SIGN     : 0);
                                                                CYCLES (8);     break;
        case 0x45: /* RETN       */ z80_regs.pc_l = memory_read (SP++);
                                    z80_regs.pc_h = memory_read (SP++);
                                    IFF1 = IFF2;                CYCLES (14);    break;
        case 0x47: /* LD I, A    */ I = A;                      CYCLES (9);     break;
        case 0x48: /* IN C,(C)   */ IN_C (C);                   CYCLES (12);    break;
        case 0x4a: /* ADC HL,BC  */ temp_16 = BC + CARRY_BIT;
                                    SET_FLAGS_ADC_16 (BC);
                                    HL += temp_16;              CYCLES (15);    break;
        case 0x4b: /* LD BC,(**) */ C = memory_read (NN);
                                    B = memory_read (NN + 1);   CYCLES (20);    break;
        case 0x4d: /* RETI       */ z80_regs.pc_l = memory_read (SP++);
                                    z80_regs.pc_h = memory_read (SP++);
                                                                CYCLES (14);    break; /* TODO: Signals the IO device that the interrupt is handled? */

        case 0x51: /* OUT (C),D  */ io_write (C, D);            CYCLES (12);    break;
        case 0x52: /* SBC HL,DE  */ temp_16 = DE + CARRY_BIT;
                                    SET_FLAGS_SBC_16 (DE); HL -= temp_16;
                                                                CYCLES (15);    break;
        case 0x53: /* LD (**),DE */ memory_write (NN,     E);
                                    memory_write (NN + 1, D);   CYCLES (20);    break;
        case 0x56: /* IM 1       */ z80_regs.im = 1;            CYCLES (8);     break;
        case 0x57: /* LD A, I    */ A = I;
                                    F = (F                 & Z80_FLAG_CARRY       ) |
                                        (z80_regs.i & 0x80 ? Z80_FLAG_SIGN     : 0) |
                                        (z80_regs.i == 0   ? Z80_FLAG_ZERO     : 0) |
                                        (z80_regs.iff2     ? Z80_FLAG_OVERFLOW : 0);
                                                                CYCLES (9);     break;
        case 0x59: /* OUT (C),E  */ io_write (C, E);            CYCLES (12);    break;
        case 0x5a: /* ADC HL,DE  */ temp_16 = DE + CARRY_BIT;
                                    SET_FLAGS_ADC_16 (DE);
                                    HL += temp_16;              CYCLES (15);    break;
        case 0x5b: /* LD DE,(**) */ E = memory_read (NN);
                                    D = memory_read (NN + 1);   CYCLES (20);    break;
        case 0x5f: /* LD A,R     */ A = R;
                                    F = (F                 & Z80_FLAG_CARRY       ) |
                                        (z80_regs.r & 0x80 ? Z80_FLAG_SIGN     : 0) |
                                        (z80_regs.r == 0   ? Z80_FLAG_ZERO     : 0) |
                                        (z80_regs.iff2     ? Z80_FLAG_OVERFLOW : 0);
                                                                CYCLES(9);      break;

        case 0x61: /* OUT (C),H  */ io_write (C, H);            CYCLES (12);    break;
        case 0x62: /* SBC HL,HL  */ temp_16 = HL + CARRY_BIT;
                                    SET_FLAGS_SBC_16 (HL);
                                    HL -= temp_16;              CYCLES (15);    break;
        case 0x67: /* RRD        */ temp_1 = memory_read (HL);
                                    temp_2 = A;
                                    A &= 0xf0; A |= (temp_1 & 0x0f);
                                    temp_1 >>= 4; temp_1 |= (temp_2 << 4);
                                    memory_write (HL, temp_1);
                                    SET_FLAGS_RRD_RLD;          CYCLES (18);    break;
        case 0x69: /* OUT (C),L  */ io_write (C, L);            CYCLES (12);    break;
        case 0x6a: /* ADC HL,HL  */ temp_16 = HL + CARRY_BIT;
                                    SET_FLAGS_ADC_16 (HL); HL += temp_16;
                                                                CYCLES (15);    break;
        case 0x6f: /* RLD        */ temp_1 = memory_read (HL);
                                    temp_2 = A;
                                    A &= 0xf0; A |= (temp_1 >> 4);
                                    temp_1 <<= 4; temp_1 |= (temp_2 & 0x0f);
                                    memory_write (HL, temp_1);
                                    SET_FLAGS_RRD_RLD;          CYCLES (18);    break;

        case 0x71: /* OUT (C),0  */ io_write (C, 0);            CYCLES (12);    break;
        case 0x72: /* SBC HL,SP  */ temp_16 = SP + CARRY_BIT;
                                    SET_FLAGS_SBC_16 (SP);
                                    HL -= temp_16;              CYCLES (15);    break;
        case 0x73: /* LD (**),SP */ memory_write (NN,     z80_regs.sp_l);
                                    memory_write (NN + 1, z80_regs.sp_h);
                                                                CYCLES (20);    break;
        case 0x78: /* IN A,(C)   */ IN_C (A);                   CYCLES (12);    break;
        case 0x79: /* OUT (C),A  */ io_write (C, A);            CYCLES (12);    break;
        case 0x7a: /* ADC HL,SP  */ temp_16 = SP + CARRY_BIT;
                                    SET_FLAGS_ADC_16 (SP); HL += temp_16;
                                                                CYCLES (15);    break;
        case 0x7b: /* LD SP,(**) */ z80_regs.sp_l = memory_read (NN);
                                    z80_regs.sp_h = memory_read (NN + 1);
                                                                CYCLES (20);    break;

        case 0xa0: /* LDI        */ memory_write (DE, memory_read (HL));
                                    HL++; DE++; BC--;
                                    F &= (Z80_FLAG_CARRY | Z80_FLAG_ZERO | Z80_FLAG_SIGN);
                                    F |= (BC ? Z80_FLAG_OVERFLOW : 0);
                                                                CYCLES (16);    break;
        case 0xa1: /* CPI        */ temp_1 = memory_read (HL);
                                    HL++;
                                    BC--;
                                    SET_FLAGS_CPD_CPI (temp_1); CYCLES (16);    break;
        case 0xa2: /* INI        */ memory_write (HL, io_read (z80_regs.c));
                                    HL++; z80_regs.b--;
                                    F = (F      & Z80_FLAG_CARRY) |
                                        (         Z80_FLAG_SUB  ) |
                                        (B == 0 ? Z80_FLAG_ZERO : 0);
                                                                CYCLES (16);    break;
        case 0xa3: /* OUTI       */ io_write (C, memory_read(HL)),
                                    HL++; B--;
                                    F = (F      & Z80_FLAG_CARRY) |
                                        (         Z80_FLAG_SUB  ) |
                                        (B == 0 ? Z80_FLAG_ZERO : 0);
                                                                CYCLES (16);    break;
        case 0xa8: /* LDD        */ memory_write (DE, memory_read (HL));
                                    HL--; DE--; BC--;
                                    F &= (Z80_FLAG_CARRY | Z80_FLAG_ZERO | Z80_FLAG_SIGN);
                                    F |= (BC ? Z80_FLAG_OVERFLOW : 0);
                                                                CYCLES (16);    break;
        case 0xa9: /* CPD        */ temp_1 = memory_read (HL);
                                    HL--;
                                    BC--;
                                    SET_FLAGS_CPD_CPI (temp_1); CYCLES (16);    break;
        case 0xab: /* OUTD       */ temp_1 = memory_read (HL);
                                    z80_regs.b--;
                                    io_write (z80_regs.c, temp_1);
                                    HL--;
                                    /* TODO: Confirm 'unknown' flag behaviour */
                                    F |= Z80_FLAG_SUB;
                                    F = (F & ~Z80_FLAG_ZERO) | (B == 0 ? Z80_FLAG_ZERO : 0);
                                                                CYCLES (16);    break;

        case 0xb0: /* LDIR       */ memory_write (DE, memory_read (HL));
                                    HL++; DE++; BC--;
                                    F = (F & (Z80_FLAG_CARRY | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) |
                                        (BC ? Z80_FLAG_OVERFLOW : 0);
                                    if (BC) { PC -= 2;          CYCLES (21); }
                                    else    {                   CYCLES (16); }  break;
        case 0xb1: /* CPIR       */ temp_1 = memory_read (HL);
                                    HL++; BC--;
                                    if (BC != 0 && A != temp_1) {
                                        PC -= 2;                CYCLES (21); }
                                    else {                      CYCLES (16); }
                                    SET_FLAGS_CPD_CPI (temp_1);                 break;
        case 0xb3: /* OTIR       */ io_write (C, memory_read(HL)),
                                    HL++; B--;
                                    F = (F & Z80_FLAG_CARRY) |
                                                 (Z80_FLAG_SUB | Z80_FLAG_ZERO);
                                    if (B) { PC -= 2;           CYCLES (21); }
                                    else   {                    CYCLES (16); }  break;
        case 0xb8: /* LDDR       */ memory_write (DE, memory_read (HL));
                                    HL--; DE--; BC--;
                                    F = (F & (Z80_FLAG_CARRY | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) |
                                        (BC ? Z80_FLAG_OVERFLOW : 0);
                                    if (BC) { PC -= 2;          CYCLES (21); }
                                    else    {                   CYCLES (16); }  break;
        case 0xb9: /* CPDR       */ temp_1 = memory_read (HL);
                                    HL--;
                                    BC--;
                                    SET_FLAGS_CPD_CPI (temp_1);
                                    if (BC != 0 && A != temp_1) {
                                        PC -= 2;                CYCLES (21); }
                                    else {                      CYCLES (16); }  break;

        default:
        fprintf (stderr, "Unknown extended instruction: \"%s\" (%02x).\n",
                 z80_instruction_name_extended[instruction], instruction);
        snepulator.abort = true;
    }

    return 0;
}

uint32_t z80_ix_iy_bit_instruction (uint16_t reg_ix_iy_w)
{
    /* Note: The displacement comes first, then the instruction */
    uint8_t displacement = memory_read (PC++);
    uint8_t instruction = memory_read (PC++);
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
        case 0x00: /* RLC (ix+*) */ data = (data << 1) | ((data & 0x80) ? 0x01 : 0x00);
                                    SET_FLAGS_RLC (data);       CYCLES (23);    break;
        case 0x08: /* RRC (ix+*) */ data = (data >> 1) | (data << 7);
                                    SET_FLAGS_RRC (data);                       break;
        case 0x10: /* RL  (ix+*) */ temp = data;
                                    data = (data << 1) | ((F & Z80_FLAG_CARRY) ? 0x01 : 0x00);
                                    SET_FLAGS_RL (data);
                                    F |= (temp & 0x80) ? Z80_FLAG_CARRY : 0;    break;
        case 0x18: /* RR  (ix+*) */ temp = data;
                                    data = (data >> 1) | ((F & Z80_FLAG_CARRY) ? 0x80 : 0x00);
                                    SET_FLAGS_RR (data);
                                    F |= (temp & 0x01) ? Z80_FLAG_CARRY : 0;    break;

        case 0x20: /* SLA (ix+*) */ temp = data;
                                    data = (data << 1); SET_FLAGS_RL (data);
                                    F |= (temp & 0x80) ? Z80_FLAG_CARRY : 0;    break;
        case 0x28: /* SRA (ix+*) */ temp = data;
                                    data = (data >> 1) | (data & 0x80); SET_FLAGS_RR (data);
                                    F |= (temp & 0x01) ? Z80_FLAG_CARRY : 0;    break;

        case 0x30: /* SLL (ix+*) */ temp = data;
                                    data = (data << 1) | 0x01; SET_FLAGS_RL (data);
                                    F |= (temp & 0x80) ? Z80_FLAG_CARRY : 0;    break;
        case 0x38: /* SRL (ix+*) */ temp = data;
                                    data = (data >> 1); SET_FLAGS_RR (data);
                                    F |= (temp & 0x01) ? Z80_FLAG_CARRY : 0;    break;
        case 0x40: case 0x48: case 0x50: case 0x58: /* BIT */
        case 0x60: case 0x68: case 0x70: case 0x78:
            F = (F & Z80_FLAG_CARRY) |
                         ((bit & data) ? 0 : Z80_FLAG_PARITY) |
                         (Z80_FLAG_HALF) |
                         ((bit & data) ? 0 : Z80_FLAG_ZERO) |
                         (((bit == BIT_7) && (data & BIT_7)) ? Z80_FLAG_SIGN : 0);
            write_data = false;                                 CYCLES (20);    break;
        case 0x80: case 0x88: case 0x90: case 0x98: /* RES */
        case 0xa0: case 0xa8: case 0xb0: case 0xb8:
            data &= ~bit;                                       CYCLES (23);    break;
        case 0xc0: case 0xc8: case 0xd0: case 0xd8: /* SET */
        case 0xe0: case 0xe8: case 0xf0: case 0xf8:
            data |= bit;                                        CYCLES (23);    break;
        default:
            fprintf (stderr, "Unknown ix/iy bit instruction: \"%s\" (%02x).\n",
                     z80_instruction_name_bits[instruction], instruction);
            write_data = false;
            snepulator.abort = true;
    }

    /* Write data */
    if (write_data)
    {
        memory_write (reg_ix_iy_w + (int8_t) displacement, data);

        switch (instruction & 0x07)
        {
            case 0x00: B = data; break;
            case 0x01: C = data; break;
            case 0x02: D = data; break;
            case 0x03: E = data; break;
            case 0x04: H = data; break;
            case 0x05: L = data; break;
            case 0x07: A = data; break;
            default: break;
        }
    }

    return 0;
}

void z80_instruction (void);
#define FALL_THROUGH() { PC--; z80_instruction(); }
uint16_t z80_ix_iy_instruction (uint16_t reg_ix_iy_in)
{
    uint8_t instruction = memory_read (PC++);
    uint8_t value_read;
    uint8_t temp;

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

    reg_ix_iy.w = reg_ix_iy_in;

    switch (z80_instruction_size_ix[instruction])
    {
        case 3:
            param.l = memory_read (PC++);
            param.h = memory_read (PC++);
            break;
        case 2:
            param.l = memory_read (PC++);
            break;
        default:
            break;
    }

    /* TODO: For the fall-through instructions, how many cycles should we add? */
    switch (instruction)
    {
        case 0x00: /* -            */ FALL_THROUGH ();                          break;
        case 0x09: /* ADD IX,BC    */ SET_FLAGS_ADD_16 (reg_ix_iy.w, BC); reg_ix_iy.w += BC;
                                                                CYCLES (15);    break;

        case 0x19: /* ADD IX,DE    */ SET_FLAGS_ADD_16 (reg_ix_iy.w, DE); reg_ix_iy.w += DE;
                                                                CYCLES (15);    break;

        case 0x21: /* LD IX,**     */ reg_ix_iy.w = NN;         CYCLES (14);    break;
        case 0x22: /* LD (**),IX   */ memory_write (NN,     reg_ix_iy.l);
                                      memory_write (NN + 1, reg_ix_iy.h);
                                                                CYCLES (20);    break;
        case 0x23: /* INC IX       */ reg_ix_iy.w++;            CYCLES (10);    break;
        case 0x24: /* INC IXH      */ reg_ix_iy.h++; SET_FLAGS_INC (reg_ix_iy.h);
                                                                CYCLES (8);     break;
        case 0x25: /* DEC IXH      */ reg_ix_iy.h--; SET_FLAGS_DEC (reg_ix_iy.h);
                                                                CYCLES (8);     break;
        case 0x26: /* LD IXH,*     */ reg_ix_iy.h = N;          CYCLES (11);    break;
        case 0x29: /* ADD IX,IX    */ SET_FLAGS_ADD_16 (reg_ix_iy.w, reg_ix_iy.w);
                                      reg_ix_iy.w += reg_ix_iy.w;
                                                                CYCLES (15);    break;
        case 0x2a: /* LD IX,(**)   */ reg_ix_iy.l = memory_read (NN);
                                      reg_ix_iy.h = memory_read (NN + 1);
                                                                CYCLES (20);    break;
        case 0x2b: /* DEC IX       */ reg_ix_iy.w--;            CYCLES (10);    break;
        case 0x2c: /* INC IXL      */ reg_ix_iy.l++; SET_FLAGS_INC (reg_ix_iy.l);
                                                                CYCLES (8);     break;
        case 0x2d: /* DEC IXL      */ reg_ix_iy.l--; SET_FLAGS_DEC (reg_ix_iy.l);
                                                                CYCLES (8);     break;
        case 0x2e: /* LD IXL,*     */ reg_ix_iy.l = N;          CYCLES (11);    break;

        case 0x34: /* INC (IX+*)   */ temp = memory_read (reg_ix_iy.w + (int8_t) N);
                                      temp++; SET_FLAGS_INC (temp);
                                      memory_write (reg_ix_iy.w + (int8_t) N, temp);
                                                                CYCLES (23);    break;
        case 0x35: /* DEC (IX+*)   */ temp = memory_read (reg_ix_iy.w + (int8_t) N);
                                      temp--; SET_FLAGS_DEC (temp);
                                      memory_write (reg_ix_iy.w + (int8_t) N, temp);
                                                                CYCLES (23);    break;
        case 0x36: /* LD (IX+*),*  */ memory_write (reg_ix_iy.w + (int8_t) N, param.h);
                                                                CYCLES (19);    break;
        case 0x39: /* ADD IX,SP    */ SET_FLAGS_ADD_16 (reg_ix_iy.w, SP); reg_ix_iy.w += SP;
                                                                CYCLES (15);    break;

        case 0x40: /* -            */ FALL_THROUGH ();                          break;
        case 0x41: /* -            */ FALL_THROUGH ();                          break;
        case 0x42: /* -            */ FALL_THROUGH ();                          break;
        case 0x43: /* -            */ FALL_THROUGH ();                          break;
        case 0x44: /* LD B,IXH     */ B = reg_ix_iy.h;                          break;
        case 0x45: /* LD B,IXL     */ B = reg_ix_iy.l;                          break;
        case 0x46: /* LD B,(IX+*)  */ B = memory_read (reg_ix_iy.w + (int8_t) N);
                                                                CYCLES (19);    break;
        case 0x47: /* -            */ FALL_THROUGH ();                          break;
        case 0x48: /* -            */ FALL_THROUGH ();                          break;
        case 0x49: /* -            */ FALL_THROUGH ();                          break;
        case 0x4a: /* -            */ FALL_THROUGH ();                          break;
        case 0x4b: /* -            */ FALL_THROUGH ();                          break;
        case 0x4c: /* LD C,IXH     */ C = reg_ix_iy.h;                          break;
        case 0x4d: /* LD C,IXL     */ C = reg_ix_iy.l;                          break;
        case 0x4e: /* LD C,(IX+*)  */ C = memory_read (reg_ix_iy.w + (int8_t) N);
                                                                CYCLES (19);    break;
        case 0x4f: /* -            */ PC--; z80_instruction ();                 break;

        case 0x50: /* -            */ FALL_THROUGH ();                          break;
        case 0x51: /* -            */ FALL_THROUGH ();                          break;
        case 0x52: /* -            */ FALL_THROUGH ();                          break;
        case 0x53: /* -            */ FALL_THROUGH ();                          break;
        case 0x54: /* LD D,IXH     */ D = reg_ix_iy.h;                          break;
        case 0x55: /* LD D,IXL     */ D = reg_ix_iy.l;                          break;
        case 0x56: /* LD D,(IX+*)  */ D = memory_read (reg_ix_iy.w + (int8_t) N);
                                                                CYCLES (19);    break;
        case 0x57: /* -            */ FALL_THROUGH ();                          break;
        case 0x58: /* -            */ FALL_THROUGH ();                          break;
        case 0x59: /* -            */ FALL_THROUGH ();                          break;
        case 0x5a: /* -            */ FALL_THROUGH ();                          break;
        case 0x5b: /* -            */ FALL_THROUGH ();                          break;
        case 0x5c: /* LD E,IXH     */ E = reg_ix_iy.h;                          break;
        case 0x5d: /* LD E,IXL     */ E = reg_ix_iy.l;                          break;
        case 0x5e: /* LD E,(IX+*)  */ E = memory_read (reg_ix_iy.w + (int8_t) N);
                                                                CYCLES (19);    break;
        case 0x5f: /* -            */ FALL_THROUGH ();                          break;

        case 0x60: /* LD IXH,B     */ reg_ix_iy.h = B;                          break;
        case 0x61: /* LD IXH,C     */ reg_ix_iy.h = C;                          break;
        case 0x62: /* LD IXH,D     */ reg_ix_iy.h = D;                          break;
        case 0x63: /* LD IXH,E     */ reg_ix_iy.h = E;                          break;
        case 0x64: /* LD IXH,IXH   */ reg_ix_iy.h = reg_ix_iy.h;                break;
        case 0x65: /* LD IXH,IXL   */ reg_ix_iy.h = reg_ix_iy.l;                break;
        case 0x66: /* LD H,(IX+*)  */ H = memory_read (reg_ix_iy.w + (int8_t) N);
                                                                CYCLES (19);    break;
        case 0x67: /* LD IXH,A     */ reg_ix_iy.h = A;                          break;
        case 0x68: /* LD IXH,B     */ reg_ix_iy.l = B;                          break;
        case 0x69: /* LD IXH,C     */ reg_ix_iy.l = C;                          break;
        case 0x6a: /* LD IXH,D     */ reg_ix_iy.l = D;                          break;
        case 0x6b: /* LD IXH,E     */ reg_ix_iy.l = E;                          break;
        case 0x6c: /* LD IXL,IXH   */ reg_ix_iy.l = reg_ix_iy.h;                break;
        case 0x6d: /* LD IXL,IXL   */ reg_ix_iy.l = reg_ix_iy.l;                break;
        case 0x6e: /* LD L,(IX+*)  */ L = memory_read (reg_ix_iy.w + (int8_t) N);
                                                                CYCLES (19);    break;
        case 0x6f: /* LD IXL,A     */ reg_ix_iy.l = A;                          break;

        case 0x70: /* LD (IX+*),B  */ memory_write (reg_ix_iy.w + (int8_t) N, B);
                                                                CYCLES (19);    break;
        case 0x71: /* LD (IX+*),C  */ memory_write (reg_ix_iy.w + (int8_t) N, C);
                                                                CYCLES (19);    break;
        case 0x72: /* LD (IX+*),D  */ memory_write (reg_ix_iy.w + (int8_t) N, D);
                                                                CYCLES (19);    break;
        case 0x73: /* LD (IX+*),E  */ memory_write (reg_ix_iy.w + (int8_t) N, E);
                                                                CYCLES (19);    break;
        case 0x74: /* LD (IX+*),H  */ memory_write (reg_ix_iy.w + (int8_t) N, H);
                                                                CYCLES (19);    break;
        case 0x75: /* LD (IX+*),L  */ memory_write (reg_ix_iy.w + (int8_t) N, L);
                                                                CYCLES (19);    break;
        case 0x77: /* LD (IX+*),A  */ memory_write (reg_ix_iy.w + (int8_t) N, A);
                                                                CYCLES (19);    break;
        case 0x78: /* -            */ FALL_THROUGH ();                          break;
        case 0x79: /* -            */ FALL_THROUGH ();                          break;
        case 0x7a: /* -            */ FALL_THROUGH ();                          break;
        case 0x7b: /* -            */ FALL_THROUGH ();                          break;
        case 0x7c: /* LD A,IXH     */ A = reg_ix_iy.h;                          break;
        case 0x7d: /* LD A,IXL     */ A = reg_ix_iy.l;                          break;
        case 0x7e: /* LD A,(IX+*)  */ A = memory_read (reg_ix_iy.w + (int8_t) N);
                                                                CYCLES (19);    break;
        case 0x7f: /* -            */ FALL_THROUGH ();                          break;

        case 0x84: /* ADD A,IXH    */ SET_FLAGS_ADD (A, reg_ix_iy.h);
                                      A += reg_ix_iy.h;                         break;
        case 0x85: /* ADD A,IXL    */ SET_FLAGS_ADD (A, reg_ix_iy.l);
                                      A += reg_ix_iy.l;                         break;
        case 0x86: /* ADD A,(IX+*) */ temp = memory_read (reg_ix_iy.w + (int8_t) N);
                                      SET_FLAGS_ADD (A, temp);
                                      A += temp;                CYCLES (19);    break;

        case 0x8c: /* ADC A,IXH    */ temp = reg_ix_iy.h + CARRY_BIT;
                                      SET_FLAGS_ADC (reg_ix_iy.h);
                                      A += temp;                                break;
        case 0x8d: /* ADC A,IXL    */ temp = reg_ix_iy.l + CARRY_BIT;
                                      SET_FLAGS_ADC (reg_ix_iy.l); A += temp;   break;
        case 0x8e: /* ADC A,(IX+*) */ value_read = memory_read (reg_ix_iy.w + (int8_t) N);
                                      temp = value_read + CARRY_BIT;
                                      SET_FLAGS_ADC (value_read);
                                      A += temp;                CYCLES (19);    break;

        case 0x94: /* SUB A,IXH    */ SET_FLAGS_SUB (A, reg_ix_iy.h);
                                      A -= reg_ix_iy.h;                         break;
        case 0x95: /* SUB A,IXL    */ SET_FLAGS_SUB (A, reg_ix_iy.l);
                                      A -= reg_ix_iy.l;                         break;
        case 0x96: /* SUB A,(IX+*) */ temp = memory_read (reg_ix_iy.w + (int8_t) N);
                                      SET_FLAGS_SUB (A, temp);
                                      A -= temp;                CYCLES (19);    break;
        case 0x9c: /* SBC A,IXH    */ temp = reg_ix_iy.h + CARRY_BIT;
                                      SET_FLAGS_SBC (reg_ix_iy.h); A -= temp;   break;
        case 0x9d: /* SBC A,IXL    */ temp = reg_ix_iy.l + CARRY_BIT;
                                      SET_FLAGS_SBC (reg_ix_iy.l); A -= temp;   break;
        case 0x9e: /* SBC A,(IX+*) */ value_read= memory_read (reg_ix_iy.w + (int8_t) N);
                                      temp = value_read + CARRY_BIT;
                                      SET_FLAGS_SBC (value_read);
                                      A -= temp;                CYCLES (19);    break;

        case 0xa4: /* AND A,IXH    */ A &= reg_ix_iy.h; SET_FLAGS_AND;          break;
        case 0xa5: /* AND A,IXL    */ A &= reg_ix_iy.l; SET_FLAGS_AND;          break;
        case 0xa6: /* AND A,(IX+*) */ A &= memory_read (reg_ix_iy.w + (int8_t) N);
                                      SET_FLAGS_AND;            CYCLES (19);    break;
        case 0xac: /* XOR A,IXH    */ A ^= reg_ix_iy.h; SET_FLAGS_OR_XOR;       break;
        case 0xad: /* XOR A,IXL    */ A ^= reg_ix_iy.l; SET_FLAGS_OR_XOR;       break;
        case 0xae: /* XOR A,(IX+*) */ A ^= memory_read (reg_ix_iy.w + (int8_t) N);
                                      SET_FLAGS_OR_XOR;         CYCLES (19);    break;

        case 0xb4: /* OR A,IXH     */ A |= reg_ix_iy.h; SET_FLAGS_OR_XOR;       break;
        case 0xb5: /* OR A,IXL     */ A |= reg_ix_iy.l; SET_FLAGS_OR_XOR;       break;
        case 0xb6: /* OR A,(IX+*)  */ A |= memory_read (reg_ix_iy.w + (int8_t) N);
                                      SET_FLAGS_OR_XOR;         CYCLES (19);    break;
        case 0xbc: /* CP  A,IXH    */ SET_FLAGS_SUB (A, reg_ix_iy.h);           break;
        case 0xbd: /* CP  A,IXL    */ SET_FLAGS_SUB (A, reg_ix_iy.l);           break;
        case 0xbe: /* CP  A,(IX+*) */ temp = memory_read (reg_ix_iy.w + (int8_t) N);
                                      SET_FLAGS_SUB (A, temp);  CYCLES (19);    break;

        case 0xcb: /* IX Bit Instructions */ z80_ix_iy_bit_instruction (reg_ix_iy.w);
                                                                                break;
        case 0xcd: /* -            */ FALL_THROUGH ();                          break;
        case 0xe1: /* POP IX       */ reg_ix_iy.l = memory_read (SP++);
                                      reg_ix_iy.h = memory_read (SP++);
                                                                CYCLES (14);    break;
        case 0xe5: /* PUSH IX      */ memory_write (--SP, reg_ix_iy.h);
                                      memory_write (--SP, reg_ix_iy.l);
                                                                CYCLES (15);    break;
        case 0xe6: /* -            */ FALL_THROUGH ();                          break;
        case 0xe9: /* JP (IX)      */ PC = reg_ix_iy.w;         CYCLES (8);     break;

        case 0xf9: /* LD SP,IX     */ SP = reg_ix_iy.w;         CYCLES (10);    break;

        default:
        fprintf (stderr, "Unknown ix/iy instruction: \"%s\" (%02x).\n",
                 z80_instruction_name_ix[instruction], instruction);
        snepulator.abort = true;
    }

    return reg_ix_iy.w;
}

uint32_t z80_bit_instruction ()
{
    uint8_t instruction = memory_read (PC++);
    uint8_t data;
    uint8_t temp;
    uint8_t bit;
    bool write_data = true;

    /* Read data */
    switch (instruction & 0x07)
    {
        case 0x00: data = B; break;
        case 0x01: data = C; break;
        case 0x02: data = D; break;
        case 0x03: data = E; break;
        case 0x04: data = H; break;
        case 0x05: data = L; break;
        case 0x06: data = memory_read (HL); break;
        case 0x07: data = A; break;
    }

    /* For bit/res/set, determine the bit to operate on */
    bit = 1 << ((instruction >> 3) & 0x07);

    switch (instruction & 0xf8)
    {
        case 0x00: /* RLC X */ data = (data << 1) | (data >> 7); SET_FLAGS_RLC (data);
                               CYCLES ((instruction & 0x07) == 0x06 ? 15 : 8);  break;
        case 0x08: /* RRC X */ data = (data >> 1) | (data << 7); SET_FLAGS_RRC (data);
                               CYCLES ((instruction & 0x07) == 0x06 ? 15 : 8);  break;

        case 0x10: /* RL  X */ temp = data;
                               data = (data << 1) | ((F & Z80_FLAG_CARRY) ? 0x01 : 0x00); SET_FLAGS_RL (data);
                               F |= (temp & 0x80) ? Z80_FLAG_CARRY : 0;
                               CYCLES ((instruction & 0x07) == 0x06 ? 15 : 8);  break;
        case 0x18: /* RR  X */ temp = data;
                               data = (data >> 1) | ((F & Z80_FLAG_CARRY) ? 0x80 : 0x00); SET_FLAGS_RR (data);
                               F |= (temp & 0x01) ? Z80_FLAG_CARRY : 0;
                               CYCLES ((instruction & 0x07) == 0x06 ? 15 : 8);  break;

        case 0x20: /* SLA X */ temp = data;
                               data = (data << 1); SET_FLAGS_RL (data);
                               F |= (temp & 0x80) ? Z80_FLAG_CARRY : 0;
                               CYCLES ((instruction & 0x07) == 0x06 ? 15 : 8);  break;
        case 0x28: /* SRA X */ temp = data;
                               data = (data >> 1) | (data & 0x80); SET_FLAGS_RR (data);
                               F |= (temp & 0x01) ? Z80_FLAG_CARRY : 0;
                               CYCLES ((instruction & 0x07) == 0x06 ? 15 : 8);  break;

        case 0x30: /* SLL X */ temp = data;
                               data = (data << 1) | 0x01; SET_FLAGS_RL (data);
                               F |= (temp & 0x80) ? Z80_FLAG_CARRY : 0;
                               CYCLES ((instruction & 0x07) == 0x06 ? 15 : 8);  break;
        case 0x38: /* SRL X */ temp = data;
                               data = (data >> 1); SET_FLAGS_RR (data);
                               F |= (temp & 0x01) ? Z80_FLAG_CARRY : 0;
                               CYCLES ((instruction & 0x07) == 0x06 ? 15 : 8);  break;

        case 0x40: case 0x48: case 0x50: case 0x58: /* BIT */
        case 0x60: case 0x68: case 0x70: case 0x78:
            F = (F & Z80_FLAG_CARRY) |
                         ((bit & data) ? 0 : Z80_FLAG_PARITY) |
                         (Z80_FLAG_HALF) |
                         ((bit & data) ? 0 : Z80_FLAG_ZERO) |
                         (((bit == BIT_7) && (data & BIT_7)) ? Z80_FLAG_SIGN : 0);
            write_data = false;
            CYCLES ((instruction & 0x07) == 0x06 ? 12 : 8);                     break;

        case 0x80: case 0x88: case 0x90: case 0x98: /* RES */
        case 0xa0: case 0xa8: case 0xb0: case 0xb8:
            data &= ~bit;
            CYCLES ((instruction & 0x07) == 0x06 ? 15 : 8);                     break;

        case 0xc0: case 0xc8: case 0xd0: case 0xd8: /* SET */
        case 0xe0: case 0xe8: case 0xf0: case 0xf8:
            data |= bit;
            CYCLES ((instruction & 0x07) == 0x06 ? 15 : 8);                     break;

        default:
            fprintf (stderr, "Unknown bit instruction: \"%s\" (%02x).\n",
                     z80_instruction_name_bits[instruction], instruction);
            snepulator.abort = true;
    }

    /* Write data */
    if (write_data)
    {
        switch (instruction & 0x07)
        {
            case 0x00: B = data;                                                break;
            case 0x01: C = data;                                                break;
            case 0x02: D = data;                                                break;
            case 0x03: E = data;                                                break;
            case 0x04: H = data;                                                break;
            case 0x05: L = data;                                                break;
            case 0x06: memory_write (HL, data);                                 break;
            case 0x07: A = data;                                                break;
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
    switch (F & (Z80_FLAG_CARRY | Z80_FLAG_HALF))
    {
        case Z80_FLAG_NONE:
                 if ((A & 0xf0) < 0xa0 && (A & 0x0f) < 0x0a)    diff = 0x00;
            else if ((A & 0xf0) < 0x90 && (A & 0x0f) > 0x09)    diff = 0x06;
            else if ((A & 0xf0) > 0x90 && (A & 0x0f) < 0x0a)    diff = 0x60;
            else if ((A & 0xf0) > 0x80 && (A & 0x0f) > 0x09)    diff = 0x66;
            break;
        case Z80_FLAG_HALF:
                 if ((A & 0xf0) < 0xa0 && (A & 0x0f) < 0x0a)    diff = 0x06;
            else if ((A & 0xf0) < 0x90 && (A & 0x0f) > 0x09)    diff = 0x06;
            else if ((A & 0xf0) > 0x80 && (A & 0x0f) > 0x09)    diff = 0x66;
            else if ((A & 0xf0) > 0x90 && (A & 0x0f) < 0x0a)    diff = 0x66;
            break;
        case Z80_FLAG_CARRY:
                 if (                     (A & 0x0f) < 0x0a)    diff = 0x60;
            else if (                     (A & 0x0f) > 0x09)    diff = 0x66;
            break;
        case Z80_FLAG_CARRY | Z80_FLAG_HALF:
                                                                diff = 0x66;
            break;
    }

    /* Calculate carry out */
    if (((A & 0xf0) > 0x80 && (A & 0x0f) > 0x09) ||
        ((A & 0xf0) > 0x90 && (A & 0x0f) < 0x0a) ||
        (F & Z80_FLAG_CARRY))
        set_carry = true;

    /* Calculate half-carry out */
    if ( (!(F & Z80_FLAG_SUB) && (A & 0x0f) > 0x09) ||
         ( (F & Z80_FLAG_SUB) && (F & Z80_FLAG_HALF) && (A & 0x0f) < 0x06))
        set_half = true;

    /* Apply diff */
    if (F & Z80_FLAG_SUB)
        A -= diff;
    else
        A += diff;

    F = (F                    & Z80_FLAG_SUB       ) |
        (uint8_even_parity[A] ? Z80_FLAG_PARITY : 0) |
        (set_carry            ? Z80_FLAG_CARRY  : 0) |
        (set_half             ? Z80_FLAG_HALF   : 0) |
        (A == 0x00            ? Z80_FLAG_ZERO   : 0) |
        (A & 0x80             ? Z80_FLAG_SIGN   : 0);
}

#define LD(X,Y) { X = Y; }
#define JP(X)   { PC = X; }
#define JR(X)   { PC += (int8_t) X; }

/* 8-bit ALU Operations */
#define ADD(X,Y) { SET_FLAGS_ADD (X, Y); X += Y; }
#define SUB(X,Y) { SET_FLAGS_SUB (X, Y); X -= Y; }

#define AND(X,Y) { X &= Y; SET_FLAGS_AND; }
#define OR(X,Y)  { X |= Y; SET_FLAGS_OR_XOR; }
#define XOR(X,Y) { X ^= Y; SET_FLAGS_OR_XOR; }

#define INC(X) { X++; SET_FLAGS_INC (X); }
#define DEC(X) { X--; SET_FLAGS_DEC (X); }

/* 16-bit ALU Operations */
#define ADD_16(X,Y) { SET_FLAGS_ADD_16 (X, Y); X += Y; }
#define SUB_16(X,Y) { SET_FLAGS_SUB_16 (X, Y); X -= Y; }
#define INC_16(X)   { X++; }
#define DEC_16(X)   { X--; }

/* Stack Operations */
#define PUSH_16(X,Y) { memory_write (--SP, X); \
                       memory_write (--SP, Y); }
#define POP_16(X,Y)  { Y = memory_read (SP++); \
                       X = memory_read (SP++); }
#define CALL(X)      { memory_write (--SP, z80_regs.pc_h); \
                       memory_write (--SP, z80_regs.pc_l); \
                       JP (X); }
#define RET()        { z80_regs.pc_l = memory_read (SP++); \
                       z80_regs.pc_h = memory_read (SP++); }

void z80_instruction ()
{
    uint8_t instruction;
    uint8_t value_read;
    uint8_t temp;

    union {
        uint16_t w;
        struct {
            uint8_t l;
            uint8_t h;
        };
    } param;

    /* TODO: This register should be incremented in more places than just here */
    R = (R & 0x80) |((R + 1) & 0x7f);

    /* Fetch */
    instruction = memory_read (PC++);

    switch (z80_instruction_size[instruction])
    {
        case 3:
            param.l = memory_read (PC++);
            param.h = memory_read (PC++);
            break;
        case 2:
            param.l = memory_read (PC++);
            break;
        default:
            break;
    }

    switch (instruction)
    {
        case 0x00: /* NOP        */                             CYCLES (4);     break;
        case 0x01: /* LD BC,**   */ LD (BC, NN);                CYCLES (10);    break;
        case 0x02: /* LD (BC),A  */ memory_write (BC, A);       CYCLES (7);     break;
        case 0x03: /* INC BC     */ INC_16 (BC);                CYCLES (6);     break;
        case 0x04: /* INC B      */ INC (B);                    CYCLES (4);     break;
        case 0x05: /* DEC B      */ DEC (B);                    CYCLES (4);     break;
        case 0x06: /* LD B,*     */ LD (B, N);                  CYCLES (7);     break;
        case 0x07: /* RLCA       */ A = (A << 1) | (A >> 7);
                                    SET_FLAGS_RLCA (A);         CYCLES (4);     break;
        case 0x08: /* EX AF AF'  */ SWAP (uint16_t, AF, z80_regs.alt_af);
                                                                CYCLES (4);     break;
        case 0x09: /* ADD HL,BC  */ ADD_16 (HL, BC);            CYCLES (11);    break;
        case 0x0a: /* LD A,(BC)  */ A = memory_read (BC);       CYCLES (7);     break;
        case 0x0b: /* DEC BC     */ DEC_16 (BC);                CYCLES (6);     break;
        case 0x0c: /* INC C      */ INC (C);                    CYCLES (4);     break;
        case 0x0d: /* DEC C      */ DEC (C);                    CYCLES (4);     break;
        case 0x0e: /* LD C,*     */ LD (C, N);                  CYCLES (7);     break;
        case 0x0f: /* RRCA       */ A = (A >> 1) | (A << 7);
                                    F = (F & (Z80_FLAG_PARITY | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) |
                                        ((A & 0x80) ? Z80_FLAG_CARRY : 0);
                                                                CYCLES (4);     break;

        case 0x10: /* DJNZ       */ if (--B) { JR (N);          CYCLES (13); }
                                    else {                      CYCLES (8);  }  break;
        case 0x11: /* LD DE,**   */ LD (DE, NN);                CYCLES (10);    break;
        case 0x12: /* LD (DE),A  */ memory_write (DE, A);       CYCLES (7);     break;
        case 0x13: /* INC DE     */ INC_16 (DE);                CYCLES (6);     break;
        case 0x14: /* INC D      */ INC (D);                    CYCLES (4);     break;
        case 0x15: /* DEC D      */ DEC (D);                    CYCLES (4);     break;
        case 0x16: /* LD D,*     */ LD (D, N);                  CYCLES (7);     break;
        case 0x17: /* RLA        */ temp = A;
                                    A = (A << 1) + CARRY_BIT;
                                    F = (F & (Z80_FLAG_PARITY | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) |
                                                 ((temp & 0x80) ? Z80_FLAG_CARRY : 0);
                                                                CYCLES (4);     break;
        case 0x18: /* JR *       */ JR (N);                     CYCLES (12);    break;
        case 0x19: /* ADD HL,DE  */ ADD_16 (HL, DE);            CYCLES (11);    break;
        case 0x1a: /* LD A,(DE)  */ A = memory_read (DE);       CYCLES (7);     break;
        case 0x1b: /* DEC DE     */ DEC_16 (DE);                CYCLES (6);     break;
        case 0x1c: /* INC E      */ INC (E);                    CYCLES (4);     break;
        case 0x1d: /* DEC E      */ DEC (E);                    CYCLES (4);     break;
        case 0x1e: /* LD E,*     */ LD (E, N);                  CYCLES (7);     break;
        case 0x1f: /* RRA        */ temp = A;
                                    A = (A >> 1) + ((F & Z80_FLAG_CARRY) ? 0x80 : 0);
                                    F = (F & (Z80_FLAG_PARITY | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) |
                                                 ((temp & 0x01) ? Z80_FLAG_CARRY : 0);
                                                                CYCLES (4);     break;

        case 0x20: /* JR NZ      */ if (!(F & Z80_FLAG_ZERO)) {
                                        JR (N);                 CYCLES (12); }
                                    else {                      CYCLES (7);  }  break;
        case 0x21: /* LD HL,**   */ LD (HL, NN);                CYCLES (10);    break;
        case 0x22: /* LD (**),HL */ memory_write (NN,     L);
                                    memory_write (NN + 1, H);   CYCLES (16);    break;
        case 0x23: /* INC HL     */ INC_16 (HL);                CYCLES (6);     break;
        case 0x24: /* INC H      */ INC (H);                    CYCLES (4);     break;
        case 0x25: /* DEC H      */ DEC (H);                    CYCLES (4);     break;
        case 0x26: /* LD H,*     */ LD (H, N);                  CYCLES (7);     break;
        case 0x27: /* DAA        */ z80_instruction_daa ();     CYCLES (4);     break;
        case 0x28: /* JR Z       */ if (F & Z80_FLAG_ZERO) {
                                        JR (N);                 CYCLES (12); }
                                    else {                      CYCLES (7);  }  break;
        case 0x29: /* ADD HL,HL  */ ADD_16 (HL, HL);            CYCLES (11);    break;
        case 0x2a: /* LD HL,(**) */ L = memory_read (NN);
                                    H = memory_read (NN + 1);   CYCLES (16);    break;
        case 0x2b: /* DEC HL     */ DEC_16 (HL);                CYCLES (6);     break;
        case 0x2c: /* INC L      */ INC (L);                    CYCLES (4);     break;
        case 0x2d: /* DEC L      */ DEC (L);                    CYCLES (4);     break;
        case 0x2e: /* LD L,*     */ LD (L, N)                   CYCLES (7);     break;
        case 0x2f: /* CPL        */ A = ~A; SET_FLAGS_CPL;      CYCLES (4);     break;

        case 0x30: /* JR NC      */ if (!(F & Z80_FLAG_CARRY)) {
                                        JR (N);                 CYCLES (12); }
                                    else {                      CYCLES (7);  }  break;
        case 0x31: /* LD SP,**   */ LD (SP, NN);                CYCLES (10);    break;
        case 0x32: /* LD (**),A  */ memory_write (NN, A);       CYCLES (13);    break;
        case 0x33: /* INC SP     */ INC_16 (SP);                CYCLES (6);     break;
        case 0x34: /* INC (HL)   */ temp = memory_read (HL);
                                    temp++;
                                    memory_write (HL, temp);
                                    SET_FLAGS_INC (temp);       CYCLES (11);    break;
        case 0x35: /* DEC (HL)   */ temp = memory_read (HL);
                                    temp--;
                                    memory_write (HL, temp);
                                    SET_FLAGS_DEC (temp);       CYCLES (11);    break;
        case 0x36: /* LD (HL),*  */ memory_write (HL, N);       CYCLES (10);    break;
        case 0x37: /* SCF        */ F = (F & (Z80_FLAG_SIGN | Z80_FLAG_ZERO | Z80_FLAG_OVERFLOW)) | Z80_FLAG_CARRY;
                                                                CYCLES (4);     break;
        case 0x38: /* JR C,*     */ if (F & Z80_FLAG_CARRY) {
                                        JR (N);                 CYCLES (12); }
                                    else {                      CYCLES (7);  }  break;
        case 0x39: /* ADD HL,SP  */ ADD_16 (HL, SP);            CYCLES (11);    break;
        case 0x3a: /* LD A,(**)  */ A = memory_read (NN);       CYCLES (13);    break;
        case 0x3b: /* DEC SP     */ DEC (SP);                   CYCLES (6);     break;
        case 0x3c: /* INC A      */ INC (A);                    CYCLES (4);     break;
        case 0x3d: /* DEC A      */ DEC (A);                    CYCLES (4);     break;
        case 0x3e: /* LD A,*     */ LD (A, N);                  CYCLES (7);     break;
        case 0x3f: /* CCF        */ F = (F & (Z80_FLAG_SIGN | Z80_FLAG_ZERO | Z80_FLAG_OVERFLOW)) |
                                                 (CARRY_BIT ? Z80_FLAG_HALF : Z80_FLAG_CARRY);
                                                                CYCLES (4);     break;

        case 0x40: /* LD B,B     */ LD (B, B);                  CYCLES (4);     break;
        case 0x41: /* LD B,C     */ LD (B, C);                  CYCLES (4);     break;
        case 0x42: /* LD B,D     */ LD (B, D);                  CYCLES (4);     break;
        case 0x43: /* LD B,E     */ LD (B, E);                  CYCLES (4);     break;
        case 0x44: /* LD B,H     */ LD (B, H);                  CYCLES (4);     break;
        case 0x45: /* LD B,L     */ LD (B, L);                  CYCLES (4);     break;
        case 0x46: /* LD B,(HL)  */ B = memory_read(HL);        CYCLES (7);     break;
        case 0x47: /* LD B,A     */ LD (B, A);                  CYCLES (4);     break;
        case 0x48: /* LD C,B     */ LD (C, B);                  CYCLES (4);     break;
        case 0x49: /* LD C,C     */ LD (C, C);                  CYCLES (4);     break;
        case 0x4a: /* LD C,D     */ LD (C, D);                  CYCLES (4);     break;
        case 0x4b: /* LD C,E     */ LD (C, E);                  CYCLES (4);     break;
        case 0x4c: /* LD C,H     */ LD (C, H);                  CYCLES (4);     break;
        case 0x4d: /* LD C,L     */ LD (C, L);                  CYCLES (4);     break;
        case 0x4e: /* LD C,(HL)  */ C = memory_read(HL);        CYCLES (7);     break;
        case 0x4f: /* LD C,A     */ LD (C, A);                  CYCLES (4);     break;

        case 0x50: /* LD D,B     */ LD (D, B);                  CYCLES (4);     break;
        case 0x51: /* LD D,C     */ LD (D, C);                  CYCLES (4);     break;
        case 0x52: /* LD D,D     */ LD (D, D);                  CYCLES (4);     break;
        case 0x53: /* LD D,E     */ LD (D, E);                  CYCLES (4);     break;
        case 0x54: /* LD D,H     */ LD (D, H);                  CYCLES (4);     break;
        case 0x55: /* LD D,L     */ LD (D, L);                  CYCLES (4);     break;
        case 0x56: /* LD D,(HL)  */ D = memory_read(HL);        CYCLES (7);     break;
        case 0x57: /* LD D,A     */ LD (D, A);                  CYCLES (4);     break;
        case 0x58: /* LD E,B     */ LD (E, B);                  CYCLES (4);     break;
        case 0x59: /* LD E,C     */ LD (E, C);                  CYCLES (4);     break;
        case 0x5a: /* LD E,D     */ LD (E, D);                  CYCLES (4);     break;
        case 0x5b: /* LD E,E     */ LD (E, E);                  CYCLES (4);     break;
        case 0x5c: /* LD E,H     */ LD (E, H);                  CYCLES (4);     break;
        case 0x5d: /* LD E,L     */ LD (E, L);                  CYCLES (4);     break;
        case 0x5e: /* LD E,(HL)  */ E = memory_read(HL);        CYCLES (7);     break;
        case 0x5f: /* LD E,A     */ LD (E, A);                  CYCLES (4);     break;

        case 0x60: /* LD H,B     */ LD (H, B);                  CYCLES (4);     break;
        case 0x61: /* LD H,C     */ LD (H, C);                  CYCLES (4);     break;
        case 0x62: /* LD H,D     */ LD (H, D);                  CYCLES (4);     break;
        case 0x63: /* LD H,E     */ LD (H, E);                  CYCLES (4);     break;
        case 0x64: /* LD H,H     */ LD (H, H);                  CYCLES (4);     break;
        case 0x65: /* LD H,L     */ LD (H, L);                  CYCLES (4);     break;
        case 0x66: /* LD H,(HL)  */ H = memory_read(HL);        CYCLES (7);     break;
        case 0x67: /* LD H,A     */ LD (H, A);                  CYCLES (4);     break;
        case 0x68: /* LD L,B     */ LD (L, B);                  CYCLES (4);     break;
        case 0x69: /* LD L,C     */ LD (L, C);                  CYCLES (4);     break;
        case 0x6a: /* LD L,D     */ LD (L, D);                  CYCLES (4);     break;
        case 0x6b: /* LD L,E     */ LD (L, E);                  CYCLES (4);     break;
        case 0x6c: /* LD L,H     */ LD (L, H);                  CYCLES (4);     break;
        case 0x6d: /* LD L,L     */ LD (L, L);                  CYCLES (4);     break;
        case 0x6e: /* LD L,(HL)  */ L = memory_read(HL);        CYCLES (7);     break;
        case 0x6f: /* LD L,A     */ LD (L, A);                  CYCLES (4);     break;

        case 0x70: /* LD (HL),B  */ memory_write (HL, B);       CYCLES (7);     break;
        case 0x71: /* LD (HL),C  */ memory_write (HL, C);       CYCLES (7);     break;
        case 0x72: /* LD (HL),D  */ memory_write (HL, D);       CYCLES (7);     break;
        case 0x73: /* LD (HL),E  */ memory_write (HL, E);       CYCLES (7);     break;
        case 0x74: /* LD (HL),H  */ memory_write (HL, H);       CYCLES (7);     break;
        case 0x75: /* LD (HL),L  */ memory_write (HL, L);       CYCLES (7);     break;
        case 0x76: /* HALT       */ z80_regs.halt = true;       CYCLES (4);     break;
        case 0x77: /* LD (HL),A  */ memory_write (HL, A);       CYCLES (7);     break;
        case 0x78: /* LD A,B     */ LD (A, B);                  CYCLES (4);     break;
        case 0x79: /* LD A,C     */ LD (A, C);                  CYCLES (4);     break;
        case 0x7a: /* LD A,D     */ LD (A, D);                  CYCLES (4);     break;
        case 0x7b: /* LD A,E     */ LD (A, E);                  CYCLES (4);     break;
        case 0x7c: /* LD A,H     */ LD (A, H);                  CYCLES (4);     break;
        case 0x7d: /* LD A,L     */ LD (A, L);                  CYCLES (4);     break;
        case 0x7e: /* LD A,(HL)  */ A = memory_read(HL);        CYCLES (7);     break;
        case 0x7f: /* LD A,A     */ LD (A, A);                  CYCLES (4);     break;

        case 0x80: /* ADD A,B    */ ADD (A, B);                 CYCLES (4);     break;
        case 0x81: /* ADD A,C    */ ADD (A, C);                 CYCLES (4);     break;
        case 0x82: /* ADD A,D    */ ADD (A, D);                 CYCLES (4);     break;
        case 0x83: /* ADD A,E    */ ADD (A, E);                 CYCLES (4);     break;
        case 0x84: /* ADD A,H    */ ADD (A, H);                 CYCLES (4);     break;
        case 0x85: /* ADD A,L    */ ADD (A, L);                 CYCLES (4);     break;
        case 0x86: /* ADD A,(HL) */ temp = memory_read (HL);
                                    SET_FLAGS_ADD (A, temp); A += temp;
                                                                CYCLES (7);     break;
        case 0x87: /* ADD A,A    */ ADD (A, A);                 CYCLES (4);     break;
        case 0x88: /* ADC A,B    */ temp = B + CARRY_BIT; SET_FLAGS_ADC (B);
                                    A += temp;                  CYCLES (4);     break;
        case 0x89: /* ADC A,C    */ temp = C + CARRY_BIT; SET_FLAGS_ADC (C);
                                    A += temp;                  CYCLES (4);     break;
        case 0x8a: /* ADC A,D    */ temp = D + CARRY_BIT; SET_FLAGS_ADC (D);
                                    A += temp;                  CYCLES (4);     break;
        case 0x8b: /* ADC A,E    */ temp = E + CARRY_BIT; SET_FLAGS_ADC (E);
                                    A += temp;                  CYCLES (4);     break;
        case 0x8c: /* ADC A,H    */ temp = H + CARRY_BIT; SET_FLAGS_ADC (H);
                                    A += temp;                  CYCLES (4);     break;
        case 0x8d: /* ADC A,L    */ temp = L + CARRY_BIT; SET_FLAGS_ADC (L);
                                    A += temp;                  CYCLES (4);     break;
        case 0x8e: /* ADC A,(HL) */ value_read = memory_read (HL);
                                    temp = value_read + CARRY_BIT;
                                    SET_FLAGS_ADC (value_read);
                                    A += temp;                  CYCLES (7);     break;
        case 0x8f: /* ADC A,A    */ temp = A + CARRY_BIT; SET_FLAGS_ADC (A);
                                    A += temp;                  CYCLES (4);     break;

        case 0x90: /* SUB A,B    */ SUB (A, B);                 CYCLES (4);     break;
        case 0x91: /* SUB A,C    */ SUB (A, C);                 CYCLES (4);     break;
        case 0x92: /* SUB A,D    */ SUB (A, D);                 CYCLES (4);     break;
        case 0x93: /* SUB A,E    */ SUB (A, E);                 CYCLES (4);     break;
        case 0x94: /* SUB A,H    */ SUB (A, H);                 CYCLES (4);     break;
        case 0x95: /* SUB A,L    */ SUB (A, L);                 CYCLES (4);     break;
        case 0x96: /* SUB A,(HL) */ temp = memory_read (HL);
                                    SET_FLAGS_SUB (A, temp);
                                    A -= temp;                  CYCLES (7);     break;
        case 0x97: /* SUB A,A    */ SUB (A, A);                 CYCLES (4);     break;
        case 0x98: /* SBC A,B    */ temp = B + CARRY_BIT; SET_FLAGS_SBC (B);
                                    A -= temp;                  CYCLES (4);     break;
        case 0x99: /* SBC A,C    */ temp = C + CARRY_BIT; SET_FLAGS_SBC (C);
                                    A -= temp;                  CYCLES (4);     break;
        case 0x9a: /* SBC A,D    */ temp = D + CARRY_BIT; SET_FLAGS_SBC (D);
                                    A -= temp;                  CYCLES (4);     break;
        case 0x9b: /* SBC A,E    */ temp = E + CARRY_BIT; SET_FLAGS_SBC (E);
                                    A -= temp;                  CYCLES (4);     break;
        case 0x9c: /* SBC A,H    */ temp = H + CARRY_BIT; SET_FLAGS_SBC (H);
                                    A -= temp;                  CYCLES (4);     break;
        case 0x9d: /* SBC A,L    */ temp = L + CARRY_BIT; SET_FLAGS_SBC (L);
                                    A -= temp;                  CYCLES (4);     break;
        case 0x9e: /* SBC A,(HL) */ value_read = memory_read (HL);
                                    temp = value_read + CARRY_BIT;
                                    SET_FLAGS_SBC (value_read);
                                    A -= temp;                  CYCLES (7);     break;
        case 0x9f: /* SBC A,A    */ temp = A + CARRY_BIT; SET_FLAGS_SBC (A);
                                    A -= temp;                  CYCLES (4);     break;

        case 0xa0: /* AND A,B    */ AND (A, B);                 CYCLES (4);     break;
        case 0xa1: /* AND A,C    */ AND (A, C);                 CYCLES (4);     break;
        case 0xa2: /* AND A,D    */ AND (A, D);                 CYCLES (4);     break;
        case 0xa3: /* AND A,E    */ AND (A, E);                 CYCLES (4);     break;
        case 0xa4: /* AND A,H    */ AND (A, H);                 CYCLES (4);     break;
        case 0xa5: /* AND A,L    */ AND (A, L);                 CYCLES (4);     break;
        case 0xa6: /* AND A,(HL) */ A &= memory_read (HL); SET_FLAGS_AND;
                                                                CYCLES (7);     break;
        case 0xa7: /* AND A,A    */ AND (A, A);                 CYCLES (4);     break;
        case 0xa8: /* XOR A,B    */ XOR (A, B);                 CYCLES (4);     break;
        case 0xa9: /* XOR A,C    */ XOR (A, C);                 CYCLES (4);     break;
        case 0xaa: /* XOR A,D    */ XOR (A, D);                 CYCLES (4);     break;
        case 0xab: /* XOR A,E    */ XOR (A, E);                 CYCLES (4);     break;
        case 0xac: /* XOR A,H    */ XOR (A, H);                 CYCLES (4);     break;
        case 0xad: /* XOR A,L    */ XOR (A, L);                 CYCLES (4);     break;
        case 0xae: /* XOR A,(HL) */ A ^= memory_read(HL); SET_FLAGS_OR_XOR;
                                                                CYCLES (7);     break;
        case 0xaf: /* XOR A,A    */ XOR (A, A);                 CYCLES (4);     break;

        case 0xb0: /* OR  A,B    */ OR (A, B);                  CYCLES (4);     break;
        case 0xb1: /* OR  A,C    */ OR (A, C);                  CYCLES (4);     break;
        case 0xb2: /* OR  A,D    */ OR (A, D);                  CYCLES (4);     break;
        case 0xb3: /* OR  A,E    */ OR (A, E);                  CYCLES (4);     break;
        case 0xb4: /* OR  A,H    */ OR (A, H);                  CYCLES (4);     break;
        case 0xb5: /* OR  A,L    */ OR (A, L);                  CYCLES (4);     break;
        case 0xb6: /* OR (HL)    */ A |= memory_read (HL); SET_FLAGS_OR_XOR;
                                                                CYCLES (7);     break;
        case 0xb7: /* OR  A,A    */ OR (A, A);                  CYCLES (4);     break;
        case 0xb8: /* CP A,B     */ SET_FLAGS_SUB (A, B);       CYCLES (4);     break;
        case 0xb9: /* CP A,C     */ SET_FLAGS_SUB (A, C);       CYCLES (4);     break;
        case 0xba: /* CP A,D     */ SET_FLAGS_SUB (A, D);       CYCLES (4);     break;
        case 0xbb: /* CP A,E     */ SET_FLAGS_SUB (A, E);       CYCLES (4);     break;
        case 0xbc: /* CP A,H     */ SET_FLAGS_SUB (A, H);       CYCLES (4);     break;
        case 0xbd: /* CP A,L     */ SET_FLAGS_SUB (A, L);       CYCLES (4);     break;
        case 0xbe: /* CP A,(HL)  */ temp = memory_read (HL); SET_FLAGS_SUB (A, temp);
                                                                CYCLES (7);     break;
        case 0xbf: /* CP A,A     */ SET_FLAGS_SUB (A, A);       CYCLES (4);     break;

        case 0xc0: /* RET NZ     */ if (!(F & Z80_FLAG_ZERO)) {
                                        RET ();                 CYCLES (11);  }
                                    else {                      CYCLES (5);  }  break;
        case 0xc1: /* POP BC     */ POP_16 (B, C);              CYCLES (10);    break;
        case 0xc2: /* JP NZ,**   */ PC = (F & Z80_FLAG_ZERO) ? PC : NN;
                                                                CYCLES (10);    break;
        case 0xc3: /* JP **      */ JP (NN);                    CYCLES (10);    break;
        case 0xc4: /* CALL NZ,** */ if (!(F & Z80_FLAG_ZERO)) {
                                        CALL (NN);              CYCLES (17); }
                                    else {                      CYCLES (10); }  break;
        case 0xc5: /* PUSH BC    */ PUSH_16 (B, C);             CYCLES (11);    break;
        case 0xc6: /* ADD A,*    */ ADD (A, N);                 CYCLES (7);     break;
        case 0xc7: /* RST 00h    */ CALL (0x00);                CYCLES (11);    break;
        case 0xc8: /* RET Z      */ if (F & Z80_FLAG_ZERO) {
                                        RET ();                 CYCLES (11); }
                                    else {                      CYCLES (5);  }  break;
        case 0xc9: /* RET        */ RET ();                     CYCLES (10);    break;
        case 0xca: /* JP Z,**    */ PC = (F & Z80_FLAG_ZERO) ? NN : PC;
                                                                CYCLES (10);    break;
        case 0xcb: /* Bit Instruction */ z80_bit_instruction (); break;
        case 0xcc: /* CALL Z,**  */ if (F & Z80_FLAG_ZERO) {
                                        CALL (NN);              CYCLES (17); }
                                    else {                      CYCLES (10); }  break;
        case 0xcd: /* CALL **    */ CALL (NN);                  CYCLES (17);    break;
        case 0xce: /* ADC A,*    */ temp = N + CARRY_BIT;
                                    SET_FLAGS_ADC (N);
                                    A += temp;                  CYCLES (7);     break;
        case 0xcf: /* RST 08h    */ CALL (0x08);                CYCLES (11);    break;

        case 0xd0: /* RET NC     */ if (!(F & Z80_FLAG_CARRY)) {
                                        RET ();                 CYCLES (11); }
                                    else {                      CYCLES (5);  }  break;
        case 0xd1: /* POP DE     */ E = memory_read (SP++);
                                    D = memory_read (SP++);
                                                                CYCLES (10);    break;
        case 0xd2: /* JP NC,**   */ PC = (F & Z80_FLAG_CARRY) ? PC : NN;
                                                                CYCLES (10);    break;
        case 0xd3: /* OUT (*),A  */ io_write (N, A);            CYCLES (11);    break;
        case 0xd4: /* CALL NC,** */ if (!(F & Z80_FLAG_CARRY)) {
                                        CALL (NN);              CYCLES (17); }
                                    else {                      CYCLES (10); }  break;
        case 0xd5: /* PUSH DE    */ PUSH_16 (D, E);             CYCLES (11);    break;
        case 0xd6: /* SUB A,*    */ SUB (A, N);                 CYCLES (7);     break;
        case 0xd7: /* RST 10h    */ CALL (0x10);                CYCLES (11);    break;
        case 0xd8: /* RET C      */ if (F & Z80_FLAG_CARRY) {
                                        RET ();                   CYCLES (11); }
                                    else {                      CYCLES (5);  }  break;
        case 0xd9: /* EXX        */ SWAP (uint16_t, BC, z80_regs.alt_bc);
                                    SWAP (uint16_t, DE, z80_regs.alt_de);
                                    SWAP (uint16_t, HL, z80_regs.alt_hl);
                                                                CYCLES (4);     break;
        case 0xda: /* JP C,**    */ PC = (F & Z80_FLAG_CARRY) ? NN : PC;
                                                                CYCLES (10);    break;
        case 0xdb: /* IN A,(*)   */ A = io_read (N);            CYCLES (11);    break;
        case 0xdc: /* CALL C,**  */ if (F & Z80_FLAG_CARRY) {
                                        CALL (NN);              CYCLES (17); }
                                    else {                      CYCLES (10); }  break;

        case 0xdd: /* IX         */ IX = z80_ix_iy_instruction (IX);            break;
        case 0xde: /* SBC A,*    */ temp = N + CARRY_BIT;
                                    SET_FLAGS_SBC (N);
                                    A -= temp;                  CYCLES (7);     break;
        case 0xdf: /* RST 18h    */ CALL (0x18);                CYCLES (11);    break;

        case 0xe0: /* RET PO     */ if (!(F & Z80_FLAG_PARITY)) {
                                        RET ();                 CYCLES (11); }
                                    else {                      CYCLES (5);  }  break;
        case 0xe1: /* POP HL     */ POP_16 (H, L);              CYCLES (10);    break;
        case 0xe2: /* JP PO      */ PC = (F & Z80_FLAG_PARITY) ? PC : NN;
                                                                CYCLES (10);    break;
        case 0xe3: /* EX (SP),HL */ temp = L;
                                    L = memory_read (SP);
                                    memory_write (SP, temp);
                                    temp = H;
                                    H = memory_read (SP + 1);
                                    memory_write (SP + 1, temp);
                                                                CYCLES (19);    break;
        case 0xe4: /* CALL PO,** */ if (!(F & Z80_FLAG_PARITY)) {
                                        CALL (NN);              CYCLES (17); }
                                    else {                      CYCLES (10); }  break;
        case 0xe5: /* PUSH HL    */ PUSH_16 (H, L);             CYCLES (11);    break;
        case 0xe6: /* AND A,*    */ A &= N; SET_FLAGS_AND;      CYCLES (7);     break;
        case 0xe7: /* RST 20h    */ CALL (0x20);                CYCLES (11);    break;
        case 0xe8: /* RET PE     */ if (F & Z80_FLAG_PARITY) {
                                        RET ();                 CYCLES (11); }
                                    else {                      CYCLES (5);  }  break;
        case 0xe9: /* JP (HL)    */ JP (HL);                    CYCLES (4);     break;
        case 0xea: /* JP PE,**   */ if (F & Z80_FLAG_PARITY) JP (NN);
                                                                CYCLES (10);    break;
        case 0xeb: /* EX DE,HL   */ SWAP (uint16_t, DE, HL);    CYCLES (4);     break;
        case 0xed: /* Extended Instructions */ z80_extended_instruction ();     break;
        case 0xee: /* XOR A,*    */ A ^= N; SET_FLAGS_OR_XOR;   CYCLES (7);     break;
        case 0xef: /* RST 28h    */ CALL (0x28);                CYCLES (11);    break;

        case 0xf0: /* RET P      */ if (!(F & Z80_FLAG_SIGN)) {
                                        RET ();                 CYCLES (11); }
                                    else {                      CYCLES (5);  }  break;
        case 0xf1: /* POP AF     */ POP_16 (A, F);              CYCLES (10);    break;
        case 0xf2: /* JP P,**    */ if (!(F & Z80_FLAG_SIGN)) JP (NN);
                                                                CYCLES (10);    break;
        case 0xf3: /* DI         */ IFF1 = false; IFF2 = false; CYCLES (4);     break;
        case 0xf4: /* CALL P,**  */ if (!(F & Z80_FLAG_SIGN)) {
                                        CALL (NN);              CYCLES (17); }
                                    else {                      CYCLES (10); }  break;
        case 0xf5: /* PUSH AF    */ PUSH_16 (A, F);             CYCLES (11);    break;
        case 0xf6: /* OR A,*     */ A |= N; SET_FLAGS_OR_XOR;   CYCLES (7);     break;
        case 0xf7: /* RST 30h    */ CALL (0x30);                CYCLES (11);    break;
        case 0xf8: /* RET M      */ if (F & Z80_FLAG_SIGN) {
                                        RET ();                 CYCLES (11); }
                                    else {                      CYCLES (5);  }  break;
        case 0xf9: /* LD SP,HL   */ LD (SP, HL);                CYCLES (6);     break;
        case 0xfa: /* JP M,**    */ PC = (F & Z80_FLAG_SIGN) ? NN : PC;
                                                                CYCLES (10);    break;
        case 0xfb: /* EI         */ IFF1 = true; IFF2 = true;
                                    instructions_before_interrupts = 2;
                                                                CYCLES (4);     break;
        case 0xfc: /* CALL M,**  */ if (F & Z80_FLAG_SIGN) {
                                        CALL (NN);              CYCLES (17); }
                                    else {                      CYCLES (10); }  break;
        case 0xfd: /* IY         */ IY = z80_ix_iy_instruction (IY);            break;

        case 0xfe: /* CP A,*     */ SET_FLAGS_SUB (A, N);       CYCLES (7);     break;
        case 0xff: /* RST 38h    */ CALL (0x38);                CYCLES (11);    break;

        default:
            fprintf (stderr, "Unknown instruction: \"%s\" (%02x).\n",
                     z80_instruction_name[instruction], instruction);
            snepulator.abort = true;
    }
}

/* TODO: Remove knowledge of the VDP from here */
extern void vdp_clock_update (uint64_t cycles);
extern bool vdp_get_interrupt (void);
extern bool sms_nmi_check (void);

void z80_run_until_cycle (uint64_t run_until)
{
    while (z80_cycle < run_until)
    {
        /* TIMING DEBUG */
        uint64_t previous_cycle_count = z80_cycle;
        uint8_t debug_instruction_0 = memory_read (PC + 0);
        uint8_t debug_instruction_1 = memory_read (PC + 1);
        uint8_t debug_instruction_2 = memory_read (PC + 2);
        uint8_t debug_instruction_3 = memory_read (PC + 3);
        if (z80_regs.halt)
        {
            /* NOP */ CYCLES (4);
        }
        else
        {
            z80_instruction ();
        }

        if (z80_cycle == previous_cycle_count)
        {
            fprintf (stderr, "Instruction %x %x %x %x took no time\n",
                     debug_instruction_0,
                     debug_instruction_1,
                     debug_instruction_2,
                     debug_instruction_3);

            if (debug_instruction_0 == 0xcb)
                fprintf (stderr, "DECODE %s %s %x %x took no time\n",
                         z80_instruction_name[debug_instruction_0],
                         z80_instruction_name_bits[debug_instruction_1],
                         debug_instruction_2,
                         debug_instruction_3);
            else if (debug_instruction_0 == 0xed)
                fprintf (stderr, "DECODE %s %s %x %x took no time\n",
                         z80_instruction_name[debug_instruction_0],
                         z80_instruction_name_extended[debug_instruction_1],
                         debug_instruction_2,
                         debug_instruction_3);
            else if ((debug_instruction_0 == 0xdd || debug_instruction_0 == 0xfd) && debug_instruction_1 == 0xcb)
                fprintf (stderr, "DECODE %s %s %s took no time\n",
                         z80_instruction_name[debug_instruction_0],
                         z80_instruction_name_ix[debug_instruction_1],
                         z80_instruction_name_bits[debug_instruction_2]);
            else if (debug_instruction_0 == 0xdd || debug_instruction_0 == 0xfd)
                    fprintf (stderr, "DECODE %s %s %x %x took no time\n",
                             z80_instruction_name[debug_instruction_0],
                             z80_instruction_name_ix[debug_instruction_1],
                             debug_instruction_2,
                             debug_instruction_3);
            else
                fprintf (stderr, "DECODE %s %x %x %x took no time\n",
                         z80_instruction_name[debug_instruction_0],
                         debug_instruction_1,
                         debug_instruction_2,
                         debug_instruction_3);
            snepulator.abort = true;
        }
        /* END TIMING DEBUG */

        /* Time has passed, update the VDP state */
        /* TODO: This shouldn't really be in the z80 code. Perhaps a register-able time-passed function? */
        vdp_clock_update (z80_cycle);

        /* Check for interrupts */
        if (instructions_before_interrupts)
            instructions_before_interrupts--;

        /* TODO: Make this less Master System specific */
        if (!instructions_before_interrupts)
        {
            /* First, check for non-maskable interrupts */
            if (sms_nmi_check())
            {
                IFF1 = false;
                /* TODO: Cycle count? */
                memory_write (--SP, z80_regs.pc_h);
                memory_write (--SP, z80_regs.pc_l);
                PC = 0x66;
            }

            /* Then check for maskable interrupts */
            if (IFF1 && vdp_get_interrupt ())
            {
                if (z80_regs.halt)
                {
                    z80_regs.halt = false;
                    PC += 1;
                }

                IFF1 = false;
                IFF2 = false;

                switch (z80_regs.im)
                {
                    /* TODO: Cycle count? */
                    case 1:
                        memory_write (--SP, z80_regs.pc_h);
                        memory_write (--SP, z80_regs.pc_l);
                        PC = 0x38;
                        break;
                    default:
                        fprintf (stderr, "Unknown interrupt mode %d.\n", z80_regs.im);
                        snepulator.abort = true;
                }
            }

        }

    }
}
