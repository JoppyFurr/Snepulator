#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <arpa/inet.h>

#include "../util.h"
#include "../snepulator.h"
#include "../save_state.h"
#include "z80.h"
#include "z80_names.h"

extern Snepulator_State state;

#define SWAP(TYPE, X, Y) { TYPE tmp = X; X = Y; Y = tmp; }

/* State */
Z80_State z80_state;

/* Exposed cycle counter since power-on */
uint64_t z80_cycle = 0;

/* Cycles used by the current instruction */
static uint64_t used_cycles = 0;

/* Function pointers for accessing the rest of the system */
uint8_t (* memory_read) (uint16_t) = NULL;
void    (* memory_write)(uint16_t, uint8_t) = NULL;
uint8_t (* io_read)     (uint8_t) = NULL;
void    (* io_write)    (uint8_t, uint8_t) = NULL;

/* TODO: Consider the accuracy of the R register */

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
    memset (&z80_state, 0, sizeof (z80_state));
    z80_state.af = 0xffff;
    z80_state.sp = 0xffff;
    z80_state.excess_cycles = 0;
    z80_cycle = 0;
    used_cycles = 0;
}


#define SET_FLAGS_AND { z80_state.flag_carry = 0; \
                        z80_state.flag_sub = 0; \
                        z80_state.flag_parity_overflow = uint8_even_parity [z80_state.a]; \
                        z80_state.flag_half = 1; \
                        z80_state.flag_zero = (z80_state.a == 0x00); \
                        z80_state.flag_sign = (z80_state.a >> 7); }

#define SET_FLAGS_OR_XOR { z80_state.flag_carry = 0; \
                           z80_state.flag_sub = 0; \
                           z80_state.flag_parity_overflow = uint8_even_parity [z80_state.a]; \
                           z80_state.flag_half = 0; \
                           z80_state.flag_zero = (z80_state.a == 0x00); \
                           z80_state.flag_sign = (z80_state.a >> 7); }

#define SET_FLAGS_ADD(X,Y) { z80_state.flag_carry = (X + Y) >> 8; \
                             z80_state.flag_sub = 0; \
                             z80_state.flag_parity_overflow = (((int8_t) X + (int8_t) Y) > 127 || ((int8_t) X + (int8_t) Y) < -128); \
                             z80_state.flag_half = ((X & 0x0f) + (Y & 0x0f)) >> 4; \
                             z80_state.flag_zero = (((X + Y) & 0xff) == 0x00); \
                             z80_state.flag_sign = (X + Y) >> 7; }

#define SET_FLAGS_SUB(X,Y) { z80_state.flag_carry = (X - Y) >> 8; \
                             z80_state.flag_sub = 1; \
                             z80_state.flag_parity_overflow = (((int8_t) X - (int8_t) Y) > 127 || ((int8_t) X - (int8_t) Y) < -128); \
                             z80_state.flag_half = ((X & 0x0f) - (Y & 0x0f)) >> 4; \
                             z80_state.flag_zero = (X == Y); \
                             z80_state.flag_sign = (X - Y) >> 7; }

#define SET_FLAGS_ADC(X) { z80_state.flag_sub = 0; \
                           z80_state.flag_parity_overflow = (((int8_t) z80_state.a + (int8_t) X + z80_state.flag_carry) > 127 || \
                                                             ((int8_t) z80_state.a + (int8_t) X + z80_state.flag_carry) < -128); \
                           z80_state.flag_half = ((z80_state.a & 0x0f) + (X & 0x0f) + z80_state.flag_carry) >> 4; \
                           z80_state.flag_zero = (((z80_state.a + X + z80_state.flag_carry) & 0xff) == 0x00); \
                           z80_state.flag_sign = (z80_state.a + X + z80_state.flag_carry) >> 7; \
                           z80_state.flag_carry = (z80_state.a + X + z80_state.flag_carry) >> 8; }

#define SET_FLAGS_SBC(X) { z80_state.flag_sub = 1; \
                           z80_state.flag_parity_overflow = (((int8_t) z80_state.a - (int8_t) X - z80_state.flag_carry) > 127 || \
                                                             ((int8_t) z80_state.a - (int8_t) X - z80_state.flag_carry) < -128); \
                           z80_state.flag_half = ((z80_state.a & 0x0f) - (X & 0x0f) - z80_state.flag_carry) >> 4; \
                           z80_state.flag_zero = (((z80_state.a - X - z80_state.flag_carry) & 0xff) == 0x00); \
                           z80_state.flag_sign = (z80_state.a - X - z80_state.flag_carry) >> 7; \
                           z80_state.flag_carry = (z80_state.a - X - z80_state.flag_carry) >> 8; }

#define SET_FLAGS_INC(X) { z80_state.flag_sub = 0; \
                           z80_state.flag_parity_overflow = (X == 0x80); \
                           z80_state.flag_half = ((X & 0x0f) == 0x00); \
                           z80_state.flag_zero = (X == 0x00); \
                           z80_state.flag_sign = X >> 7; }

#define SET_FLAGS_DEC(X) { z80_state.flag_sub = 1; \
                           z80_state.flag_parity_overflow = (X == 0x7f); \
                           z80_state.flag_half = ((X & 0x0f) == 0x0f); \
                           z80_state.flag_zero = (X == 0x00); \
                           z80_state.flag_sign = X >> 7; }

#define SET_FLAGS_ADD_16(X,Y) { z80_state.flag_carry = (X + Y) >> 16; \
                                z80_state.flag_sub = 0; \
                                z80_state.flag_half = ((X & 0x0fff) + (Y & 0xfff)) >> 12; }

#define SET_FLAGS_ADC_16(X) { z80_state.flag_sub = 0; \
                              z80_state.flag_parity_overflow = (((int16_t) z80_state.hl + (int16_t) X + z80_state.flag_carry) >  32767 || \
                                                                ((int16_t) z80_state.hl + (int16_t) X + z80_state.flag_carry) < -32768); \
                              z80_state.flag_half = ((z80_state.hl & 0xfff) + (X & 0xfff) + z80_state.flag_carry) >> 12; \
                              z80_state.flag_zero = (((z80_state.hl + X + z80_state.flag_carry) & 0xffff) == 0x0000); \
                              z80_state.flag_sign = (z80_state.hl + X + z80_state.flag_carry) >> 15; \
                              z80_state.flag_carry = (z80_state.hl + X + z80_state.flag_carry) >> 16; }

#define SET_FLAGS_SBC_16(X) { z80_state.flag_sub = 1; \
                              z80_state.flag_parity_overflow = (((int16_t) z80_state.hl - (int16_t) X - z80_state.flag_carry) >  32767 || \
                                                                ((int16_t) z80_state.hl - (int16_t) X - z80_state.flag_carry) < -32768); \
                              z80_state.flag_half = ((z80_state.hl & 0xfff) - (X & 0xfff) - z80_state.flag_carry) >> 12; \
                              z80_state.flag_zero = (((z80_state.hl - X - z80_state.flag_carry) & 0xffff) == 0x0000); \
                              z80_state.flag_sign = (z80_state.hl - X - z80_state.flag_carry) >> 15; \
                              z80_state.flag_carry = (z80_state.hl - X - z80_state.flag_carry) >> 16; }

#define SET_FLAGS_CPI_CPD(X) { z80_state.flag_sub = 1; \
                               z80_state.flag_parity_overflow = (z80_state.bc != 0); \
                               z80_state.flag_half = ((z80_state.a & 0x0f) - (X & 0x0f)) >> 4; \
                               z80_state.flag_zero = (z80_state.a == X); \
                               z80_state.flag_sign = (z80_state.a - X) >> 7; }

#define SET_FLAGS_RLC(X) { z80_state.flag_carry = X; \
                           z80_state.flag_sub = 0; \
                           z80_state.flag_parity_overflow = uint8_even_parity [X]; \
                           z80_state.flag_half = 0; \
                           z80_state.flag_zero = (X == 0x00); \
                           z80_state.flag_sign = X >> 7; }

#define SET_FLAGS_RRC(X) { z80_state.flag_carry = X >> 7; \
                           z80_state.flag_sub = 0; \
                           z80_state.flag_parity_overflow = uint8_even_parity [X]; \
                           z80_state.flag_half = 0; \
                           z80_state.flag_zero = (X == 0x00); \
                           z80_state.flag_sign = X >> 7; }

#define SET_FLAGS_RL_RR(X) { z80_state.flag_sub = 0; \
                             z80_state.flag_parity_overflow = uint8_even_parity [X]; \
                             z80_state.flag_half = 0; \
                             z80_state.flag_zero = (X == 0x00); \
                             z80_state.flag_sign = X >> 7; }

#define SET_FLAGS_RLD_RRD { z80_state.flag_sub = 0; \
                            z80_state.flag_parity_overflow = uint8_even_parity [z80_state.a]; \
                            z80_state.flag_half = 0; \
                            z80_state.flag_zero = (z80_state.a == 0x00); \
                            z80_state.flag_sign = z80_state.a >> 7; }

#define SET_FLAGS_ED_IN(X) { z80_state.flag_sub = 0; \
                             z80_state.flag_parity_overflow = uint8_even_parity [X]; \
                             z80_state.flag_half = 0; \
                             z80_state.flag_zero = (X == 0); \
                             z80_state.flag_sign = X >> 7; }



/*
 * Read and execute an IX / IY bit instruction.
 * Called after reading the prefix.
 */
void z80_ix_iy_bit_instruction (uint16_t reg_ix_iy_w)
{
    /* Note: The displacement comes first, then the instruction */
    uint8_t displacement = memory_read (z80_state.pc++);
    uint8_t instruction = memory_read (z80_state.pc++);
    uint8_t data;
    uint8_t bit;
    bool write_data = true;
    uint8_t temp;


    /* All IX/IY bit instructions take one parameter */

    /* Read data */
    data = memory_read (reg_ix_iy_w + (int8_t) displacement);

    switch (instruction & 0xf8)
    {
        case 0x00: /* RLC (ix+*) */
            data = (data << 1) | ((data & 0x80) ? 0x01 : 0x00);
            SET_FLAGS_RLC (data);
            used_cycles += 23;
            break;

        case 0x08: /* RRC (ix+*) */
            data = (data >> 1) | (data << 7);
            SET_FLAGS_RRC (data);
            used_cycles += 23;
            break;

        case 0x10: /* RL  (ix+*) */
            temp = data;
            data = (data << 1) | z80_state.flag_carry;
            SET_FLAGS_RL_RR (data);
            z80_state.flag_carry = temp >> 7;
            used_cycles += 23;
            break;

        case 0x18: /* RR  (ix+*) */
            temp = data;
            data = (data >> 1) | (z80_state.flag_carry << 7);
            SET_FLAGS_RL_RR (data);
            z80_state.flag_carry = temp;
            used_cycles += 23;
            break;

        case 0x20: /* SLA (ix+*) */
            temp = data;
            data = (data << 1); SET_FLAGS_RL_RR (data);
            z80_state.flag_carry = temp >> 7;
            used_cycles += 23;
            break;

        case 0x28: /* SRA (ix+*) */
            temp = data;
            data = (data >> 1) | (data & 0x80); SET_FLAGS_RL_RR (data);
            z80_state.flag_carry = temp;
            used_cycles += 23;
            break;

        case 0x30: /* SLL (ix+*) */
            temp = data;
            data = (data << 1) | 0x01; SET_FLAGS_RL_RR (data);
            z80_state.flag_carry = temp >> 7;
            used_cycles += 23;
            break;

        case 0x38: /* SRL (ix+*) */
            temp = data;
            data = (data >> 1); SET_FLAGS_RL_RR (data);
            z80_state.flag_carry = temp;
            used_cycles += 23;
            break;

        /* BIT */
        case 0x40: case 0x48: case 0x50: case 0x58:
        case 0x60: case 0x68: case 0x70: case 0x78:
            bit = (instruction >> 3) & 0x07;
            z80_state.flag_sub = 0;
            z80_state.flag_parity_overflow = ~data >> bit;
            z80_state.flag_half = 1;
            z80_state.flag_zero = ~data >> bit;
            z80_state.flag_sign = (data & (1 << bit)) >> 7;
            used_cycles += 20;
            write_data = false;
            break;

        /* RES */
        case 0x80: case 0x88: case 0x90: case 0x98:
        case 0xa0: case 0xa8: case 0xb0: case 0xb8:
            bit = (instruction >> 3) & 0x07;
            data &= ~(1 << bit);
            used_cycles += 23;
            break;

        /* SET */
        case 0xc0: case 0xc8: case 0xd0: case 0xd8:
        case 0xe0: case 0xe8: case 0xf0: case 0xf8:
            bit = (instruction >> 3) & 0x07;
            data |= (1 << bit);
            used_cycles += 23;
            break;

        default:
            /* Unreachable */
            break;
    }

    /* Write data */
    if (write_data)
    {
        memory_write (reg_ix_iy_w + (int8_t) displacement, data);

        switch (instruction & 0x07)
        {
            case 0x00: z80_state.b = data; break;
            case 0x01: z80_state.c = data; break;
            case 0x02: z80_state.d = data; break;
            case 0x03: z80_state.e = data; break;
            case 0x04: z80_state.h = data; break;
            case 0x05: z80_state.l = data; break;
            case 0x07: z80_state.a = data; break;
            default: break;
        }
    }
}


/****************************/
/**  IX / IY Instructions  **/
/****************************/


extern void (*z80_instruction [256]) (void);
static uint16_t z80_ix_iy_fall_through (uint16_t ix)
{
    uint8_t instruction = memory_read (z80_state.pc - 1);
    z80_instruction [instruction] ();
    used_cycles += 4;
    return ix;
}


/* ADD IX, BC */
static uint16_t z80_ix_iy_09_add_ix_bc (uint16_t ix)
{
    SET_FLAGS_ADD_16 (ix, z80_state.bc);
    ix += z80_state.bc;
    used_cycles += 15;
    return ix;
}


/* ADD IX, DE */
static uint16_t z80_ix_iy_19_add_ix_de (uint16_t ix)
{
    SET_FLAGS_ADD_16 (ix, z80_state.de);
    ix += z80_state.de;
    used_cycles += 15;
    return ix;
}


/* LD IX, ** */
static uint16_t z80_ix_iy_21_ld_ix_xx (uint16_t ix)
{
    uint16_t_Split data;
    data.l = memory_read (z80_state.pc++);
    data.h = memory_read (z80_state.pc++);
    used_cycles += 14;
    return data.w;
}


/* LD (**), IX */
static uint16_t z80_ix_iy_22_ld_xx_ix (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);
    memory_write (addr.w,     _ix.l);
    memory_write (addr.w + 1, _ix.h);
    used_cycles += 20;
    return ix;
}


/* INC IX */
static uint16_t z80_ix_iy_23_inc_ix (uint16_t ix)
{
    ix++;
    used_cycles += 10;
    return ix;
}


/* INC IXH (undocumented) */
static uint16_t z80_ix_iy_24_inc_ixh (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.h++;
    SET_FLAGS_INC (_ix.h);
    used_cycles += 8;
    return _ix.w;
}


/* DEC IXH (undocumented) */
static uint16_t z80_ix_iy_25_dec_ixh (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.h--;
    SET_FLAGS_DEC (_ix.h);
    used_cycles += 8;
    return _ix.w;
}


/* LD IXH, * (undocumented) */
static uint16_t z80_ix_iy_26_ld_ixh_x (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.h = memory_read (z80_state.pc++);
    used_cycles += 11;
    return _ix.w;
}


/* ADD IX, IX */
static uint16_t z80_ix_iy_29_add_ix_ix (uint16_t ix)
{
    SET_FLAGS_ADD_16 (ix, ix);
    ix += ix;
    used_cycles += 15;
    return ix;
}


/* LD IX, (**) */
static uint16_t z80_ix_iy_2a_ld_ix_xx (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);
    _ix.l = memory_read (addr.w);
    _ix.h = memory_read (addr.w + 1);
    used_cycles += 20;
    return _ix.w;
}


/* DEC IX */
static uint16_t z80_ix_iy_2b_dec_ix (uint16_t ix)
{
    ix--;
    used_cycles += 10;
    return ix;
}


/* INC IXL (undocumented) */
static uint16_t z80_ix_iy_2c_inc_ixl (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.l++;
    SET_FLAGS_INC (_ix.l);
    used_cycles += 8;
    return _ix.w;
}


/* DEC IXL (undocumented) */
static uint16_t z80_ix_iy_2d_dec_ixl (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.l--;
    SET_FLAGS_DEC (_ix.l);
    used_cycles += 8;
    return _ix.w;
}


/* LD IXL, * (undocumented) */
static uint16_t z80_ix_iy_2e_ld_ixl_x (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.l = memory_read (z80_state.pc++);
    used_cycles += 11;
    return _ix.w;
}


/* INC (IX + *) */
static uint16_t z80_ix_iy_34_inc_ixx (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    uint8_t data = memory_read (ix + offset);
    data++;
    SET_FLAGS_INC (data);
    memory_write (ix + offset, data);
    used_cycles += 23;
    return ix;
}


/* DEC (IX + *) */
static uint16_t z80_ix_iy_35_dec_ixx (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    uint8_t data = memory_read (ix + offset);
    data--;
    SET_FLAGS_DEC (data);
    memory_write (ix + offset, data);
    used_cycles += 23;
    return ix;
}


/* LD (IX + *), * */
static uint16_t z80_ix_iy_36_ld_ixx_x (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    uint8_t data = memory_read (z80_state.pc++);
    memory_write (ix + offset, data);
    used_cycles += 19;
    return ix;
}


/* ADD IX, SP */
static uint16_t z80_ix_iy_39_add_ix_sp (uint16_t ix)
{
    SET_FLAGS_ADD_16 (ix, z80_state.sp);
    ix += z80_state.sp;
    used_cycles = 15;
    return ix;
}


/* LD B, IXH (undocumented) */
static uint16_t z80_ix_iy_44_ld_b_ixh (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    z80_state.b = _ix.h;
    used_cycles += 8;
    return ix;
}


/* LD B, IXL (undocumented) */
static uint16_t z80_ix_iy_45_ld_b_ixl (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    z80_state.b = _ix.l;
    used_cycles += 8;
    return ix;
}


/* LD B, (IX + *) */
static uint16_t z80_ix_iy_46_ld_b_ixx (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    z80_state.b = memory_read (ix + offset);
    used_cycles += 19;
    return ix;
}


/* LD C, IXH (undocumented) */
static uint16_t z80_ix_iy_4c_ld_c_ixh (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    z80_state.c = _ix.h;
    used_cycles += 8;
    return ix;
}


/* LD C, IXL (undocumented) */
static uint16_t z80_ix_iy_4d_ld_c_ixl (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    z80_state.c = _ix.l;
    used_cycles += 8;
    return ix;
}


/* LD C, (IX + *) */
static uint16_t z80_ix_iy_4e_ld_c_ixx (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    z80_state.c = memory_read (ix + offset);
    used_cycles += 19;
    return ix;
}


/* LD D, IXH (undocumented) */
static uint16_t z80_ix_iy_54_ld_d_ixh (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    z80_state.d = _ix.h;
    used_cycles += 8;
    return ix;
}


/* LD D, IXL (undocumented) */
static uint16_t z80_ix_iy_55_ld_d_ixl (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    z80_state.d = _ix.l;
    used_cycles += 8;
    return ix;
}


/* LD D, (IX + *) */
static uint16_t z80_ix_iy_56_ld_d_ixx (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    z80_state.d = memory_read (ix + offset);
    used_cycles += 19;
    return ix;
}


/* LD E, IXH (undocumented) */
static uint16_t z80_ix_iy_5c_ld_e_ixh (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    z80_state.e = _ix.h;
    used_cycles += 8;
    return ix;
}


/* LD E, IXL (undocumented) */
static uint16_t z80_ix_iy_5d_ld_e_ixl (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    z80_state.e = _ix.l;
    used_cycles += 8;
    return ix;
}


/* LD E, (IX + *) */
static uint16_t z80_ix_iy_5e_ld_e_ixx (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    z80_state.e = memory_read (ix + offset);
    used_cycles += 19;
    return ix;
}


/* LD IXH, B (undocumented) */
static uint16_t z80_ix_iy_60_ld_ixh_b (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.h = z80_state.b;
    used_cycles += 8;
    return _ix.w;
}


/* LD IXH, C (undocumented) */
static uint16_t z80_ix_iy_61_ld_ixh_c (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.h = z80_state.c;
    used_cycles += 8;
    return _ix.w;
}


/* LD IXH, D (undocumented) */
static uint16_t z80_ix_iy_62_ld_ixh_d (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.h = z80_state.d;
    used_cycles += 8;
    return _ix.w;
}


/* LD IXH, E (undocumented) */
static uint16_t z80_ix_iy_63_ld_ixh_e (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.h = z80_state.e;
    used_cycles += 8;
    return _ix.w;
}


/* LD IXH, IXH (undocumented) */
static uint16_t z80_ix_iy_64_ld_ixh_ixh (uint16_t ix)
{
    used_cycles += 8;
    return ix;
}


/* LD IXH, IXL (undocumented) */
static uint16_t z80_ix_iy_65_ld_ixh_ixl (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.h = _ix.l;
    used_cycles += 8;
    return _ix.w;
}


/* LD H, (IX + *) */
static uint16_t z80_ix_iy_66_ld_h_ixx (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    z80_state.h = memory_read (ix + offset);
    used_cycles += 19;
    return ix;
}


/* LD IXH, A (undocumented) */
static uint16_t z80_ix_iy_67_ld_ixh_a (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.h = z80_state.a;
    used_cycles += 8;
    return _ix.w;
}


/* LD IXL, B (undocumented) */
static uint16_t z80_ix_iy_68_ld_ixl_b (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.l = z80_state.b;
    used_cycles += 8;
    return _ix.w;
}


/* LD IXL, C (undocumented) */
static uint16_t z80_ix_iy_69_ld_ixl_c (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.l = z80_state.c;
    used_cycles += 8;
    return _ix.w;
}


/* LD IXL, D (undocumented) */
static uint16_t z80_ix_iy_6a_ld_ixl_d (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.l = z80_state.d;
    used_cycles += 8;
    return _ix.w;
}


/* LD IXL, E (undocumented) */
static uint16_t z80_ix_iy_6b_ld_ixl_e (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.l = z80_state.e;
    used_cycles += 8;
    return _ix.w;
}


/* LD IXL, IXH (undocumented) */
static uint16_t z80_ix_iy_6c_ld_ixl_ixh (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.l = _ix.h;
    used_cycles += 8;
    return _ix.w;
}


/* LD IXL, IXL (undocumented) */
static uint16_t z80_ix_iy_6d_ld_ixl_ixl (uint16_t ix)
{
    used_cycles += 8;
    return ix;
}


/* LD L, (IX + *) */
static uint16_t z80_ix_iy_6e_ld_l_ixx (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    z80_state.l = memory_read (ix + offset);
    used_cycles += 19;
    return ix;
}


/* LD IXL, A (undocumented) */
static uint16_t z80_ix_iy_6f_ld_ixl_a (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.l = z80_state.a;
    used_cycles += 8;
    return _ix.w;
}


/* LD (IX + *), B */
static uint16_t z80_ix_iy_70_ld_ixx_b (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    memory_write (ix + offset, z80_state.b);
    used_cycles += 19;
    return ix;
}


/* LD (IX + *), C */
static uint16_t z80_ix_iy_71_ld_ixx_c (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    memory_write (ix + offset, z80_state.c);
    used_cycles += 19;
    return ix;
}


/* LD (IX + *), D */
static uint16_t z80_ix_iy_72_ld_ixx_d (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    memory_write (ix + offset, z80_state.d);
    used_cycles += 19;
    return ix;
}


/* LD (IX + *), E */
static uint16_t z80_ix_iy_73_ld_ixx_e (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    memory_write (ix + offset, z80_state.e);
    used_cycles += 19;
    return ix;
}


/* LD (IX + *), H */
static uint16_t z80_ix_iy_74_ld_ixx_h (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    memory_write (ix + offset, z80_state.h);
    used_cycles += 19;
    return ix;
}


/* LD (IX + *), L */
static uint16_t z80_ix_iy_75_ld_ixx_l (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    memory_write (ix + offset, z80_state.l);
    used_cycles += 19;
    return ix;
}


/* LD (IX + *), A */
static uint16_t z80_ix_iy_77_ld_ixx_a (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    memory_write (ix + offset, z80_state.a);
    used_cycles += 19;
    return ix;
}


/* LD A, IXH (undocumented) */
static uint16_t z80_ix_iy_7c_ld_a_ixh (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    z80_state.a = _ix.h;
    used_cycles += 8;
    return ix;
}


/* LD A, IXL (undocumented) */
static uint16_t z80_ix_iy_7d_ld_a_ixl (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    z80_state.a = _ix.l;
    used_cycles += 8;
    return ix;
}


/* LD A, (IX + *) */
static uint16_t z80_ix_iy_7e_ld_a_ixx (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    z80_state.a = memory_read (ix + offset);
    used_cycles += 19;
    return ix;
}


/* ADD A, IXH (undocumented) */
static uint16_t z80_ix_iy_84_add_a_ixh (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    SET_FLAGS_ADD (z80_state.a, _ix.h);
    z80_state.a += _ix.h;
    used_cycles += 8;
    return ix;
}


/* ADD A, IXL (undocumented) */
static uint16_t z80_ix_iy_85_add_a_ixl (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    SET_FLAGS_ADD (z80_state.a, _ix.l);
    z80_state.a += _ix.l;
    used_cycles += 8;
    return ix;
}


/* ADD A, (IX + *) */
static uint16_t z80_ix_iy_86_add_a_ixx (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    uint8_t data = memory_read (ix + offset);
    SET_FLAGS_ADD (z80_state.a, data);
    z80_state.a += data;
    used_cycles += 19;
    return ix;
}


/* ADC A, IXH (undocumented) */
static uint16_t z80_ix_iy_8c_adc_a_ixh (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    uint8_t value = _ix.h + z80_state.flag_carry;
    SET_FLAGS_ADC (_ix.h);
    z80_state.a += value;
    used_cycles += 8;
    return ix;
}


/* ADC A, IXL (undocumented) */
static uint16_t z80_ix_iy_8d_adc_a_ixl (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    uint8_t value = _ix.l + z80_state.flag_carry;
    SET_FLAGS_ADC (_ix.l);
    z80_state.a += value;
    used_cycles += 8;
    return ix;
}


/* ADC A, (IX + *) */
static uint16_t z80_ix_iy_8e_adc_a_ixx (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    uint8_t value = memory_read (ix + offset);
    uint8_t carry = z80_state.flag_carry;
    SET_FLAGS_ADC (value);
    z80_state.a += (value + carry);
    used_cycles += 19;
    return ix;
}


/* SUB A, IXH (undocumented) */
static uint16_t z80_ix_iy_94_sub_a_ixh (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    SET_FLAGS_SUB (z80_state.a, _ix.h);
    z80_state.a -= _ix.h;
    used_cycles += 8;
    return ix;
}


/* SUB A, IXL (undocumented) */
static uint16_t z80_ix_iy_95_sub_a_ixl (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    SET_FLAGS_SUB (z80_state.a, _ix.l);
    z80_state.a -= _ix.l;
    used_cycles += 8;
    return ix;
}


/* SUB A, (IX + *) */
static uint16_t z80_ix_iy_96_sub_a_ixx (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    uint8_t data = memory_read (ix + offset);
    SET_FLAGS_SUB (z80_state.a, data);
    z80_state.a -= data;
    used_cycles += 19;
    return ix;
}


/* SBC A, IXH (undocumented) */
static uint16_t z80_ix_iy_9c_sbc_a_ixh (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    uint8_t value = _ix.h + z80_state.flag_carry;
    SET_FLAGS_SBC (_ix.h);
    z80_state.a -= value;
    used_cycles += 8;
    return ix;
}


/* SBC A, IXL (undocumented) */
static uint16_t z80_ix_iy_9d_sbc_a_ixl (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    uint8_t value= _ix.l + z80_state.flag_carry;
    SET_FLAGS_SBC (_ix.l);
    z80_state.a -= value;
    used_cycles += 8;
    return ix;
}


/* SBC A, (IX + *) */
static uint16_t z80_ix_iy_9e_sbc_a_ixx (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    uint8_t value = memory_read (ix + offset);
    uint8_t carry = z80_state.flag_carry;
    SET_FLAGS_SBC (value);
    z80_state.a -= (value + carry);
    used_cycles += 19;
    return ix;
}


/* AND A, IXH (undocumented) */
static uint16_t z80_ix_iy_a4_and_a_ixh (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    z80_state.a &= _ix.h;
    SET_FLAGS_AND;
    used_cycles += 8;
    return ix;
}


/* AND A, IXL (undocumented) */
static uint16_t z80_ix_iy_a5_and_a_ixl (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    z80_state.a &= _ix.l;
    SET_FLAGS_AND;
    used_cycles += 8;
    return ix;
}


/* AND A, (IX + *) */
static uint16_t z80_ix_iy_a6_and_a_ixx (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    z80_state.a &= memory_read (ix + offset);
    SET_FLAGS_AND;
    used_cycles += 19;
    return ix;
}


/* XOR A, IXH (undocumented) */
static uint16_t z80_ix_iy_ac_xor_a_ixh (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    z80_state.a ^= _ix.h;
    SET_FLAGS_OR_XOR;
    used_cycles += 8;
    return ix;
}


/* XOR A, IXL (undocumented) */
static uint16_t z80_ix_iy_ad_xor_a_ixl (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    z80_state.a ^= _ix.l;
    SET_FLAGS_OR_XOR;
    used_cycles += 8;
    return ix;
}


/* XOR A, (IX + *) */
static uint16_t z80_ix_iy_ae_xor_a_ixx (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    z80_state.a ^= memory_read (ix + offset);
    SET_FLAGS_OR_XOR;
    used_cycles += 19;
    return ix;
}


/* OR A, IXH (undocumented) */
static uint16_t z80_ix_iy_b4_or_a_ixh (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    z80_state.a |= _ix.h;
    SET_FLAGS_OR_XOR;
    used_cycles += 8;
    return ix;
}


/* OR A, IXL (undocumented) */
static uint16_t z80_ix_iy_b5_or_a_ixl (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    z80_state.a |= _ix.l;
    SET_FLAGS_OR_XOR;
    used_cycles += 8;
    return ix;
}


/* OR A, (IX + *) */
static uint16_t z80_ix_iy_b6_or_a_ixx (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    z80_state.a |= memory_read (ix + offset);
    SET_FLAGS_OR_XOR;
    used_cycles += 19;
    return ix;
}


/* CP A, IXH (undocumented) */
static uint16_t z80_ix_iy_bc_cp_a_ixh (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    SET_FLAGS_SUB (z80_state.a, _ix.h);
    used_cycles += 8;
    return ix;
}


/* CP A, IXL (undocumented) */
static uint16_t z80_ix_iy_bd_cp_a_ixl (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    SET_FLAGS_SUB (z80_state.a, _ix.l);
    used_cycles += 8;
    return ix;
}


/* CP A, (IX + *) */
static uint16_t z80_ix_iy_be_cp_a_ixx (uint16_t ix)
{
    int8_t offset = memory_read (z80_state.pc++);
    uint8_t data = memory_read (ix + offset);
    SET_FLAGS_SUB (z80_state.a, data);
    used_cycles += 19;
    return ix;
}


/* IX / IY BIT PREFIX */
static uint16_t z80_ix_iy_cb_prefix (uint16_t ix)
{
    z80_ix_iy_bit_instruction (ix);
    return ix;
}


/* POP IX */
static uint16_t z80_ix_iy_e1_pop_ix (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.l = memory_read (z80_state.sp++);
    _ix.h = memory_read (z80_state.sp++);
    used_cycles += 14;
    return _ix.w;
}


/* EX (SP), IX */
static uint16_t z80_ix_iy_e3_ex_sp_ix (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.l = memory_read (z80_state.sp);
    _ix.h = memory_read (z80_state.sp + 1);
    memory_write (z80_state.sp,     _ix.l);
    memory_write (z80_state.sp + 1, _ix.h);
    used_cycles += 23;
    return _ix.w;
}


/* PUSH IX */
static uint16_t z80_ix_iy_e5_push_ix (uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    memory_write (--z80_state.sp, _ix.h);
    memory_write (--z80_state.sp, _ix.l);
    used_cycles += 15;
    return ix;
}


/* JP (IX) */
static uint16_t z80_ix_iy_e9_jp_ix (uint16_t ix)
{
    z80_state.pc = ix;
    used_cycles += 8;
    return ix;
}


/* LD SP, IX */
static uint16_t z80_ix_iy_f9_ld_sp_ix (uint16_t ix)
{
    z80_state.sp = ix;
    used_cycles += 10;
    return ix;
}


uint16_t (*z80_ix_iy_instruction [256]) (uint16_t) = {
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_09_add_ix_bc,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_19_add_ix_de,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_21_ld_ix_xx,      z80_ix_iy_22_ld_xx_ix,      z80_ix_iy_23_inc_ix,
    z80_ix_iy_24_inc_ixh,       z80_ix_iy_25_dec_ixh,       z80_ix_iy_26_ld_ixh_x,      z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_29_add_ix_ix,     z80_ix_iy_2a_ld_ix_xx,      z80_ix_iy_2b_dec_ix,
    z80_ix_iy_2c_inc_ixl,       z80_ix_iy_2d_dec_ixl,       z80_ix_iy_2e_ld_ixl_x,      z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_34_inc_ixx,       z80_ix_iy_35_dec_ixx,       z80_ix_iy_36_ld_ixx_x,      z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_39_add_ix_sp,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_44_ld_b_ixh,      z80_ix_iy_45_ld_b_ixl,      z80_ix_iy_46_ld_b_ixx,      z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_4c_ld_c_ixh,      z80_ix_iy_4d_ld_c_ixl,      z80_ix_iy_4e_ld_c_ixx,      z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_54_ld_d_ixh,      z80_ix_iy_55_ld_d_ixl,      z80_ix_iy_56_ld_d_ixx,      z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_5c_ld_e_ixh,      z80_ix_iy_5d_ld_e_ixl,      z80_ix_iy_5e_ld_e_ixx,      z80_ix_iy_fall_through,
    z80_ix_iy_60_ld_ixh_b,      z80_ix_iy_61_ld_ixh_c,      z80_ix_iy_62_ld_ixh_d,      z80_ix_iy_63_ld_ixh_e,
    z80_ix_iy_64_ld_ixh_ixh,    z80_ix_iy_65_ld_ixh_ixl,    z80_ix_iy_66_ld_h_ixx,      z80_ix_iy_67_ld_ixh_a,
    z80_ix_iy_68_ld_ixl_b,      z80_ix_iy_69_ld_ixl_c,      z80_ix_iy_6a_ld_ixl_d,      z80_ix_iy_6b_ld_ixl_e,
    z80_ix_iy_6c_ld_ixl_ixh,    z80_ix_iy_6d_ld_ixl_ixl,    z80_ix_iy_6e_ld_l_ixx,      z80_ix_iy_6f_ld_ixl_a,
    z80_ix_iy_70_ld_ixx_b,      z80_ix_iy_71_ld_ixx_c,      z80_ix_iy_72_ld_ixx_d,      z80_ix_iy_73_ld_ixx_e,
    z80_ix_iy_74_ld_ixx_h,      z80_ix_iy_75_ld_ixx_l,      z80_ix_iy_fall_through,     z80_ix_iy_77_ld_ixx_a,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_7c_ld_a_ixh,      z80_ix_iy_7d_ld_a_ixl,      z80_ix_iy_7e_ld_a_ixx,      z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_84_add_a_ixh,     z80_ix_iy_85_add_a_ixl,     z80_ix_iy_86_add_a_ixx,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_8c_adc_a_ixh,     z80_ix_iy_8d_adc_a_ixl,     z80_ix_iy_8e_adc_a_ixx,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_94_sub_a_ixh,     z80_ix_iy_95_sub_a_ixl,     z80_ix_iy_96_sub_a_ixx,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_9c_sbc_a_ixh,     z80_ix_iy_9d_sbc_a_ixl,     z80_ix_iy_9e_sbc_a_ixx,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_a4_and_a_ixh,     z80_ix_iy_a5_and_a_ixl,     z80_ix_iy_a6_and_a_ixx,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_ac_xor_a_ixh,     z80_ix_iy_ad_xor_a_ixl,     z80_ix_iy_ae_xor_a_ixx,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_b4_or_a_ixh,      z80_ix_iy_b5_or_a_ixl,      z80_ix_iy_b6_or_a_ixx,      z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_bc_cp_a_ixh,      z80_ix_iy_bd_cp_a_ixl,      z80_ix_iy_be_cp_a_ixx,      z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_cb_prefix,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_e1_pop_ix,        z80_ix_iy_fall_through,     z80_ix_iy_e3_ex_sp_ix,
    z80_ix_iy_fall_through,     z80_ix_iy_e5_push_ix,       z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_e9_jp_ix,         z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_f9_ld_sp_ix,      z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
    z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,     z80_ix_iy_fall_through,
};


/************************/
/**  Bit Instructions  **/
/************************/


/* RLC */
static uint8_t z80_cb_00_rlc (uint8_t value)
{
    value = (value << 1) | (value >> 7);
    SET_FLAGS_RLC (value);
    return value;
}


/* RRC */
static uint8_t z80_cb_08_rrc (uint8_t value)
{
    value = (value >> 1) | (value << 7);
    SET_FLAGS_RRC (value);
    return value;
}


/* RL */
static uint8_t z80_cb_10_rl (uint8_t value)
{
    uint8_t result;
    result = (value << 1) | z80_state.flag_carry;
    SET_FLAGS_RL_RR (result);
    z80_state.flag_carry = value >> 7;
    return result;
}


/* RR */
static uint8_t z80_cb_18_rr (uint8_t value)
{
    uint8_t result;
    result = (value >> 1) | (z80_state.flag_carry << 7);
    SET_FLAGS_RL_RR (result);
    z80_state.flag_carry = value;
    return result;
}


/* SLA */
static uint8_t z80_cb_20_sla (uint8_t value)
{
    uint8_t result;
    result = (value << 1);
    SET_FLAGS_RL_RR (result);
    z80_state.flag_carry = value >> 7;
    return result;
}


/* SRA */
static uint8_t z80_cb_28_sra (uint8_t value)
{
    uint8_t result;
    result = (value >> 1) | (value & 0x80);
    SET_FLAGS_RL_RR (result);
    z80_state.flag_carry = value;
    return result;
}

/* SLL (undocumented) */
static uint8_t z80_cb_30_sll (uint8_t value)
{
    uint8_t result;
    result = (value << 1) | 0x01;
    SET_FLAGS_RL_RR (result);
    z80_state.flag_carry = value >> 7;
    return result;
}


/* SRL */
static uint8_t z80_cb_38_srl (uint8_t value)
{
    uint8_t result;
    result = (value >> 1);
    SET_FLAGS_RL_RR (result);
    z80_state.flag_carry = value;
    return result;
}


/* BIT 0 */
static uint8_t z80_cb_40_bit_0 (uint8_t value)
{
    z80_state.flag_sub = 0;
    z80_state.flag_parity_overflow =  ~value >> 0;
    z80_state.flag_half = 1;
    z80_state.flag_zero = ~value >> 0;
    z80_state.flag_sign = 0;
    return value;
}


/* BIT 1 */
static uint8_t z80_cb_48_bit_1 (uint8_t value)
{
    z80_state.flag_sub = 0;
    z80_state.flag_parity_overflow =  ~value >> 1;
    z80_state.flag_half = 1;
    z80_state.flag_zero = ~value >> 1;
    z80_state.flag_sign = 0;
    return value;
}


/* BIT 2 */
static uint8_t z80_cb_50_bit_2 (uint8_t value)
{
    z80_state.flag_sub = 0;
    z80_state.flag_parity_overflow =  ~value >> 2;
    z80_state.flag_half = 1;
    z80_state.flag_zero = ~value >> 2;
    z80_state.flag_sign = 0;
    return value;
}


/* BIT 3 */
static uint8_t z80_cb_58_bit_3 (uint8_t value)
{
    z80_state.flag_sub = 0;
    z80_state.flag_parity_overflow =  ~value >> 3;
    z80_state.flag_half = 1;
    z80_state.flag_zero = ~value >> 3;
    z80_state.flag_sign = 0;
    return value;
}


/* BIT 4 */
static uint8_t z80_cb_60_bit_4 (uint8_t value)
{
    z80_state.flag_sub = 0;
    z80_state.flag_parity_overflow =  ~value >> 4;
    z80_state.flag_half = 1;
    z80_state.flag_zero = ~value >> 4;
    z80_state.flag_sign = 0;
    return value;
}


/* BIT 5 */
static uint8_t z80_cb_68_bit_5 (uint8_t value)
{
    z80_state.flag_sub = 0;
    z80_state.flag_parity_overflow =  ~value >> 5;
    z80_state.flag_half = 1;
    z80_state.flag_zero = ~value >> 5;
    z80_state.flag_sign = 0;
    return value;
}


/* BIT 6 */
static uint8_t z80_cb_70_bit_6 (uint8_t value)
{
    z80_state.flag_sub = 0;
    z80_state.flag_parity_overflow =  ~value >> 6;
    z80_state.flag_half = 1;
    z80_state.flag_zero = ~value >> 6;
    z80_state.flag_sign = 0;
    return value;
}


/* BIT 7 */
static uint8_t z80_cb_78_bit_7 (uint8_t value)
{
    z80_state.flag_sub = 0;
    z80_state.flag_parity_overflow =  ~value >> 7;
    z80_state.flag_half = 1;
    z80_state.flag_zero = ~value >> 7;
    z80_state.flag_sign = value >> 7;
    return value;
}


/* RES 0 */
static uint8_t z80_cb_80_res_0 (uint8_t value)
{
    return value & 0xfe;
}


/* RES 1 */
static uint8_t z80_cb_88_res_1 (uint8_t value)
{
    return value & 0xfd;
}


/* RES 2 */
static uint8_t z80_cb_90_res_2 (uint8_t value)
{
    return value & 0xfb;
}


/* RES 3 */
static uint8_t z80_cb_98_res_3 (uint8_t value)
{
    return value & 0xf7;
}


/* RES 4 */
static uint8_t z80_cb_a0_res_4 (uint8_t value)
{
    return value & 0xef;
}


/* RES 5 */
static uint8_t z80_cb_a8_res_5 (uint8_t value)
{
    return value & 0xdf;
}


/* RES 6 */
static uint8_t z80_cb_b0_res_6 (uint8_t value)
{
    return value & 0xbf;
}


/* RES 7 */
static uint8_t z80_cb_b8_res_7 (uint8_t value)
{
    return value & 0x7f;
}


/* SET 0 */
static uint8_t z80_cb_c0_set_0 (uint8_t value)
{
    return value | 0x01;
}


/* SET 1 */
static uint8_t z80_cb_c8_set_1 (uint8_t value)
{
    return value | 0x02;
}


/* SET 2 */
static uint8_t z80_cb_d0_set_2 (uint8_t value)
{
    return value | 0x04;
}


/* SET 3 */
static uint8_t z80_cb_d8_set_3 (uint8_t value)
{
    return value | 0x08;
}


/* SET 4 */
static uint8_t z80_cb_e0_set_4 (uint8_t value)
{
    return value | 0x10;
}


/* SET 5 */
static uint8_t z80_cb_e8_set_5 (uint8_t value)
{
    return value | 0x20;
}


/* SET 6 */
static uint8_t z80_cb_f0_set_6 (uint8_t value)
{
    return value | 0x40;
}


/* SET 7 */
static uint8_t z80_cb_f8_set_7 (uint8_t value)
{
    return value | 0x80;
}


uint8_t (*z80_cb_instruction [32]) (uint8_t) = {
    z80_cb_00_rlc,      z80_cb_08_rrc,      z80_cb_10_rl,       z80_cb_18_rr,
    z80_cb_20_sla,      z80_cb_28_sra,      z80_cb_30_sll,      z80_cb_38_srl,
    z80_cb_40_bit_0,    z80_cb_48_bit_1,    z80_cb_50_bit_2,    z80_cb_58_bit_3,
    z80_cb_60_bit_4,    z80_cb_68_bit_5,    z80_cb_70_bit_6,    z80_cb_78_bit_7,
    z80_cb_80_res_0,    z80_cb_88_res_1,    z80_cb_90_res_2,    z80_cb_98_res_3,
    z80_cb_a0_res_4,    z80_cb_a8_res_5,    z80_cb_b0_res_6,    z80_cb_b8_res_7,
    z80_cb_c0_set_0,    z80_cb_c8_set_1,    z80_cb_d0_set_2,    z80_cb_d8_set_3,
    z80_cb_e0_set_4,    z80_cb_e8_set_5,    z80_cb_f0_set_6,    z80_cb_f8_set_7
};


/*****************************/
/**  Extended Instructions  **/
/*****************************/


/* IN B, (C) */
static void z80_ed_40_in_b_c (void)
{
    z80_state.b = io_read (z80_state.c);
    SET_FLAGS_ED_IN (z80_state.b);
    used_cycles += 12;
}


/* OUT (C), B */
static void z80_ed_41_out_c_b (void)
{
    io_write (z80_state.c, z80_state.b);
    used_cycles += 12;
}


/* SBC HL, BC */
static void z80_ed_42_sbc_hl_bc (void)
{
    uint16_t temp;
    temp = z80_state.bc + z80_state.flag_carry;
    SET_FLAGS_SBC_16 (z80_state.bc);
    z80_state.hl -= temp;
    used_cycles += 15;
}


/* LD (**), BC */
static void z80_ed_43_ld_xx_bc (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    memory_write (addr.w,     z80_state.c);
    memory_write (addr.w + 1, z80_state.b);
    used_cycles += 20;
}


/* NEG */
static void z80_ed_44_neg (void)
{
    uint8_t half;
    half = 0 - (z80_state.a & 0x0f);
    z80_state.a = 0 - (int8_t) z80_state.a;

    z80_state.flag_carry = (z80_state.a != 0);
    z80_state.flag_sub = 1;
    z80_state.flag_parity_overflow = (z80_state.a == 0x80);
    z80_state.flag_half = half >> 4;
    z80_state.flag_zero = (z80_state.a == 0x00);
    z80_state.flag_sign = z80_state.a >> 7;

    used_cycles += 8;
}


/* RETN */
static void z80_ed_45_retn (void)
{
    z80_state.pc_l = memory_read (z80_state.sp++);
    z80_state.pc_h = memory_read (z80_state.sp++);
    z80_state.iff1 = z80_state.iff2;
    used_cycles += 14;
}


/* IM 0 */
static void z80_ed_46_im_0 (void)
{
    z80_state.im = 0;
    used_cycles += 8;
}


/* LD I, A */
static void z80_ed_47_ld_i_a (void)
{
    z80_state.i = z80_state.a;
    used_cycles += 9;
}


/* IN C, (C) */
static void z80_ed_48_in_c_c (void)
{
    z80_state.c = io_read (z80_state.c);
    SET_FLAGS_ED_IN (z80_state.c);
    used_cycles += 12;
}


/* OUT (C), C */
static void z80_ed_49_out_c_c (void)
{
    io_write (z80_state.c, z80_state.c);
    used_cycles += 12;
}


/* ADC HL, BC */
static void z80_ed_4a_adc_hl_bc (void)
{
    uint16_t temp;
    temp = z80_state.bc + z80_state.flag_carry;
    SET_FLAGS_ADC_16 (z80_state.bc);
    z80_state.hl += temp;
    used_cycles += 15;
}


/* LD BC, (**) */
static void z80_ed_4b_ld_bc_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    z80_state.c = memory_read (addr.w);
    z80_state.b = memory_read (addr.w + 1);
    used_cycles += 20;
}


/* NEG (undocumented) */
static void z80_ed_4c_neg (void)
{
    z80_ed_44_neg ();
}


/* RETI */
static void z80_ed_4d_reti (void)
{
    z80_state.pc_l = memory_read (z80_state.sp++);
    z80_state.pc_h = memory_read (z80_state.sp++);
    used_cycles += 14;
}


/* IM 0 (undocumented) */
static void z80_ed_4e_im_0 (void)
{
    z80_ed_46_im_0 ();
}


/* LD R, A */
static void z80_ed_4f_ld_r_a (void)
{
    z80_state.r = z80_state.a;
    used_cycles += 9;
}


/* IN D, (C) */
static void z80_ed_50_in_d_c (void)
{
    z80_state.d = io_read (z80_state.c);
    SET_FLAGS_ED_IN (z80_state.d);
    used_cycles += 12;
}


/* OUT (C), D */
static void z80_ed_51_out_c_d (void)
{
    io_write (z80_state.c, z80_state.d);
    used_cycles += 12;
}


/* SBC HL, DE */
static void z80_ed_52_sbc_hl_de (void)
{
    uint16_t temp;
    temp = z80_state.de + z80_state.flag_carry;
    SET_FLAGS_SBC_16 (z80_state.de);
    z80_state.hl -= temp;
    used_cycles += 15;
}


/* LD (**), DC */
static void z80_ed_53_ld_xx_de (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    memory_write (addr.w,     z80_state.e);
    memory_write (addr.w + 1, z80_state.d);
    used_cycles += 20;
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
    z80_state.im = 1;
    used_cycles += 8;
}


/* LD A, I */
static void z80_ed_57_ld_a_i (void)
{
    z80_state.a = z80_state.i;
    z80_state.flag_sub = 0;
    z80_state.flag_parity_overflow = z80_state.iff2;
    z80_state.flag_half =  0;
    z80_state.flag_zero = (z80_state.i == 0);
    z80_state.flag_sign = z80_state.i >> 7;
    used_cycles += 9;
}


/* IN E, (C) */
static void z80_ed_58_in_e_c (void)
{
    z80_state.e = io_read (z80_state.c);
    SET_FLAGS_ED_IN (z80_state.e);
    used_cycles += 12;
}


/* OUT (C), E */
static void z80_ed_59_out_c_e (void)
{
    io_write (z80_state.c, z80_state.e);
    used_cycles += 12;
}


/* ADC HL, DE */
static void z80_ed_5a_adc_hl_de (void)
{
    uint16_t temp = z80_state.de + z80_state.flag_carry;
    SET_FLAGS_ADC_16 (z80_state.de);
    z80_state.hl += temp;
    used_cycles += 15;
}


/* LD DE, (**) */
static void z80_ed_5b_ld_de_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    z80_state.e = memory_read (addr.w);
    z80_state.d = memory_read (addr.w + 1);
    used_cycles += 20;
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
    z80_state.im = 2;
    used_cycles += 8;
}


/* LD A, R */
static void z80_ed_5f_ld_a_r (void)
{
    z80_state.a = z80_state.r;
    z80_state.flag_sub =  0;
    z80_state.flag_parity_overflow = z80_state.iff2;
    z80_state.flag_half = 0;
    z80_state.flag_zero = (z80_state.r == 0);
    z80_state.flag_sign = z80_state.r >> 7;
    used_cycles += 9;
}


/* IN H, (C) */
static void z80_ed_60_in_h_c (void)
{
    z80_state.h = io_read (z80_state.c);
    SET_FLAGS_ED_IN (z80_state.h);
    used_cycles += 12;
}


/* OUT (C), H */
static void z80_ed_61_out_c_h (void)
{
    io_write (z80_state.c, z80_state.h);
    used_cycles += 12;
}


/* SBC HL, HL */
static void z80_ed_62_sbc_hl_hl (void)
{
    uint16_t temp = z80_state.hl + z80_state.flag_carry;
    SET_FLAGS_SBC_16 (z80_state.hl);
    z80_state.hl -= temp;
    used_cycles += 15;
}


/* LD (**), hl (undocumented) */
static void z80_ed_63_ld_xx_hl (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    memory_write (addr.w,     z80_state.l);
    memory_write (addr.w + 1, z80_state.h);
    used_cycles += 20;
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
    shifted.l = memory_read (z80_state.hl);
    shifted.h = z80_state.a & 0x0f;
    shifted.w = (shifted.w >> 4) | ((shifted.w & 0x000f) << 8);

    /* Lower 8 bits go to memory */
    memory_write (z80_state.hl, shifted.l);

    /* Upper 4 bits go to A */
    z80_state.a = (z80_state.a & 0xf0) | shifted.h;

    SET_FLAGS_RLD_RRD;
    used_cycles += 18;
}


/* IN L, (C) */
static void z80_ed_68_in_l_c (void)
{
    z80_state.l = io_read (z80_state.c);
    SET_FLAGS_ED_IN (z80_state.l);
    used_cycles += 12;
}


/* OUT (C), L */
static void z80_ed_69_out_c_l (void)
{
    io_write (z80_state.c, z80_state.l);
    used_cycles += 12;
}


/* ADC HL, HL */
static void z80_ed_6a_adc_hl_hl (void)
{
    uint16_t temp = z80_state.hl + z80_state.flag_carry;
    SET_FLAGS_ADC_16 (z80_state.hl);
    z80_state.hl += temp;
    used_cycles += 15;
}


/* LD HL, (**) (undocumented) */
static void z80_ed_6b_ld_hl_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    z80_state.l = memory_read (addr.w);
    z80_state.h = memory_read (addr.w + 1);
    used_cycles += 20;
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
    shifted.w = ((uint16_t) memory_read (z80_state.hl) << 4) | (z80_state.a & 0x0f);

    /* Lower 8 bits go to memory */
    memory_write (z80_state.hl, shifted.l);

    /* Upper 4 bits go to A */
    z80_state.a = (z80_state.a & 0xf0) | shifted.h;

    SET_FLAGS_RLD_RRD;
    used_cycles += 18;
}


/* IN (C) (undocumented) */
static void z80_ed_70_in_c (void)
{
    uint8_t throwaway;
    throwaway = io_read (z80_state.c);
    SET_FLAGS_ED_IN (throwaway);
    used_cycles += 12;
}


/* OUT (C), 0 (undocumented) */
static void z80_ed_71_out_c_0 (void)
{
    io_write (z80_state.c, 0);
    used_cycles += 12;
}


/* SBC HL, SP */
static void z80_ed_72_sbc_hl_sp (void)
{
    uint16_t temp = z80_state.sp + z80_state.flag_carry;
    SET_FLAGS_SBC_16 (z80_state.sp);
    z80_state.hl -= temp;
    used_cycles += 15;
}


/* LD (**), SP */
static void z80_ed_73_ld_xx_sp (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    memory_write (addr.w,     z80_state.sp_l);
    memory_write (addr.w + 1, z80_state.sp_h);
    used_cycles += 20;
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
    z80_state.a = io_read (z80_state.c);
    SET_FLAGS_ED_IN (z80_state.a);
    used_cycles += 12;
}


/* OUT (C), A */
static void z80_ed_79_out_c_a (void)
{
    io_write (z80_state.c, z80_state.a);
    used_cycles += 12;
}


/* ADC HL, SP */
static void z80_ed_7a_adc_hl_sp (void)
{
    uint16_t temp = z80_state.sp + z80_state.flag_carry;
    SET_FLAGS_ADC_16 (z80_state.sp);
    z80_state.hl += temp;
    used_cycles += 15;
}


/* LD SP, (**) */
static void z80_ed_7b_ld_sp_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    z80_state.sp_l = memory_read (addr.w);
    z80_state.sp_h = memory_read (addr.w + 1);
    used_cycles += 20;
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
    memory_write (z80_state.de, memory_read (z80_state.hl));
    z80_state.hl++;
    z80_state.de++;
    z80_state.bc--;
    z80_state.flag_sub = 0;
    z80_state.flag_parity_overflow = (z80_state.bc != 0);
    z80_state.flag_half = 0;
    used_cycles += 16;
}


/* CPI */
static void z80_ed_a1_cpi (void)
{
    uint8_t temp = memory_read (z80_state.hl);
    z80_state.hl++;
    z80_state.bc--;
    SET_FLAGS_CPI_CPD (temp);
    used_cycles += 16;
}


/* INI */
static void z80_ed_a2_ini (void)
{
    memory_write (z80_state.hl, io_read (z80_state.c));
    z80_state.hl++;
    z80_state.b--;
    z80_state.flag_sub = 1;
    z80_state.flag_parity_overflow = 0;
    z80_state.flag_half = 0;
    z80_state.flag_zero = (z80_state.b == 0);
    z80_state.flag_sign = 0;

    used_cycles += 16;
}


/* OUTI */
static void z80_ed_a3_outi (void)
{
    io_write (z80_state.c, memory_read (z80_state.hl));
    z80_state.hl++;
    z80_state.b--;
    z80_state.flag_sub = 1;
    z80_state.flag_parity_overflow = 0;
    z80_state.flag_half = 0;
    z80_state.flag_zero = (z80_state.b == 0);
    z80_state.flag_sign = 0;
    used_cycles += 16;
}


/* LDD */
static void z80_ed_a8_ldd (void)
{
    memory_write (z80_state.de, memory_read (z80_state.hl));
    z80_state.hl--;
    z80_state.de--;
    z80_state.bc--;
    z80_state.flag_sub = 0;
    z80_state.flag_parity_overflow = (z80_state.bc != 0);
    z80_state.flag_half = 0;
    used_cycles += 16;
}


/* CPD */
static void z80_ed_a9_cpd (void)
{
    uint8_t temp = memory_read (z80_state.hl);
    z80_state.hl--;
    z80_state.bc--;
    SET_FLAGS_CPI_CPD (temp);
    used_cycles += 16;
}


/* IND */
static void z80_ed_aa_ind (void)
{
    memory_write (z80_state.hl, io_read (z80_state.c));
    z80_state.hl--;
    z80_state.b--;
    z80_state.flag_sub = 1;
    z80_state.flag_parity_overflow = 0;
    z80_state.flag_half = 0;
    z80_state.flag_zero = (z80_state.b == 0);
    z80_state.flag_sign = 0;
    used_cycles += 16;
}


/* OUTD */
static void z80_ed_ab_outd (void)
{
    /* TODO: Implement 'unknown' flag behaviour.
     *       Described in 'The Undocumented Z80 Documented'. */
    uint8_t temp = memory_read (z80_state.hl);
    z80_state.b--;
    io_write (z80_state.c, temp);
    z80_state.hl--;
    z80_state.flag_sub = 1;
    z80_state.flag_zero = (z80_state.b == 0);
    used_cycles += 16;
}


/* LDIR */
static void z80_ed_b0_ldir (void)
{
    memory_write (z80_state.de, memory_read (z80_state.hl));
    z80_state.hl++;
    z80_state.de++;
    z80_state.bc--;
    z80_state.flag_sub = 0;
    z80_state.flag_parity_overflow = (z80_state.bc != 0);
    z80_state.flag_half = 0;
    if (z80_state.bc)
    {
        z80_state.pc -= 2;
        used_cycles += 21;
    }
    else
    {
        used_cycles += 16;
    }
}


/* CPIR */
static void z80_ed_b1_cpir (void)
{
    uint8_t temp = memory_read (z80_state.hl);
    z80_state.hl++;
    z80_state.bc--;
    if (z80_state.bc != 0 && z80_state.a != temp)
    {
        z80_state.pc -= 2;
        used_cycles += 21;
    }
    else
    {
        used_cycles += 16;
    }
    SET_FLAGS_CPI_CPD (temp);
}


/* INIR */
static void z80_ed_b2_inir (void)
{
    memory_write (z80_state.hl, io_read (z80_state.c));
    z80_state.hl++;
    z80_state.b--;
    z80_state.flag_sub = 1;
    z80_state.flag_parity_overflow = 0;
    z80_state.flag_half = 0;
    z80_state.flag_zero = 1;
    z80_state.flag_sign = 0;
    if (z80_state.b == 0)
    {
        used_cycles += 16;
    }
    else
    {
        z80_state.pc -= 2;
        used_cycles += 21;
    }
}


/* OTIR */
static void z80_ed_b3_otir (void)
{
    io_write (z80_state.c, memory_read (z80_state.hl));
    z80_state.hl++;
    z80_state.b--;
    z80_state.flag_sub = 1;
    z80_state.flag_parity_overflow = 0;
    z80_state.flag_half = 0;
    z80_state.flag_zero = 1;
    z80_state.flag_sign = 0;
    if (z80_state.b)
    {
        z80_state.pc -= 2;
        used_cycles += 21;
    }
    else
    {
        used_cycles += 16;
    }
}


/* LDDR */
static void z80_ed_b8_lddr (void)
{
    memory_write (z80_state.de, memory_read (z80_state.hl));
    z80_state.hl--;
    z80_state.de--;
    z80_state.bc--;
    z80_state.flag_sub = 0;
    z80_state.flag_parity_overflow = (z80_state.bc != 0);
    z80_state.flag_half = 0;
    if (z80_state.bc)
    {
        z80_state.pc -= 2;
        used_cycles += 21;
    }
    else
    {
        used_cycles += 16;
    }
}


/* CPDR */
static void z80_ed_b9_cpdr (void)
{
    uint8_t temp = memory_read (z80_state.hl);
    z80_state.hl--;
    z80_state.bc--;
    SET_FLAGS_CPI_CPD (temp);
    if (z80_state.bc != 0 && z80_state.a != temp)
    {
        z80_state.pc -= 2;
        used_cycles += 21;
    }
    else
    {
        used_cycles += 16;
    }
}


/* INDR */
static void z80_ed_ba_indr (void)
{
    memory_write (z80_state.hl, io_read (z80_state.c));
    z80_state.hl--;
    z80_state.b--;
    z80_state.flag_sub = 1;
    z80_state.flag_parity_overflow = 0;
    z80_state.flag_half = 0;
    z80_state.flag_zero = 1;
    z80_state.flag_sign = 0;
    if (z80_state.b == 0)
    {
        used_cycles += 16;
    }
    else
    {
        z80_state.pc -= 2;
        used_cycles += 21;
    }
}


/* OTDR */
static void z80_ed_bb_otdr (void)
{
    io_write (z80_state.c, memory_read (z80_state.hl));
    z80_state.hl--;
    z80_state.b--;
    z80_state.flag_sub = 1;
    z80_state.flag_parity_overflow = 0;
    z80_state.flag_half = 0;
    z80_state.flag_zero = 1;
    z80_state.flag_sign = 0;
    if (z80_state.b)
    {
        z80_state.pc -= 2;
        used_cycles += 21;
    }
    else
    {
        used_cycles += 16;
    }
}


/* NOP (undocumented) */
static void z80_ed_xx_nop (void)
{
    used_cycles += 8;
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
    used_cycles += 4;
}


/* LD BC, ** */
static void z80_01_ld_bc_xx (void)
{
    z80_state.c = memory_read (z80_state.pc++);
    z80_state.b = memory_read (z80_state.pc++);
    used_cycles += 10;
}


/* LD (BC), A */
static void z80_02_ld_bc_a (void)
{
    memory_write (z80_state.bc, z80_state.a);
    used_cycles += 7;
}


/* INC BC */
static void z80_03_inc_bc (void)
{
    z80_state.bc++;
    used_cycles += 6;
}


/* INC B */
static void z80_04_inc_b (void)
{
    z80_state.b++;
    SET_FLAGS_INC (z80_state.b);
    used_cycles += 4;
}


/* DEC B */
static void z80_05_dec_b (void)
{
    z80_state.b--;
    SET_FLAGS_DEC (z80_state.b);
    used_cycles += 4;
}


/* LD B, * */
static void z80_06_ld_b_x (void)
{
    z80_state.b = memory_read (z80_state.pc++);
    used_cycles += 7;
}


/* RLCA */
static void z80_07_rlca (void)
{
    z80_state.a = (z80_state.a << 1) | (z80_state.a >> 7);
    z80_state.flag_carry = z80_state.a;
    z80_state.flag_sub = 0;
    z80_state.flag_half = 0;
    used_cycles += 4;
}


/* EX AF AF' */
static void z80_08_ex_af_af (void)
{
    SWAP (uint16_t, z80_state.af, z80_state.af_alt);
    used_cycles += 4;
}


/* ADD HL, BC */
static void z80_09_add_hl_bc (void)
{
    SET_FLAGS_ADD_16 (z80_state.hl, z80_state.bc);
    z80_state.hl += z80_state.bc;
    used_cycles += 11;
}


/* LD A, (BC) */
static void z80_0a_ld_a_bc (void)
{
    z80_state.a = memory_read (z80_state.bc);
    used_cycles += 7;
}


/* DEC BC */
static void z80_0b_dec_bc (void)
{
    z80_state.bc--;
    used_cycles += 6;
}


/* INC C */
static void z80_0c_inc_c (void)
{
    z80_state.c++;
    SET_FLAGS_INC (z80_state.c);
    used_cycles += 4;
}


/* DEC C */
static void z80_0d_dec_c (void)
{
    z80_state.c--;
    SET_FLAGS_DEC (z80_state.c);
    used_cycles += 4;
}


/* LD C, * */
static void z80_0e_ld_c_x (void)
{
    z80_state.c = memory_read (z80_state.pc++);
    used_cycles += 7;
}


/* RRCA */
static void z80_0f_rrca (void)
{
    z80_state.a = (z80_state.a >> 1) | (z80_state.a << 7);
    z80_state.flag_carry = z80_state.a >> 7;
    z80_state.flag_sub = 0;
    z80_state.flag_half = 0;
    used_cycles += 4;
}


/* DJNZ */
static void z80_10_djnz (void)
{
    uint8_t imm = memory_read (z80_state.pc++);

    if (--z80_state.b)
    {
        z80_state.pc += (int8_t) imm;
        used_cycles += 13;
    }
    else
    {
        used_cycles += 8;
    }
}


/* LD DE, ** */
static void z80_11_ld_de_xx (void)
{
    z80_state.e = memory_read (z80_state.pc++);
    z80_state.d = memory_read (z80_state.pc++);
    used_cycles += 10;
}


/* LD (DE), A */
static void z80_12_ld_de_a (void)
{
    memory_write (z80_state.de, z80_state.a);
    used_cycles += 7;
}


/* INC DE */
static void z80_13_inc_de (void)
{
    z80_state.de++;
    used_cycles += 6;
}


/* INC D */
static void z80_14_inc_d (void)
{
    z80_state.d++;
    SET_FLAGS_INC (z80_state.d);
    used_cycles += 4;
}


/* DEC D */
static void z80_15_dec_d (void)
{
    z80_state.d--;
    SET_FLAGS_DEC (z80_state.d);
    used_cycles += 4;
}


/* LD D, * */
static void z80_16_ld_d_x (void)
{
    z80_state.d = memory_read (z80_state.pc++);
    used_cycles += 7;
}


/* RLA */
static void z80_17_rla (void)
{
    uint8_t temp = z80_state.a;
    z80_state.a = (z80_state.a << 1) + z80_state.flag_carry;
    z80_state.flag_carry = temp >> 7;
    z80_state.flag_sub = 0;
    z80_state.flag_half = 0;
    used_cycles += 4;
}


/* JR */
static void z80_18_jr (void)
{
    uint8_t imm = memory_read (z80_state.pc++);
    z80_state.pc += (int8_t) imm;
    used_cycles += 12;
}


/* ADD HL, DE */
static void z80_19_add_hl_de (void)
{
    SET_FLAGS_ADD_16 (z80_state.hl, z80_state.de);
    z80_state.hl += z80_state.de;
    used_cycles += 11;
}


/* LD A, (DE) */
static void z80_1a_ld_a_de (void)
{
    z80_state.a = memory_read (z80_state.de);
    used_cycles += 7;
}


/* DEC DE */
static void z80_1b_dec_de (void)
{
    z80_state.de--;
    used_cycles += 6;
}


/* INC E */
static void z80_1c_inc_e (void)
{
    z80_state.e++;
    SET_FLAGS_INC (z80_state.e);
    used_cycles += 4;
}


/* DEC E */
static void z80_1d_dec_e (void)
{
    z80_state.e--;
    SET_FLAGS_DEC (z80_state.e);
    used_cycles += 4;
}


/* LD E, * */
static void z80_1e_ld_e_x (void)
{
    z80_state.e = memory_read (z80_state.pc++);
    used_cycles += 7;
}


/* RRA */
static void z80_1f_rra (void)
{
    uint8_t temp = z80_state.a;
    z80_state.a = (z80_state.a >> 1) + (z80_state.flag_carry << 7);
    z80_state.flag_carry = temp;
    z80_state.flag_sub = 0;
    z80_state.flag_half = 0;
    used_cycles += 4;
}


/* JR NZ */
static void z80_20_jr_nz (void)
{
    uint8_t imm = memory_read (z80_state.pc++);

    if (z80_state.flag_zero)
    {
        used_cycles += 7;
    }
    else
    {
        z80_state.pc += (int8_t) imm;
        used_cycles += 12;
    }
}


/* LD HL, ** */
static void z80_21_ld_hl_xx (void)
{
    z80_state.l = memory_read (z80_state.pc++);
    z80_state.h = memory_read (z80_state.pc++);
    used_cycles += 10;
}


/* LD (**), HL */
static void z80_22_ld_xx_hl (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);
    memory_write (addr.w,     z80_state.l);
    memory_write (addr.w + 1, z80_state.h);
    used_cycles += 16;
}


/* INC HL */
static void z80_23_inc_hl (void)
{
    z80_state.hl++;
    used_cycles += 6;
}


/* INC H */
static void z80_24_inc_h (void)
{
    z80_state.h++;
    SET_FLAGS_INC (z80_state.h);
    used_cycles += 4;
}


/* DEC H */
static void z80_25_dec_h (void)
{
    z80_state.h--;
    SET_FLAGS_DEC (z80_state.h);
    used_cycles += 4;
}


/* LD H, * */
static void z80_26_ld_h_x (void)
{
    z80_state.h = memory_read (z80_state.pc++);
    used_cycles += 7;
}


/* DAA */
static void z80_27_daa (void)
{
    bool set_carry = false;
    bool set_half = false;
    uint8_t diff = 0x00;

    /* Calculate diff to apply */
    switch (z80_state.f & (Z80_FLAG_CARRY | Z80_FLAG_HALF))
    {
        case Z80_FLAG_NONE:
                 if ((z80_state.a & 0xf0) < 0xa0 && (z80_state.a & 0x0f) < 0x0a)    diff = 0x00;
            else if ((z80_state.a & 0xf0) < 0x90 && (z80_state.a & 0x0f) > 0x09)    diff = 0x06;
            else if ((z80_state.a & 0xf0) > 0x90 && (z80_state.a & 0x0f) < 0x0a)    diff = 0x60;
            else if ((z80_state.a & 0xf0) > 0x80 && (z80_state.a & 0x0f) > 0x09)    diff = 0x66;
            break;
        case Z80_FLAG_HALF:
                 if ((z80_state.a & 0xf0) < 0xa0 && (z80_state.a & 0x0f) < 0x0a)    diff = 0x06;
            else if ((z80_state.a & 0xf0) < 0x90 && (z80_state.a & 0x0f) > 0x09)    diff = 0x06;
            else if ((z80_state.a & 0xf0) > 0x80 && (z80_state.a & 0x0f) > 0x09)    diff = 0x66;
            else if ((z80_state.a & 0xf0) > 0x90 && (z80_state.a & 0x0f) < 0x0a)    diff = 0x66;
            break;
        case Z80_FLAG_CARRY:
                 if (                              (z80_state.a & 0x0f) < 0x0a)     diff = 0x60;
            else if (                              (z80_state.a & 0x0f) > 0x09)     diff = 0x66;
            break;
        case Z80_FLAG_CARRY | Z80_FLAG_HALF:
                                                                                    diff = 0x66;
            break;
    }

    /* Calculate carry out */
    if (((z80_state.a & 0xf0) > 0x80 && (z80_state.a & 0x0f) > 0x09) ||
        ((z80_state.a & 0xf0) > 0x90 && (z80_state.a & 0x0f) < 0x0a) ||
        (z80_state.f & Z80_FLAG_CARRY))
    {
        set_carry = true;
    }

    /* Calculate half-carry out */
    if ( (!z80_state.flag_sub && (z80_state.a & 0x0f) > 0x09) ||
         ( z80_state.flag_sub && z80_state.flag_half && (z80_state.a & 0x0f) < 0x06))
    {
        set_half = true;
    }

    /* Apply diff */
    if (z80_state.flag_sub)
    {
        z80_state.a -= diff;
    }
    else
    {
        z80_state.a += diff;
    }

    z80_state.flag_carry = set_carry;
    z80_state.flag_parity_overflow = uint8_even_parity [z80_state.a];
    z80_state.flag_half = set_half;
    z80_state.flag_zero = (z80_state.a == 0x00);
    z80_state.flag_sign = z80_state.a >> 7;

    used_cycles += 4;
}


/* JR Z */
static void z80_28_jr_z (void)
{
    uint8_t imm = memory_read (z80_state.pc++);

    if (z80_state.flag_zero)
    {
        z80_state.pc += (int8_t) imm;
        used_cycles += 12;
    }
    else
    {
        used_cycles += 7;
    }
}


/* ADD HL, HL */
static void z80_29_add_hl_hl (void)
{
    SET_FLAGS_ADD_16 (z80_state.hl, z80_state.hl);
    z80_state.hl += z80_state.hl;
    used_cycles += 11;
}


/* LD, HL, (**) */
static void z80_2a_ld_hl_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);
    z80_state.l = memory_read (addr.w);
    z80_state.h = memory_read (addr.w + 1);
    used_cycles += 16;
}


/* DEC HL */
static void z80_2b_dec_hl (void)
{
    z80_state.hl--;
    used_cycles += 6;
}


/* INC L */
static void z80_2c_inc_l (void)
{
    z80_state.l++;
    SET_FLAGS_INC (z80_state.l);
    used_cycles += 4;
}


/* DEC L */
static void z80_2d_dec_l (void)
{
    z80_state.l--;
    SET_FLAGS_DEC (z80_state.l);
    used_cycles += 4;
}


/* LD L, * */
static void z80_2e_ld_l_x (void)
{
    z80_state.l = memory_read (z80_state.pc++);
    used_cycles += 7;
}


/* CPL */
static void z80_2f_cpl (void)
{
    z80_state.a = ~z80_state.a;
    z80_state.flag_sub = 1;
    z80_state.flag_half = 1;
    used_cycles += 4;
}


/* JR NC */
static void z80_30_jr_nc (void)
{
    uint8_t imm = memory_read (z80_state.pc++);

    if (z80_state.flag_carry)
    {
        used_cycles += 7;
    }
    else
    {
        z80_state.pc += (int8_t) imm;
        used_cycles += 12;
    }
}


/* LD SP, ** */
static void z80_31_ld_sp_xx (void)
{
    z80_state.sp_l = memory_read (z80_state.pc++);
    z80_state.sp_h = memory_read (z80_state.pc++);
    used_cycles += 10;
}


/* LD (**), A */
static void z80_32_ld_xx_a (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);
    memory_write (addr.w, z80_state.a);
    used_cycles += 13;
}


/* INC SP */
static void z80_33_inc_sp (void)
{
    z80_state.sp++;
    used_cycles += 6;
}


/* INC (HL) */
static void z80_34_inc_hl (void)
{
    uint8_t value = memory_read (z80_state.hl);
    value++;
    memory_write (z80_state.hl, value);
    SET_FLAGS_INC (value);
    used_cycles += 11;
}


/* DEC (HL) */
static void z80_35_dec_hl (void)
{
    uint8_t value = memory_read (z80_state.hl);
    value--;
    memory_write (z80_state.hl, value);
    SET_FLAGS_DEC (value);
    used_cycles += 11;
}


/* LD (HL), * */
static void z80_36_ld_hl_x (void)
{
    memory_write (z80_state.hl, memory_read (z80_state.pc++));
    used_cycles += 10;
}


/* SCF */
static void z80_37_scf (void)
{
    z80_state.flag_carry = 1;
    z80_state.flag_sub = 0;
    z80_state.flag_half = 0;
    used_cycles += 4;
}


/* JR C, * */
static void z80_38_jr_c_x (void)
{
    uint8_t imm = memory_read (z80_state.pc++);

    if (z80_state.flag_carry)
    {
        z80_state.pc += (int8_t) imm;
        used_cycles += 12;
    }
    else
    {
        used_cycles += 7;
    }
}


/* ADD HL, SP */
static void z80_39_add_hl_sp (void)
{
    SET_FLAGS_ADD_16 (z80_state.hl, z80_state.sp);
    z80_state.hl += z80_state.sp;
    used_cycles += 11;
}


/* LD, A, (**) */
static void z80_3a_ld_a_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);
    z80_state.a = memory_read (addr.w);
    used_cycles += 13;
}


/* DEC SP */
static void z80_3b_dec_sp (void)
{
    z80_state.sp--;
    used_cycles += 6;
}


/* INC A */
static void z80_3c_inc_a (void)
{
    z80_state.a++;
    SET_FLAGS_INC (z80_state.a);
    used_cycles += 4;
}

/* DEC A */
static void z80_3d_dec_a (void)
{
    z80_state.a--;
    SET_FLAGS_DEC (z80_state.a);
    used_cycles += 4;
}


/* LD A, * */
static void z80_3e_ld_a_x (void)
{
    z80_state.a = memory_read (z80_state.pc++);
    used_cycles += 7;
}


/* CCF */
static void z80_3f_ccf (void)
{
    z80_state.flag_sub = 0;
    z80_state.flag_half = z80_state.flag_carry;
    z80_state.flag_carry = ~z80_state.flag_carry;
    used_cycles += 4;
}


/* LD B, B */
static void z80_40_ld_b_b (void)
{
    used_cycles += 4;
}


/* LD B, C */
static void z80_41_ld_b_c (void)
{
    z80_state.b = z80_state.c;
    used_cycles += 4;
}


/* LD B, D */
static void z80_42_ld_b_d (void)
{
    z80_state.b = z80_state.d;
    used_cycles += 4;
}


/* LD B, E */
static void z80_43_ld_b_e (void)
{
    z80_state.b = z80_state.e;
    used_cycles += 4;
}


/* LD B, H */
static void z80_44_ld_b_h (void)
{
    z80_state.b = z80_state.h;
    used_cycles += 4;
}


/* LD B, L */
static void z80_45_ld_b_l (void)
{
    z80_state.b = z80_state.l;
    used_cycles += 4;
}


/* LD B, (HL) */
static void z80_46_ld_b_hl (void)
{
    z80_state.b = memory_read (z80_state.hl);
    used_cycles += 7;
}


/* LD B, A */
static void z80_47_ld_b_a (void)
{
    z80_state.b = z80_state.a;
    used_cycles += 4;
}


/* LD C, B */
static void z80_48_ld_c_b (void)
{
    z80_state.c = z80_state.b;
    used_cycles += 4;
}


/* LD C, C */
static void z80_49_ld_c_c (void)
{
    used_cycles += 4;
}


/* LD C, D */
static void z80_4a_ld_c_d (void)
{
    z80_state.c = z80_state.d;
    used_cycles += 4;
}


/* LD C, E */
static void z80_4b_ld_c_e (void)
{
    z80_state.c = z80_state.e;
    used_cycles += 4;
}


/* LD C, H */
static void z80_4c_ld_c_h (void)
{
    z80_state.c = z80_state.h;
    used_cycles += 4;
}


/* LD C, L */
static void z80_4d_ld_c_l (void)
{
    z80_state.c = z80_state.l;
    used_cycles += 4;
}


/* LD C, (HL) */
static void z80_4e_ld_c_hl (void)
{
    z80_state.c = memory_read (z80_state.hl);
    used_cycles += 7;
}


/* LD C, A */
static void z80_4f_ld_c_a (void)
{
    z80_state.c = z80_state.a;
    used_cycles += 4;
}


/* LD D, B */
static void z80_50_ld_d_b (void)
{
    z80_state.d = z80_state.b;
    used_cycles += 4;
}


/* LD D, C */
static void z80_51_ld_d_c (void)
{
    z80_state.d = z80_state.c;
    used_cycles += 4;
}


/* LD D, D */
static void z80_52_ld_d_d (void)
{
    used_cycles += 4;
}


/* LD D, E */
static void z80_53_ld_d_e (void)
{
    z80_state.d = z80_state.e;
    used_cycles += 4;
}


/* LD D, H */
static void z80_54_ld_d_h (void)
{
    z80_state.d = z80_state.h;
    used_cycles += 4;
}


/* LD D, L */
static void z80_55_ld_d_l (void)
{
    z80_state.d = z80_state.l;
    used_cycles += 4;
}


/* LD D, (HL) */
static void z80_56_ld_d_hl (void)
{
    z80_state.d = memory_read (z80_state.hl);
    used_cycles += 7;
}


/* LD D, A */
static void z80_57_ld_d_a (void)
{
    z80_state.d = z80_state.a;
    used_cycles += 4;
}


/* LD E, B */
static void z80_58_ld_e_b (void)
{
    z80_state.e = z80_state.b;
    used_cycles += 4;
}


/* LD E, C */
static void z80_59_ld_e_c (void)
{
    z80_state.e = z80_state.c;
    used_cycles += 4;
}


/* LD E, D */
static void z80_5a_ld_e_d (void)
{
    z80_state.e = z80_state.d;
    used_cycles += 4;
}


/* LD E, E */
static void z80_5b_ld_e_e (void)
{
    used_cycles += 4;
}


/* LD E, H */
static void z80_5c_ld_e_h (void)
{
    z80_state.e = z80_state.h;
    used_cycles += 4;
}


/* LD E, L */
static void z80_5d_ld_e_l (void)
{
    z80_state.e = z80_state.l;
    used_cycles += 4;
}


/* LD E, (HL) */
static void z80_5e_ld_e_hl (void)
{
    z80_state.e = memory_read (z80_state.hl);
    used_cycles += 7;
}


/* LD E, A */
static void z80_5f_ld_e_a (void)
{
    z80_state.e = z80_state.a;
    used_cycles += 4;
}


/* LD H, B */
static void z80_60_ld_h_b (void)
{
    z80_state.h = z80_state.b;
    used_cycles += 4;
}


/* LD H, C */
static void z80_61_ld_h_c (void)
{
    z80_state.h = z80_state.c;
    used_cycles += 4;
}


/* LD H, D */
static void z80_62_ld_h_d (void)
{
    z80_state.h = z80_state.d;
    used_cycles += 4;
}


/* LD H, E */
static void z80_63_ld_h_e (void)
{
    z80_state.h = z80_state.e;
    used_cycles += 4;
}


/* LD H, H */
static void z80_64_ld_h_h (void)
{
    used_cycles += 4;
}


/* LD H, L  */
static void z80_65_ld_h_l (void)
{
    z80_state.h = z80_state.l;
    used_cycles += 4;
}


/* LD H, (HL) */
static void z80_66_ld_h_hl (void)
{
    z80_state.h = memory_read (z80_state.hl);
    used_cycles += 7;
}


/* LD H, A */
static void z80_67_ld_h_a (void)
{
    z80_state.h = z80_state.a;
    used_cycles += 4;
}


/* LD L, B */
static void z80_68_ld_l_b (void)
{
    z80_state.l = z80_state.b;
    used_cycles += 4;
}


/* LD L, C */
static void z80_69_ld_l_c (void)
{
    z80_state.l = z80_state.c;
    used_cycles += 4;
}


/* LD L, D */
static void z80_6a_ld_l_d (void)
{
    z80_state.l = z80_state.d;
    used_cycles += 4;
}


/* LD L, E */
static void z80_6b_ld_l_e (void)
{
    z80_state.l = z80_state.e;
    used_cycles += 4;
}


/* LD L, H */
static void z80_6c_ld_l_h (void)
{
    z80_state.l = z80_state.h;
    used_cycles += 4;
}


/* LD L, L */
static void z80_6d_ld_l_l (void)
{
    used_cycles += 4;
}


/* LD L, (HL) */
static void z80_6e_ld_l_hl (void)
{
    z80_state.l = memory_read (z80_state.hl);
    used_cycles += 7;
}


/* LD L, A */
static void z80_6f_ld_l_a (void)
{
    z80_state.l = z80_state.a;
    used_cycles += 4;
}


/* LD (HL), B */
static void z80_70_ld_hl_b (void)
{
    memory_write (z80_state.hl, z80_state.b);
    used_cycles += 7;
}


/* LD (HL), C */
static void z80_71_ld_hl_c (void)
{
    memory_write (z80_state.hl, z80_state.c);
    used_cycles += 7;
}


/* LD (HL), D */
static void z80_72_ld_hl_d (void)
{
    memory_write (z80_state.hl, z80_state.d);
    used_cycles += 7;
}


/* LD (HL), E */
static void z80_73_ld_hl_e (void)
{
    memory_write (z80_state.hl, z80_state.e);
    used_cycles += 7;
}


/* LD (HL), H */
static void z80_74_ld_hl_h (void)
{
    memory_write (z80_state.hl, z80_state.h);
    used_cycles += 7;
}


/* LD (HL), L */
static void z80_75_ld_hl_l (void)
{
    memory_write (z80_state.hl, z80_state.l);
    used_cycles += 7;
}


/* HALT */
static void z80_76_halt (void)
{
    z80_state.pc--;
    z80_state.halt = true;
    used_cycles += 4;
}


/* LD (HL), A */
static void z80_77_ld_hl_a (void)
{
    memory_write (z80_state.hl, z80_state.a);
    used_cycles += 7;
}


/* LD A, B */
static void z80_78_ld_a_b (void)
{
    z80_state.a = z80_state.b;
    used_cycles += 4;
}


/* LD A, C */
static void z80_79_ld_a_c (void)
{
    z80_state.a = z80_state.c;
    used_cycles += 4;
}


/* LD A, D */
static void z80_7a_ld_a_d (void)
{
    z80_state.a = z80_state.d;
    used_cycles += 4;
}


/* LD A, E */
static void z80_7b_ld_a_e (void)
{
    z80_state.a = z80_state.e;
    used_cycles += 4;
}


/* LD A, H */
static void z80_7c_ld_a_h (void)
{
    z80_state.a = z80_state.h;
    used_cycles += 4;
}


/* LD A, L */
static void z80_7d_ld_a_l (void)
{
    z80_state.a = z80_state.l;
    used_cycles += 4;
}


/* LD A, (HL) */
static void z80_7e_ld_a_hl (void)
{
    z80_state.a = memory_read (z80_state.hl);
    used_cycles += 7;
}


/* LD A, A */
static void z80_7f_ld_a_a (void)
{
    used_cycles += 4;
}


/* ADD A, B */
static void z80_80_add_a_b (void)
{
    SET_FLAGS_ADD (z80_state.a, z80_state.b);
    z80_state.a += z80_state.b;
    used_cycles += 4;
}

/* ADD A, C */
static void z80_81_add_a_c (void)
{
    SET_FLAGS_ADD (z80_state.a, z80_state.c);
    z80_state.a += z80_state.c;
    used_cycles += 4;
}


/* ADD A, D */
static void z80_82_add_a_d (void)
{
    SET_FLAGS_ADD (z80_state.a, z80_state.d);
    z80_state.a += z80_state.d;
    used_cycles += 4;
}


/* ADD A, E */
static void z80_83_add_a_e (void)
{
    SET_FLAGS_ADD (z80_state.a, z80_state.e);
    z80_state.a += z80_state.e;
    used_cycles += 4;
}

/* ADD A, H */
static void z80_84_add_a_h (void)
{
    SET_FLAGS_ADD (z80_state.a, z80_state.h);
    z80_state.a += z80_state.h;
    used_cycles += 4;
}


/* ADD A, L */
static void z80_85_add_a_l (void)
{
    SET_FLAGS_ADD (z80_state.a, z80_state.l);
    z80_state.a += z80_state.l;
    used_cycles += 4;
}


/* ADD A, (HL) */
static void z80_86_add_a_hl (void)
{
    uint8_t value = memory_read (z80_state.hl);
    SET_FLAGS_ADD (z80_state.a, value);
    z80_state.a += value;
    used_cycles += 7;
}


/* ADD A, A */
static void z80_87_add_a_a (void)
{
    SET_FLAGS_ADD (z80_state.a, z80_state.a);
    z80_state.a += z80_state.a;
    used_cycles += 4;
}


/* ADC A, B */
static void z80_88_adc_a_b (void)
{
    uint8_t temp = z80_state.b + z80_state.flag_carry;
    SET_FLAGS_ADC (z80_state.b);
    z80_state.a += temp;
    used_cycles += 4;
}


/* ADC A, C */
static void z80_89_adc_a_c (void)
{
    uint8_t temp = z80_state.c + z80_state.flag_carry;
    SET_FLAGS_ADC (z80_state.c);
    z80_state.a += temp;
    used_cycles += 4;
}


/* ADC A, D */
static void z80_8a_adc_a_d (void)
{
    uint8_t temp = z80_state.d + z80_state.flag_carry;
    SET_FLAGS_ADC (z80_state.d);
    z80_state.a += temp;
    used_cycles += 4;
}


/* ADC A, E */
static void z80_8b_adc_a_e (void)
{
    uint8_t temp = z80_state.e + z80_state.flag_carry;
    SET_FLAGS_ADC (z80_state.e);
    z80_state.a += temp;
    used_cycles += 4;
}


/* ADC A, H */
static void z80_8c_adc_a_h (void)
{
    uint8_t temp = z80_state.h + z80_state.flag_carry;
    SET_FLAGS_ADC (z80_state.h);
    z80_state.a += temp;
    used_cycles += 4;
}


/* ADC A, L */
static void z80_8d_adc_a_l (void)
{
    uint8_t temp = z80_state.l + z80_state.flag_carry;
    SET_FLAGS_ADC (z80_state.l);
    z80_state.a += temp;
    used_cycles += 4;
}


/* ADC A, (HL) */
static void z80_8e_adc_a_hl (void)
{
    uint8_t value = memory_read (z80_state.hl);
    uint8_t temp = value + z80_state.flag_carry;
    SET_FLAGS_ADC (value);
    z80_state.a += temp;
    used_cycles += 7;
}


/* ADC A, A */
static void z80_8f_adc_a_a (void)
{
    uint8_t temp = z80_state.a + z80_state.flag_carry;
    SET_FLAGS_ADC (z80_state.a);
    z80_state.a += temp;
    used_cycles += 4;
}


/* SUB A, B */
static void z80_90_sub_a_b (void)
{
    SET_FLAGS_SUB (z80_state.a, z80_state.b);
    z80_state.a -= z80_state.b;
    used_cycles += 4;
}


/* SUB A, C */
static void z80_91_sub_a_c (void)
{
    SET_FLAGS_SUB (z80_state.a, z80_state.c);
    z80_state.a -= z80_state.c;
    used_cycles += 4;
}


/* SUB A, D */
static void z80_92_sub_a_d (void)
{
    SET_FLAGS_SUB (z80_state.a, z80_state.d);
    z80_state.a -= z80_state.d;
    used_cycles += 4;
}


/* SUB A, E */
static void z80_93_sub_a_e (void)
{
    SET_FLAGS_SUB (z80_state.a, z80_state.e);
    z80_state.a -= z80_state.e;
    used_cycles += 4;
}


/* SUB A, H */
static void z80_94_sub_a_h (void)
{
    SET_FLAGS_SUB (z80_state.a, z80_state.h);
    z80_state.a -= z80_state.h;
    used_cycles += 4;
}


/* SUB A, L */
static void z80_95_sub_a_l (void)
{
    SET_FLAGS_SUB (z80_state.a, z80_state.l);
    z80_state.a -= z80_state.l;
    used_cycles += 4;
}


/* SUB A, (HL) */
static void z80_96_sub_a_hl (void)
{
    uint8_t temp = memory_read (z80_state.hl);
    SET_FLAGS_SUB (z80_state.a, temp);
    z80_state.a -= temp;
    used_cycles += 7;
}


/* SUB A, A */
static void z80_97_sub_a_a (void)
{
    SET_FLAGS_SUB (z80_state.a, z80_state.a);
    z80_state.a -= z80_state.a;
    used_cycles += 4;
}


/* SBC A, B */
static void z80_98_sbc_a_b (void)
{
    uint8_t temp = z80_state.b + z80_state.flag_carry;
    SET_FLAGS_SBC (z80_state.b);
    z80_state.a -= temp;
    used_cycles += 4;
}


/* SBC A, C */
static void z80_99_sbc_a_c (void)
{
    uint8_t temp = z80_state.c + z80_state.flag_carry;
    SET_FLAGS_SBC (z80_state.c);
    z80_state.a -= temp;
    used_cycles += 4;
}


/* SBC A, D */
static void z80_9a_sbc_a_d (void)
{
    uint8_t temp = z80_state.d + z80_state.flag_carry;
    SET_FLAGS_SBC (z80_state.d);
    z80_state.a -= temp;
    used_cycles += 4;
}


/* SBC A, E */
static void z80_9b_sbc_a_e (void)
{
    uint8_t temp = z80_state.e + z80_state.flag_carry;
    SET_FLAGS_SBC (z80_state.e);
    z80_state.a -= temp;
    used_cycles += 4;
}


/* SBC A, H */
static void z80_9c_sbc_a_h (void)
{
    uint8_t temp = z80_state.h + z80_state.flag_carry;
    SET_FLAGS_SBC (z80_state.h);
    z80_state.a -= temp;
    used_cycles += 4;
}


/* SBC A, L */
static void z80_9d_sbc_a_l (void)
{
    uint8_t temp = z80_state.l + z80_state.flag_carry;
    SET_FLAGS_SBC (z80_state.l);
    z80_state.a -= temp;
    used_cycles += 4;
}


/* SBC A, (HL) */
static void z80_9e_sbc_a_hl (void)
{
    uint8_t value = memory_read (z80_state.hl);
    uint8_t temp = value + z80_state.flag_carry;
    SET_FLAGS_SBC (value);
    z80_state.a -= temp;
    used_cycles += 7;
}


/* SBC A, A */
static void z80_9f_sbc_a_a (void)
{
    uint8_t temp = z80_state.a + z80_state.flag_carry;
    SET_FLAGS_SBC (z80_state.a);
    z80_state.a -= temp;
    used_cycles += 4;
}

/* AND A, B */
static void z80_a0_and_a_b (void)
{
    z80_state.a &= z80_state.b;
    SET_FLAGS_AND;
    used_cycles += 4;
}


/* AND A, C */
static void z80_a1_and_a_c (void)
{
    z80_state.a &= z80_state.c;
    SET_FLAGS_AND;
    used_cycles += 4;
}


/* AND A, D */
static void z80_a2_and_a_d (void)
{
    z80_state.a &= z80_state.d;
    SET_FLAGS_AND;
    used_cycles += 4;
}


/* AND A, E */
static void z80_a3_and_a_e (void)
{
    z80_state.a &= z80_state.e;
    SET_FLAGS_AND;
    used_cycles += 4;
}


/* AND A, H */
static void z80_a4_and_a_h (void)
{
    z80_state.a &= z80_state.h;
    SET_FLAGS_AND;
    used_cycles += 4;
}


/* AND A, L */
static void z80_a5_and_a_l (void)
{
    z80_state.a &= z80_state.l;
    SET_FLAGS_AND;
    used_cycles += 4;
}


/* AND A, (HL) */
static void z80_a6_and_a_hl (void)
{
    z80_state.a &= memory_read (z80_state.hl);
    SET_FLAGS_AND;
    used_cycles += 7;
}


/* AND A, A */
static void z80_a7_and_a_a (void)
{
    SET_FLAGS_AND;
    used_cycles += 4;
}


/* XOR A, B */
static void z80_a8_xor_a_b (void)
{
    z80_state.a ^= z80_state.b;
    SET_FLAGS_OR_XOR;
    used_cycles += 4;
}


/* XOR A, C */
static void z80_a9_xor_a_c (void)
{
    z80_state.a ^= z80_state.c;
    SET_FLAGS_OR_XOR;
    used_cycles += 4;
}


/* XOR A, D */
static void z80_aa_xor_a_d (void)
{
    z80_state.a ^= z80_state.d;
    SET_FLAGS_OR_XOR;
    used_cycles += 4;
}


/* XOR A, E */
static void z80_ab_xor_a_e (void)
{
    z80_state.a ^= z80_state.e;
    SET_FLAGS_OR_XOR;
    used_cycles += 4;
}


/* XOR A, H */
static void z80_ac_xor_a_h (void)
{
    z80_state.a ^= z80_state.h;
    SET_FLAGS_OR_XOR;
    used_cycles += 4;
}


/* XOR A, L */
static void z80_ad_xor_a_l (void)
{
    z80_state.a ^= z80_state.l;
    SET_FLAGS_OR_XOR;
    used_cycles += 4;
}


/* XOR A, (HL) */
static void z80_ae_xor_a_hl (void)
{
    z80_state.a ^= memory_read (z80_state.hl);
    SET_FLAGS_OR_XOR;
    used_cycles += 7;
}


/* XOR A, A */
static void z80_af_xor_a_a (void)
{
    z80_state.a ^= z80_state.a;
    SET_FLAGS_OR_XOR;
    used_cycles += 4;
}


/* OR A, B */
static void z80_b0_or_a_b (void)
{
    z80_state.a |= z80_state.b;
    SET_FLAGS_OR_XOR;
    used_cycles += 4;
}


/* OR A, C */
static void z80_b1_or_a_c (void)
{
    z80_state.a |= z80_state.c;
    SET_FLAGS_OR_XOR;
    used_cycles += 4;
}


/* OR A, D */
static void z80_b2_or_a_d (void)
{
    z80_state.a |= z80_state.d;
    SET_FLAGS_OR_XOR;
    used_cycles += 4;
}


/* OR A, E */
static void z80_b3_or_a_e (void)
{
    z80_state.a |= z80_state.e;
    SET_FLAGS_OR_XOR;
    used_cycles += 4;
}


/* OR A, H */
static void z80_b4_or_a_h (void)
{
    z80_state.a |= z80_state.h;
    SET_FLAGS_OR_XOR;
    used_cycles += 4;
}


/* OR A, L */
static void z80_b5_or_a_l (void)
{
    z80_state.a |= z80_state.l;
    SET_FLAGS_OR_XOR;
    used_cycles += 4;
}


/* OR A, (HL) */
static void z80_b6_or_a_hl (void)
{
    z80_state.a |= memory_read (z80_state.hl);
    SET_FLAGS_OR_XOR;
    used_cycles += 7;
}


/* OR A, A */
static void z80_b7_or_a_a (void)
{
    SET_FLAGS_OR_XOR;
    used_cycles += 4;
}


/* CP A, B */
static void z80_b8_cp_a_b (void)
{
    SET_FLAGS_SUB (z80_state.a, z80_state.b);
    used_cycles += 4;
}


/* CP A, C */
static void z80_b9_cp_a_c (void)
{
    SET_FLAGS_SUB (z80_state.a, z80_state.c);
    used_cycles += 4;
}


/* CP A, D */
static void z80_ba_cp_a_d (void)
{
    SET_FLAGS_SUB (z80_state.a, z80_state.d);
    used_cycles += 4;
}


/* CP A, E */
static void z80_bb_cp_a_e (void)
{
    SET_FLAGS_SUB (z80_state.a, z80_state.e);
    used_cycles += 4;
}


/* CP A, H */
static void z80_bc_cp_a_h (void)
{
    SET_FLAGS_SUB (z80_state.a, z80_state.h);
    used_cycles += 4;
}


/* CP A, L */
static void z80_bd_cp_a_l (void)
{
    SET_FLAGS_SUB (z80_state.a, z80_state.l);
    used_cycles += 4;
}

/* CP A, (HL) */
static void z80_be_cp_a_hl (void)
{
    uint8_t value = memory_read (z80_state.hl);
    SET_FLAGS_SUB (z80_state.a, value);
    used_cycles += 7;
}

/* CP A, A */
static void z80_bf_cp_a_a (void)
{
    SET_FLAGS_SUB (z80_state.a, z80_state.a);
    used_cycles += 4;
}


/* RET NZ */
static void z80_c0_ret_nz (void)
{
    if (z80_state.flag_zero)
    {
        used_cycles += 5;
    }
    else
    {
        z80_state.pc_l = memory_read (z80_state.sp++);
        z80_state.pc_h = memory_read (z80_state.sp++);
        used_cycles += 11;
    }
}


/* POP BC */
static void z80_c1_pop_bc (void)
{
    z80_state.c = memory_read (z80_state.sp++);
    z80_state.b = memory_read (z80_state.sp++);
    used_cycles += 10;
}


/* JP NZ, ** */
static void z80_c2_jp_nz_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    if (!z80_state.flag_zero)
    {
        z80_state.pc = addr.w;
    }
    used_cycles += 10;
}


/* JP ** */
static void z80_c3_jp_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);
    z80_state.pc = addr.w;
    used_cycles += 10;
}


/* CALL NZ, ** */
static void z80_c4_call_nz_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    if (z80_state.flag_zero)
    {
        used_cycles += 10;
    }
    else
    {
        memory_write (--z80_state.sp, z80_state.pc_h);
        memory_write (--z80_state.sp, z80_state.pc_l);
        z80_state.pc = addr.w;
        used_cycles += 17;
    }
}


/* PUSH BC */
static void z80_c5_push_bc (void)
{
    memory_write (--z80_state.sp, z80_state.b);
    memory_write (--z80_state.sp, z80_state.c);
    used_cycles += 11;
}


/* ADD A, * */
static void z80_c6_add_a_x (void)
{
    uint8_t imm = memory_read (z80_state.pc++);
    /* ADD A,*    */
    SET_FLAGS_ADD (z80_state.a, imm);
    z80_state.a += imm;
    used_cycles += 7;
}


/* RST 00h */
static void z80_c7_rst_00 (void)
{
    /* RST 00h    */
    memory_write (--z80_state.sp, z80_state.pc_h);
    memory_write (--z80_state.sp, z80_state.pc_l);
    z80_state.pc = 0x0000;
    used_cycles += 11;
}


/* RET Z */
static void z80_c8_ret_z (void)
{
    /* RET Z      */
    if (z80_state.flag_zero)
    {
        z80_state.pc_l = memory_read (z80_state.sp++);
        z80_state.pc_h = memory_read (z80_state.sp++);
        used_cycles += 11;
    }
    else
    {
        used_cycles += 5;
    }
}


/* RET */
static void z80_c9_ret (void)
{
    z80_state.pc_l = memory_read (z80_state.sp++);
    z80_state.pc_h = memory_read (z80_state.sp++);
    used_cycles += 10;
}


/* JP Z, ** */
static void z80_ca_jp_z_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    if (z80_state.flag_zero)
    {
        z80_state.pc = addr.w;
    }
    used_cycles += 10;
}


/* BIT PREFIX */
static void z80_cb_prefix (void)
{
    uint8_t instruction = memory_read (z80_state.pc++);

    switch (instruction & 0x07)
    {
        case 0x00:
            z80_state.b = z80_cb_instruction [instruction >> 3] (z80_state.b);
            used_cycles += 8;
            break;

        case 0x01:
            z80_state.c = z80_cb_instruction [instruction >> 3] (z80_state.c);
            used_cycles += 8;
            break;

        case 0x02:
            z80_state.d = z80_cb_instruction [instruction >> 3] (z80_state.d);
            used_cycles += 8;
            break;

        case 0x03:
            z80_state.e = z80_cb_instruction [instruction >> 3] (z80_state.e);
            used_cycles += 8;
            break;

        case 0x04:
            z80_state.h = z80_cb_instruction [instruction >> 3] (z80_state.h);
            used_cycles += 8;
            break;

        case 0x05:
            z80_state.l = z80_cb_instruction [instruction >> 3] (z80_state.l);
            used_cycles += 8;
            break;

        case 0x06:
            if ((instruction & 0xc0) == 0x40)
            {
                /* The BIT instruction is read-only */
                z80_cb_instruction [instruction >> 3] (memory_read (z80_state.hl));
                used_cycles += 12;
            }
            else
            {
                memory_write (z80_state.hl, z80_cb_instruction [instruction >> 3] (memory_read (z80_state.hl)));
                used_cycles += 15;
            }
            break;

        case 0x07:
            z80_state.a = z80_cb_instruction [instruction >> 3] (z80_state.a);
            used_cycles += 8;
            break;
    }
}


/* CALL Z, ** */
static void z80_cc_call_z_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    if (z80_state.flag_zero)
    {
        memory_write (--z80_state.sp, z80_state.pc_h);
        memory_write (--z80_state.sp, z80_state.pc_l);
        z80_state.pc = addr.w;
        used_cycles += 17;
    }
    else
    {
        used_cycles += 10;
    }
}


/* CALL ** */
static void z80_cd_call_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);
    memory_write (--z80_state.sp, z80_state.pc_h);
    memory_write (--z80_state.sp, z80_state.pc_l);
    z80_state.pc = addr.w;
    used_cycles += 17;
}


/* ADC A, * */
static void z80_ce_adc_a_x (void)
{
    uint8_t imm = memory_read (z80_state.pc++);
    uint8_t temp = imm + z80_state.flag_carry;
    SET_FLAGS_ADC (imm);
    z80_state.a += temp;
    used_cycles += 7;
}


/* RST 08h */
static void z80_cf_rst_08 (void)
{
    memory_write (--z80_state.sp, z80_state.pc_h);
    memory_write (--z80_state.sp, z80_state.pc_l);
    z80_state.pc = 0x0008;
    used_cycles += 11;
}


/* RET NC */
static void z80_d0_ret_nc (void)
{
    if (z80_state.flag_carry)
    {
        used_cycles += 5;
    }
    else
    {
        z80_state.pc_l = memory_read (z80_state.sp++);
        z80_state.pc_h = memory_read (z80_state.sp++);
        used_cycles += 11;
    }
}


/* POP DE */
static void z80_d1_pop_de (void)
{
    z80_state.e = memory_read (z80_state.sp++);
    z80_state.d = memory_read (z80_state.sp++);
    used_cycles += 10;
}


/* JP NC, ** */
static void z80_d2_jp_nc_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    if (!z80_state.flag_carry)
    {
        z80_state.pc = addr.w;
    }
    used_cycles += 10;
}


/* OUT (*), A */
static void z80_d3_out_x_a (void)
{
    io_write (memory_read (z80_state.pc++), z80_state.a);
    used_cycles += 11;
}


/* CALL NC, ** */
static void z80_d4_call_nc_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    /* CALL NC,** */
    if (z80_state.flag_carry)
    {
        used_cycles += 10;
    }
    else
    {
        memory_write (--z80_state.sp, z80_state.pc_h);
        memory_write (--z80_state.sp, z80_state.pc_l);
        z80_state.pc = addr.w;
        used_cycles += 17;
    }
}


/* PUSH DE */
static void z80_d5_push_de (void)
{
    memory_write (--z80_state.sp, z80_state.d);
    memory_write (--z80_state.sp, z80_state.e);
    used_cycles += 11;
}


/* SUB A, * */
static void z80_d6_sub_a_x (void)
{
    uint8_t imm = memory_read (z80_state.pc++);
    SET_FLAGS_SUB (z80_state.a, imm);
    z80_state.a -= imm;
    used_cycles += 7;
}


/* RST 10h */
static void z80_d7_rst_10 (void)
{
    memory_write (--z80_state.sp, z80_state.pc_h);
    memory_write (--z80_state.sp, z80_state.pc_l);
    z80_state.pc = 0x10;
    used_cycles += 11;
}


/* RET C */
static void z80_d8_ret_c (void)
{
    if (z80_state.flag_carry)
    {
        z80_state.pc_l = memory_read (z80_state.sp++);
        z80_state.pc_h = memory_read (z80_state.sp++);
        used_cycles += 11;
    }
    else
    {
        used_cycles += 5;
    }
}


/* EXX */
static void z80_d9_exx (void)
{
    SWAP (uint16_t, z80_state.bc, z80_state.bc_alt);
    SWAP (uint16_t, z80_state.de, z80_state.de_alt);
    SWAP (uint16_t, z80_state.hl, z80_state.hl_alt);
    used_cycles += 4;
}


/* JP C, ** */
static void z80_da_jp_c_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    if (z80_state.flag_carry)
    {
        z80_state.pc = addr.w;
    }
    used_cycles += 10;
}


/* IN A, (*) */
static void z80_db_in_a_x (void)
{
    z80_state.a = io_read (memory_read (z80_state.pc++));
    used_cycles += 11;
}


/* CALL C, ** */
static void z80_dc_call_c_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    if (z80_state.flag_carry)
    {
        memory_write (--z80_state.sp, z80_state.pc_h);
        memory_write (--z80_state.sp, z80_state.pc_l);
        z80_state.pc = addr.w;
        used_cycles += 17;
    }
    else
    {
        used_cycles += 10;
    }
}


/* IX PREFIX */
static void z80_dd_ix (void)
{
    /* Fetch */
    uint8_t instruction = memory_read (z80_state.pc++);

    /* Execute */
    z80_state.ix = z80_ix_iy_instruction [instruction] (z80_state.ix);
}


/* SBC A, * */
static void z80_de_sbc_a_x (void)
{
    uint8_t imm = memory_read (z80_state.pc++);
    uint8_t temp = imm + z80_state.flag_carry;
    SET_FLAGS_SBC (imm);
    z80_state.a -= temp;
    used_cycles += 7;
}


/* RST 18h */
static void z80_df_rst_18 (void)
{
    memory_write (--z80_state.sp, z80_state.pc_h);
    memory_write (--z80_state.sp, z80_state.pc_l);
    z80_state.pc = 0x0018;
    used_cycles += 11;
}


/* RET PO */
static void z80_e0_ret_po (void)
{
    if (z80_state.flag_parity_overflow)
    {
        used_cycles += 5;
    }
    else
    {
        z80_state.pc_l = memory_read (z80_state.sp++);
        z80_state.pc_h = memory_read (z80_state.sp++);
        used_cycles += 11;
    }
}


/* POP HL */
static void z80_e1_pop_hl (void)
{
    z80_state.l = memory_read (z80_state.sp++);
    z80_state.h = memory_read (z80_state.sp++);
    used_cycles += 10;
}


/* JP PO, ** */
static void z80_e2_jp_po_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    if (!z80_state.flag_parity_overflow)
    {
        z80_state.pc = addr.w;
    }
    used_cycles += 10;
}


/* EX (SP), HL */
static void z80_e3_ex_sp_hl (void)
{
    uint8_t temp = z80_state.l;
    z80_state.l = memory_read (z80_state.sp);
    memory_write (z80_state.sp, temp);
    temp = z80_state.h;
    z80_state.h = memory_read (z80_state.sp + 1);
    memory_write (z80_state.sp + 1, temp);
    used_cycles += 19;
}


/* CALL PO, ** */
static void z80_e4_call_po_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    if (z80_state.flag_parity_overflow)
    {
        used_cycles += 10;
    }
    else
    {
        memory_write (--z80_state.sp, z80_state.pc_h);
        memory_write (--z80_state.sp, z80_state.pc_l);
        z80_state.pc = addr.w;
        used_cycles += 17;
    }
}


/* PUSH HL */
static void z80_e5_push_hl (void)
{
    memory_write (--z80_state.sp, z80_state.h);
    memory_write (--z80_state.sp, z80_state.l);
    used_cycles += 11;
}


/* AND A, * */
static void z80_e6_and_a_x (void)
{
    z80_state.a &= memory_read (z80_state.pc++);
    SET_FLAGS_AND;
    used_cycles += 7;
}


/* RST 20h */
static void z80_e7_rst_20 (void)
{
    memory_write (--z80_state.sp, z80_state.pc_h);
    memory_write (--z80_state.sp, z80_state.pc_l);
    z80_state.pc = 0x0020;
    used_cycles += 11;
}


/* RET PE */
static void z80_e8_ret_pe (void)
{
    if (z80_state.flag_parity_overflow)
    {
        z80_state.pc_l = memory_read (z80_state.sp++);
        z80_state.pc_h = memory_read (z80_state.sp++);
        used_cycles += 11;
    }
    else
    {
        used_cycles += 5;
    }
}


/* JP (HL) */
static void z80_e9_jp_hl (void)
{
    z80_state.pc = z80_state.hl;
    used_cycles += 4;
}


/* JP PE, ** */
static void z80_ea_jp_pe_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    if (z80_state.flag_parity_overflow)
    {
        z80_state.pc = addr.w;
    }
    used_cycles += 10;
}


/* EX DE, HL */
static void z80_eb_ex_de_hl (void)
{
    SWAP (uint16_t, z80_state.de, z80_state.hl);
    used_cycles += 4;
}


/* CALL PE, ** */
static void z80_ec_call_pe_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    if (z80_state.flag_parity_overflow)
    {
        memory_write (--z80_state.sp, z80_state.pc_h);
        memory_write (--z80_state.sp, z80_state.pc_l);
        z80_state.pc = addr.w;
        used_cycles += 17;
    }
    else
    {
        used_cycles += 10;
    }
}


/* EXTENDED PREFIX */
static void z80_ed_prefix (void)
{
    /* Fetch */
    uint8_t instruction = memory_read (z80_state.pc++);

    /* Execute */
    z80_ed_instruction [instruction] ();
}


/* XOR A, * */
static void z80_ee_xor_a_x (void)
{
    z80_state.a ^= memory_read (z80_state.pc++);
    SET_FLAGS_OR_XOR;
    used_cycles += 7;
}


/* RST 28h */
static void z80_ef_rst_28 (void)
{
    memory_write (--z80_state.sp, z80_state.pc_h);
    memory_write (--z80_state.sp, z80_state.pc_l);
    z80_state.pc = 0x0028;
    used_cycles += 11;
}


/* RET P */
static void z80_f0_ret_p (void)
{
    if (z80_state.flag_sign)
    {
        used_cycles += 5;
    }
    else
    {
        z80_state.pc_l = memory_read (z80_state.sp++);
        z80_state.pc_h = memory_read (z80_state.sp++);
        used_cycles += 11;
    }
}


/* POP AF */
static void z80_f1_pop_af (void)
{
    z80_state.f = memory_read (z80_state.sp++);
    z80_state.a = memory_read (z80_state.sp++);
    used_cycles += 10;
}


/* JP P, ** */
static void z80_f2_jp_p_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    if (!z80_state.flag_sign)
    {
        z80_state.pc = addr.w;
    }
    used_cycles += 10;
}


/* DI */
static void z80_f3_di (void)
{
    z80_state.iff1 = false;
    z80_state.iff2 = false;
    used_cycles += 4;
}


/* CALL P,** */
static void z80_f4_call_p_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    if (z80_state.flag_sign)
    {
        used_cycles += 10;
    }
    else
    {
        memory_write (--z80_state.sp, z80_state.pc_h);
        memory_write (--z80_state.sp, z80_state.pc_l);
        z80_state.pc = addr.w;
        used_cycles += 17;
    }
}


/* PUSH AF */
static void z80_f5_push_af (void)
{
    memory_write (--z80_state.sp, z80_state.a);
    memory_write (--z80_state.sp, z80_state.f);
    used_cycles += 11;
}


/* OR A, * */
static void z80_f6_or_a_x (void)
{
    z80_state.a |= memory_read (z80_state.pc++);
    SET_FLAGS_OR_XOR;
    used_cycles += 7;
}


/* RST 30h */
static void z80_f7_rst_30 (void)
{
    memory_write (--z80_state.sp, z80_state.pc_h);
    memory_write (--z80_state.sp, z80_state.pc_l);
    z80_state.pc = 0x0030;
    used_cycles += 11;
}


/* RET M */
static void z80_f8_ret_m (void)
{
    if (z80_state.flag_sign)
    {
        z80_state.pc_l = memory_read (z80_state.sp++);
        z80_state.pc_h = memory_read (z80_state.sp++);
        used_cycles += 11;
    }
    else
    {
        used_cycles += 5;
    }
}


/* LD SP, HL */
static void z80_f9_ld_sp_hl (void)
{
    z80_state.sp = z80_state.hl;
    used_cycles += 6;
}


/* JP M, ** */
static void z80_fa_jp_m_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    if (z80_state.flag_sign)
    {
        z80_state.pc = addr.w;
    }
    used_cycles += 10;
}


/* EI */
static void z80_fb_ei (void)
{
    z80_state.iff1 = true;
    z80_state.iff2 = true;
    z80_state.wait_after_ei = true;
    used_cycles += 4;
}


/* CALL M, ** */
static void z80_fc_call_m_xx (void)
{
    uint16_t_Split addr;
    addr.l = memory_read (z80_state.pc++);
    addr.h = memory_read (z80_state.pc++);

    if (z80_state.flag_sign)
    {
        memory_write (--z80_state.sp, z80_state.pc_h);
        memory_write (--z80_state.sp, z80_state.pc_l);
        z80_state.pc = addr.w;
        used_cycles += 17;
    }
    else
    {
        used_cycles += 10;
    }
}


/* IY PREFIX */
static void z80_fd_prefix (void)
{
    /* Fetch */
    uint8_t instruction = memory_read (z80_state.pc++);

    /* Execute */
    z80_state.iy = z80_ix_iy_instruction [instruction] (z80_state.iy);
}


/* CP A, * */
static void z80_fe_cp_a_x (void)
{
    uint8_t imm = memory_read (z80_state.pc++);
    SET_FLAGS_SUB (z80_state.a, imm);
    used_cycles += 7;
}


/* RST 38h */
static void z80_ff_rst_38 (void)
{
    memory_write (--z80_state.sp, z80_state.pc_h);
    memory_write (--z80_state.sp, z80_state.pc_l);
    z80_state.pc = 0x0038;
    used_cycles += 11;
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
    z80_state.r = (z80_state.r & 0x80) |((z80_state.r + 1) & 0x7f);

    /* Fetch */
    instruction = memory_read (z80_state.pc++);

    /* Execute */
    z80_instruction [instruction] ();
}


/*
 * Simulate the Z80 for the specified number of clock cycles.
 */
void z80_run_cycles (uint64_t cycles)
{
    cycles += z80_state.excess_cycles;

    /* For now, we only run an instruction if we have
     * enough cycles to run any instruction with following interrupt */
    for (used_cycles = 0; cycles > 34; cycles -= used_cycles, z80_cycle += used_cycles)
    {
        used_cycles = 0;

        if (z80_state.halt)
        {
            /* NOP */
            used_cycles += 4;
        }
        else
        {
            z80_run_instruction ();
        }

        /* Check for interrupts */
        if (z80_state.wait_after_ei)
        {
            z80_state.wait_after_ei = false;
        }
        else
        {
            /* First, check for a non-maskable interrupt (edge-triggered) */
            static bool nmi_previous = 0;
            bool nmi = state.get_nmi ();
            if (nmi && nmi_previous == 0)
            {
                if (z80_state.halt)
                {
                    z80_state.halt = false;
                    z80_state.pc += 1;
                }
                z80_state.iff1 = false;
                memory_write (--z80_state.sp, z80_state.pc_h);
                memory_write (--z80_state.sp, z80_state.pc_l);
                z80_state.pc = 0x66;
                used_cycles += 11;
            }
            nmi_previous = nmi;

            /* Then check for maskable interrupts */
            if (z80_state.iff1 && state.get_int ())
            {
                if (z80_state.halt)
                {
                    z80_state.halt = false;
                    z80_state.pc += 1;
                }

                z80_state.iff1 = false;
                z80_state.iff2 = false;

                switch (z80_state.im)
                {
                    case 1:
                        memory_write (--z80_state.sp, z80_state.pc_h);
                        memory_write (--z80_state.sp, z80_state.pc_l);
                        z80_state.pc = 0x38;
                        used_cycles += 13;
                        break;
                    default:
                        snprintf (state.error_buffer, 79, "Unsupported interrupt mode %d.", z80_state.im);
                        snepulator_error ("Z80 Error", state.error_buffer);
                        return;
                }
            }

        }

    }

    z80_state.excess_cycles = cycles;
}


/*
 * Export Z80 state.
 */
void z80_state_save (void)
{
    Z80_State z80_state_be = {
        .af =            htons (z80_state.af),
        .bc =            htons (z80_state.bc),
        .de =            htons (z80_state.de),
        .hl =            htons (z80_state.hl),
        .af_alt =        htons (z80_state.af_alt),
        .bc_alt =        htons (z80_state.bc_alt),
        .de_alt =        htons (z80_state.de_alt),
        .hl_alt =        htons (z80_state.hl_alt),
        .ir =            htons (z80_state.ir),
        .ix =            htons (z80_state.ix),
        .iy =            htons (z80_state.iy),
        .sp =            htons (z80_state.sp),
        .pc =            htons (z80_state.pc),
        .im =            z80_state.im,
        .iff1 =          z80_state.iff1,
        .iff2 =          z80_state.iff2,
        .wait_after_ei = z80_state.wait_after_ei,
        .halt =          z80_state.halt,
        .excess_cycles = htonl (z80_state.excess_cycles)
    };

    save_state_section_add (SECTION_ID_Z80, 1, sizeof (z80_state_be), &z80_state_be);
}


/*
 * Import Z80 state.
 */
void z80_state_load (uint32_t version, uint32_t size, void *data)
{
    Z80_State z80_state_be;

    if (size == sizeof (z80_state_be))
    {
        memcpy (&z80_state_be, data, sizeof (z80_state_be));

        z80_state.af =            ntohs (z80_state_be.af);
        z80_state.bc =            ntohs (z80_state_be.bc);
        z80_state.de =            ntohs (z80_state_be.de);
        z80_state.hl =            ntohs (z80_state_be.hl);
        z80_state.af_alt =        ntohs (z80_state_be.af_alt);
        z80_state.bc_alt =        ntohs (z80_state_be.bc_alt);
        z80_state.de_alt =        ntohs (z80_state_be.de_alt);
        z80_state.hl_alt =        ntohs (z80_state_be.hl_alt);
        z80_state.ir =            ntohs (z80_state_be.ir);
        z80_state.ix =            ntohs (z80_state_be.ix);
        z80_state.iy =            ntohs (z80_state_be.iy);
        z80_state.sp =            ntohs (z80_state_be.sp);
        z80_state.pc =            ntohs (z80_state_be.pc);
        z80_state.im =            z80_state_be.im;
        z80_state.iff1 =          z80_state_be.iff1;
        z80_state.iff2 =          z80_state_be.iff2;
        z80_state.wait_after_ei = z80_state_be.wait_after_ei;
        z80_state.halt =          z80_state_be.halt;
        z80_state.excess_cycles = ntohl (z80_state_be.excess_cycles);
    }
    else
    {
        snepulator_error ("Error", "Save-state contains incorrect Z80 size");
    }
}
