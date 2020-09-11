#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../util.h"
#include "../snepulator.h"
extern Snepulator_State state;

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

/* Cycle count (reset each time z80_run_cycles () completes) */
uint64_t z80_cycle = 0;
#define CYCLES(X) { z80_cycle += (X); }

/* Function pointers for accessing the rest of the system */
uint8_t (* memory_read) (uint16_t) = NULL;
void    (* memory_write)(uint16_t, uint8_t) = NULL;
uint8_t (* io_read)     (uint8_t) = NULL;
void    (* io_write)    (uint8_t, uint8_t) = NULL;

/* TODO: Note: Interrupts should not be accepted until after the instruction following EI */

/* TODO: Consider the accuracy of the R register */

uint8_t instructions_before_interrupts = 0;

#define X 1 /* Extended */
#define U 0 /* Unused */

static const uint8_t z80_instruction_size_ix [256] = {
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

static const uint8_t uint8_even_parity [256] = {
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


/*
 * Reset the Z80 registers to power-on defaults.
 * TODO: Some functions come from state.x, others as parameters. Make this more consistent.
 */
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


#define SET_FLAGS_AND { F = (uint8_even_parity [z80_regs.a] ? Z80_FLAG_PARITY : 0) | \
                            (                                 Z80_FLAG_HALF      ) | \
                            (A == 0x00                      ? Z80_FLAG_ZERO   : 0) | \
                            ((A & 0x80)                     ? Z80_FLAG_SIGN   : 0); }

#define SET_FLAGS_OR_XOR { F = (uint8_even_parity [z80_regs.a] ? Z80_FLAG_PARITY : 0) | \
                               (A == 0x00                      ? Z80_FLAG_ZERO   : 0) | \
                               ((A & 0x80)                     ? Z80_FLAG_SIGN   : 0); }

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

#define SET_FLAGS_RLC(X) { F = (uint8_even_parity [X]                  ? Z80_FLAG_PARITY : 0) | \
                               ((X & 0x01)                             ? Z80_FLAG_CARRY  : 0) | \
                               (X == 0x00                              ? Z80_FLAG_ZERO   : 0) | \
                               ((X & 0x80)                             ? Z80_FLAG_SIGN   : 0); }

#define SET_FLAGS_RRC(X) { F = (uint8_even_parity [X]                  ? Z80_FLAG_PARITY : 0) | \
                               ((X & 0x80)                             ? Z80_FLAG_CARRY  : 0) | \
                               (X == 0x00                              ? Z80_FLAG_ZERO   : 0) | \
                               ((X & 0x80)                             ? Z80_FLAG_SIGN   : 0); }

#define SET_FLAGS_RL(X) { F = (uint8_even_parity [X]                   ? Z80_FLAG_PARITY : 0) | \
                              (X == 0x00                               ? Z80_FLAG_ZERO   : 0) | \
                              ((X & 0x80)                              ? Z80_FLAG_SIGN   : 0); }

#define SET_FLAGS_RR(X) { F = (uint8_even_parity [X]                   ? Z80_FLAG_PARITY : 0) | \
                              (X == 0x00                               ? Z80_FLAG_ZERO   : 0) | \
                              ((X & 0x80)                              ? Z80_FLAG_SIGN   : 0); }

#define SET_FLAGS_RRD_RLD { F = (F                                & Z80_FLAG_CARRY)      | \
                                (uint8_even_parity [z80_regs.a]   ? Z80_FLAG_PARITY : 0) | \
                                (A == 0x00                        ? Z80_FLAG_ZERO   : 0) | \
                                (A & 0x80                         ? Z80_FLAG_SIGN   : 0); }


#define SET_FLAGS_ED_IN(X) { F = (F                     & Z80_FLAG_CARRY     ) | \
                                 (X & 0x80              ? Z80_FLAG_SIGN   : 0) | \
                                 (X == 0                ? Z80_FLAG_ZERO   : 0) | \
                                 (uint8_even_parity [X] ? Z80_FLAG_PARITY : 0); }



/*
 * Read and execute an IX / IY bit instruction.
 * Called after reading the prefix.
 */
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
                                    SET_FLAGS_RRC (data);       CYCLES (23);    break;
        case 0x10: /* RL  (ix+*) */ temp = data;
                                    data = (data << 1) | ((F & Z80_FLAG_CARRY) ? 0x01 : 0x00);
                                    SET_FLAGS_RL (data);
                                    F |= (temp & 0x80) ? Z80_FLAG_CARRY : 0;
                                                                CYCLES (23);    break;
        case 0x18: /* RR  (ix+*) */ temp = data;
                                    data = (data >> 1) | ((F & Z80_FLAG_CARRY) ? 0x80 : 0x00);
                                    SET_FLAGS_RR (data);
                                    F |= (temp & 0x01) ? Z80_FLAG_CARRY : 0;
                                                                CYCLES (23);    break;

        case 0x20: /* SLA (ix+*) */ temp = data;
                                    data = (data << 1); SET_FLAGS_RL (data);
                                    F |= (temp & 0x80) ? Z80_FLAG_CARRY : 0;
                                                                CYCLES (23);    break;
        case 0x28: /* SRA (ix+*) */ temp = data;
                                    data = (data >> 1) | (data & 0x80); SET_FLAGS_RR (data);
                                    F |= (temp & 0x01) ? Z80_FLAG_CARRY : 0;
                                                                CYCLES (23);    break;

        case 0x30: /* SLL (ix+*) */ temp = data;
                                    data = (data << 1) | 0x01; SET_FLAGS_RL (data);
                                    F |= (temp & 0x80) ? Z80_FLAG_CARRY : 0;
                                                                CYCLES (23);    break;
        case 0x38: /* SRL (ix+*) */ temp = data;
                                    data = (data >> 1); SET_FLAGS_RR (data);
                                    F |= (temp & 0x01) ? Z80_FLAG_CARRY : 0;
                                                                CYCLES (23);    break;
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
            snprintf (state.error_buffer, 79, "Unknown ix/iy bit instruction: \"%s\" (%02x).",
                      z80_instruction_name_bits [instruction] , instruction);
            snepulator_error ("Z80 Error", state.error_buffer);
            return -1;
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


void z80_run_instruction (void);

/* TODO: Additional cycles? */
#define FALL_THROUGH() { PC--; z80_run_instruction (); }


/*
 * Read and execute an IX / IY instruction.
 * Called after reading the prefix.
 */
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
        case 0x44: /* LD B,IXH     */ B = reg_ix_iy.h;          CYCLES (8);     break;
        case 0x45: /* LD B,IXL     */ B = reg_ix_iy.l;          CYCLES (8);     break;
        case 0x46: /* LD B,(IX+*)  */ B = memory_read (reg_ix_iy.w + (int8_t) N);
                                                                CYCLES (19);    break;
        case 0x47: /* -            */ FALL_THROUGH ();                          break;
        case 0x48: /* -            */ FALL_THROUGH ();                          break;
        case 0x49: /* -            */ FALL_THROUGH ();                          break;
        case 0x4a: /* -            */ FALL_THROUGH ();                          break;
        case 0x4b: /* -            */ FALL_THROUGH ();                          break;
        case 0x4c: /* LD C,IXH     */ C = reg_ix_iy.h;          CYCLES (8);     break;
        case 0x4d: /* LD C,IXL     */ C = reg_ix_iy.l;          CYCLES (8);     break;
        case 0x4e: /* LD C,(IX+*)  */ C = memory_read (reg_ix_iy.w + (int8_t) N);
                                                                CYCLES (19);    break;
        case 0x4f: /* -            */ FALL_THROUGH ();                          break;

        case 0x50: /* -            */ FALL_THROUGH ();                          break;
        case 0x51: /* -            */ FALL_THROUGH ();                          break;
        case 0x52: /* -            */ FALL_THROUGH ();                          break;
        case 0x53: /* -            */ FALL_THROUGH ();                          break;
        case 0x54: /* LD D,IXH     */ D = reg_ix_iy.h;          CYCLES (8);     break;
        case 0x55: /* LD D,IXL     */ D = reg_ix_iy.l;          CYCLES (8);     break;
        case 0x56: /* LD D,(IX+*)  */ D = memory_read (reg_ix_iy.w + (int8_t) N);
                                                                CYCLES (19);    break;
        case 0x57: /* -            */ FALL_THROUGH ();                          break;
        case 0x58: /* -            */ FALL_THROUGH ();                          break;
        case 0x59: /* -            */ FALL_THROUGH ();                          break;
        case 0x5a: /* -            */ FALL_THROUGH ();                          break;
        case 0x5b: /* -            */ FALL_THROUGH ();                          break;
        case 0x5c: /* LD E,IXH     */ E = reg_ix_iy.h;          CYCLES (8);     break;
        case 0x5d: /* LD E,IXL     */ E = reg_ix_iy.l;          CYCLES (8);     break;
        case 0x5e: /* LD E,(IX+*)  */ E = memory_read (reg_ix_iy.w + (int8_t) N);
                                                                CYCLES (19);    break;
        case 0x5f: /* -            */ FALL_THROUGH ();                          break;

        case 0x60: /* LD IXH,B     */ reg_ix_iy.h = B;          CYCLES (8);     break;
        case 0x61: /* LD IXH,C     */ reg_ix_iy.h = C;          CYCLES (8);     break;
        case 0x62: /* LD IXH,D     */ reg_ix_iy.h = D;          CYCLES (8);     break;
        case 0x63: /* LD IXH,E     */ reg_ix_iy.h = E;          CYCLES (8);     break;
        case 0x64: /* LD IXH,IXH   */ reg_ix_iy.h = reg_ix_iy.h; CYCLES (8);    break;
        case 0x65: /* LD IXH,IXL   */ reg_ix_iy.h = reg_ix_iy.l; CYCLES (8);    break;
        case 0x66: /* LD H,(IX+*)  */ H = memory_read (reg_ix_iy.w + (int8_t) N);
                                                                CYCLES (19);    break;
        case 0x67: /* LD IXH,A     */ reg_ix_iy.h = A;          CYCLES (8);     break;
        case 0x68: /* LD IXL,B     */ reg_ix_iy.l = B;          CYCLES (8);     break;
        case 0x69: /* LD IXL,C     */ reg_ix_iy.l = C;          CYCLES (8);     break;
        case 0x6a: /* LD IXL,D     */ reg_ix_iy.l = D;          CYCLES (8);     break;
        case 0x6b: /* LD IXL,E     */ reg_ix_iy.l = E;          CYCLES (8);     break;
        case 0x6c: /* LD IXL,IXH   */ reg_ix_iy.l = reg_ix_iy.h; CYCLES (8);    break;
        case 0x6d: /* LD IXL,IXL   */ reg_ix_iy.l = reg_ix_iy.l; CYCLES (8);    break;
        case 0x6e: /* LD L,(IX+*)  */ L = memory_read (reg_ix_iy.w + (int8_t) N);
                                                                CYCLES (19);    break;
        case 0x6f: /* LD IXL,A     */ reg_ix_iy.l = A;          CYCLES (8);     break;

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
        case 0x7c: /* LD A,IXH     */ A = reg_ix_iy.h;          CYCLES (8);     break;
        case 0x7d: /* LD A,IXL     */ A = reg_ix_iy.l;          CYCLES (8);     break;
        case 0x7e: /* LD A,(IX+*)  */ A = memory_read (reg_ix_iy.w + (int8_t) N);
                                                                CYCLES (19);    break;
        case 0x7f: /* -            */ FALL_THROUGH ();                          break;

        case 0x84: /* ADD A,IXH    */ SET_FLAGS_ADD (A, reg_ix_iy.h);
                                      A += reg_ix_iy.h;         CYCLES (8);     break;
        case 0x85: /* ADD A,IXL    */ SET_FLAGS_ADD (A, reg_ix_iy.l);
                                      A += reg_ix_iy.l;         CYCLES (8);     break;
        case 0x86: /* ADD A,(IX+*) */ temp = memory_read (reg_ix_iy.w + (int8_t) N);
                                      SET_FLAGS_ADD (A, temp);
                                      A += temp;                CYCLES (19);    break;

        case 0x8c: /* ADC A,IXH    */ temp = reg_ix_iy.h + CARRY_BIT;
                                      SET_FLAGS_ADC (reg_ix_iy.h);
                                      A += temp;                CYCLES (8);     break;
        case 0x8d: /* ADC A,IXL    */ temp = reg_ix_iy.l + CARRY_BIT;
                                      SET_FLAGS_ADC (reg_ix_iy.l);
                                      A += temp;                CYCLES (8);     break;
        case 0x8e: /* ADC A,(IX+*) */ value_read = memory_read (reg_ix_iy.w + (int8_t) N);
                                      temp = value_read + CARRY_BIT;
                                      SET_FLAGS_ADC (value_read);
                                      A += temp;                CYCLES (19);    break;

        case 0x94: /* SUB A,IXH    */ SET_FLAGS_SUB (A, reg_ix_iy.h);
                                      A -= reg_ix_iy.h;         CYCLES (8);     break;
        case 0x95: /* SUB A,IXL    */ SET_FLAGS_SUB (A, reg_ix_iy.l);
                                      A -= reg_ix_iy.l;         CYCLES (8);     break;
        case 0x96: /* SUB A,(IX+*) */ temp = memory_read (reg_ix_iy.w + (int8_t) N);
                                      SET_FLAGS_SUB (A, temp);
                                      A -= temp;                CYCLES (19);    break;
        case 0x9c: /* SBC A,IXH    */ temp = reg_ix_iy.h + CARRY_BIT;
                                      SET_FLAGS_SBC (reg_ix_iy.h);
                                      A -= temp;                CYCLES (8);     break;
        case 0x9d: /* SBC A,IXL    */ temp = reg_ix_iy.l + CARRY_BIT;
                                      SET_FLAGS_SBC (reg_ix_iy.l);
                                      A -= temp;                CYCLES (8);     break;
        case 0x9e: /* SBC A,(IX+*) */ value_read= memory_read (reg_ix_iy.w + (int8_t) N);
                                      temp = value_read + CARRY_BIT;
                                      SET_FLAGS_SBC (value_read);
                                      A -= temp;                CYCLES (19);    break;

        case 0xa4: /* AND A,IXH    */ A &= reg_ix_iy.h; SET_FLAGS_AND;
                                                                CYCLES (8);     break;
        case 0xa5: /* AND A,IXL    */ A &= reg_ix_iy.l; SET_FLAGS_AND;
                                                                CYCLES (8);     break;
        case 0xa6: /* AND A,(IX+*) */ A &= memory_read (reg_ix_iy.w + (int8_t) N);
                                      SET_FLAGS_AND;            CYCLES (19);    break;
        case 0xac: /* XOR A,IXH    */ A ^= reg_ix_iy.h; SET_FLAGS_OR_XOR;
                                                                CYCLES (8);     break;
        case 0xad: /* XOR A,IXL    */ A ^= reg_ix_iy.l; SET_FLAGS_OR_XOR;
                                                                CYCLES (8);     break;
        case 0xae: /* XOR A,(IX+*) */ A ^= memory_read (reg_ix_iy.w + (int8_t) N);
                                      SET_FLAGS_OR_XOR;         CYCLES (19);    break;

        case 0xb4: /* OR A,IXH     */ A |= reg_ix_iy.h; SET_FLAGS_OR_XOR;
                                                                CYCLES (8);     break;
        case 0xb5: /* OR A,IXL     */ A |= reg_ix_iy.l; SET_FLAGS_OR_XOR;
                                                                CYCLES (8);     break;
        case 0xb6: /* OR A,(IX+*)  */ A |= memory_read (reg_ix_iy.w + (int8_t) N);
                                      SET_FLAGS_OR_XOR;         CYCLES (19);    break;
        case 0xbc: /* CP  A,IXH    */ SET_FLAGS_SUB (A, reg_ix_iy.h);
                                                                CYCLES (8);     break;
        case 0xbd: /* CP  A,IXL    */ SET_FLAGS_SUB (A, reg_ix_iy.l);
                                                                CYCLES (8);     break;
        case 0xbe: /* CP  A,(IX+*) */ temp = memory_read (reg_ix_iy.w + (int8_t) N);
                                      SET_FLAGS_SUB (A, temp);  CYCLES (19);    break;

        case 0xcb: /* IX Bit Instructions */ z80_ix_iy_bit_instruction (reg_ix_iy.w);
                                                                                break;
        case 0xcd: /* -            */ FALL_THROUGH ();                          break;
        case 0xe1: /* POP IX       */ reg_ix_iy.l = memory_read (SP++);
                                      reg_ix_iy.h = memory_read (SP++);
                                                                CYCLES (14);    break;
        case 0xe3: /* EX (SP),IX */ temp = reg_ix_iy.l;
                                    reg_ix_iy.l = memory_read (SP);
                                    memory_write (SP, temp);
                                    temp = reg_ix_iy.h;
                                    reg_ix_iy.h = memory_read (SP + 1);
                                    memory_write (SP + 1, temp); CYCLES (23);   break;
        case 0xe5: /* PUSH IX      */ memory_write (--SP, reg_ix_iy.h);
                                      memory_write (--SP, reg_ix_iy.l);
                                                                CYCLES (15);    break;
        case 0xe6: /* -            */ FALL_THROUGH ();                          break;
        case 0xe9: /* JP (IX)      */ PC = reg_ix_iy.w;         CYCLES (8);     break;

        case 0xf9: /* LD SP,IX     */ SP = reg_ix_iy.w;         CYCLES (10);    break;

        default:
            snprintf (state.error_buffer, 79, "Unknown ix/iy instruction: \"%s\" (%02x).",
                      z80_instruction_name_ix [instruction] , instruction);
            snepulator_error ("Z80 Error", state.error_buffer);
            return -1;
    }

    return reg_ix_iy.w;
}


/*
 * Read and execute a bit instruction.
 * Called after reading the 0xcb prefix.
 */
uint32_t z80_bit_instruction ()
{
    uint8_t instruction = memory_read (PC++);
    uint8_t data = 0x00;
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
            snprintf (state.error_buffer, 79, "Unknown bit instruction: \"%s\" (%02x).",
                      z80_instruction_name_bits [instruction] , instruction);
            snepulator_error ("Z80 Error", state.error_buffer);
            return -1;
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


/*
 * Execute the DAA instruction.
 */
void z80_instruction_daa ()
{
    bool set_carry = false;
    bool set_half = false;
    uint8_t diff = 0x00;

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

    F = (F                     & Z80_FLAG_SUB       ) |
        (uint8_even_parity [A] ? Z80_FLAG_PARITY : 0) |
        (set_carry             ? Z80_FLAG_CARRY  : 0) |
        (set_half              ? Z80_FLAG_HALF   : 0) |
        (A == 0x00             ? Z80_FLAG_ZERO   : 0) |
        (A & 0x80              ? Z80_FLAG_SIGN   : 0);
}


/*****************************/
/**  Extended Instructions  **/
/*****************************/


/* IN B, (C) */
static void z80_ed_40_in_b_c (void)
{
    z80_regs.b = io_read (z80_regs.c);
    SET_FLAGS_ED_IN (z80_regs.b);
    z80_cycle += 12;
}


/* OUT (C), B */
static void z80_ed_41_out_c_b (void)
{
    io_write (z80_regs.c, z80_regs.b);
    z80_cycle += 12;
}


/* SBC HL, BC */
static void z80_ed_42_sbc_hl_bc (void)
{
    uint16_t temp;
    temp = z80_regs.bc + CARRY_BIT;
    SET_FLAGS_SBC_16 (z80_regs.bc);
    z80_regs.hl -= temp;
    z80_cycle += 15;
}


/* LD (**), BC */
static void z80_ed_43_ld_xx_bc (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    memory_write (addr.w,     z80_regs.c);
    memory_write (addr.w + 1, z80_regs.b);
    z80_cycle += 20;
}


/* NEG */
static void z80_ed_44_neg (void)
{
    uint8_t temp;
    temp = z80_regs.a;
    z80_regs.a = 0 - (int8_t) z80_regs.a;
    z80_regs.f = (temp != 0                  ? Z80_FLAG_CARRY    : 0) |
                 (                             Z80_FLAG_SUB         ) |
                 (temp == 0x80               ? Z80_FLAG_OVERFLOW : 0) |
                 ((0 - (temp & 0x0f)) & 0x10 ? Z80_FLAG_HALF     : 0) |
                 (z80_regs.a == 0            ? Z80_FLAG_ZERO     : 0) |
                 (z80_regs.a & 0x80          ? Z80_FLAG_SIGN     : 0);
    z80_cycle += 8;
}


/* RETN */
static void z80_ed_45_retn (void)
{
    z80_regs.pc_l = memory_read (z80_regs.sp++);
    z80_regs.pc_h = memory_read (z80_regs.sp++);
    IFF1 = IFF2;
    z80_cycle += 14;
}


/* IM 0 */
static void z80_ed_46_im_0 (void)
{
    z80_regs.im = 0;
    z80_cycle += 8;
}


/* LD I, A */
static void z80_ed_47_ld_i_a (void)
{
    z80_regs.i = z80_regs.a;
    z80_cycle += 9;
}


/* IN C, (C) */
static void z80_ed_48_in_c_c (void)
{
    z80_regs.c = io_read (z80_regs.c);
    SET_FLAGS_ED_IN (z80_regs.c);
    z80_cycle += 12;
}


/* OUT (C), C */
static void z80_ed_49_out_c_c (void)
{
    io_write (z80_regs.c, z80_regs.c);
    z80_cycle += 12;
}


/* ADC HL, BC */
static void z80_ed_4a_adc_hl_bc (void)
{
    uint16_t temp;
    temp = z80_regs.bc + CARRY_BIT;
    SET_FLAGS_ADC_16 (z80_regs.bc);
    z80_regs.hl += temp;
    z80_cycle += 15;
}


/* LD BC, (**) */
static void z80_ed_4b_ld_bc_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    z80_regs.c = memory_read (addr.w);
    z80_regs.b = memory_read (addr.w + 1);
    z80_cycle += 20;
}


/* NEG (undocumented) */
static void z80_ed_4c_neg (void)
{
    z80_ed_44_neg ();
}


/* RETI */
static void z80_ed_4d_reti (void)
{
    z80_regs.pc_l = memory_read (z80_regs.sp++);
    z80_regs.pc_h = memory_read (z80_regs.sp++);
    z80_cycle += 14;
}


/* IM 0 (undocumented) */
static void z80_ed_4e_im_0 (void)
{
    z80_ed_46_im_0 ();
}


/* LD R, A */
static void z80_ed_4f_ld_r_a (void)
{
    z80_regs.r = z80_regs.a;
    z80_cycle += 9;
}


/* IN D, (C) */
static void z80_ed_50_in_d_c (void)
{
    z80_regs.d = io_read (z80_regs.c);
    SET_FLAGS_ED_IN (z80_regs.d);
    z80_cycle += 12;
}


/* OUT (C), D */
static void z80_ed_51_out_c_d (void)
{
    io_write (z80_regs.c, z80_regs.d);
    z80_cycle += 12;
}


/* SBC HL, DE */
static void z80_ed_52_sbc_hl_de (void)
{
    uint16_t temp;
    temp = z80_regs.de + CARRY_BIT;
    SET_FLAGS_SBC_16 (z80_regs.de);
    z80_regs.hl -= temp;
    z80_cycle += 15;
}


/* LD (**), DC */
static void z80_ed_53_ld_xx_de (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    memory_write (addr.w,     z80_regs.e);
    memory_write (addr.w + 1, z80_regs.d);
    z80_cycle += 20;
}


/* NEG (undocumented) */
static void z80_ed_54_neg (void)
{
    z80_ed_44_neg ();
}


/* RETN */
static void z80_ed_55_retn (void)
{
    z80_ed_45_retn ();
}


/* IM 1 */
static void z80_ed_56_im_1 (void)
{
    z80_regs.im = 1;
    z80_cycle += 8;
}


/* LD A, I */
static void z80_ed_57_ld_a_i (void)
{
    z80_regs.a = z80_regs.i;
    F = (F                 & Z80_FLAG_CARRY       ) |
        (z80_regs.i & 0x80 ? Z80_FLAG_SIGN     : 0) |
        (z80_regs.i == 0   ? Z80_FLAG_ZERO     : 0) |
        (z80_regs.iff2     ? Z80_FLAG_OVERFLOW : 0);
    z80_cycle += 9;
}


/* IN E, (C) */
static void z80_ed_58_in_e_c (void)
{
    z80_regs.e = io_read (z80_regs.c);
    SET_FLAGS_ED_IN (z80_regs.e);
    z80_cycle += 12;
}


/* OUT (C), E */
static void z80_ed_59_out_c_e (void)
{
    io_write (z80_regs.c, z80_regs.e);
    z80_cycle += 12;
}


/* ADC HL, DE */
static void z80_ed_5a_adc_hl_de (void)
{
    uint16_t temp = z80_regs.de + CARRY_BIT;
    SET_FLAGS_ADC_16 (z80_regs.de);
    z80_regs.hl += temp;
    z80_cycle += 15;
}


/* LD DE, (**) */
static void z80_ed_5b_ld_de_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    E = memory_read (addr.w);
    D = memory_read (addr.w + 1);
    z80_cycle += 20;
}


/* NEG (undocumented) */
static void z80_ed_5c_neg (void)
{
    z80_ed_44_neg ();
}


/* RETN */
static void z80_ed_5d_retn (void)
{
    z80_ed_45_retn ();
}


/* IM 2 */
static void z80_ed_5e_im_2 (void)
{
    z80_regs.im = 2;
    z80_cycle += 8;
}


/* LD A, R */
static void z80_ed_5f_ld_a_r (void)
{
    z80_regs.a = z80_regs.r;
    z80_regs.f = (z80_regs.f        & Z80_FLAG_CARRY       ) |
                 (z80_regs.r & 0x80 ? Z80_FLAG_SIGN     : 0) |
                 (z80_regs.r == 0   ? Z80_FLAG_ZERO     : 0) |
                 (z80_regs.iff2     ? Z80_FLAG_OVERFLOW : 0);
    z80_cycle += 9;
}


/* IN H, (C) */
static void z80_ed_60_in_h_c (void)
{
    z80_regs.h = io_read (z80_regs.c);
    SET_FLAGS_ED_IN (z80_regs.h);
    z80_cycle += 12;
}


/* OUT (C), H */
static void z80_ed_61_out_c_h (void)
{
    io_write (z80_regs.c, z80_regs.h);
    z80_cycle += 12;
}


/* SBC HL, HL */
static void z80_ed_62_sbc_hl_hl (void)
{
    uint16_t temp = z80_regs.hl + CARRY_BIT;
    SET_FLAGS_SBC_16 (z80_regs.hl);
    z80_regs.hl -= temp;
    z80_cycle += 15;
}


/* LD (**), hl (undocumented) */
static void z80_ed_63_ld_xx_hl (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    memory_write (addr.w,     z80_regs.l);
    memory_write (addr.w + 1, z80_regs.h);
    z80_cycle += 20;
}


/* NEG (undocumented) */
static void z80_ed_64_neg (void)
{
    z80_ed_44_neg ();
}


/* RETN */
static void z80_ed_65_retn (void)
{
    z80_ed_45_retn ();
}


/* IM 0 */
static void z80_ed_66_im_0 (void)
{
    z80_ed_46_im_0 ();
}


/* RRD */
static void z80_ed_67_rrd (void)
{
    uint16_t_Split shifted;

    /* Calculate 12-bit value */
    shifted.l = memory_read (z80_regs.hl);
    shifted.h = z80_regs.a & 0x0f;
    shifted.w = (shifted.w >> 4) | ((shifted.w & 0x000f) << 8);

    /* Lower 8 bits go to memory */
    memory_write (z80_regs.hl, shifted.l);

    /* Upper 4 bits go to A */
    z80_regs.a = (z80_regs.a & 0xf0) | shifted.h;

    SET_FLAGS_RRD_RLD;
    z80_cycle += 18;
}


/* IN L, (C) */
static void z80_ed_68_in_l_c (void)
{
    z80_regs.l = io_read (z80_regs.c);
    SET_FLAGS_ED_IN (z80_regs.l);
    z80_cycle += 12;
}


/* OUT (C), L */
static void z80_ed_69_out_c_l (void)
{
    io_write (z80_regs.c, z80_regs.l);
    z80_cycle += 12;
}


/* ADC HL, HL */
static void z80_ed_6a_adc_hl_hl (void)
{
    uint16_t temp = z80_regs.hl + CARRY_BIT;
    SET_FLAGS_ADC_16 (z80_regs.hl);
    z80_regs.hl += temp;
    z80_cycle += 15;
}


/* LD HL, (**) (undocumented) */
static void z80_ed_6b_ld_hl_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    L = memory_read (addr.w);
    H = memory_read (addr.w + 1);
    z80_cycle += 20;
}


/* NEG (undocumented) */
static void z80_ed_6c_neg (void)
{
    z80_ed_44_neg ();
}


/* RETN */
static void z80_ed_6d_retn (void)
{
    z80_ed_45_retn ();
}


/* IM 0 (undocumented) */
static void z80_ed_6e_im_0 (void)
{
    z80_ed_46_im_0 ();
}


/* RLD */
static void z80_ed_6f_rld (void)
{
    uint16_t_Split shifted;

    /* Calculate 12-bit value */
    shifted.w = ((uint16_t) memory_read (z80_regs.hl) << 4) | (z80_regs.a & 0x0f);

    /* Lower 8 bits go to memory */
    memory_write (z80_regs.hl, shifted.l);

    /* Upper 4 bits go to A */
    z80_regs.a = (z80_regs.a & 0xf0) | shifted.h;

    SET_FLAGS_RRD_RLD;
    z80_cycle += 18;
}


/* IN (C) (undocumented) */
static void z80_ed_70_in_c (void)
{
    uint8_t throwaway;
    throwaway = io_read (z80_regs.c);
    SET_FLAGS_ED_IN (throwaway);
    z80_cycle += 12;
}


/* OUT (C), 0 (undocumented) */
static void z80_ed_71_out_c_0 (void)
{
    io_write (z80_regs.c, 0);
    z80_cycle += 12;
}


/* SBC HL, SP */
static void z80_ed_72_sbc_hl_sp (void)
{
    uint16_t temp = z80_regs.sp + CARRY_BIT;
    SET_FLAGS_SBC_16 (z80_regs.sp);
    z80_regs.hl -= temp;
    z80_cycle += 15;
}


/* LD (**), SP */
static void z80_ed_73_ld_xx_sp (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    memory_write (addr.w,     z80_regs.sp_l);
    memory_write (addr.w + 1, z80_regs.sp_h);
    z80_cycle += 20;
}


/* NEG (undocumented) */
static void z80_ed_74_neg (void)
{
    z80_ed_44_neg ();
}


/* RETN */
static void z80_ed_75_retn (void)
{
    z80_ed_45_retn ();
}


/* IM 1 */
static void z80_ed_76_im_1 (void)
{
    z80_ed_56_im_1 ();
}


/* IN A, (C) */
static void z80_ed_78_in_a_c (void)
{
    z80_regs.a = io_read (z80_regs.c);
    SET_FLAGS_ED_IN (z80_regs.a);
    z80_cycle += 12;
}


/* OUT (C), A */
static void z80_ed_79_out_c_a (void)
{
    io_write (z80_regs.c, z80_regs.a);
    z80_cycle += 12;
}


/* ADC HL, SP */
static void z80_ed_7a_adc_hl_sp (void)
{
    uint16_t temp = z80_regs.sp + CARRY_BIT;
    SET_FLAGS_ADC_16 (z80_regs.sp);
    z80_regs.hl += temp;
    z80_cycle += 15;
}


/* LD SP, (**) */
static void z80_ed_7b_ld_sp_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    z80_regs.sp_l = memory_read (addr.w);
    z80_regs.sp_h = memory_read (addr.w + 1);
    z80_cycle += 20;
}


/* NEG (undocumented) */
static void z80_ed_7c_neg (void)
{
    z80_ed_44_neg ();
}


/* RETN */
static void z80_ed_7d_retn (void)
{
    z80_ed_45_retn ();
}


/* IM 2 */
static void z80_ed_7e_im_2 (void)
{
    z80_ed_5e_im_2 ();
}


/* LDI */
static void z80_ed_a0_ldi (void)
{
    memory_write (z80_regs.de, memory_read (z80_regs.hl));
    z80_regs.hl++;
    z80_regs.de++;
    z80_regs.bc--;
    z80_regs.f &= (Z80_FLAG_CARRY | Z80_FLAG_ZERO | Z80_FLAG_SIGN);
    z80_regs.f |= (z80_regs.bc ? Z80_FLAG_OVERFLOW : 0);
    z80_cycle += 16;
}


/* CPI */
static void z80_ed_a1_cpi (void)
{
    uint8_t temp = memory_read (z80_regs.hl);
    z80_regs.hl++;
    z80_regs.bc--;
    SET_FLAGS_CPD_CPI (temp);
    z80_cycle += 16;
}


/* INI */
static void z80_ed_a2_ini (void)
{
    memory_write (z80_regs.hl, io_read (z80_regs.c));
    z80_regs.hl++;
    z80_regs.b--;
    z80_regs.f = (z80_regs.f      & Z80_FLAG_CARRY) |
                 (                  Z80_FLAG_SUB  ) |
                 (z80_regs.b == 0 ? Z80_FLAG_ZERO : 0);
    z80_cycle += 16;
}


/* OUTI */
static void z80_ed_a3_outi (void)
{
    io_write (z80_regs.c, memory_read (z80_regs.hl));
    z80_regs.hl++;
    z80_regs.b--;
    z80_regs.f = (z80_regs.f      & Z80_FLAG_CARRY) |
                 (                  Z80_FLAG_SUB  ) |
                 (z80_regs.b == 0 ? Z80_FLAG_ZERO : 0);
    z80_cycle += 16;
}


/* LDD */
static void z80_ed_a8_ldd (void)
{
    memory_write (z80_regs.de, memory_read (z80_regs.hl));
    z80_regs.hl--;
    z80_regs.de--;
    z80_regs.bc--;
    z80_regs.f &= (Z80_FLAG_CARRY | Z80_FLAG_ZERO | Z80_FLAG_SIGN);
    z80_regs.f |= (z80_regs.bc ? Z80_FLAG_OVERFLOW : 0);
    z80_cycle += 16;
}


/* CPD */
static void z80_ed_a9_cpd (void)
{
    uint8_t temp = memory_read (z80_regs.hl);
    z80_regs.hl--;
    z80_regs.bc--;
    SET_FLAGS_CPD_CPI (temp);
    z80_cycle += 16;
}


/* IND */
static void z80_ed_aa_ind (void)
{
    memory_write (z80_regs.hl, io_read (z80_regs.c));
    z80_regs.hl--;
    z80_regs.b--;
    z80_regs.f = (z80_regs.f      & Z80_FLAG_CARRY) |
                 (                  Z80_FLAG_SUB  ) |
                 (z80_regs.b == 0 ? Z80_FLAG_ZERO : 0);
    z80_cycle += 16;
}


/* OUTD */
static void z80_ed_ab_outd (void)
{
    /* TODO: Implement 'unknown' flag behaviour.
     *       Described in 'The Undocumented Z80 Documented'. */
    uint8_t temp = memory_read (z80_regs.hl);
    z80_regs.b--;
    io_write (z80_regs.c, temp);
    z80_regs.hl--;
    z80_regs.f |= Z80_FLAG_SUB;
    z80_regs.f = (z80_regs.f & ~Z80_FLAG_ZERO) | (z80_regs.b == 0 ? Z80_FLAG_ZERO : 0);
    z80_cycle += 16;
}


/* LDIR */
static void z80_ed_b0_ldir (void)
{
    memory_write (z80_regs.de, memory_read (z80_regs.hl));
    z80_regs.hl++;
    z80_regs.de++;
    z80_regs.bc--;
    z80_regs.f = (z80_regs.f & (Z80_FLAG_CARRY | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) | (z80_regs.bc ? Z80_FLAG_OVERFLOW : 0);
    if (z80_regs.bc)
    {
        z80_regs.pc -= 2;
        z80_cycle += 21;
    }
    else
    {
        z80_cycle += 16;
    }
}


/* CPIR */
static void z80_ed_b1_cpir (void)
{
    uint8_t temp = memory_read (z80_regs.hl);
    z80_regs.hl++;
    z80_regs.bc--;
    if (z80_regs.bc != 0 && z80_regs.a != temp)
    {
        z80_regs.pc -= 2;
        z80_cycle += 21;
    }
    else
    {
        z80_cycle += 16;
    }
    SET_FLAGS_CPD_CPI (temp);
}


/* INIR */
static void z80_ed_b2_inir (void)
{
    memory_write (z80_regs.hl, io_read (z80_regs.c));
    z80_regs.hl++;
    z80_regs.b--;
    z80_regs.f = (z80_regs.f & Z80_FLAG_CARRY) | (Z80_FLAG_SUB | Z80_FLAG_ZERO );
    if (z80_regs.b == 0)
    {
        z80_cycle += 16;
    }
    else
    {
        z80_regs.pc -= 2;
        z80_cycle += 21;
    }
}


/* OTIR */
static void z80_ed_b3_otir (void)
{
    io_write (z80_regs.c, memory_read (z80_regs.hl));
    z80_regs.hl++;
    z80_regs.b--;
    z80_regs.f = (z80_regs.f & Z80_FLAG_CARRY) | (Z80_FLAG_SUB | Z80_FLAG_ZERO);
    if (z80_regs.b)
    {
        z80_regs.pc -= 2;
        z80_cycle += 21;
    }
    else
    {
        z80_cycle += 16;
    }
}


/* LDDR */
static void z80_ed_b8_lddr (void)
{
    memory_write (z80_regs.de, memory_read (z80_regs.hl));
    z80_regs.hl--;
    z80_regs.de--;
    z80_regs.bc--;
    z80_regs.f = (z80_regs.f & (Z80_FLAG_CARRY | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) | (z80_regs.bc ? Z80_FLAG_OVERFLOW : 0);
    if (z80_regs.bc)
    {
        z80_regs.pc -= 2;
        z80_cycle += 21;
    }
    else
    {
        z80_cycle += 16;
    }
}


/* CPDR */
static void z80_ed_b9_cpdr (void)
{
    uint8_t temp = memory_read (z80_regs.hl);
    z80_regs.hl--;
    z80_regs.bc--;
    SET_FLAGS_CPD_CPI (temp);
    if (z80_regs.bc != 0 && z80_regs.a != temp)
    {
        z80_regs.pc -= 2;
        z80_cycle += 21;
    }
    else
    {
        z80_cycle += 16;
    }
}


/* INDR */
static void z80_ed_ba_indr (void)
{
    memory_write (z80_regs.hl, io_read (z80_regs.c));
    z80_regs.hl--;
    z80_regs.b--;
    z80_regs.f = (z80_regs.f & Z80_FLAG_CARRY) | (Z80_FLAG_SUB | Z80_FLAG_ZERO );
    if (z80_regs.b == 0)
    {
        z80_cycle += 16;
    }
    else
    {
        z80_regs.pc -= 2;
        z80_cycle += 21;
    }
}


/* OTDR */
static void z80_ed_bb_otdr (void)
{
    io_write (z80_regs.c, memory_read (z80_regs.hl));
    z80_regs.hl--;
    z80_regs.b--;
    z80_regs.f = (z80_regs.f & Z80_FLAG_CARRY) | (Z80_FLAG_SUB | Z80_FLAG_ZERO);
    if (z80_regs.b)
    {
        z80_regs.pc -= 2;
        z80_cycle += 21;
    }
    else
    {
        z80_cycle += 16;
    }
}


/* NOP (undocumented) */
static void z80_ed_xx_nop (void)
{
    z80_cycle += 8;
}

void (*z80_ed_instruction [256]) (void) = {
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_40_in_b_c,   z80_ed_41_out_c_b,  z80_ed_42_sbc_hl_bc,    z80_ed_43_ld_xx_bc,
    z80_ed_44_neg,      z80_ed_45_retn,     z80_ed_46_im_0,         z80_ed_47_ld_i_a,
    z80_ed_48_in_c_c,   z80_ed_49_out_c_c,  z80_ed_4a_adc_hl_bc,    z80_ed_4b_ld_bc_xx,
    z80_ed_4c_neg,      z80_ed_4d_reti,     z80_ed_4e_im_0,         z80_ed_4f_ld_r_a,
    z80_ed_50_in_d_c,   z80_ed_51_out_c_d,  z80_ed_52_sbc_hl_de,    z80_ed_53_ld_xx_de,
    z80_ed_54_neg,      z80_ed_55_retn,     z80_ed_56_im_1,         z80_ed_57_ld_a_i,
    z80_ed_58_in_e_c,   z80_ed_59_out_c_e,  z80_ed_5a_adc_hl_de,    z80_ed_5b_ld_de_xx,
    z80_ed_5c_neg,      z80_ed_5d_retn,     z80_ed_5e_im_2,         z80_ed_5f_ld_a_r,
    z80_ed_60_in_h_c,   z80_ed_61_out_c_h,  z80_ed_62_sbc_hl_hl,    z80_ed_63_ld_xx_hl,
    z80_ed_64_neg,      z80_ed_65_retn,     z80_ed_66_im_0,         z80_ed_67_rrd,
    z80_ed_68_in_l_c,   z80_ed_69_out_c_l,  z80_ed_6a_adc_hl_hl,    z80_ed_6b_ld_hl_xx,
    z80_ed_6c_neg,      z80_ed_6d_retn,     z80_ed_6e_im_0,         z80_ed_6f_rld,
    z80_ed_70_in_c,     z80_ed_71_out_c_0,  z80_ed_72_sbc_hl_sp,    z80_ed_73_ld_xx_sp,
    z80_ed_74_neg,      z80_ed_75_retn,     z80_ed_76_im_1,         z80_ed_xx_nop,
    z80_ed_78_in_a_c,   z80_ed_79_out_c_a,  z80_ed_7a_adc_hl_sp,    z80_ed_7b_ld_sp_xx,
    z80_ed_7c_neg,      z80_ed_7d_retn,     z80_ed_7e_im_2,         z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_a0_ldi,      z80_ed_a1_cpi,      z80_ed_a2_ini,          z80_ed_a3_outi,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_a8_ldd,      z80_ed_a9_cpd,      z80_ed_aa_ind,          z80_ed_ab_outd,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_b0_ldir,     z80_ed_b1_cpir,     z80_ed_b2_inir,         z80_ed_b3_otir,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_b8_lddr,     z80_ed_b9_cpdr,     z80_ed_ba_indr,         z80_ed_bb_otdr,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
    z80_ed_xx_nop,      z80_ed_xx_nop,      z80_ed_xx_nop,          z80_ed_xx_nop,
};


/*************************/
/**  Main Instructions  **/
/*************************/

/* NOP */
static void z80_00_nop (void)
{
    z80_cycle += 4;
}


/* LD BC, ** */
static void z80_01_ld_bc_xx (void)
{
    z80_regs.c = memory_read (z80_regs.pc++);
    z80_regs.b = memory_read (z80_regs.pc++);
    z80_cycle += 10;
}


/* LD (BC), A */
static void z80_02_ld_bc_a (void)
{
    memory_write (z80_regs.bc, z80_regs.a);
    z80_cycle += 7;
}


/* INC BC */
static void z80_03_inc_bc (void)
{
    z80_regs.bc++;
    z80_cycle += 6;
}


/* INC B */
static void z80_04_inc_b (void)
{
    z80_regs.b++;
    SET_FLAGS_INC (z80_regs.b);
    z80_cycle += 4;
}


/* DEC B */
static void z80_05_dec_b (void)
{
    z80_regs.b--;
    SET_FLAGS_DEC (z80_regs.b);
    z80_cycle += 4;
}


/* LD B, * */
static void z80_06_ld_b_x (void)
{
    z80_regs.b = memory_read (z80_regs.pc++);
    z80_cycle += 7;
}


/* RLCA */
static void z80_07_rlca (void)
{
    z80_regs.a = (z80_regs.a << 1) | (z80_regs.a >> 7);
    SET_FLAGS_RLCA (z80_regs.a);
    z80_cycle += 4;
}


/* EX AF AF' */
static void z80_08_ex_af_af (void)
{
    SWAP (uint16_t, z80_regs.af, z80_regs.alt_af);
    z80_cycle += 4;
}


/* ADD HL, BC */
static void z80_09_add_hl_bc (void)
{
    SET_FLAGS_ADD_16 (z80_regs.hl, z80_regs.bc);
    z80_regs.hl += z80_regs.bc;
    z80_cycle += 11;
}


/* LD A, (BC) */
static void z80_0a_ld_a_bc (void)
{
    z80_regs.a = memory_read (z80_regs.bc);
    z80_cycle += 7;
}


/* DEC BC */
static void z80_0b_dec_bc (void)
{
    z80_regs.bc--;
    z80_cycle += 6;
}


/* INC C */
static void z80_0c_inc_c (void)
{
    z80_regs.c++;
    SET_FLAGS_INC (z80_regs.c);
    z80_cycle += 4;
}


/* DEC C */
static void z80_0d_dec_c (void)
{
    z80_regs.c--;
    SET_FLAGS_DEC (z80_regs.c);
    z80_cycle += 4;
}


/* LD C, * */
static void z80_0e_ld_c_x (void)
{
    z80_regs.c = memory_read (z80_regs.pc++);
    z80_cycle += 7;
}


/* RRCA */
static void z80_0f_rrca (void)
{
    A = (A >> 1) | (A << 7);
    F = (F & (Z80_FLAG_PARITY | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) | ((A & 0x80) ? Z80_FLAG_CARRY : 0);
    z80_cycle += 4;
}


/* DJNZ */
static void z80_10_djnz (void)
{
    uint8_t imm = memory_read (z80_regs.pc++);

    if (--z80_regs.b)
    {
        z80_regs.pc += (int8_t) imm;
        z80_cycle += 13;
    }
    else
    {
        z80_cycle += 8;
    }
}


/* LD DE, ** */
static void z80_11_ld_de_xx (void)
{
    z80_regs.e = memory_read (z80_regs.pc++);
    z80_regs.d = memory_read (z80_regs.pc++);
    z80_cycle += 10;
}


/* LD (DE), A */
static void z80_12_ld_de_a (void)
{
    memory_write (z80_regs.de, z80_regs.a);
    z80_cycle += 7;
}


/* INC DE */
static void z80_13_inc_de (void)
{
    z80_regs.de++;
    z80_cycle += 6;
}


/* INC D */
static void z80_14_inc_d (void)
{
    z80_regs.d++;
    SET_FLAGS_INC (z80_regs.d);
    z80_cycle += 4;
}


/* DEC D */
static void z80_15_dec_d (void)
{
    z80_regs.d--;
    SET_FLAGS_DEC (z80_regs.d);
    z80_cycle += 4;
}


/* LD D, * */
static void z80_16_ld_d_x (void)
{
    z80_regs.d = memory_read (z80_regs.pc++);
    z80_cycle += 7;
}


/* RLA */
static void z80_17_rla (void)
{
    uint8_t temp = z80_regs.a;
    z80_regs.a = (z80_regs.a << 1) + CARRY_BIT;
    z80_regs.f = (z80_regs.f & (Z80_FLAG_PARITY | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) | ((temp & 0x80) ? Z80_FLAG_CARRY : 0);
    z80_cycle += 4;
}


/* JR */
static void z80_18_jr (void)
{
    uint8_t imm = memory_read (z80_regs.pc++);
    z80_regs.pc += (int8_t) imm;
    z80_cycle += 12;
}


/* ADD HL, DE */
static void z80_19_add_hl_de (void)
{
    SET_FLAGS_ADD_16 (z80_regs.hl, z80_regs.de);
    z80_regs.hl += z80_regs.de;
    z80_cycle += 11;
}


/* LD A, (DE) */
static void z80_1a_ld_a_de (void)
{
    z80_regs.a = memory_read (z80_regs.de);
    z80_cycle += 7;
}


/* DEC DE */
static void z80_1b_dec_de (void)
{
    z80_regs.de--;
    z80_cycle += 6;
}


/* INC E */
static void z80_1c_inc_e (void)
{
    z80_regs.e++;
    SET_FLAGS_INC (z80_regs.e);
    z80_cycle += 4;
}


/* DEC E */
static void z80_1d_dec_e (void)
{
    z80_regs.e--;
    SET_FLAGS_DEC (z80_regs.e);
    z80_cycle += 4;
}


/* LD E, * */
static void z80_1e_ld_e_x (void)
{
    z80_regs.e = memory_read (z80_regs.pc++);
    z80_cycle += 7;
}


/* RRA */
static void z80_1f_rra (void)
{
    uint8_t temp = z80_regs.a;
    z80_regs.a = (z80_regs.a >> 1) + ((z80_regs.f & Z80_FLAG_CARRY) ? 0x80 : 0);
    z80_regs.f = (z80_regs.f & (Z80_FLAG_PARITY | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) | ((temp & 0x01) ? Z80_FLAG_CARRY : 0);
    z80_cycle += 4;
}


/* JR NZ */
static void z80_20_jr_nz (void)
{
    uint8_t imm = memory_read (z80_regs.pc++);

    if (!(z80_regs.f & Z80_FLAG_ZERO))
    {
        z80_regs.pc += (int8_t) imm;
        z80_cycle += 12;
    }
    else
    {
        z80_cycle += 7;
    }
}


/* LD HL, ** */
static void z80_21_ld_hl_xx (void)
{
    z80_regs.l = memory_read (z80_regs.pc++);
    z80_regs.h = memory_read (z80_regs.pc++);
    z80_cycle += 10;
}


/* LD (**), HL */
static void z80_22_ld_xx_hl (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);
    memory_write (addr.w,     z80_regs.l);
    memory_write (addr.w + 1, z80_regs.h);
    z80_cycle += 16;
}


/* INC HL */
static void z80_23_inc_hl (void)
{
    z80_regs.hl++;
    z80_cycle += 6;
}


/* INC H */
static void z80_24_inc_h (void)
{
    z80_regs.h++;
    SET_FLAGS_INC (z80_regs.h);
    z80_cycle += 4;
}


/* DEC H */
static void z80_25_dec_h (void)
{
    z80_regs.h--;
    SET_FLAGS_DEC (z80_regs.h);
    z80_cycle += 4;
}


/* LD H, * */
static void z80_26_ld_h_x (void)
{
    z80_regs.h = memory_read (z80_regs.pc++);
    z80_cycle += 7;
}


/* DAA */
static void z80_27_daa (void)
{
    z80_instruction_daa ();
    z80_cycle += 4;
}


/* JR Z */
static void z80_28_jr_z (void)
{
    uint8_t imm = memory_read (z80_regs.pc++);

    if (z80_regs.f & Z80_FLAG_ZERO)
    {
        z80_regs.pc += (int8_t) imm;
        z80_cycle += 12;
    }
    else
    {
        z80_cycle += 7;
    }
}


/* ADD HL, HL */
static void z80_29_add_hl_hl (void)
{
    SET_FLAGS_ADD_16 (z80_regs.hl, z80_regs.hl);
    z80_regs.hl += z80_regs.hl;
    z80_cycle += 11;
}


/* LD, HL, (**) */
static void z80_2a_ld_hl_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);
    z80_regs.l = memory_read (addr.w);
    z80_regs.h = memory_read (addr.w + 1);
    z80_cycle += 16;
}


/* DEC HL */
static void z80_2b_dec_hl (void)
{
    z80_regs.hl--;
    z80_cycle += 6;
}


/* INC L */
static void z80_2c_inc_l (void)
{
    z80_regs.l++;
    SET_FLAGS_INC (z80_regs.l);
    z80_cycle += 4;
}


/* DEC L */
static void z80_2d_dec_l (void)
{
    z80_regs.l--;
    SET_FLAGS_DEC (z80_regs.l);
    z80_cycle += 4;
}


/* LD L, * */
static void z80_2e_ld_l_x (void)
{
    z80_regs.l = memory_read (z80_regs.pc++);
    z80_cycle += 7;
}


/* CPL */
static void z80_2f_cpl (void)
{
    z80_regs.a = ~z80_regs.a;
    z80_regs.f |= Z80_FLAG_HALF | Z80_FLAG_SUB;
    z80_cycle += 4;
}


/* JR NC */
static void z80_30_jr_nc (void)
{
    uint8_t imm = memory_read (z80_regs.pc++);

    if (z80_regs.f & Z80_FLAG_CARRY)
    {
        z80_cycle += 7;
    }
    else
    {
        z80_regs.pc += (int8_t) imm;
        z80_cycle += 12;
    }
}


/* LD SP, ** */
static void z80_31_ld_sp_xx (void)
{
    z80_regs.sp_l = memory_read (z80_regs.pc++);
    z80_regs.sp_h = memory_read (z80_regs.pc++);
    z80_cycle += 10;
}


/* LD (**), A */
static void z80_32_ld_xx_a (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);
    memory_write (addr.w, z80_regs.a);
    z80_cycle += 13;
}


/* INC SP */
static void z80_33_inc_sp (void)
{
    z80_regs.sp++;
    z80_cycle += 6;
}


/* INC (HL) */
static void z80_34_inc_hl (void)
{
    uint8_t value = memory_read (z80_regs.hl);
    value++;
    memory_write (z80_regs.hl, value);
    SET_FLAGS_INC (value);
    z80_cycle += 11;
}


/* DEC (HL) */
static void z80_35_dec_hl (void)
{
    uint8_t value = memory_read (z80_regs.hl);
    value--;
    memory_write (z80_regs.hl, value);
    SET_FLAGS_DEC (value);
    z80_cycle += 11;
}


/* LD (HL), * */
static void z80_36_ld_hl_x (void)
{
    memory_write (z80_regs.hl, memory_read (z80_regs.pc++));
    z80_cycle += 10;
}


/* SCF */
static void z80_37_scf (void)
{
    z80_regs.f = (z80_regs.f & (Z80_FLAG_SIGN | Z80_FLAG_ZERO | Z80_FLAG_OVERFLOW)) | Z80_FLAG_CARRY;
    z80_cycle += 4;
}


/* JR C, * */
static void z80_38_jr_c_x (void)
{
    uint8_t imm = memory_read (z80_regs.pc++);

    if (z80_regs.f & Z80_FLAG_CARRY)
    {
        z80_regs.pc += (int8_t) imm;
        z80_cycle += 12;
    }
    else
    {
        z80_cycle += 7;
    }
}


/* ADD HL, SP */
static void z80_39_add_hl_sp (void)
{
    SET_FLAGS_ADD_16 (z80_regs.hl, z80_regs.sp);
    z80_regs.hl += z80_regs.sp;
    z80_cycle += 11;
}


/* LD, A, (**) */
static void z80_3a_ld_a_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);
    z80_regs.a = memory_read (addr.w);
    z80_cycle += 13;
}


/* DEC SP */
static void z80_3b_dec_sp (void)
{
    z80_regs.sp--;
    z80_cycle += 6;
}


/* INC A */
static void z80_3c_inc_a (void)
{
    z80_regs.a++;
    SET_FLAGS_INC (z80_regs.a);
    z80_cycle += 4;
}

/* DEC A */
static void z80_3d_dec_a (void)
{
    z80_regs.a--;
    SET_FLAGS_DEC (z80_regs.a);
    z80_cycle += 4;
}


/* LD A, * */
static void z80_3e_ld_a_x (void)
{
    z80_regs.a = memory_read (z80_regs.pc++);
    z80_cycle += 7;
}


/* CCF */
static void z80_3f_ccf (void)
{
    z80_regs.f = (z80_regs.f & (Z80_FLAG_SIGN | Z80_FLAG_ZERO | Z80_FLAG_OVERFLOW)) | (CARRY_BIT ? Z80_FLAG_HALF : Z80_FLAG_CARRY);
    z80_cycle += 4;
}


/* LD B, B */
static void z80_40_ld_b_b (void)
{
    z80_cycle += 4;
}


/* LD B, C */
static void z80_41_ld_b_c (void)
{
    z80_regs.b = z80_regs.c;
    z80_cycle += 4;
}


/* LD B, D */
static void z80_42_ld_b_d (void)
{
    z80_regs.b = z80_regs.d;
    z80_cycle += 4;
}


/* LD B, E */
static void z80_43_ld_b_e (void)
{
    z80_regs.b = z80_regs.e;
    z80_cycle += 4;
}


/* LD B, H */
static void z80_44_ld_b_h (void)
{
    z80_regs.b = z80_regs.h;
    z80_cycle += 4;
}


/* LD B, L */
static void z80_45_ld_b_l (void)
{
    z80_regs.b = z80_regs.l;
    z80_cycle += 4;
}


/* LD B, (HL) */
static void z80_46_ld_b_hl (void)
{
    z80_regs.b = memory_read (z80_regs.hl);
    z80_cycle += 7;
}


/* LD B, A */
static void z80_47_ld_b_a (void)
{
    z80_regs.b = z80_regs.a;
    z80_cycle += 4;
}


/* LD C, B */
static void z80_48_ld_c_b (void)
{
    z80_regs.c = z80_regs.b;
    z80_cycle += 4;
}


/* LD C, C */
static void z80_49_ld_c_c (void)
{
    z80_cycle += 4;
}


/* LD C, D */
static void z80_4a_ld_c_d (void)
{
    z80_regs.c = z80_regs.d;
    z80_cycle += 4;
}


/* LD C, E */
static void z80_4b_ld_c_e (void)
{
    z80_regs.c = z80_regs.e;
    z80_cycle += 4;
}


/* LD C, H */
static void z80_4c_ld_c_h (void)
{
    z80_regs.c = z80_regs.h;
    z80_cycle += 4;
}


/* LD C, L */
static void z80_4d_ld_c_l (void)
{
    z80_regs.c = z80_regs.l;
    z80_cycle += 4;
}


/* LD C, (HL) */
static void z80_4e_ld_c_hl (void)
{
    z80_regs.c = memory_read (z80_regs.hl);
    z80_cycle += 7;
}


/* LD C, A */
static void z80_4f_ld_c_a (void)
{
    z80_regs.c = z80_regs.a;
    z80_cycle += 4;
}


/* LD D, B */
static void z80_50_ld_d_b (void)
{
    z80_regs.d = z80_regs.b;
    z80_cycle += 4;
}


/* LD D, C */
static void z80_51_ld_d_c (void)
{
    z80_regs.d = z80_regs.c;
    z80_cycle += 4;
}


/* LD D, D */
static void z80_52_ld_d_d (void)
{
    z80_cycle += 4;
}


/* LD D, E */
static void z80_53_ld_d_e (void)
{
    z80_regs.d = z80_regs.e;
    z80_cycle += 4;
}


/* LD D, H */
static void z80_54_ld_d_h (void)
{
    z80_regs.d = z80_regs.h;
    z80_cycle += 4;
}


/* LD D, L */
static void z80_55_ld_d_l (void)
{
    z80_regs.d = z80_regs.l;
    z80_cycle += 4;
}


/* LD D, (HL) */
static void z80_56_ld_d_hl (void)
{
    z80_regs.d = memory_read (z80_regs.hl);
    z80_cycle += 7;
}


/* LD D, A */
static void z80_57_ld_d_a (void)
{
    z80_regs.d = z80_regs.a;
    z80_cycle += 4;
}


/* LD E, B */
static void z80_58_ld_e_b (void)
{
    z80_regs.e = z80_regs.b;
    z80_cycle += 4;
}


/* LD E, C */
static void z80_59_ld_e_c (void)
{
    z80_regs.e = z80_regs.c;
    z80_cycle += 4;
}


/* LD E, D */
static void z80_5a_ld_e_d (void)
{
    z80_regs.e = z80_regs.d;
    z80_cycle += 4;
}


/* LD E, E */
static void z80_5b_ld_e_e (void)
{
    z80_cycle += 4;
}


/* LD E, H */
static void z80_5c_ld_e_h (void)
{
    z80_regs.e = z80_regs.h;
    z80_cycle += 4;
}


/* LD E, L */
static void z80_5d_ld_e_l (void)
{
    z80_regs.e = z80_regs.l;
    z80_cycle += 4;
}


/* LD E, (HL) */
static void z80_5e_ld_e_hl (void)
{
    z80_regs.e = memory_read (z80_regs.hl);
    z80_cycle += 7;
}


/* LD E, A */
static void z80_5f_ld_e_a (void)
{
    z80_regs.e = z80_regs.a;
    z80_cycle += 4;
}


/* LD H, B */
static void z80_60_ld_h_b (void)
{
    z80_regs.h = z80_regs.b;
    z80_cycle += 4;
}


/* LD H, C */
static void z80_61_ld_h_c (void)
{
    z80_regs.h = z80_regs.c;
    z80_cycle += 4;
}


/* LD H, D */
static void z80_62_ld_h_d (void)
{
    z80_regs.h = z80_regs.d;
    z80_cycle += 4;
}


/* LD H, E */
static void z80_63_ld_h_e (void)
{
    z80_regs.h = z80_regs.e;
    z80_cycle += 4;
}


/* LD H, H */
static void z80_64_ld_h_h (void)
{
    z80_cycle += 4;
}


/* LD H, L  */
static void z80_65_ld_h_l (void)
{
    z80_regs.h = z80_regs.l;
    z80_cycle += 4;
}


/* LD H, (HL) */
static void z80_66_ld_h_hl (void)
{
    z80_regs.h = memory_read (z80_regs.hl);
    z80_cycle += 7;
}


/* LD H, A */
static void z80_67_ld_h_a (void)
{
    z80_regs.h = z80_regs.a;
    z80_cycle += 4;
}


/* LD L, B */
static void z80_68_ld_l_b (void)
{
    z80_regs.l = z80_regs.b;
    z80_cycle += 4;
}


/* LD L, C */
static void z80_69_ld_l_c (void)
{
    z80_regs.l = z80_regs.c;
    z80_cycle += 4;
}


/* LD L, D */
static void z80_6a_ld_l_d (void)
{
    z80_regs.l = z80_regs.d;
    z80_cycle += 4;
}


/* LD L, E */
static void z80_6b_ld_l_e (void)
{
    z80_regs.l = z80_regs.e;
    z80_cycle += 4;
}


/* LD L, H */
static void z80_6c_ld_l_h (void)
{
    z80_regs.l = z80_regs.h;
    z80_cycle += 4;
}


/* LD L, L */
static void z80_6d_ld_l_l (void)
{
    z80_cycle += 4;
}


/* LD L, (HL) */
static void z80_6e_ld_l_hl (void)
{
    z80_regs.l = memory_read (z80_regs.hl);
    z80_cycle += 7;
}


/* LD L, A */
static void z80_6f_ld_l_a (void)
{
    z80_regs.l = z80_regs.a;
    z80_cycle += 4;
}


/* LD (HL), B */
static void z80_70_ld_hl_b (void)
{
    memory_write (z80_regs.hl, z80_regs.b);
    z80_cycle += 7;
}


/* LD (HL), C */
static void z80_71_ld_hl_c (void)
{
    memory_write (z80_regs.hl, z80_regs.c);
    z80_cycle += 7;
}


/* LD (HL), D */
static void z80_72_ld_hl_d (void)
{
    memory_write (z80_regs.hl, z80_regs.d);
    z80_cycle += 7;
}


/* LD (HL), E */
static void z80_73_ld_hl_e (void)
{
    memory_write (z80_regs.hl, z80_regs.e);
    z80_cycle += 7;
}


/* LD (HL), H */
static void z80_74_ld_hl_h (void)
{
    memory_write (z80_regs.hl, z80_regs.h);
    z80_cycle += 7;
}


/* LD (HL), L */
static void z80_75_ld_hl_l (void)
{
    memory_write (z80_regs.hl, z80_regs.l);
    z80_cycle += 7;
}


/* HALT */
static void z80_76_halt (void)
{
    z80_regs.pc--;
    z80_regs.halt = true;
    z80_cycle += 4;
}


/* LD (HL), A */
static void z80_77_ld_hl_a (void)
{
    memory_write (z80_regs.hl, z80_regs.a);
    z80_cycle += 7;
}


/* LD A, B */
static void z80_78_ld_a_b (void)
{
    z80_regs.a = z80_regs.b;
    z80_cycle += 4;
}


/* LD A, C */
static void z80_79_ld_a_c (void)
{
    z80_regs.a = z80_regs.c;
    z80_cycle += 4;
}


/* LD A, D */
static void z80_7a_ld_a_d (void)
{
    z80_regs.a = z80_regs.d;
    z80_cycle += 4;
}


/* LD A, E */
static void z80_7b_ld_a_e (void)
{
    z80_regs.a = z80_regs.e;
    z80_cycle += 4;
}


/* LD A, H */
static void z80_7c_ld_a_h (void)
{
    z80_regs.a = z80_regs.h;
    z80_cycle += 4;
}


/* LD A, L */
static void z80_7d_ld_a_l (void)
{
    z80_regs.a = z80_regs.l;
    z80_cycle += 4;
}


/* LD A, (HL) */
static void z80_7e_ld_a_hl (void)
{
    z80_regs.a = memory_read (z80_regs.hl);
    z80_cycle += 7;
}


/* LD A, A */
static void z80_7f_ld_a_a (void)
{
    z80_cycle += 4;
}


/* ADD A, B */
static void z80_80_add_a_b (void)
{
    SET_FLAGS_ADD (z80_regs.a, z80_regs.b);
    z80_regs.a += z80_regs.b;
    z80_cycle += 4;
}

/* ADD A, C */
static void z80_81_add_a_c (void)
{
    SET_FLAGS_ADD (z80_regs.a, z80_regs.c);
    z80_regs.a += z80_regs.c;
    z80_cycle += 4;
}


/* ADD A, D */
static void z80_82_add_a_d (void)
{
    SET_FLAGS_ADD (z80_regs.a, z80_regs.d);
    z80_regs.a += z80_regs.d;
    z80_cycle += 4;
}


/* ADD A, E */
static void z80_83_add_a_e (void)
{
    SET_FLAGS_ADD (z80_regs.a, z80_regs.e);
    z80_regs.a += z80_regs.e;
    z80_cycle += 4;
}

/* ADD A, H */
static void z80_84_add_a_h (void)
{
    SET_FLAGS_ADD (z80_regs.a, z80_regs.h);
    z80_regs.a += z80_regs.h;
    z80_cycle += 4;
}


/* ADD A, L */
static void z80_85_add_a_l (void)
{
    SET_FLAGS_ADD (z80_regs.a, z80_regs.l);
    z80_regs.a += z80_regs.l;
    z80_cycle += 4;
}


/* ADD A, (HL) */
static void z80_86_add_a_hl (void)
{
    uint8_t value = memory_read (z80_regs.hl);
    SET_FLAGS_ADD (z80_regs.a, value);
    z80_regs.a += value;
    z80_cycle += 7;
}


/* ADD A, A */
static void z80_87_add_a_a (void)
{
    SET_FLAGS_ADD (z80_regs.a, z80_regs.a);
    z80_regs.a += z80_regs.a;
    z80_cycle += 4;
}


/* ADC A, B */
static void z80_88_adc_a_b (void)
{
    uint8_t temp = z80_regs.b + CARRY_BIT;
    SET_FLAGS_ADC (z80_regs.b);
    z80_regs.a += temp;
    z80_cycle += 4;
}


/* ADC A, C */
static void z80_89_adc_a_c (void)
{
    uint8_t temp = z80_regs.c + CARRY_BIT;
    SET_FLAGS_ADC (z80_regs.c);
    z80_regs.a += temp;
    z80_cycle += 4;
}


/* ADC A, D */
static void z80_8a_adc_a_d (void)
{
    uint8_t temp = z80_regs.d + CARRY_BIT;
    SET_FLAGS_ADC (z80_regs.d);
    z80_regs.a += temp;
    z80_cycle += 4;
}


/* ADC A, E */
static void z80_8b_adc_a_e (void)
{
    uint8_t temp = z80_regs.e + CARRY_BIT;
    SET_FLAGS_ADC (z80_regs.e);
    z80_regs.a += temp;
    z80_cycle += 4;
}


/* ADC A, H */
static void z80_8c_adc_a_h (void)
{
    uint8_t temp = z80_regs.h + CARRY_BIT;
    SET_FLAGS_ADC (z80_regs.h);
    z80_regs.a += temp;
    z80_cycle += 4;
}


/* ADC A, L */
static void z80_8d_adc_a_l (void)
{
    uint8_t temp = z80_regs.l + CARRY_BIT;
    SET_FLAGS_ADC (z80_regs.l);
    z80_regs.a += temp;
    z80_cycle += 4;
}


/* ADC A, (HL) */
static void z80_8e_adc_a_hl (void)
{
    uint8_t value = memory_read (z80_regs.hl);
    uint8_t temp = value + CARRY_BIT;
    SET_FLAGS_ADC (value);
    z80_regs.a += temp;
    z80_cycle += 7;
}


/* ADC A, A */
static void z80_8f_adc_a_a (void)
{
    uint8_t temp = z80_regs.a + CARRY_BIT;
    SET_FLAGS_ADC (z80_regs.a);
    z80_regs.a += temp;
    z80_cycle += 4;
}


/* SUB A, B */
static void z80_90_sub_a_b (void)
{
    SET_FLAGS_SUB (z80_regs.a, z80_regs.b);
    z80_regs.a -= z80_regs.b;
    z80_cycle += 4;
}


/* SUB A, C */
static void z80_91_sub_a_c (void)
{
    SET_FLAGS_SUB (z80_regs.a, z80_regs.c);
    z80_regs.a -= z80_regs.c;
    z80_cycle += 4;
}


/* SUB A, D */
static void z80_92_sub_a_d (void)
{
    SET_FLAGS_SUB (z80_regs.a, z80_regs.d);
    z80_regs.a -= z80_regs.d;
    z80_cycle += 4;
}


/* SUB A, E */
static void z80_93_sub_a_e (void)
{
    SET_FLAGS_SUB (z80_regs.a, z80_regs.e);
    z80_regs.a -= z80_regs.e;
    z80_cycle += 4;
}


/* SUB A, H */
static void z80_94_sub_a_h (void)
{
    SET_FLAGS_SUB (z80_regs.a, z80_regs.h);
    z80_regs.a -= z80_regs.h;
    z80_cycle += 4;
}


/* SUB A, L */
static void z80_95_sub_a_l (void)
{
    SET_FLAGS_SUB (z80_regs.a, z80_regs.l);
    z80_regs.a -= z80_regs.l;
    z80_cycle += 4;
}


/* SUB A, (HL) */
static void z80_96_sub_a_hl (void)
{
    uint8_t temp = memory_read (z80_regs.hl);
    SET_FLAGS_SUB (z80_regs.a, temp);
    z80_regs.a -= temp;
    z80_cycle += 7;
}


/* SUB A, A */
static void z80_97_sub_a_a (void)
{
    SET_FLAGS_SUB (z80_regs.a, z80_regs.a);
    z80_regs.a -= z80_regs.a;
    z80_cycle += 4;
}


/* SBC A, B */
static void z80_98_sbc_a_b (void)
{
    uint8_t temp = z80_regs.b + CARRY_BIT;
    SET_FLAGS_SBC (z80_regs.b);
    z80_regs.a -= temp;
    z80_cycle += 4;
}


/* SBC A, C */
static void z80_99_sbc_a_c (void)
{
    uint8_t temp = z80_regs.c + CARRY_BIT;
    SET_FLAGS_SBC (z80_regs.c);
    z80_regs.a -= temp;
    z80_cycle += 4;
}


/* SBC A, D */
static void z80_9a_sbc_a_d (void)
{
    uint8_t temp = z80_regs.d + CARRY_BIT;
    SET_FLAGS_SBC (z80_regs.d);
    z80_regs.a -= temp;
    z80_cycle += 4;
}


/* SBC A, E */
static void z80_9b_sbc_a_e (void)
{
    uint8_t temp = z80_regs.e + CARRY_BIT;
    SET_FLAGS_SBC (z80_regs.e);
    z80_regs.a -= temp;
    z80_cycle += 4;
}


/* SBC A, H */
static void z80_9c_sbc_a_h (void)
{
    uint8_t temp = z80_regs.h + CARRY_BIT;
    SET_FLAGS_SBC (z80_regs.h);
    z80_regs.a -= temp;
    z80_cycle += 4;
}


/* SBC A, L */
static void z80_9d_sbc_a_l (void)
{
    uint8_t temp = z80_regs.l + CARRY_BIT;
    SET_FLAGS_SBC (z80_regs.l);
    z80_regs.a -= temp;
    z80_cycle += 4;
}


/* SBC A, (HL) */
static void z80_9e_sbc_a_hl (void)
{
    uint8_t value = memory_read (z80_regs.hl);
    uint8_t temp = value + CARRY_BIT;
    SET_FLAGS_SBC (value);
    z80_regs.a -= temp;
    z80_cycle += 7;
}


/* SBC A, A */
static void z80_9f_sbc_a_a (void)
{
    uint8_t temp = z80_regs.a + CARRY_BIT;
    SET_FLAGS_SBC (z80_regs.a);
    z80_regs.a -= temp;
    z80_cycle += 4;
}

/* AND A, B */
static void z80_a0_and_a_b (void)
{
    z80_regs.a &= z80_regs.b;
    SET_FLAGS_AND;
    z80_cycle += 4;
}


/* AND A, C */
static void z80_a1_and_a_c (void)
{
    z80_regs.a &= z80_regs.c;
    SET_FLAGS_AND;
    z80_cycle += 4;
}


/* AND A, D */
static void z80_a2_and_a_d (void)
{
    z80_regs.a &= z80_regs.d;
    SET_FLAGS_AND;
    z80_cycle += 4;
}


/* AND A, E */
static void z80_a3_and_a_e (void)
{
    z80_regs.a &= z80_regs.e;
    SET_FLAGS_AND;
    z80_cycle += 4;
}


/* AND A, H */
static void z80_a4_and_a_h (void)
{
    z80_regs.a &= z80_regs.h;
    SET_FLAGS_AND;
    z80_cycle += 4;
}


/* AND A, L */
static void z80_a5_and_a_l (void)
{
    z80_regs.a &= z80_regs.l;
    SET_FLAGS_AND;
    z80_cycle += 4;
}


/* AND A, (HL) */
static void z80_a6_and_a_hl (void)
{
    z80_regs.a &= memory_read (z80_regs.hl);
    SET_FLAGS_AND;
    z80_cycle += 7;
}


/* AND A, A */
static void z80_a7_and_a_a (void)
{
    SET_FLAGS_AND;
    z80_cycle += 4;
}


/* XOR A, B */
static void z80_a8_xor_a_b (void)
{
    z80_regs.a ^= z80_regs.b;
    SET_FLAGS_OR_XOR;
    z80_cycle += 4;
}


/* XOR A, C */
static void z80_a9_xor_a_c (void)
{
    z80_regs.a ^= z80_regs.c;
    SET_FLAGS_OR_XOR;
    z80_cycle += 4;
}


/* XOR A, D */
static void z80_aa_xor_a_d (void)
{
    z80_regs.a ^= z80_regs.d;
    SET_FLAGS_OR_XOR;
    z80_cycle += 4;
}


/* XOR A, E */
static void z80_ab_xor_a_e (void)
{
    z80_regs.a ^= z80_regs.e;
    SET_FLAGS_OR_XOR;
    z80_cycle += 4;
}


/* XOR A, H */
static void z80_ac_xor_a_h (void)
{
    z80_regs.a ^= z80_regs.h;
    SET_FLAGS_OR_XOR;
    z80_cycle += 4;
}


/* XOR A, L */
static void z80_ad_xor_a_l (void)
{
    z80_regs.a ^= z80_regs.l;
    SET_FLAGS_OR_XOR;
    z80_cycle += 4;
}


/* XOR A, (HL) */
static void z80_ae_xor_a_hl (void)
{
    z80_regs.a ^= memory_read (z80_regs.hl);
    SET_FLAGS_OR_XOR;
    z80_cycle += 7;
}


/* XOR A, A */
static void z80_af_xor_a_a (void)
{
    z80_regs.a ^= z80_regs.a;
    SET_FLAGS_OR_XOR;
    z80_cycle += 4;
}


/* OR A, B */
static void z80_b0_or_a_b (void)
{
    z80_regs.a |= z80_regs.b;
    SET_FLAGS_OR_XOR;
    z80_cycle += 4;
}


/* OR A, C */
static void z80_b1_or_a_c (void)
{
    z80_regs.a |= z80_regs.c;
    SET_FLAGS_OR_XOR;
    z80_cycle += 4;
}


/* OR A, D */
static void z80_b2_or_a_d (void)
{
    z80_regs.a |= z80_regs.d;
    SET_FLAGS_OR_XOR;
    z80_cycle += 4;
}


/* OR A, E */
static void z80_b3_or_a_e (void)
{
    z80_regs.a |= z80_regs.e;
    SET_FLAGS_OR_XOR;
    z80_cycle += 4;
}


/* OR A, H */
static void z80_b4_or_a_h (void)
{
    z80_regs.a |= z80_regs.h;
    SET_FLAGS_OR_XOR;
    z80_cycle += 4;
}


/* OR A, L */
static void z80_b5_or_a_l (void)
{
    z80_regs.a |= z80_regs.l;
    SET_FLAGS_OR_XOR;
    z80_cycle += 4;
}


/* OR A, (HL) */
static void z80_b6_or_a_hl (void)
{
    z80_regs.a |= memory_read (z80_regs.hl);
    SET_FLAGS_OR_XOR;
    z80_cycle += 7;
}


/* OR A, A */
static void z80_b7_or_a_a (void)
{
    SET_FLAGS_OR_XOR;
    z80_cycle += 4;
}


/* CP A, B */
static void z80_b8_cp_a_b (void)
{
    SET_FLAGS_SUB (z80_regs.a, z80_regs.b);
    z80_cycle += 4;
}


/* CP A, C */
static void z80_b9_cp_a_c (void)
{
    SET_FLAGS_SUB (z80_regs.a, z80_regs.c);
    z80_cycle += 4;
}


/* CP A, D */
static void z80_ba_cp_a_d (void)
{
    SET_FLAGS_SUB (z80_regs.a, z80_regs.d);
    z80_cycle += 4;
}


/* CP A, E */
static void z80_bb_cp_a_e (void)
{
    SET_FLAGS_SUB (z80_regs.a, z80_regs.e);
    z80_cycle += 4;
}


/* CP A, H */
static void z80_bc_cp_a_h (void)
{
    SET_FLAGS_SUB (z80_regs.a, z80_regs.h);
    z80_cycle += 4;
}


/* CP A, L */
static void z80_bd_cp_a_l (void)
{
    SET_FLAGS_SUB (z80_regs.a, z80_regs.l);
    z80_cycle += 4;
}

/* CP A, (HL) */
static void z80_be_cp_a_hl (void)
{
    uint8_t value = memory_read (z80_regs.hl);
    SET_FLAGS_SUB (z80_regs.a, value);
    z80_cycle += 7;
}

/* CP A, A */
static void z80_bf_cp_a_a (void)
{
    SET_FLAGS_SUB (z80_regs.a, z80_regs.a);
    z80_cycle += 4;
}


/* RET NZ */
static void z80_c0_ret_nz (void)
{
    if (z80_regs.f & Z80_FLAG_ZERO) {
        z80_cycle += 5;
    }
    else
    {
        z80_regs.pc_l = memory_read (z80_regs.sp++);
        z80_regs.pc_h = memory_read (z80_regs.sp++);
        z80_cycle += 11;
    }
}


/* POP BC */
static void z80_c1_pop_bc (void)
{
    z80_regs.c = memory_read (z80_regs.sp++);
    z80_regs.b = memory_read (z80_regs.sp++);
    z80_cycle += 10;
}


/* JP NZ, ** */
static void z80_c2_jp_nz_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    if (!(z80_regs.f & Z80_FLAG_ZERO))
    {
        z80_regs.pc = addr.w;
    }

    z80_cycle += 10;
}


/* JP ** */
static void z80_c3_jp_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);
    z80_regs.pc = addr.w;
    z80_cycle += 10;
}


/* CALL NZ, ** */
static void z80_c4_call_nz_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    if (z80_regs.f & Z80_FLAG_ZERO)
    {
        z80_cycle += 10;
    }
    else
    {
        memory_write (--z80_regs.sp, z80_regs.pc_h);
        memory_write (--z80_regs.sp, z80_regs.pc_l);
        z80_regs.pc = addr.w;
        z80_cycle += 17;
    }
}


/* PUSH BC */
static void z80_c5_push_bc (void)
{
    memory_write (--z80_regs.sp, z80_regs.b);
    memory_write (--z80_regs.sp, z80_regs.c);
    z80_cycle += 11;
}


/* ADD A, * */
static void z80_c6_add_a_x (void)
{
    uint8_t imm = memory_read (z80_regs.pc++);
    /* ADD A,*    */
    SET_FLAGS_ADD (z80_regs.a, imm);
    z80_regs.a += imm;
    z80_cycle += 7;
}


/* RST 00h */
static void z80_c7_rst_00 (void)
{
    /* RST 00h    */
    memory_write (--z80_regs.sp, z80_regs.pc_h);
    memory_write (--z80_regs.sp, z80_regs.pc_l);
    z80_regs.pc = 0x0000;
    z80_cycle += 11;
}


/* RET Z */
static void z80_c8_ret_z (void)
{
    /* RET Z      */
    if (z80_regs.f & Z80_FLAG_ZERO)
    {
        z80_regs.pc_l = memory_read (z80_regs.sp++);
        z80_regs.pc_h = memory_read (z80_regs.sp++);
        z80_cycle += 11;
    }
    else
    {
        z80_cycle += 5;
    }
}


/* RET */
static void z80_c9_ret (void)
{
    z80_regs.pc_l = memory_read (z80_regs.sp++);
    z80_regs.pc_h = memory_read (z80_regs.sp++);
    z80_cycle += 10;
}


/* JP Z, ** */
static void z80_ca_jp_z_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    if (z80_regs.f & Z80_FLAG_ZERO)
    {
        z80_regs.pc = addr.w;
    }
    z80_cycle += 10;
}


/* BIT PREFIX */
static void z80_cb_prefix (void)
{
    z80_bit_instruction ();
}


/* CALL Z, ** */
static void z80_cc_call_z_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    if (z80_regs.f & Z80_FLAG_ZERO)
    {
        memory_write (--z80_regs.sp, z80_regs.pc_h);
        memory_write (--z80_regs.sp, z80_regs.pc_l);
        z80_regs.pc = addr.w;
        z80_cycle += 17;
    }
    else
    {
        z80_cycle += 10;
    }
}


/* CALL ** */
static void z80_cd_call_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);
    memory_write (--z80_regs.sp, z80_regs.pc_h);
    memory_write (--z80_regs.sp, z80_regs.pc_l);
    z80_regs.pc = addr.w;
    z80_cycle += 17;
}


/* ADC A, * */
static void z80_ce_adc_a_x (void)
{
    uint8_t imm = memory_read (z80_regs.pc++);
    uint8_t temp = imm + CARRY_BIT;
    SET_FLAGS_ADC (imm);
    z80_regs.a += temp;
    z80_cycle += 7;
}


/* RST 08h */
static void z80_cf_rst_08 (void)
{
    memory_write (--z80_regs.sp, z80_regs.pc_h);
    memory_write (--z80_regs.sp, z80_regs.pc_l);
    z80_regs.pc = 0x0008;
    z80_cycle += 11;
}


/* RET NC */
static void z80_d0_ret_nc (void)
{
    if (z80_regs.f & Z80_FLAG_CARRY)
    {
        z80_cycle += 5;
    }
    else
    {
        z80_regs.pc_l = memory_read (z80_regs.sp++);
        z80_regs.pc_h = memory_read (z80_regs.sp++);
        z80_cycle += 11;
    }
}


/* POP DE */
static void z80_d1_pop_de (void)
{
    z80_regs.e = memory_read (z80_regs.sp++);
    z80_regs.d = memory_read (z80_regs.sp++);
    z80_cycle += 10;
}


/* JP NC, ** */
static void z80_d2_jp_nc_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    if (!(z80_regs.f & Z80_FLAG_CARRY))
    {
        z80_regs.pc = addr.w;
    }
    z80_cycle += 10;
}


/* OUT (*), A */
static void z80_d3_out_x_a (void)
{
    io_write (memory_read (z80_regs.pc++), z80_regs.a);
    z80_cycle += 11;
}


/* CALL NC, ** */
static void z80_d4_call_nc_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    /* CALL NC,** */
    if (z80_regs.f & Z80_FLAG_CARRY)
    {
        z80_cycle += 10;
    }
    else
    {
        memory_write (--z80_regs.sp, z80_regs.pc_h);
        memory_write (--z80_regs.sp, z80_regs.pc_l);
        z80_regs.pc = addr.w;
        z80_cycle += 17;
    }
}


/* PUSH DE */
static void z80_d5_push_de (void)
{
    memory_write (--z80_regs.sp, z80_regs.d);
    memory_write (--z80_regs.sp, z80_regs.e);
    z80_cycle += 11;
}


/* SUB A, * */
static void z80_d6_sub_a_x (void)
{
    uint8_t imm = memory_read (z80_regs.pc++);
    SET_FLAGS_SUB (z80_regs.a, imm);
    z80_regs.a -= imm;
    z80_cycle += 7;
}


/* RST 10h */
static void z80_d7_rst_10 (void)
{
    memory_write (--z80_regs.sp, z80_regs.pc_h);
    memory_write (--z80_regs.sp, z80_regs.pc_l);
    z80_regs.pc = 0x10;
    z80_cycle += 11;
}


/* RET C */
static void z80_d8_ret_c (void)
{
    if (z80_regs.f & Z80_FLAG_CARRY)
    {
        z80_regs.pc_l = memory_read (z80_regs.sp++);
        z80_regs.pc_h = memory_read (z80_regs.sp++);
        z80_cycle += 11;
    }
    else
    {
        z80_cycle += 5;
    }
}


/* EXX */
static void z80_d9_exx (void)
{
    SWAP (uint16_t, z80_regs.bc, z80_regs.alt_bc);
    SWAP (uint16_t, z80_regs.de, z80_regs.alt_de);
    SWAP (uint16_t, z80_regs.hl, z80_regs.alt_hl);
    z80_cycle += 4;
}


/* JP C, ** */
static void z80_da_jp_c_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    if (z80_regs.f & Z80_FLAG_CARRY)
    {
        z80_regs.pc = addr.w;
    }
    z80_cycle += 10;
}


/* IN A, (*) */
static void z80_db_in_a_x (void)
{
    z80_regs.a = io_read (memory_read (z80_regs.pc++));
    z80_cycle += 11;
}


/* CALL C, ** */
static void z80_dc_call_c_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    if (z80_regs.f & Z80_FLAG_CARRY)
    {
        memory_write (--z80_regs.sp, z80_regs.pc_h);
        memory_write (--z80_regs.sp, z80_regs.pc_l);
        z80_regs.pc = addr.w;
        z80_cycle += 17;
    }
    else
    {
        z80_cycle += 10;
    }
}


/* IX PREFIX */
static void z80_dd_ix (void)
{
    z80_regs.ix = z80_ix_iy_instruction (z80_regs.ix);
}


/* SBC A, * */
static void z80_de_sbc_a_x (void)
{
    uint8_t imm = memory_read (z80_regs.pc++);
    uint8_t temp = imm + CARRY_BIT;
    SET_FLAGS_SBC (imm);
    z80_regs.a -= temp;
    z80_cycle += 7;
}


/* RST 18h */
static void z80_df_rst_18 (void)
{
    memory_write (--z80_regs.sp, z80_regs.pc_h);
    memory_write (--z80_regs.sp, z80_regs.pc_l);
    z80_regs.pc = 0x0018;
    z80_cycle += 11;
}


/* RET PO */
static void z80_e0_ret_po (void)
{
    if (z80_regs.f & Z80_FLAG_PARITY)
    {
        z80_cycle += 5;
    }
    else
    {
        z80_regs.pc_l = memory_read (z80_regs.sp++);
        z80_regs.pc_h = memory_read (z80_regs.sp++);
        z80_cycle += 11;
    }
}


/* POP HL */
static void z80_e1_pop_hl (void)
{
    z80_regs.l = memory_read (z80_regs.sp++);
    z80_regs.h = memory_read (z80_regs.sp++);
    z80_cycle += 10;
}


/* JP PO, ** */
static void z80_e2_jp_po_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    if (z80_regs.f & Z80_FLAG_PARITY)
    {
        z80_regs.pc = addr.w;
    }
    z80_cycle += 10;
}


/* EX (SP), HL */
static void z80_e3_ex_sp_hl (void)
{
    uint8_t temp = z80_regs.l;
    z80_regs.l = memory_read (z80_regs.sp);
    memory_write (z80_regs.sp, temp);
    temp = z80_regs.h;
    z80_regs.h = memory_read (z80_regs.sp + 1);
    memory_write (z80_regs.sp + 1, temp);
    z80_cycle += 19;
}


/* CALL PO, ** */
static void z80_e4_call_po_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    if (z80_regs.f & Z80_FLAG_PARITY)
    {
        z80_cycle += 10;
    }
    else
    {
        memory_write (--z80_regs.sp, z80_regs.pc_h);
        memory_write (--z80_regs.sp, z80_regs.pc_l);
        z80_regs.pc = addr.w;
        z80_cycle += 17;
    }
}


/* PUSH HL */
static void z80_e5_push_hl (void)
{
    memory_write (--z80_regs.sp, z80_regs.h);
    memory_write (--z80_regs.sp, z80_regs.l);
    z80_cycle += 11;
}


/* AND A, * */
static void z80_e6_and_a_x (void)
{
    z80_regs.a &= memory_read (z80_regs.pc++);
    SET_FLAGS_AND;
    z80_cycle += 7;
}


/* RST 20h */
static void z80_e7_rst_20 (void)
{
    memory_write (--z80_regs.sp, z80_regs.pc_h);
    memory_write (--z80_regs.sp, z80_regs.pc_l);
    z80_regs.pc = 0x0020;
    z80_cycle += 11;
}


/* RET PE */
static void z80_e8_ret_pe (void)
{
    if (z80_regs.f & Z80_FLAG_PARITY)
    {
        z80_regs.pc_l = memory_read (z80_regs.sp++);
        z80_regs.pc_h = memory_read (z80_regs.sp++);
        z80_cycle += 11;
    }
    else
    {
        z80_cycle += 5;
    }
}


/* JP (HL) */
static void z80_e9_jp_hl (void)
{
    z80_regs.pc = z80_regs.hl;
    z80_cycle += 4;
}


/* JP PE, ** */
static void z80_ea_jp_pe_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    if (z80_regs.f & Z80_FLAG_PARITY)
    {
        z80_regs.pc = addr.w;
    }
    z80_cycle += 10;
}


/* EX DE, HL */
static void z80_eb_ex_de_hl (void)
{
    SWAP (uint16_t, z80_regs.de, z80_regs.hl);
    z80_cycle += 4;
}


/* CALL PE, ** */
static void z80_ec_call_pe_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    if (z80_regs.f & Z80_FLAG_PARITY)
    {
        memory_write (--z80_regs.sp, z80_regs.pc_h);
        memory_write (--z80_regs.sp, z80_regs.pc_l);
        z80_regs.pc = addr.w;
        z80_cycle += 17;
    }
    else
    {
        z80_cycle += 10;
    }
}


/* EXTENDED PREFIX */
static void z80_ed_prefix (void)
{
    /* Fetch */
    uint8_t instruction = memory_read (PC++);

    /* Execute */
    z80_ed_instruction [instruction] ();
}


/* XOR A, * */
static void z80_ee_xor_a_x (void)
{
    z80_regs.a ^= memory_read (z80_regs.pc++);
    SET_FLAGS_OR_XOR;
    z80_cycle += 7;
}


/* RST 28h */
static void z80_ef_rst_28 (void)
{
    memory_write (--z80_regs.sp, z80_regs.pc_h);
    memory_write (--z80_regs.sp, z80_regs.pc_l);
    z80_regs.pc = 0x0028;
    z80_cycle += 11;
}


/* RET P */
static void z80_f0_ret_p (void)
{
    if (z80_regs.f & Z80_FLAG_SIGN)
    {
        z80_cycle += 5;
    }
    else
    {
        z80_regs.pc_l = memory_read (z80_regs.sp++);
        z80_regs.pc_h = memory_read (z80_regs.sp++);
        z80_cycle += 11;
    }
}


/* POP AF */
static void z80_f1_pop_af (void)
{
    z80_regs.f = memory_read (z80_regs.sp++);
    z80_regs.a = memory_read (z80_regs.sp++);
    z80_cycle += 10;
}


/* JP P, ** */
static void z80_f2_jp_p_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    if (!(z80_regs.f & Z80_FLAG_SIGN))
    {
        z80_regs.pc = addr.w;
    }
    z80_cycle += 10;
}


/* DI */
static void z80_f3_di (void)
{
    IFF1 = false;
    IFF2 = false;
    z80_cycle += 4;
}


/* CALL P,** */
static void z80_f4_call_p_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    if (z80_regs.f & Z80_FLAG_SIGN)
    {
        z80_cycle += 10;
    }
    else
    {
        memory_write (--z80_regs.sp, z80_regs.pc_h);
        memory_write (--z80_regs.sp, z80_regs.pc_l);
        z80_regs.pc = addr.w;
        z80_cycle += 17;
    }
}


/* PUSH AF */
static void z80_f5_push_af (void)
{
    memory_write (--z80_regs.sp, z80_regs.a);
    memory_write (--z80_regs.sp, z80_regs.f);
    z80_cycle += 11;
}


/* OR A, * */
static void z80_f6_or_a_x (void)
{
    z80_regs.a |= memory_read (z80_regs.pc++);
    SET_FLAGS_OR_XOR;
    z80_cycle += 7;
}


/* RST 30h */
static void z80_f7_rst_30 (void)
{
    memory_write (--z80_regs.sp, z80_regs.pc_h);
    memory_write (--z80_regs.sp, z80_regs.pc_l);
    z80_regs.pc = 0x0030;
    z80_cycle += 11;
}


/* RET M */
static void z80_f8_ret_m (void)
{
    if (z80_regs.f & Z80_FLAG_SIGN)
    {
        z80_regs.pc_l = memory_read (z80_regs.sp++);
        z80_regs.pc_h = memory_read (z80_regs.sp++);
        z80_cycle += 11;
    }
    else
    {
        z80_cycle += 5;
    }
}


/* LD SP, HL */
static void z80_f9_ld_sp_hl (void)
{
    z80_regs.sp = z80_regs.hl;
    z80_cycle += 6;
}


/* JP M, ** */
static void z80_fa_jp_m_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    if (z80_regs.f & Z80_FLAG_SIGN)
    {
        z80_regs.pc = addr.w;
    }
    z80_cycle += 10;
}


/* EI */
static void z80_fb_ei (void)
{
    IFF1 = true;
    IFF2 = true;
    instructions_before_interrupts = 2;
    z80_cycle += 4;
}


/* CALL M, ** */
static void z80_fc_call_m_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_regs.pc++);
    addr.h = memory_read (z80_regs.pc++);

    if (z80_regs.f & Z80_FLAG_SIGN)
    {
        memory_write (--z80_regs.sp, z80_regs.pc_h);
        memory_write (--z80_regs.sp, z80_regs.pc_l);
        z80_regs.pc = addr.w;
        z80_cycle += 17;
    }
    else
    {
        z80_cycle += 10;
    }
}


/* IY PREFIX */
static void z80_fd_prefix (void)
{
    z80_regs.iy = z80_ix_iy_instruction (z80_regs.iy);
}


/* CP A, * */
static void z80_fe_cp_a_x (void)
{
    uint8_t imm = memory_read (z80_regs.pc++);
    SET_FLAGS_SUB (z80_regs.a, imm);
    z80_cycle += 7;
}


/* RST 38h */
static void z80_ff_rst_38 (void)
{
    memory_write (--z80_regs.sp, z80_regs.pc_h);
    memory_write (--z80_regs.sp, z80_regs.pc_l);
    z80_regs.pc = 0x0038;
    z80_cycle += 11;
}


void (*z80_instruction [256]) (void) = {
    z80_00_nop,         z80_01_ld_bc_xx,    z80_02_ld_bc_a,     z80_03_inc_bc,
    z80_04_inc_b,       z80_05_dec_b,       z80_06_ld_b_x,      z80_07_rlca,
    z80_08_ex_af_af,    z80_09_add_hl_bc,   z80_0a_ld_a_bc,     z80_0b_dec_bc,
    z80_0c_inc_c,       z80_0d_dec_c,       z80_0e_ld_c_x,      z80_0f_rrca,
    z80_10_djnz,        z80_11_ld_de_xx,    z80_12_ld_de_a,     z80_13_inc_de,
    z80_14_inc_d,       z80_15_dec_d,       z80_16_ld_d_x,      z80_17_rla,
    z80_18_jr,          z80_19_add_hl_de,   z80_1a_ld_a_de,     z80_1b_dec_de,
    z80_1c_inc_e,       z80_1d_dec_e,       z80_1e_ld_e_x,      z80_1f_rra,
    z80_20_jr_nz,       z80_21_ld_hl_xx,    z80_22_ld_xx_hl,    z80_23_inc_hl,
    z80_24_inc_h,       z80_25_dec_h,       z80_26_ld_h_x,      z80_27_daa,
    z80_28_jr_z,        z80_29_add_hl_hl,   z80_2a_ld_hl_xx,    z80_2b_dec_hl,
    z80_2c_inc_l,       z80_2d_dec_l,       z80_2e_ld_l_x,      z80_2f_cpl,
    z80_30_jr_nc,       z80_31_ld_sp_xx,    z80_32_ld_xx_a,     z80_33_inc_sp,
    z80_34_inc_hl,      z80_35_dec_hl,      z80_36_ld_hl_x,     z80_37_scf,
    z80_38_jr_c_x,      z80_39_add_hl_sp,   z80_3a_ld_a_xx,     z80_3b_dec_sp,
    z80_3c_inc_a,       z80_3d_dec_a,       z80_3e_ld_a_x,      z80_3f_ccf,
    z80_40_ld_b_b,      z80_41_ld_b_c,      z80_42_ld_b_d,      z80_43_ld_b_e,
    z80_44_ld_b_h,      z80_45_ld_b_l,      z80_46_ld_b_hl,     z80_47_ld_b_a,
    z80_48_ld_c_b,      z80_49_ld_c_c,      z80_4a_ld_c_d,      z80_4b_ld_c_e,
    z80_4c_ld_c_h,      z80_4d_ld_c_l,      z80_4e_ld_c_hl,     z80_4f_ld_c_a,
    z80_50_ld_d_b,      z80_51_ld_d_c,      z80_52_ld_d_d,      z80_53_ld_d_e,
    z80_54_ld_d_h,      z80_55_ld_d_l,      z80_56_ld_d_hl,     z80_57_ld_d_a,
    z80_58_ld_e_b,      z80_59_ld_e_c,      z80_5a_ld_e_d,      z80_5b_ld_e_e,
    z80_5c_ld_e_h,      z80_5d_ld_e_l,      z80_5e_ld_e_hl,     z80_5f_ld_e_a,
    z80_60_ld_h_b,      z80_61_ld_h_c,      z80_62_ld_h_d,      z80_63_ld_h_e,
    z80_64_ld_h_h,      z80_65_ld_h_l,      z80_66_ld_h_hl,     z80_67_ld_h_a,
    z80_68_ld_l_b,      z80_69_ld_l_c,      z80_6a_ld_l_d,      z80_6b_ld_l_e,
    z80_6c_ld_l_h,      z80_6d_ld_l_l,      z80_6e_ld_l_hl,     z80_6f_ld_l_a,
    z80_70_ld_hl_b,     z80_71_ld_hl_c,     z80_72_ld_hl_d,     z80_73_ld_hl_e,
    z80_74_ld_hl_h,     z80_75_ld_hl_l,     z80_76_halt,        z80_77_ld_hl_a,
    z80_78_ld_a_b,      z80_79_ld_a_c,      z80_7a_ld_a_d,      z80_7b_ld_a_e,
    z80_7c_ld_a_h,      z80_7d_ld_a_l,      z80_7e_ld_a_hl,     z80_7f_ld_a_a,
    z80_80_add_a_b,     z80_81_add_a_c,     z80_82_add_a_d,     z80_83_add_a_e,
    z80_84_add_a_h,     z80_85_add_a_l,     z80_86_add_a_hl,    z80_87_add_a_a,
    z80_88_adc_a_b,     z80_89_adc_a_c,     z80_8a_adc_a_d,     z80_8b_adc_a_e,
    z80_8c_adc_a_h,     z80_8d_adc_a_l,     z80_8e_adc_a_hl,    z80_8f_adc_a_a,
    z80_90_sub_a_b,     z80_91_sub_a_c,     z80_92_sub_a_d,     z80_93_sub_a_e,
    z80_94_sub_a_h,     z80_95_sub_a_l,     z80_96_sub_a_hl,    z80_97_sub_a_a,
    z80_98_sbc_a_b,     z80_99_sbc_a_c,     z80_9a_sbc_a_d,     z80_9b_sbc_a_e,
    z80_9c_sbc_a_h,     z80_9d_sbc_a_l,     z80_9e_sbc_a_hl,    z80_9f_sbc_a_a,
    z80_a0_and_a_b,     z80_a1_and_a_c,     z80_a2_and_a_d,     z80_a3_and_a_e,
    z80_a4_and_a_h,     z80_a5_and_a_l,     z80_a6_and_a_hl,    z80_a7_and_a_a,
    z80_a8_xor_a_b,     z80_a9_xor_a_c,     z80_aa_xor_a_d,     z80_ab_xor_a_e,
    z80_ac_xor_a_h,     z80_ad_xor_a_l,     z80_ae_xor_a_hl,    z80_af_xor_a_a,
    z80_b0_or_a_b,      z80_b1_or_a_c,      z80_b2_or_a_d,      z80_b3_or_a_e,
    z80_b4_or_a_h,      z80_b5_or_a_l,      z80_b6_or_a_hl,     z80_b7_or_a_a,
    z80_b8_cp_a_b,      z80_b9_cp_a_c,      z80_ba_cp_a_d,      z80_bb_cp_a_e,
    z80_bc_cp_a_h,      z80_bd_cp_a_l,      z80_be_cp_a_hl,     z80_bf_cp_a_a,
    z80_c0_ret_nz,      z80_c1_pop_bc,      z80_c2_jp_nz_xx,    z80_c3_jp_xx,
    z80_c4_call_nz_xx,  z80_c5_push_bc,     z80_c6_add_a_x,     z80_c7_rst_00,
    z80_c8_ret_z,       z80_c9_ret,         z80_ca_jp_z_xx,     z80_cb_prefix,
    z80_cc_call_z_xx,   z80_cd_call_xx,     z80_ce_adc_a_x,     z80_cf_rst_08,
    z80_d0_ret_nc,      z80_d1_pop_de,      z80_d2_jp_nc_xx,    z80_d3_out_x_a,
    z80_d4_call_nc_xx,  z80_d5_push_de,     z80_d6_sub_a_x,     z80_d7_rst_10,
    z80_d8_ret_c,       z80_d9_exx,         z80_da_jp_c_xx,     z80_db_in_a_x,
    z80_dc_call_c_xx,   z80_dd_ix,          z80_de_sbc_a_x,     z80_df_rst_18,
    z80_e0_ret_po,      z80_e1_pop_hl,      z80_e2_jp_po_xx,    z80_e3_ex_sp_hl,
    z80_e4_call_po_xx,  z80_e5_push_hl,     z80_e6_and_a_x,     z80_e7_rst_20,
    z80_e8_ret_pe,      z80_e9_jp_hl,       z80_ea_jp_pe_xx,    z80_eb_ex_de_hl,
    z80_ec_call_pe_xx,  z80_ed_prefix,      z80_ee_xor_a_x,     z80_ef_rst_28,
    z80_f0_ret_p,       z80_f1_pop_af,      z80_f2_jp_p_xx,     z80_f3_di,
    z80_f4_call_p_xx,   z80_f5_push_af,     z80_f6_or_a_x,      z80_f7_rst_30,
    z80_f8_ret_m,       z80_f9_ld_sp_hl,    z80_fa_jp_m_xx,     z80_fb_ei,
    z80_fc_call_m_xx,   z80_fd_prefix,      z80_fe_cp_a_x,      z80_ff_rst_38
};

/*
 * Execute a single Z80 instruction.
 */
void z80_run_instruction ()
{
    uint8_t instruction;

    /* TODO: This register should be incremented in more places than just here */
    R = (R & 0x80) |((R + 1) & 0x7f);

    /* Fetch */
    instruction = memory_read (PC++);

    /* Execute */
    z80_instruction [instruction] ();
}


/*
 * Simulate the Z80 for the specified number of clock cycles.
 */
void z80_run_cycles (uint64_t cycles)
{
    static uint64_t excess_cycles = 0;
    uint64_t run_until = z80_cycle + cycles - excess_cycles;

    while (z80_cycle < run_until)
    {
        /* TIMING DEBUG */
        uint64_t previous_cycle_count = z80_cycle;
        uint8_t debug_instruction [4];
        debug_instruction [0] = memory_read (PC + 0);
        debug_instruction [1] = memory_read (PC + 1);
        debug_instruction [2] = memory_read (PC + 2);
        debug_instruction [3] = memory_read (PC + 3);
        if (z80_regs.halt)
        {
            /* NOP */ CYCLES (4);
        }
        else
        {
            z80_run_instruction ();
        }

        if (z80_cycle == previous_cycle_count)
        {
            snprintf (state.error_buffer, 79, "Instruction took no time: %x %x %x %x.",
                      debug_instruction [0], debug_instruction [1], debug_instruction [2], debug_instruction [3]);
            snepulator_error ("Z80 Error", state.error_buffer);
            return;
        }
        /* END TIMING DEBUG */

        /* Check for interrupts */
        if (instructions_before_interrupts)
            instructions_before_interrupts--;

        if (!instructions_before_interrupts)
        {
            /* First, check for a non-maskable interrupt (edge-triggerd) */
            static bool nmi_previous = 0;
            bool nmi = state.get_nmi ();
            if (nmi && nmi_previous == 0)
            {
                IFF1 = false;
                memory_write (--z80_regs.sp, z80_regs.pc_h);
                memory_write (--z80_regs.sp, z80_regs.pc_l);
                PC = 0x66;
                CYCLES (11);
            }
            nmi_previous = nmi;

            /* Then check for maskable interrupts */
            if (IFF1 && state.get_int ())
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
                        memory_write (--z80_regs.sp, z80_regs.pc_h);
                        memory_write (--z80_regs.sp, z80_regs.pc_l);
                        PC = 0x38;
                        CYCLES (11);
                        break;
                    default:
                        snprintf (state.error_buffer, 79, "Unsupported interrupt mode %d.", z80_regs.im);
                        snepulator_error ("Z80 Error", state.error_buffer);
                        return;
                }
            }

        }

    }

    excess_cycles = z80_cycle - run_until;
    z80_cycle = 0; /* Prevent overflow */
}
