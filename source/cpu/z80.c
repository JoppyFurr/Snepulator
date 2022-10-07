#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <arpa/inet.h>

#include "../snepulator_types.h"
#include "../snepulator.h"
#include "../save_state.h"
#include "z80.h"
#include "z80_names.h"

extern Snepulator_State state;

#define SWAP(TYPE, X, Y) { TYPE tmp = X; X = Y; Y = tmp; }

static void (*z80_instruction [256]) (Z80_Context *);

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
 * Create the Z80 context with power-on defaults.
 */
Z80_Context *z80_init (void *parent,
                       uint8_t (* memory_read) (void *, uint16_t),
                       void    (* memory_write)(void *, uint16_t, uint8_t),
                       uint8_t (* io_read)     (void *, uint8_t),
                       void    (* io_write)    (void *, uint8_t, uint8_t),
                       bool    (* get_int)     (void *),
                       bool    (* get_nmi)     (void *))
{
    Z80_Context *context;

    context = calloc (1, sizeof (Z80_Context));
    if (context == NULL)
    {
        snepulator_error ("Error", "Unable to allocate memory for Z80_Context");
        return NULL;
    }

    context->parent       = parent;
    context->memory_read  = memory_read;
    context->memory_write = memory_write;
    context->io_read      = io_read;
    context->io_write     = io_write;
    context->get_int      = get_int;
    context->get_nmi      = get_nmi;

    context->state.af = 0xffff;
    context->state.sp = 0xffff;

    return context;
}


#define SET_FLAGS_AND { context->state.flag_carry = 0; \
                        context->state.flag_sub = 0; \
                        context->state.flag_parity_overflow = uint8_even_parity [context->state.a]; \
                        context->state.flag_half = 1; \
                        context->state.flag_zero = (context->state.a == 0x00); \
                        context->state.flag_sign = (context->state.a >> 7); }

#define SET_FLAGS_OR_XOR { context->state.flag_carry = 0; \
                           context->state.flag_sub = 0; \
                           context->state.flag_parity_overflow = uint8_even_parity [context->state.a]; \
                           context->state.flag_half = 0; \
                           context->state.flag_zero = (context->state.a == 0x00); \
                           context->state.flag_sign = (context->state.a >> 7); }

#define SET_FLAGS_ADD(X,Y) { context->state.flag_carry = (X + Y) >> 8; \
                             context->state.flag_sub = 0; \
                             context->state.flag_parity_overflow = (((int8_t) X + (int8_t) Y) > 127 || ((int8_t) X + (int8_t) Y) < -128); \
                             context->state.flag_half = ((X & 0x0f) + (Y & 0x0f)) >> 4; \
                             context->state.flag_zero = (((X + Y) & 0xff) == 0x00); \
                             context->state.flag_sign = (X + Y) >> 7; }

#define SET_FLAGS_SUB(X,Y) { context->state.flag_carry = (X - Y) >> 8; \
                             context->state.flag_sub = 1; \
                             context->state.flag_parity_overflow = (((int8_t) X - (int8_t) Y) > 127 || ((int8_t) X - (int8_t) Y) < -128); \
                             context->state.flag_half = ((X & 0x0f) - (Y & 0x0f)) >> 4; \
                             context->state.flag_zero = (X == Y); \
                             context->state.flag_sign = (X - Y) >> 7; }

#define SET_FLAGS_ADC(X) { context->state.flag_sub = 0; \
                           context->state.flag_parity_overflow = (((int8_t) context->state.a + (int8_t) X + context->state.flag_carry) > 127 || \
                                                                 ((int8_t) context->state.a + (int8_t) X + context->state.flag_carry) < -128); \
                           context->state.flag_half = ((context->state.a & 0x0f) + (X & 0x0f) + context->state.flag_carry) >> 4; \
                           context->state.flag_zero = (((context->state.a + X + context->state.flag_carry) & 0xff) == 0x00); \
                           context->state.flag_sign = (context->state.a + X + context->state.flag_carry) >> 7; \
                           context->state.flag_carry = (context->state.a + X + context->state.flag_carry) >> 8; }

#define SET_FLAGS_SBC(X) { context->state.flag_sub = 1; \
                           context->state.flag_parity_overflow = (((int8_t) context->state.a - (int8_t) X - context->state.flag_carry) > 127 || \
                                                                 ((int8_t) context->state.a - (int8_t) X - context->state.flag_carry) < -128); \
                           context->state.flag_half = ((context->state.a & 0x0f) - (X & 0x0f) - context->state.flag_carry) >> 4; \
                           context->state.flag_zero = (((context->state.a - X - context->state.flag_carry) & 0xff) == 0x00); \
                           context->state.flag_sign = (context->state.a - X - context->state.flag_carry) >> 7; \
                           context->state.flag_carry = (context->state.a - X - context->state.flag_carry) >> 8; }

#define SET_FLAGS_INC(X) { context->state.flag_sub = 0; \
                           context->state.flag_parity_overflow = (X == 0x80); \
                           context->state.flag_half = ((X & 0x0f) == 0x00); \
                           context->state.flag_zero = (X == 0x00); \
                           context->state.flag_sign = X >> 7; }

#define SET_FLAGS_DEC(X) { context->state.flag_sub = 1; \
                           context->state.flag_parity_overflow = (X == 0x7f); \
                           context->state.flag_half = ((X & 0x0f) == 0x0f); \
                           context->state.flag_zero = (X == 0x00); \
                           context->state.flag_sign = X >> 7; }

#define SET_FLAGS_ADD_16(X,Y) { context->state.flag_carry = (X + Y) >> 16; \
                                context->state.flag_sub = 0; \
                                context->state.flag_half = ((X & 0x0fff) + (Y & 0xfff)) >> 12; }

#define SET_FLAGS_ADC_16(X) { context->state.flag_sub = 0; \
                              context->state.flag_parity_overflow = (((int16_t) context->state.hl + (int16_t) X + context->state.flag_carry) >  32767 || \
                                                                    ((int16_t) context->state.hl + (int16_t) X + context->state.flag_carry) < -32768); \
                              context->state.flag_half = ((context->state.hl & 0xfff) + (X & 0xfff) + context->state.flag_carry) >> 12; \
                              context->state.flag_zero = (((context->state.hl + X + context->state.flag_carry) & 0xffff) == 0x0000); \
                              context->state.flag_sign = (context->state.hl + X + context->state.flag_carry) >> 15; \
                              context->state.flag_carry = (context->state.hl + X + context->state.flag_carry) >> 16; }

#define SET_FLAGS_SBC_16(X) { context->state.flag_sub = 1; \
                              context->state.flag_parity_overflow = (((int16_t) context->state.hl - (int16_t) X - context->state.flag_carry) >  32767 || \
                                                                    ((int16_t) context->state.hl - (int16_t) X - context->state.flag_carry) < -32768); \
                              context->state.flag_half = ((context->state.hl & 0xfff) - (X & 0xfff) - context->state.flag_carry) >> 12; \
                              context->state.flag_zero = (((context->state.hl - X - context->state.flag_carry) & 0xffff) == 0x0000); \
                              context->state.flag_sign = (context->state.hl - X - context->state.flag_carry) >> 15; \
                              context->state.flag_carry = (context->state.hl - X - context->state.flag_carry) >> 16; }

#define SET_FLAGS_RLC(X) { context->state.flag_carry = X; \
                           context->state.flag_sub = 0; \
                           context->state.flag_parity_overflow = uint8_even_parity [X]; \
                           context->state.flag_half = 0; \
                           context->state.flag_zero = (X == 0x00); \
                           context->state.flag_sign = X >> 7; }

#define SET_FLAGS_RRC(X) { context->state.flag_carry = X >> 7; \
                           context->state.flag_sub = 0; \
                           context->state.flag_parity_overflow = uint8_even_parity [X]; \
                           context->state.flag_half = 0; \
                           context->state.flag_zero = (X == 0x00); \
                           context->state.flag_sign = X >> 7; }

#define SET_FLAGS_RL_RR(X) { context->state.flag_sub = 0; \
                             context->state.flag_parity_overflow = uint8_even_parity [X]; \
                             context->state.flag_half = 0; \
                             context->state.flag_zero = (X == 0x00); \
                             context->state.flag_sign = X >> 7; }

#define SET_FLAGS_RLD_RRD { context->state.flag_sub = 0; \
                            context->state.flag_parity_overflow = uint8_even_parity [context->state.a]; \
                            context->state.flag_half = 0; \
                            context->state.flag_zero = (context->state.a == 0x00); \
                            context->state.flag_sign = context->state.a >> 7; }

#define SET_FLAGS_ED_IN(X) { context->state.flag_sub = 0; \
                             context->state.flag_parity_overflow = uint8_even_parity [X]; \
                             context->state.flag_half = 0; \
                             context->state.flag_zero = (X == 0); \
                             context->state.flag_sign = X >> 7; }

#define SET_FLAGS_XY(X) { context->state.flag_x = X >> 3; \
                          context->state.flag_y = X >> 5; }

#define SET_FLAGS_XY_16(X) { context->state.flag_x = X >> 11; \
                             context->state.flag_y = X >> 13; }


/*
 * Read and execute an IX / IY bit instruction.
 * Called after reading the prefix.
 */
void z80_ix_iy_bit_instruction (Z80_Context *context, uint16_t reg_ix_iy_w)
{
    /* Note: The displacement comes first, then the instruction */
    uint8_t displacement = context->memory_read (context->parent, context->state.pc++);
    uint8_t instruction = context->memory_read (context->parent, context->state.pc++);
    uint8_t data;
    uint8_t bit;
    bool write_data = true;
    uint8_t temp;


    /* All IX/IY bit instructions take one parameter */

    /* Read data */
    data = context->memory_read (context->parent, reg_ix_iy_w + (int8_t) displacement);

    switch (instruction & 0xf8)
    {
        case 0x00: /* RLC (ix+*) */
            data = (data << 1) | ((data & 0x80) ? 0x01 : 0x00);
            SET_FLAGS_RLC (data);
            context->used_cycles += 23;
            break;

        case 0x08: /* RRC (ix+*) */
            data = (data >> 1) | (data << 7);
            SET_FLAGS_RRC (data);
            context->used_cycles += 23;
            break;

        case 0x10: /* RL  (ix+*) */
            temp = data;
            data = (data << 1) | context->state.flag_carry;
            SET_FLAGS_RL_RR (data);
            context->state.flag_carry = temp >> 7;
            context->used_cycles += 23;
            break;

        case 0x18: /* RR  (ix+*) */
            temp = data;
            data = (data >> 1) | (context->state.flag_carry << 7);
            SET_FLAGS_RL_RR (data);
            context->state.flag_carry = temp;
            context->used_cycles += 23;
            break;

        case 0x20: /* SLA (ix+*) */
            temp = data;
            data = (data << 1); SET_FLAGS_RL_RR (data);
            context->state.flag_carry = temp >> 7;
            context->used_cycles += 23;
            break;

        case 0x28: /* SRA (ix+*) */
            temp = data;
            data = (data >> 1) | (data & 0x80); SET_FLAGS_RL_RR (data);
            context->state.flag_carry = temp;
            context->used_cycles += 23;
            break;

        case 0x30: /* SLL (ix+*) */
            temp = data;
            data = (data << 1) | 0x01; SET_FLAGS_RL_RR (data);
            context->state.flag_carry = temp >> 7;
            context->used_cycles += 23;
            break;

        case 0x38: /* SRL (ix+*) */
            temp = data;
            data = (data >> 1); SET_FLAGS_RL_RR (data);
            context->state.flag_carry = temp;
            context->used_cycles += 23;
            break;

        /* BIT */
        case 0x40: case 0x48: case 0x50: case 0x58:
        case 0x60: case 0x68: case 0x70: case 0x78:
            bit = (instruction >> 3) & 0x07;
            context->state.flag_sub = 0;
            context->state.flag_parity_overflow = ~data >> bit;
            context->state.flag_half = 1;
            context->state.flag_zero = ~data >> bit;
            context->state.flag_sign = (data & (1 << bit)) >> 7;
            context->used_cycles += 20;
            write_data = false;
            break;

        /* RES */
        case 0x80: case 0x88: case 0x90: case 0x98:
        case 0xa0: case 0xa8: case 0xb0: case 0xb8:
            bit = (instruction >> 3) & 0x07;
            data &= ~(1 << bit);
            context->used_cycles += 23;
            break;

        /* SET */
        case 0xc0: case 0xc8: case 0xd0: case 0xd8:
        case 0xe0: case 0xe8: case 0xf0: case 0xf8:
            bit = (instruction >> 3) & 0x07;
            data |= (1 << bit);
            context->used_cycles += 23;
            break;

        default:
            /* Unreachable */
            break;
    }

    /* Write data */
    if (write_data)
    {
        context->memory_write (context->parent, reg_ix_iy_w + (int8_t) displacement, data);

        switch (instruction & 0x07)
        {
            case 0x00: context->state.b = data; break;
            case 0x01: context->state.c = data; break;
            case 0x02: context->state.d = data; break;
            case 0x03: context->state.e = data; break;
            case 0x04: context->state.h = data; break;
            case 0x05: context->state.l = data; break;
            case 0x07: context->state.a = data; break;
            default: break;
        }
    }
}


/****************************/
/**  IX / IY Instructions  **/
/****************************/

static uint16_t z80_ix_iy_fall_through (Z80_Context *context, uint16_t ix)
{
    uint8_t instruction = context->memory_read (context->parent, context->state.pc - 1);
    z80_instruction [instruction] (context);
    context->used_cycles += 4;
    return ix;
}


/* ADD IX, BC */
static uint16_t z80_ix_iy_09_add_ix_bc (Z80_Context *context, uint16_t ix)
{
    SET_FLAGS_ADD_16 (ix, context->state.bc);
    ix += context->state.bc;
    SET_FLAGS_XY_16 (ix);
    context->used_cycles += 15;
    return ix;
}


/* ADD IX, DE */
static uint16_t z80_ix_iy_19_add_ix_de (Z80_Context *context, uint16_t ix)
{
    SET_FLAGS_ADD_16 (ix, context->state.de);
    ix += context->state.de;
    SET_FLAGS_XY_16 (ix);
    context->used_cycles += 15;
    return ix;
}


/* LD IX, ** */
static uint16_t z80_ix_iy_21_ld_ix_xx (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split data;
    data.l = context->memory_read (context->parent, context->state.pc++);
    data.h = context->memory_read (context->parent, context->state.pc++);
    context->used_cycles += 14;
    return data.w;
}


/* LD (**), IX */
static uint16_t z80_ix_iy_22_ld_xx_ix (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);
    context->memory_write (context->parent, addr.w,     _ix.l);
    context->memory_write (context->parent, addr.w + 1, _ix.h);
    context->used_cycles += 20;
    return ix;
}


/* INC IX */
static uint16_t z80_ix_iy_23_inc_ix (Z80_Context *context, uint16_t ix)
{
    ix++;
    context->used_cycles += 10;
    return ix;
}


/* INC IXH (undocumented) */
static uint16_t z80_ix_iy_24_inc_ixh (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.h++;
    SET_FLAGS_INC (_ix.h);
    context->used_cycles += 8;
    return _ix.w;
}


/* DEC IXH (undocumented) */
static uint16_t z80_ix_iy_25_dec_ixh (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.h--;
    SET_FLAGS_DEC (_ix.h);
    context->used_cycles += 8;
    return _ix.w;
}


/* LD IXH, * (undocumented) */
static uint16_t z80_ix_iy_26_ld_ixh_x (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.h = context->memory_read (context->parent, context->state.pc++);
    context->used_cycles += 11;
    return _ix.w;
}


/* ADD IX, IX */
static uint16_t z80_ix_iy_29_add_ix_ix (Z80_Context *context, uint16_t ix)
{
    SET_FLAGS_ADD_16 (ix, ix);
    ix += ix;
    SET_FLAGS_XY_16 (ix);
    context->used_cycles += 15;
    return ix;
}


/* LD IX, (**) */
static uint16_t z80_ix_iy_2a_ld_ix_xx (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);
    _ix.l = context->memory_read (context->parent, addr.w);
    _ix.h = context->memory_read (context->parent, addr.w + 1);
    context->used_cycles += 20;
    return _ix.w;
}


/* DEC IX */
static uint16_t z80_ix_iy_2b_dec_ix (Z80_Context *context, uint16_t ix)
{
    ix--;
    context->used_cycles += 10;
    return ix;
}


/* INC IXL (undocumented) */
static uint16_t z80_ix_iy_2c_inc_ixl (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.l++;
    SET_FLAGS_INC (_ix.l);
    context->used_cycles += 8;
    return _ix.w;
}


/* DEC IXL (undocumented) */
static uint16_t z80_ix_iy_2d_dec_ixl (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.l--;
    SET_FLAGS_DEC (_ix.l);
    context->used_cycles += 8;
    return _ix.w;
}


/* LD IXL, * (undocumented) */
static uint16_t z80_ix_iy_2e_ld_ixl_x (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.l = context->memory_read (context->parent, context->state.pc++);
    context->used_cycles += 11;
    return _ix.w;
}


/* INC (IX + *) */
static uint16_t z80_ix_iy_34_inc_ixx (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    uint8_t data = context->memory_read (context->parent, ix + offset);
    data++;
    SET_FLAGS_INC (data);
    context->memory_write (context->parent, ix + offset, data);
    context->used_cycles += 23;
    return ix;
}


/* DEC (IX + *) */
static uint16_t z80_ix_iy_35_dec_ixx (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    uint8_t data = context->memory_read (context->parent, ix + offset);
    data--;
    SET_FLAGS_DEC (data);
    context->memory_write (context->parent, ix + offset, data);
    context->used_cycles += 23;
    return ix;
}


/* LD (IX + *), * */
static uint16_t z80_ix_iy_36_ld_ixx_x (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    uint8_t data = context->memory_read (context->parent, context->state.pc++);
    context->memory_write (context->parent, ix + offset, data);
    context->used_cycles += 19;
    return ix;
}


/* ADD IX, SP */
static uint16_t z80_ix_iy_39_add_ix_sp (Z80_Context *context, uint16_t ix)
{
    SET_FLAGS_ADD_16 (ix, context->state.sp);
    ix += context->state.sp;
    SET_FLAGS_XY_16 (ix);
    context->used_cycles = 15;
    return ix;
}


/* LD B, IXH (undocumented) */
static uint16_t z80_ix_iy_44_ld_b_ixh (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    context->state.b = _ix.h;
    context->used_cycles += 8;
    return ix;
}


/* LD B, IXL (undocumented) */
static uint16_t z80_ix_iy_45_ld_b_ixl (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    context->state.b = _ix.l;
    context->used_cycles += 8;
    return ix;
}


/* LD B, (IX + *) */
static uint16_t z80_ix_iy_46_ld_b_ixx (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    context->state.b = context->memory_read (context->parent, ix + offset);
    context->used_cycles += 19;
    return ix;
}


/* LD C, IXH (undocumented) */
static uint16_t z80_ix_iy_4c_ld_c_ixh (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    context->state.c = _ix.h;
    context->used_cycles += 8;
    return ix;
}


/* LD C, IXL (undocumented) */
static uint16_t z80_ix_iy_4d_ld_c_ixl (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    context->state.c = _ix.l;
    context->used_cycles += 8;
    return ix;
}


/* LD C, (IX + *) */
static uint16_t z80_ix_iy_4e_ld_c_ixx (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    context->state.c = context->memory_read (context->parent, ix + offset);
    context->used_cycles += 19;
    return ix;
}


/* LD D, IXH (undocumented) */
static uint16_t z80_ix_iy_54_ld_d_ixh (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    context->state.d = _ix.h;
    context->used_cycles += 8;
    return ix;
}


/* LD D, IXL (undocumented) */
static uint16_t z80_ix_iy_55_ld_d_ixl (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    context->state.d = _ix.l;
    context->used_cycles += 8;
    return ix;
}


/* LD D, (IX + *) */
static uint16_t z80_ix_iy_56_ld_d_ixx (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    context->state.d = context->memory_read (context->parent, ix + offset);
    context->used_cycles += 19;
    return ix;
}


/* LD E, IXH (undocumented) */
static uint16_t z80_ix_iy_5c_ld_e_ixh (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    context->state.e = _ix.h;
    context->used_cycles += 8;
    return ix;
}


/* LD E, IXL (undocumented) */
static uint16_t z80_ix_iy_5d_ld_e_ixl (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    context->state.e = _ix.l;
    context->used_cycles += 8;
    return ix;
}


/* LD E, (IX + *) */
static uint16_t z80_ix_iy_5e_ld_e_ixx (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    context->state.e = context->memory_read (context->parent, ix + offset);
    context->used_cycles += 19;
    return ix;
}


/* LD IXH, B (undocumented) */
static uint16_t z80_ix_iy_60_ld_ixh_b (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.h = context->state.b;
    context->used_cycles += 8;
    return _ix.w;
}


/* LD IXH, C (undocumented) */
static uint16_t z80_ix_iy_61_ld_ixh_c (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.h = context->state.c;
    context->used_cycles += 8;
    return _ix.w;
}


/* LD IXH, D (undocumented) */
static uint16_t z80_ix_iy_62_ld_ixh_d (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.h = context->state.d;
    context->used_cycles += 8;
    return _ix.w;
}


/* LD IXH, E (undocumented) */
static uint16_t z80_ix_iy_63_ld_ixh_e (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.h = context->state.e;
    context->used_cycles += 8;
    return _ix.w;
}


/* LD IXH, IXH (undocumented) */
static uint16_t z80_ix_iy_64_ld_ixh_ixh (Z80_Context *context, uint16_t ix)
{
    context->used_cycles += 8;
    return ix;
}


/* LD IXH, IXL (undocumented) */
static uint16_t z80_ix_iy_65_ld_ixh_ixl (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.h = _ix.l;
    context->used_cycles += 8;
    return _ix.w;
}


/* LD H, (IX + *) */
static uint16_t z80_ix_iy_66_ld_h_ixx (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    context->state.h = context->memory_read (context->parent, ix + offset);
    context->used_cycles += 19;
    return ix;
}


/* LD IXH, A (undocumented) */
static uint16_t z80_ix_iy_67_ld_ixh_a (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.h = context->state.a;
    context->used_cycles += 8;
    return _ix.w;
}


/* LD IXL, B (undocumented) */
static uint16_t z80_ix_iy_68_ld_ixl_b (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.l = context->state.b;
    context->used_cycles += 8;
    return _ix.w;
}


/* LD IXL, C (undocumented) */
static uint16_t z80_ix_iy_69_ld_ixl_c (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.l = context->state.c;
    context->used_cycles += 8;
    return _ix.w;
}


/* LD IXL, D (undocumented) */
static uint16_t z80_ix_iy_6a_ld_ixl_d (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.l = context->state.d;
    context->used_cycles += 8;
    return _ix.w;
}


/* LD IXL, E (undocumented) */
static uint16_t z80_ix_iy_6b_ld_ixl_e (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.l = context->state.e;
    context->used_cycles += 8;
    return _ix.w;
}


/* LD IXL, IXH (undocumented) */
static uint16_t z80_ix_iy_6c_ld_ixl_ixh (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.l = _ix.h;
    context->used_cycles += 8;
    return _ix.w;
}


/* LD IXL, IXL (undocumented) */
static uint16_t z80_ix_iy_6d_ld_ixl_ixl (Z80_Context *context, uint16_t ix)
{
    context->used_cycles += 8;
    return ix;
}


/* LD L, (IX + *) */
static uint16_t z80_ix_iy_6e_ld_l_ixx (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    context->state.l = context->memory_read (context->parent, ix + offset);
    context->used_cycles += 19;
    return ix;
}


/* LD IXL, A (undocumented) */
static uint16_t z80_ix_iy_6f_ld_ixl_a (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.l = context->state.a;
    context->used_cycles += 8;
    return _ix.w;
}


/* LD (IX + *), B */
static uint16_t z80_ix_iy_70_ld_ixx_b (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    context->memory_write (context->parent, ix + offset, context->state.b);
    context->used_cycles += 19;
    return ix;
}


/* LD (IX + *), C */
static uint16_t z80_ix_iy_71_ld_ixx_c (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    context->memory_write (context->parent, ix + offset, context->state.c);
    context->used_cycles += 19;
    return ix;
}


/* LD (IX + *), D */
static uint16_t z80_ix_iy_72_ld_ixx_d (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    context->memory_write (context->parent, ix + offset, context->state.d);
    context->used_cycles += 19;
    return ix;
}


/* LD (IX + *), E */
static uint16_t z80_ix_iy_73_ld_ixx_e (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    context->memory_write (context->parent, ix + offset, context->state.e);
    context->used_cycles += 19;
    return ix;
}


/* LD (IX + *), H */
static uint16_t z80_ix_iy_74_ld_ixx_h (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    context->memory_write (context->parent, ix + offset, context->state.h);
    context->used_cycles += 19;
    return ix;
}


/* LD (IX + *), L */
static uint16_t z80_ix_iy_75_ld_ixx_l (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    context->memory_write (context->parent, ix + offset, context->state.l);
    context->used_cycles += 19;
    return ix;
}


/* LD (IX + *), A */
static uint16_t z80_ix_iy_77_ld_ixx_a (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    context->memory_write (context->parent, ix + offset, context->state.a);
    context->used_cycles += 19;
    return ix;
}


/* LD A, IXH (undocumented) */
static uint16_t z80_ix_iy_7c_ld_a_ixh (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    context->state.a = _ix.h;
    context->used_cycles += 8;
    return ix;
}


/* LD A, IXL (undocumented) */
static uint16_t z80_ix_iy_7d_ld_a_ixl (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    context->state.a = _ix.l;
    context->used_cycles += 8;
    return ix;
}


/* LD A, (IX + *) */
static uint16_t z80_ix_iy_7e_ld_a_ixx (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    context->state.a = context->memory_read (context->parent, ix + offset);
    context->used_cycles += 19;
    return ix;
}


/* ADD A, IXH (undocumented) */
static uint16_t z80_ix_iy_84_add_a_ixh (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    SET_FLAGS_ADD (context->state.a, _ix.h);
    context->state.a += _ix.h;
    context->used_cycles += 8;
    return ix;
}


/* ADD A, IXL (undocumented) */
static uint16_t z80_ix_iy_85_add_a_ixl (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    SET_FLAGS_ADD (context->state.a, _ix.l);
    context->state.a += _ix.l;
    context->used_cycles += 8;
    return ix;
}


/* ADD A, (IX + *) */
static uint16_t z80_ix_iy_86_add_a_ixx (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    uint8_t data = context->memory_read (context->parent, ix + offset);
    SET_FLAGS_ADD (context->state.a, data);
    context->state.a += data;
    context->used_cycles += 19;
    return ix;
}


/* ADC A, IXH (undocumented) */
static uint16_t z80_ix_iy_8c_adc_a_ixh (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    uint8_t value = _ix.h + context->state.flag_carry;
    SET_FLAGS_ADC (_ix.h);
    context->state.a += value;
    context->used_cycles += 8;
    return ix;
}


/* ADC A, IXL (undocumented) */
static uint16_t z80_ix_iy_8d_adc_a_ixl (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    uint8_t value = _ix.l + context->state.flag_carry;
    SET_FLAGS_ADC (_ix.l);
    context->state.a += value;
    context->used_cycles += 8;
    return ix;
}


/* ADC A, (IX + *) */
static uint16_t z80_ix_iy_8e_adc_a_ixx (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    uint8_t value = context->memory_read (context->parent, ix + offset);
    uint8_t carry = context->state.flag_carry;
    SET_FLAGS_ADC (value);
    context->state.a += (value + carry);
    context->used_cycles += 19;
    return ix;
}


/* SUB A, IXH (undocumented) */
static uint16_t z80_ix_iy_94_sub_a_ixh (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    SET_FLAGS_SUB (context->state.a, _ix.h);
    context->state.a -= _ix.h;
    context->used_cycles += 8;
    return ix;
}


/* SUB A, IXL (undocumented) */
static uint16_t z80_ix_iy_95_sub_a_ixl (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    SET_FLAGS_SUB (context->state.a, _ix.l);
    context->state.a -= _ix.l;
    context->used_cycles += 8;
    return ix;
}


/* SUB A, (IX + *) */
static uint16_t z80_ix_iy_96_sub_a_ixx (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    uint8_t data = context->memory_read (context->parent, ix + offset);
    SET_FLAGS_SUB (context->state.a, data);
    context->state.a -= data;
    context->used_cycles += 19;
    return ix;
}


/* SBC A, IXH (undocumented) */
static uint16_t z80_ix_iy_9c_sbc_a_ixh (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    uint8_t value = _ix.h + context->state.flag_carry;
    SET_FLAGS_SBC (_ix.h);
    context->state.a -= value;
    context->used_cycles += 8;
    return ix;
}


/* SBC A, IXL (undocumented) */
static uint16_t z80_ix_iy_9d_sbc_a_ixl (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    uint8_t value= _ix.l + context->state.flag_carry;
    SET_FLAGS_SBC (_ix.l);
    context->state.a -= value;
    context->used_cycles += 8;
    return ix;
}


/* SBC A, (IX + *) */
static uint16_t z80_ix_iy_9e_sbc_a_ixx (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    uint8_t value = context->memory_read (context->parent, ix + offset);
    uint8_t carry = context->state.flag_carry;
    SET_FLAGS_SBC (value);
    context->state.a -= (value + carry);
    context->used_cycles += 19;
    return ix;
}


/* AND A, IXH (undocumented) */
static uint16_t z80_ix_iy_a4_and_a_ixh (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    context->state.a &= _ix.h;
    SET_FLAGS_AND;
    context->used_cycles += 8;
    return ix;
}


/* AND A, IXL (undocumented) */
static uint16_t z80_ix_iy_a5_and_a_ixl (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    context->state.a &= _ix.l;
    SET_FLAGS_AND;
    context->used_cycles += 8;
    return ix;
}


/* AND A, (IX + *) */
static uint16_t z80_ix_iy_a6_and_a_ixx (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    context->state.a &= context->memory_read (context->parent, ix + offset);
    SET_FLAGS_AND;
    context->used_cycles += 19;
    return ix;
}


/* XOR A, IXH (undocumented) */
static uint16_t z80_ix_iy_ac_xor_a_ixh (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    context->state.a ^= _ix.h;
    SET_FLAGS_OR_XOR;
    context->used_cycles += 8;
    return ix;
}


/* XOR A, IXL (undocumented) */
static uint16_t z80_ix_iy_ad_xor_a_ixl (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    context->state.a ^= _ix.l;
    SET_FLAGS_OR_XOR;
    context->used_cycles += 8;
    return ix;
}


/* XOR A, (IX + *) */
static uint16_t z80_ix_iy_ae_xor_a_ixx (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    context->state.a ^= context->memory_read (context->parent, ix + offset);
    SET_FLAGS_OR_XOR;
    context->used_cycles += 19;
    return ix;
}


/* OR A, IXH (undocumented) */
static uint16_t z80_ix_iy_b4_or_a_ixh (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    context->state.a |= _ix.h;
    SET_FLAGS_OR_XOR;
    context->used_cycles += 8;
    return ix;
}


/* OR A, IXL (undocumented) */
static uint16_t z80_ix_iy_b5_or_a_ixl (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    context->state.a |= _ix.l;
    SET_FLAGS_OR_XOR;
    context->used_cycles += 8;
    return ix;
}


/* OR A, (IX + *) */
static uint16_t z80_ix_iy_b6_or_a_ixx (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    context->state.a |= context->memory_read (context->parent, ix + offset);
    SET_FLAGS_OR_XOR;
    context->used_cycles += 19;
    return ix;
}


/* CP A, IXH (undocumented) */
static uint16_t z80_ix_iy_bc_cp_a_ixh (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    SET_FLAGS_SUB (context->state.a, _ix.h);
    context->used_cycles += 8;
    return ix;
}


/* CP A, IXL (undocumented) */
static uint16_t z80_ix_iy_bd_cp_a_ixl (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    SET_FLAGS_SUB (context->state.a, _ix.l);
    context->used_cycles += 8;
    return ix;
}


/* CP A, (IX + *) */
static uint16_t z80_ix_iy_be_cp_a_ixx (Z80_Context *context, uint16_t ix)
{
    int8_t offset = context->memory_read (context->parent, context->state.pc++);
    uint8_t data = context->memory_read (context->parent, ix + offset);
    SET_FLAGS_SUB (context->state.a, data);
    context->used_cycles += 19;
    return ix;
}


/* IX / IY BIT PREFIX */
static uint16_t z80_ix_iy_cb_prefix (Z80_Context *context, uint16_t ix)
{
    z80_ix_iy_bit_instruction (context, ix);
    return ix;
}


/* POP IX */
static uint16_t z80_ix_iy_e1_pop_ix (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.l = context->memory_read (context->parent, context->state.sp++);
    _ix.h = context->memory_read (context->parent, context->state.sp++);
    context->used_cycles += 14;
    return _ix.w;
}


/* EX (SP), IX */
static uint16_t z80_ix_iy_e3_ex_sp_ix (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    _ix.l = context->memory_read (context->parent, context->state.sp);
    _ix.h = context->memory_read (context->parent, context->state.sp + 1);
    context->memory_write (context->parent, context->state.sp,     _ix.l);
    context->memory_write (context->parent, context->state.sp + 1, _ix.h);
    context->used_cycles += 23;
    return _ix.w;
}


/* PUSH IX */
static uint16_t z80_ix_iy_e5_push_ix (Z80_Context *context, uint16_t ix)
{
    uint16_t_Split _ix = { .w = ix };
    context->memory_write (context->parent, --context->state.sp, _ix.h);
    context->memory_write (context->parent, --context->state.sp, _ix.l);
    context->used_cycles += 15;
    return ix;
}


/* JP (IX) */
static uint16_t z80_ix_iy_e9_jp_ix (Z80_Context *context, uint16_t ix)
{
    context->state.pc = ix;
    context->used_cycles += 8;
    return ix;
}


/* LD SP, IX */
static uint16_t z80_ix_iy_f9_ld_sp_ix (Z80_Context *context, uint16_t ix)
{
    context->state.sp = ix;
    context->used_cycles += 10;
    return ix;
}


static uint16_t (*z80_ix_iy_instruction [256]) (Z80_Context *, uint16_t) = {
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
static uint8_t z80_cb_00_rlc (Z80_Context *context, uint8_t value)
{
    value = (value << 1) | (value >> 7);
    SET_FLAGS_RLC (value);
    return value;
}


/* RRC */
static uint8_t z80_cb_08_rrc (Z80_Context *context, uint8_t value)
{
    value = (value >> 1) | (value << 7);
    SET_FLAGS_RRC (value);
    return value;
}


/* RL */
static uint8_t z80_cb_10_rl (Z80_Context *context, uint8_t value)
{
    uint8_t result;
    result = (value << 1) | context->state.flag_carry;
    SET_FLAGS_RL_RR (result);
    context->state.flag_carry = value >> 7;
    return result;
}


/* RR */
static uint8_t z80_cb_18_rr (Z80_Context *context, uint8_t value)
{
    uint8_t result;
    result = (value >> 1) | (context->state.flag_carry << 7);
    SET_FLAGS_RL_RR (result);
    context->state.flag_carry = value;
    return result;
}


/* SLA */
static uint8_t z80_cb_20_sla (Z80_Context *context, uint8_t value)
{
    uint8_t result;
    result = (value << 1);
    SET_FLAGS_RL_RR (result);
    context->state.flag_carry = value >> 7;
    return result;
}


/* SRA */
static uint8_t z80_cb_28_sra (Z80_Context *context, uint8_t value)
{
    uint8_t result;
    result = (value >> 1) | (value & 0x80);
    SET_FLAGS_RL_RR (result);
    context->state.flag_carry = value;
    return result;
}

/* SLL (undocumented) */
static uint8_t z80_cb_30_sll (Z80_Context *context, uint8_t value)
{
    uint8_t result;
    result = (value << 1) | 0x01;
    SET_FLAGS_RL_RR (result);
    context->state.flag_carry = value >> 7;
    return result;
}


/* SRL */
static uint8_t z80_cb_38_srl (Z80_Context *context, uint8_t value)
{
    uint8_t result;
    result = (value >> 1);
    SET_FLAGS_RL_RR (result);
    context->state.flag_carry = value;
    return result;
}


/* BIT 0 */
static uint8_t z80_cb_40_bit_0 (Z80_Context *context, uint8_t value)
{
    context->state.flag_sub = 0;
    context->state.flag_parity_overflow =  ~value >> 0;
    context->state.flag_half = 1;
    context->state.flag_zero = ~value >> 0;
    context->state.flag_sign = 0;
    return value;
}


/* BIT 1 */
static uint8_t z80_cb_48_bit_1 (Z80_Context *context, uint8_t value)
{
    context->state.flag_sub = 0;
    context->state.flag_parity_overflow =  ~value >> 1;
    context->state.flag_half = 1;
    context->state.flag_zero = ~value >> 1;
    context->state.flag_sign = 0;
    return value;
}


/* BIT 2 */
static uint8_t z80_cb_50_bit_2 (Z80_Context *context, uint8_t value)
{
    context->state.flag_sub = 0;
    context->state.flag_parity_overflow =  ~value >> 2;
    context->state.flag_half = 1;
    context->state.flag_zero = ~value >> 2;
    context->state.flag_sign = 0;
    return value;
}


/* BIT 3 */
static uint8_t z80_cb_58_bit_3 (Z80_Context *context, uint8_t value)
{
    context->state.flag_sub = 0;
    context->state.flag_parity_overflow =  ~value >> 3;
    context->state.flag_half = 1;
    context->state.flag_zero = ~value >> 3;
    context->state.flag_sign = 0;
    return value;
}


/* BIT 4 */
static uint8_t z80_cb_60_bit_4 (Z80_Context *context, uint8_t value)
{
    context->state.flag_sub = 0;
    context->state.flag_parity_overflow =  ~value >> 4;
    context->state.flag_half = 1;
    context->state.flag_zero = ~value >> 4;
    context->state.flag_sign = 0;
    return value;
}


/* BIT 5 */
static uint8_t z80_cb_68_bit_5 (Z80_Context *context, uint8_t value)
{
    context->state.flag_sub = 0;
    context->state.flag_parity_overflow =  ~value >> 5;
    context->state.flag_half = 1;
    context->state.flag_zero = ~value >> 5;
    context->state.flag_sign = 0;
    return value;
}


/* BIT 6 */
static uint8_t z80_cb_70_bit_6 (Z80_Context *context, uint8_t value)
{
    context->state.flag_sub = 0;
    context->state.flag_parity_overflow =  ~value >> 6;
    context->state.flag_half = 1;
    context->state.flag_zero = ~value >> 6;
    context->state.flag_sign = 0;
    return value;
}


/* BIT 7 */
static uint8_t z80_cb_78_bit_7 (Z80_Context *context, uint8_t value)
{
    context->state.flag_sub = 0;
    context->state.flag_parity_overflow =  ~value >> 7;
    context->state.flag_half = 1;
    context->state.flag_zero = ~value >> 7;
    context->state.flag_sign = value >> 7;
    return value;
}


/* RES 0 */
static uint8_t z80_cb_80_res_0 (Z80_Context *context, uint8_t value)
{
    return value & 0xfe;
}


/* RES 1 */
static uint8_t z80_cb_88_res_1 (Z80_Context *context, uint8_t value)
{
    return value & 0xfd;
}


/* RES 2 */
static uint8_t z80_cb_90_res_2 (Z80_Context *context, uint8_t value)
{
    return value & 0xfb;
}


/* RES 3 */
static uint8_t z80_cb_98_res_3 (Z80_Context *context, uint8_t value)
{
    return value & 0xf7;
}


/* RES 4 */
static uint8_t z80_cb_a0_res_4 (Z80_Context *context, uint8_t value)
{
    return value & 0xef;
}


/* RES 5 */
static uint8_t z80_cb_a8_res_5 (Z80_Context *context, uint8_t value)
{
    return value & 0xdf;
}


/* RES 6 */
static uint8_t z80_cb_b0_res_6 (Z80_Context *context, uint8_t value)
{
    return value & 0xbf;
}


/* RES 7 */
static uint8_t z80_cb_b8_res_7 (Z80_Context *context, uint8_t value)
{
    return value & 0x7f;
}


/* SET 0 */
static uint8_t z80_cb_c0_set_0 (Z80_Context *context, uint8_t value)
{
    return value | 0x01;
}


/* SET 1 */
static uint8_t z80_cb_c8_set_1 (Z80_Context *context, uint8_t value)
{
    return value | 0x02;
}


/* SET 2 */
static uint8_t z80_cb_d0_set_2 (Z80_Context *context, uint8_t value)
{
    return value | 0x04;
}


/* SET 3 */
static uint8_t z80_cb_d8_set_3 (Z80_Context *context, uint8_t value)
{
    return value | 0x08;
}


/* SET 4 */
static uint8_t z80_cb_e0_set_4 (Z80_Context *context, uint8_t value)
{
    return value | 0x10;
}


/* SET 5 */
static uint8_t z80_cb_e8_set_5 (Z80_Context *context, uint8_t value)
{
    return value | 0x20;
}


/* SET 6 */
static uint8_t z80_cb_f0_set_6 (Z80_Context *context, uint8_t value)
{
    return value | 0x40;
}


/* SET 7 */
static uint8_t z80_cb_f8_set_7 (Z80_Context *context, uint8_t value)
{
    return value | 0x80;
}


uint8_t (*z80_cb_instruction [32]) (Z80_Context *, uint8_t) = {
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
static void z80_ed_40_in_b_c (Z80_Context *context)
{
    context->state.b = context->io_read (context->parent, context->state.c);
    SET_FLAGS_ED_IN (context->state.b);
    context->used_cycles += 12;
}


/* OUT (C), B */
static void z80_ed_41_out_c_b (Z80_Context *context)
{
    context->io_write (context->parent, context->state.c, context->state.b);
    context->used_cycles += 12;
}


/* SBC HL, BC */
static void z80_ed_42_sbc_hl_bc (Z80_Context *context)
{
    uint16_t temp;
    temp = context->state.bc + context->state.flag_carry;
    SET_FLAGS_SBC_16 (context->state.bc);
    context->state.hl -= temp;
    SET_FLAGS_XY_16 (context->state.hl);
    context->used_cycles += 15;
}


/* LD (**), BC */
static void z80_ed_43_ld_xx_bc (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    context->memory_write (context->parent, addr.w,     context->state.c);
    context->memory_write (context->parent, addr.w + 1, context->state.b);
    context->used_cycles += 20;
}


/* NEG */
static void z80_ed_44_neg (Z80_Context *context)
{
    uint8_t half;
    half = 0 - (context->state.a & 0x0f);
    context->state.a = 0 - (int8_t) context->state.a;

    context->state.flag_carry = (context->state.a != 0);
    context->state.flag_sub = 1;
    context->state.flag_parity_overflow = (context->state.a == 0x80);
    context->state.flag_half = half >> 4;
    context->state.flag_zero = (context->state.a == 0x00);
    context->state.flag_sign = context->state.a >> 7;

    context->used_cycles += 8;
}


/* RETN */
static void z80_ed_45_retn (Z80_Context *context)
{
    context->state.pc_l = context->memory_read (context->parent, context->state.sp++);
    context->state.pc_h = context->memory_read (context->parent, context->state.sp++);
    context->state.iff1 = context->state.iff2;
    context->used_cycles += 14;
}


/* IM 0 */
static void z80_ed_46_im_0 (Z80_Context *context)
{
    context->state.im = 0;
    context->used_cycles += 8;
}


/* LD I, A */
static void z80_ed_47_ld_i_a (Z80_Context *context)
{
    context->state.i = context->state.a;
    context->used_cycles += 9;
}


/* IN C, (C) */
static void z80_ed_48_in_c_c (Z80_Context *context)
{
    context->state.c = context->io_read (context->parent, context->state.c);
    SET_FLAGS_ED_IN (context->state.c);
    context->used_cycles += 12;
}


/* OUT (C), C */
static void z80_ed_49_out_c_c (Z80_Context *context)
{
    context->io_write (context->parent, context->state.c, context->state.c);
    context->used_cycles += 12;
}


/* ADC HL, BC */
static void z80_ed_4a_adc_hl_bc (Z80_Context *context)
{
    uint16_t temp;
    temp = context->state.bc + context->state.flag_carry;
    SET_FLAGS_ADC_16 (context->state.bc);
    context->state.hl += temp;
    SET_FLAGS_XY_16 (context->state.hl);
    context->used_cycles += 15;
}


/* LD BC, (**) */
static void z80_ed_4b_ld_bc_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    context->state.c = context->memory_read (context->parent, addr.w);
    context->state.b = context->memory_read (context->parent, addr.w + 1);
    context->used_cycles += 20;
}


/* NEG (undocumented) */
static void z80_ed_4c_neg (Z80_Context *context)
{
    z80_ed_44_neg (context);
}


/* RETI */
static void z80_ed_4d_reti (Z80_Context *context)
{
    /* While not mentioned in the official documentation,
     * RETI has the same iff1 <- iff2 behaviour as RETN */
    z80_ed_45_retn (context);
}


/* IM 0 (undocumented) */
static void z80_ed_4e_im_0 (Z80_Context *context)
{
    z80_ed_46_im_0 (context);
}


/* LD R, A */
static void z80_ed_4f_ld_r_a (Z80_Context *context)
{
    context->state.r = context->state.a;
    context->used_cycles += 9;
}


/* IN D, (C) */
static void z80_ed_50_in_d_c (Z80_Context *context)
{
    context->state.d = context->io_read (context->parent, context->state.c);
    SET_FLAGS_ED_IN (context->state.d);
    context->used_cycles += 12;
}


/* OUT (C), D */
static void z80_ed_51_out_c_d (Z80_Context *context)
{
    context->io_write (context->parent, context->state.c, context->state.d);
    context->used_cycles += 12;
}


/* SBC HL, DE */
static void z80_ed_52_sbc_hl_de (Z80_Context *context)
{
    uint16_t temp;
    temp = context->state.de + context->state.flag_carry;
    SET_FLAGS_SBC_16 (context->state.de);
    context->state.hl -= temp;
    SET_FLAGS_XY_16 (context->state.hl);
    context->used_cycles += 15;
}


/* LD (**), DC */
static void z80_ed_53_ld_xx_de (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    context->memory_write (context->parent, addr.w,     context->state.e);
    context->memory_write (context->parent, addr.w + 1, context->state.d);
    context->used_cycles += 20;
}


/* NEG (undocumented) */
static void z80_ed_54_neg (Z80_Context *context)
{
    z80_ed_44_neg (context);
}


/* RETN */
static void z80_ed_55_retn (Z80_Context *context)
{
    z80_ed_45_retn (context);
}


/* IM 1 */
static void z80_ed_56_im_1 (Z80_Context *context)
{
    context->state.im = 1;
    context->used_cycles += 8;
}


/* LD A, I */
static void z80_ed_57_ld_a_i (Z80_Context *context)
{
    context->state.a = context->state.i;
    context->state.flag_sub = 0;
    context->state.flag_parity_overflow = context->state.iff2;
    context->state.flag_half =  0;
    context->state.flag_zero = (context->state.i == 0);
    context->state.flag_sign = context->state.i >> 7;
    context->used_cycles += 9;
}


/* IN E, (C) */
static void z80_ed_58_in_e_c (Z80_Context *context)
{
    context->state.e = context->io_read (context->parent, context->state.c);
    SET_FLAGS_ED_IN (context->state.e);
    context->used_cycles += 12;
}


/* OUT (C), E */
static void z80_ed_59_out_c_e (Z80_Context *context)
{
    context->io_write (context->parent, context->state.c, context->state.e);
    context->used_cycles += 12;
}


/* ADC HL, DE */
static void z80_ed_5a_adc_hl_de (Z80_Context *context)
{
    uint16_t temp = context->state.de + context->state.flag_carry;
    SET_FLAGS_ADC_16 (context->state.de);
    context->state.hl += temp;
    SET_FLAGS_XY_16 (context->state.hl);
    context->used_cycles += 15;
}


/* LD DE, (**) */
static void z80_ed_5b_ld_de_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    context->state.e = context->memory_read (context->parent, addr.w);
    context->state.d = context->memory_read (context->parent, addr.w + 1);
    context->used_cycles += 20;
}


/* NEG (undocumented) */
static void z80_ed_5c_neg (Z80_Context *context)
{
    z80_ed_44_neg (context);
}


/* RETN */
static void z80_ed_5d_retn (Z80_Context *context)
{
    z80_ed_45_retn (context);
}


/* IM 2 */
static void z80_ed_5e_im_2 (Z80_Context *context)
{
    context->state.im = 2;
    context->used_cycles += 8;
}


/* LD A, R */
static void z80_ed_5f_ld_a_r (Z80_Context *context)
{
    context->state.a = context->state.r;
    context->state.flag_sub =  0;
    context->state.flag_parity_overflow = context->state.iff2;
    context->state.flag_half = 0;
    context->state.flag_zero = (context->state.r == 0);
    context->state.flag_sign = context->state.r >> 7;
    context->used_cycles += 9;
}


/* IN H, (C) */
static void z80_ed_60_in_h_c (Z80_Context *context)
{
    context->state.h = context->io_read (context->parent, context->state.c);
    SET_FLAGS_ED_IN (context->state.h);
    context->used_cycles += 12;
}


/* OUT (C), H */
static void z80_ed_61_out_c_h (Z80_Context *context)
{
    context->io_write (context->parent, context->state.c, context->state.h);
    context->used_cycles += 12;
}


/* SBC HL, HL */
static void z80_ed_62_sbc_hl_hl (Z80_Context *context)
{
    uint16_t temp = context->state.hl + context->state.flag_carry;
    SET_FLAGS_SBC_16 (context->state.hl);
    context->state.hl -= temp;
    SET_FLAGS_XY_16 (context->state.hl);
    context->used_cycles += 15;
}


/* LD (**), hl (undocumented) */
static void z80_ed_63_ld_xx_hl (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    context->memory_write (context->parent, addr.w,     context->state.l);
    context->memory_write (context->parent, addr.w + 1, context->state.h);
    context->used_cycles += 20;
}


/* NEG (undocumented) */
static void z80_ed_64_neg (Z80_Context *context)
{
    z80_ed_44_neg (context);
}


/* RETN */
static void z80_ed_65_retn (Z80_Context *context)
{
    z80_ed_45_retn (context);
}


/* IM 0 */
static void z80_ed_66_im_0 (Z80_Context *context)
{
    z80_ed_46_im_0 (context);
}


/* RRD */
static void z80_ed_67_rrd (Z80_Context *context)
{
    uint16_t_Split shifted;

    /* Calculate 12-bit value */
    shifted.l = context->memory_read (context->parent, context->state.hl);
    shifted.h = context->state.a & 0x0f;
    shifted.w = (shifted.w >> 4) | ((shifted.w & 0x000f) << 8);

    /* Lower 8 bits go to memory */
    context->memory_write (context->parent, context->state.hl, shifted.l);

    /* Upper 4 bits go to A */
    context->state.a = (context->state.a & 0xf0) | shifted.h;

    SET_FLAGS_RLD_RRD;
    context->used_cycles += 18;
}


/* IN L, (C) */
static void z80_ed_68_in_l_c (Z80_Context *context)
{
    context->state.l = context->io_read (context->parent, context->state.c);
    SET_FLAGS_ED_IN (context->state.l);
    context->used_cycles += 12;
}


/* OUT (C), L */
static void z80_ed_69_out_c_l (Z80_Context *context)
{
    context->io_write (context->parent, context->state.c, context->state.l);
    context->used_cycles += 12;
}


/* ADC HL, HL */
static void z80_ed_6a_adc_hl_hl (Z80_Context *context)
{
    uint16_t temp = context->state.hl + context->state.flag_carry;
    SET_FLAGS_ADC_16 (context->state.hl);
    context->state.hl += temp;
    SET_FLAGS_XY_16 (context->state.hl);
    context->used_cycles += 15;
}


/* LD HL, (**) (undocumented) */
static void z80_ed_6b_ld_hl_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    context->state.l = context->memory_read (context->parent, addr.w);
    context->state.h = context->memory_read (context->parent, addr.w + 1);
    context->used_cycles += 20;
}


/* NEG (undocumented) */
static void z80_ed_6c_neg (Z80_Context *context)
{
    z80_ed_44_neg (context);
}


/* RETN */
static void z80_ed_6d_retn (Z80_Context *context)
{
    z80_ed_45_retn (context);
}


/* IM 0 (undocumented) */
static void z80_ed_6e_im_0 (Z80_Context *context)
{
    z80_ed_46_im_0 (context);
}


/* RLD */
static void z80_ed_6f_rld (Z80_Context *context)
{
    uint16_t_Split shifted;

    /* Calculate 12-bit value */
    shifted.w = ((uint16_t) context->memory_read (context->parent, context->state.hl) << 4) | (context->state.a & 0x0f);

    /* Lower 8 bits go to memory */
    context->memory_write (context->parent, context->state.hl, shifted.l);

    /* Upper 4 bits go to A */
    context->state.a = (context->state.a & 0xf0) | shifted.h;

    SET_FLAGS_RLD_RRD;
    context->used_cycles += 18;
}


/* IN (C) (undocumented) */
static void z80_ed_70_in_c (Z80_Context *context)
{
    uint8_t throwaway;
    throwaway = context->io_read (context->parent, context->state.c);
    SET_FLAGS_ED_IN (throwaway);
    context->used_cycles += 12;
}


/* OUT (C), 0 (undocumented) */
static void z80_ed_71_out_c_0 (Z80_Context *context)
{
    context->io_write (context->parent, context->state.c, 0);
    context->used_cycles += 12;
}


/* SBC HL, SP */
static void z80_ed_72_sbc_hl_sp (Z80_Context *context)
{
    uint16_t temp = context->state.sp + context->state.flag_carry;
    SET_FLAGS_SBC_16 (context->state.sp);
    context->state.hl -= temp;
    SET_FLAGS_XY_16 (context->state.hl);
    context->used_cycles += 15;
}


/* LD (**), SP */
static void z80_ed_73_ld_xx_sp (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    context->memory_write (context->parent, addr.w,     context->state.sp_l);
    context->memory_write (context->parent, addr.w + 1, context->state.sp_h);
    context->used_cycles += 20;
}


/* NEG (undocumented) */
static void z80_ed_74_neg (Z80_Context *context)
{
    z80_ed_44_neg (context);
}


/* RETN */
static void z80_ed_75_retn (Z80_Context *context)
{
    z80_ed_45_retn (context);
}


/* IM 1 */
static void z80_ed_76_im_1 (Z80_Context *context)
{
    z80_ed_56_im_1 (context);
}


/* IN A, (C) */
static void z80_ed_78_in_a_c (Z80_Context *context)
{
    context->state.a = context->io_read (context->parent, context->state.c);
    SET_FLAGS_ED_IN (context->state.a);
    context->used_cycles += 12;
}


/* OUT (C), A */
static void z80_ed_79_out_c_a (Z80_Context *context)
{
    context->io_write (context->parent, context->state.c, context->state.a);
    context->used_cycles += 12;
}


/* ADC HL, SP */
static void z80_ed_7a_adc_hl_sp (Z80_Context *context)
{
    uint16_t temp = context->state.sp + context->state.flag_carry;
    SET_FLAGS_ADC_16 (context->state.sp);
    context->state.hl += temp;
    SET_FLAGS_XY_16 (context->state.hl);
    context->used_cycles += 15;
}


/* LD SP, (**) */
static void z80_ed_7b_ld_sp_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    context->state.sp_l = context->memory_read (context->parent, addr.w);
    context->state.sp_h = context->memory_read (context->parent, addr.w + 1);
    context->used_cycles += 20;
}


/* NEG (undocumented) */
static void z80_ed_7c_neg (Z80_Context *context)
{
    z80_ed_44_neg (context);
}


/* RETN */
static void z80_ed_7d_retn (Z80_Context *context)
{
    z80_ed_45_retn (context);
}


/* IM 2 */
static void z80_ed_7e_im_2 (Z80_Context *context)
{
    z80_ed_5e_im_2 (context);
}


/* LDI */
static void z80_ed_a0_ldi (Z80_Context *context)
{
    uint8_t value = context->memory_read (context->parent, context->state.hl);
    context->memory_write (context->parent, context->state.de, value);
    value += context->state.a;
    context->state.hl++;
    context->state.de++;
    context->state.bc--;
    context->state.flag_sub = 0;
    context->state.flag_parity_overflow = (context->state.bc != 0);
    context->state.flag_half = 0;
    context->state.flag_x = value >> 3;
    context->state.flag_y = value >> 1;
    context->used_cycles += 16;
}


/* CPI */
static void z80_ed_a1_cpi (Z80_Context *context)
{
    uint8_t value = context->memory_read (context->parent, context->state.hl);
    context->state.hl++;
    context->state.bc--;
    context->state.flag_sub = 1;
    context->state.flag_parity_overflow = (context->state.bc != 0);
    context->state.flag_half = ((context->state.a & 0x0f) - (value & 0x0f)) >> 4;
    value = context->state.a - value;
    context->state.flag_zero = (value == 0);
    context->state.flag_sign = value >> 7;
    value -= context->state.flag_half;
    context->state.flag_x = value >> 3;
    context->state.flag_y = value >> 1;
    context->used_cycles += 16;
}


/* INI */
static void z80_ed_a2_ini (Z80_Context *context)
{
    context->memory_write (context->parent, context->state.hl, context->io_read (context->parent, context->state.c));
    context->state.hl++;
    context->state.b--;
    context->state.flag_sub = 1;
    context->state.flag_parity_overflow = 0;
    context->state.flag_half = 0;
    context->state.flag_zero = (context->state.b == 0);
    context->state.flag_sign = 0;

    context->used_cycles += 16;
}


/* OUTI */
static void z80_ed_a3_outi (Z80_Context *context)
{
    context->io_write (context->parent, context->state.c, context->memory_read (context->parent, context->state.hl));
    context->state.hl++;
    context->state.b--;
    context->state.flag_sub = 1;
    context->state.flag_parity_overflow = 0;
    context->state.flag_half = 0;
    context->state.flag_zero = (context->state.b == 0);
    context->state.flag_sign = 0;
    context->used_cycles += 16;
}


/* LDD */
static void z80_ed_a8_ldd (Z80_Context *context)
{
    uint8_t value = context->memory_read (context->parent, context->state.hl);
    context->memory_write (context->parent, context->state.de, value);
    value += context->state.a;
    context->state.hl--;
    context->state.de--;
    context->state.bc--;
    context->state.flag_sub = 0;
    context->state.flag_parity_overflow = (context->state.bc != 0);
    context->state.flag_half = 0;
    context->state.flag_x = value >> 3;
    context->state.flag_y = value >> 1;
    context->used_cycles += 16;
}


/* CPD */
static void z80_ed_a9_cpd (Z80_Context *context)
{
    uint8_t value = context->memory_read (context->parent, context->state.hl);
    context->state.hl--;
    context->state.bc--;
    context->state.flag_sub = 1;
    context->state.flag_parity_overflow = (context->state.bc != 0);
    context->state.flag_half = ((context->state.a & 0x0f) - (value & 0x0f)) >> 4;
    value = context->state.a - value;
    context->state.flag_zero = (value == 0);
    context->state.flag_sign = value >> 7;
    value -= context->state.flag_half;
    context->state.flag_x = value >> 3;
    context->state.flag_y = value >> 1;
    context->used_cycles += 16;
}


/* IND */
static void z80_ed_aa_ind (Z80_Context *context)
{
    context->memory_write (context->parent, context->state.hl, context->io_read (context->parent, context->state.c));
    context->state.hl--;
    context->state.b--;
    context->state.flag_sub = 1;
    context->state.flag_parity_overflow = 0;
    context->state.flag_half = 0;
    context->state.flag_zero = (context->state.b == 0);
    context->state.flag_sign = 0;
    context->used_cycles += 16;
}


/* OUTD */
static void z80_ed_ab_outd (Z80_Context *context)
{
    /* TODO: Implement 'unknown' flag behaviour.
     *       Described in 'The Undocumented Z80 Documented'. */
    uint8_t temp = context->memory_read (context->parent, context->state.hl);
    context->state.b--;
    context->io_write (context->parent, context->state.c, temp);
    context->state.hl--;
    context->state.flag_sub = 1;
    context->state.flag_zero = (context->state.b == 0);
    context->used_cycles += 16;
}


/* LDIR */
static void z80_ed_b0_ldir (Z80_Context *context)
{
    uint8_t value = context->memory_read (context->parent, context->state.hl);
    context->memory_write (context->parent, context->state.de, value);
    value += context->state.a;
    context->state.hl++;
    context->state.de++;
    context->state.bc--;
    context->state.flag_sub = 0;
    context->state.flag_parity_overflow = (context->state.bc != 0);
    context->state.flag_half = 0;
    context->state.flag_x = value >> 3;
    context->state.flag_y = value >> 1;
    if (context->state.bc)
    {
        context->state.pc -= 2;
        context->used_cycles += 21;
    }
    else
    {
        context->used_cycles += 16;
    }
}


/* CPIR */
static void z80_ed_b1_cpir (Z80_Context *context)
{
    uint8_t value = context->memory_read (context->parent, context->state.hl);
    context->state.hl++;
    context->state.bc--;
    if (context->state.bc != 0 && context->state.a != value)
    {
        context->state.pc -= 2;
        context->used_cycles += 21;
    }
    else
    {
        context->used_cycles += 16;
    }
    context->state.flag_sub = 1;
    context->state.flag_parity_overflow = (context->state.bc != 0);
    context->state.flag_half = ((context->state.a & 0x0f) - (value & 0x0f)) >> 4;
    value = context->state.a - value;
    context->state.flag_zero = (value == 0);
    context->state.flag_sign = value >> 7;
    value -= context->state.flag_half;
    context->state.flag_x = value >> 3;
    context->state.flag_y = value >> 1;
}


/* INIR */
static void z80_ed_b2_inir (Z80_Context *context)
{
    context->memory_write (context->parent, context->state.hl, context->io_read (context->parent, context->state.c));
    context->state.hl++;
    context->state.b--;
    context->state.flag_sub = 1;
    context->state.flag_parity_overflow = 0;
    context->state.flag_half = 0;
    context->state.flag_zero = 1;
    context->state.flag_sign = 0;
    if (context->state.b == 0)
    {
        context->used_cycles += 16;
    }
    else
    {
        context->state.pc -= 2;
        context->used_cycles += 21;
    }
}


/* OTIR */
static void z80_ed_b3_otir (Z80_Context *context)
{
    context->io_write (context->parent, context->state.c, context->memory_read (context->parent, context->state.hl));
    context->state.hl++;
    context->state.b--;
    context->state.flag_sub = 1;
    context->state.flag_parity_overflow = 0;
    context->state.flag_half = 0;
    context->state.flag_zero = 1;
    context->state.flag_sign = 0;
    if (context->state.b)
    {
        context->state.pc -= 2;
        context->used_cycles += 21;
    }
    else
    {
        context->used_cycles += 16;
    }
}


/* LDDR */
static void z80_ed_b8_lddr (Z80_Context *context)
{
    uint8_t value = context->memory_read (context->parent, context->state.hl);
    context->memory_write (context->parent, context->state.de, value);
    value += context->state.a;
    context->state.hl--;
    context->state.de--;
    context->state.bc--;
    context->state.flag_sub = 0;
    context->state.flag_parity_overflow = (context->state.bc != 0);
    context->state.flag_half = 0;
    context->state.flag_x = value >> 3;
    context->state.flag_y = value >> 1;
    if (context->state.bc)
    {
        context->state.pc -= 2;
        context->used_cycles += 21;
    }
    else
    {
        context->used_cycles += 16;
    }
}


/* CPDR */
static void z80_ed_b9_cpdr (Z80_Context *context)
{
    uint8_t value = context->memory_read (context->parent, context->state.hl);
    context->state.hl--;
    context->state.bc--;
    context->state.flag_sub = 1;
    context->state.flag_parity_overflow = (context->state.bc != 0);
    context->state.flag_half = ((context->state.a & 0x0f) - (value & 0x0f)) >> 4;
    if (context->state.bc != 0 && context->state.a != value)
    {
        context->state.pc -= 2;
        context->used_cycles += 21;
    }
    else
    {
        context->used_cycles += 16;
    }
    value = context->state.a - value;
    context->state.flag_zero = (value == 0);
    context->state.flag_sign = value >> 7;
    value -= context->state.flag_half;
    context->state.flag_x = value >> 3;
    context->state.flag_y = value >> 1;
}


/* INDR */
static void z80_ed_ba_indr (Z80_Context *context)
{
    context->memory_write (context->parent, context->state.hl, context->io_read (context->parent, context->state.c));
    context->state.hl--;
    context->state.b--;
    context->state.flag_sub = 1;
    context->state.flag_parity_overflow = 0;
    context->state.flag_half = 0;
    context->state.flag_zero = 1;
    context->state.flag_sign = 0;
    if (context->state.b == 0)
    {
        context->used_cycles += 16;
    }
    else
    {
        context->state.pc -= 2;
        context->used_cycles += 21;
    }
}


/* OTDR */
static void z80_ed_bb_otdr (Z80_Context *context)
{
    context->io_write (context->parent, context->state.c, context->memory_read (context->parent, context->state.hl));
    context->state.hl--;
    context->state.b--;
    context->state.flag_sub = 1;
    context->state.flag_parity_overflow = 0;
    context->state.flag_half = 0;
    context->state.flag_zero = 1;
    context->state.flag_sign = 0;
    if (context->state.b)
    {
        context->state.pc -= 2;
        context->used_cycles += 21;
    }
    else
    {
        context->used_cycles += 16;
    }
}


/* NOP (undocumented) */
static void z80_ed_xx_nop (Z80_Context *context)
{
    context->used_cycles += 8;
}


void (*z80_ed_instruction [256]) (Z80_Context *context) = {
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
static void z80_00_nop (Z80_Context *context)
{
    context->used_cycles += 4;
}


/* LD BC, ** */
static void z80_01_ld_bc_xx (Z80_Context *context)
{
    context->state.c = context->memory_read (context->parent, context->state.pc++);
    context->state.b = context->memory_read (context->parent, context->state.pc++);
    context->used_cycles += 10;
}


/* LD (BC), A */
static void z80_02_ld_bc_a (Z80_Context *context)
{
    context->memory_write (context->parent, context->state.bc, context->state.a);
    context->used_cycles += 7;
}


/* INC BC */
static void z80_03_inc_bc (Z80_Context *context)
{
    context->state.bc++;
    context->used_cycles += 6;
}


/* INC B */
static void z80_04_inc_b (Z80_Context *context)
{
    context->state.b++;
    SET_FLAGS_INC (context->state.b);
    SET_FLAGS_XY (context->state.b);
    context->used_cycles += 4;
}


/* DEC B */
static void z80_05_dec_b (Z80_Context *context)
{
    context->state.b--;
    SET_FLAGS_DEC (context->state.b);
    SET_FLAGS_XY (context->state.b);
    context->used_cycles += 4;
}


/* LD B, * */
static void z80_06_ld_b_x (Z80_Context *context)
{
    context->state.b = context->memory_read (context->parent, context->state.pc++);
    context->used_cycles += 7;
}


/* RLCA */
static void z80_07_rlca (Z80_Context *context)
{
    context->state.a = (context->state.a << 1) | (context->state.a >> 7);
    context->state.flag_carry = context->state.a;
    context->state.flag_sub = 0;
    context->state.flag_half = 0;
    context->used_cycles += 4;
}


/* EX AF AF' */
static void z80_08_ex_af_af (Z80_Context *context)
{
    SWAP (uint16_t, context->state.af, context->state.af_alt);
    context->used_cycles += 4;
}


/* ADD HL, BC */
static void z80_09_add_hl_bc (Z80_Context *context)
{
    SET_FLAGS_ADD_16 (context->state.hl, context->state.bc);
    context->state.hl += context->state.bc;
    SET_FLAGS_XY_16 (context->state.hl);
    context->used_cycles += 11;
}


/* LD A, (BC) */
static void z80_0a_ld_a_bc (Z80_Context *context)
{
    context->state.a = context->memory_read (context->parent, context->state.bc);
    context->used_cycles += 7;
}


/* DEC BC */
static void z80_0b_dec_bc (Z80_Context *context)
{
    context->state.bc--;
    context->used_cycles += 6;
}


/* INC C */
static void z80_0c_inc_c (Z80_Context *context)
{
    context->state.c++;
    SET_FLAGS_INC (context->state.c);
    SET_FLAGS_XY (context->state.c);
    context->used_cycles += 4;
}


/* DEC C */
static void z80_0d_dec_c (Z80_Context *context)
{
    context->state.c--;
    SET_FLAGS_DEC (context->state.c);
    SET_FLAGS_XY (context->state.c);
    context->used_cycles += 4;
}


/* LD C, * */
static void z80_0e_ld_c_x (Z80_Context *context)
{
    context->state.c = context->memory_read (context->parent, context->state.pc++);
    context->used_cycles += 7;
}


/* RRCA */
static void z80_0f_rrca (Z80_Context *context)
{
    context->state.a = (context->state.a >> 1) | (context->state.a << 7);
    context->state.flag_carry = context->state.a >> 7;
    context->state.flag_sub = 0;
    context->state.flag_half = 0;
    context->used_cycles += 4;
}


/* DJNZ */
static void z80_10_djnz (Z80_Context *context)
{
    uint8_t imm = context->memory_read (context->parent, context->state.pc++);

    if (--context->state.b)
    {
        context->state.pc += (int8_t) imm;
        context->used_cycles += 13;
    }
    else
    {
        context->used_cycles += 8;
    }
}


/* LD DE, ** */
static void z80_11_ld_de_xx (Z80_Context *context)
{
    context->state.e = context->memory_read (context->parent, context->state.pc++);
    context->state.d = context->memory_read (context->parent, context->state.pc++);
    context->used_cycles += 10;
}


/* LD (DE), A */
static void z80_12_ld_de_a (Z80_Context *context)
{
    context->memory_write (context->parent, context->state.de, context->state.a);
    context->used_cycles += 7;
}


/* INC DE */
static void z80_13_inc_de (Z80_Context *context)
{
    context->state.de++;
    context->used_cycles += 6;
}


/* INC D */
static void z80_14_inc_d (Z80_Context *context)
{
    context->state.d++;
    SET_FLAGS_INC (context->state.d);
    SET_FLAGS_XY (context->state.d);
    context->used_cycles += 4;
}


/* DEC D */
static void z80_15_dec_d (Z80_Context *context)
{
    context->state.d--;
    SET_FLAGS_DEC (context->state.d);
    SET_FLAGS_XY (context->state.d);
    context->used_cycles += 4;
}


/* LD D, * */
static void z80_16_ld_d_x (Z80_Context *context)
{
    context->state.d = context->memory_read (context->parent, context->state.pc++);
    context->used_cycles += 7;
}


/* RLA */
static void z80_17_rla (Z80_Context *context)
{
    uint8_t temp = context->state.a;
    context->state.a = (context->state.a << 1) + context->state.flag_carry;
    context->state.flag_carry = temp >> 7;
    context->state.flag_sub = 0;
    context->state.flag_half = 0;
    context->used_cycles += 4;
}


/* JR */
static void z80_18_jr (Z80_Context *context)
{
    uint8_t imm = context->memory_read (context->parent, context->state.pc++);
    context->state.pc += (int8_t) imm;
    context->used_cycles += 12;
}


/* ADD HL, DE */
static void z80_19_add_hl_de (Z80_Context *context)
{
    SET_FLAGS_ADD_16 (context->state.hl, context->state.de);
    context->state.hl += context->state.de;
    SET_FLAGS_XY_16 (context->state.hl);
    context->used_cycles += 11;
}


/* LD A, (DE) */
static void z80_1a_ld_a_de (Z80_Context *context)
{
    context->state.a = context->memory_read (context->parent, context->state.de);
    context->used_cycles += 7;
}


/* DEC DE */
static void z80_1b_dec_de (Z80_Context *context)
{
    context->state.de--;
    context->used_cycles += 6;
}


/* INC E */
static void z80_1c_inc_e (Z80_Context *context)
{
    context->state.e++;
    SET_FLAGS_INC (context->state.e);
    SET_FLAGS_XY (context->state.e);
    context->used_cycles += 4;
}


/* DEC E */
static void z80_1d_dec_e (Z80_Context *context)
{
    context->state.e--;
    SET_FLAGS_DEC (context->state.e);
    SET_FLAGS_XY (context->state.e);
    context->used_cycles += 4;
}


/* LD E, * */
static void z80_1e_ld_e_x (Z80_Context *context)
{
    context->state.e = context->memory_read (context->parent, context->state.pc++);
    context->used_cycles += 7;
}


/* RRA */
static void z80_1f_rra (Z80_Context *context)
{
    uint8_t temp = context->state.a;
    context->state.a = (context->state.a >> 1) + (context->state.flag_carry << 7);
    context->state.flag_carry = temp;
    context->state.flag_sub = 0;
    context->state.flag_half = 0;
    context->used_cycles += 4;
}


/* JR NZ */
static void z80_20_jr_nz (Z80_Context *context)
{
    uint8_t imm = context->memory_read (context->parent, context->state.pc++);

    if (context->state.flag_zero)
    {
        context->used_cycles += 7;
    }
    else
    {
        context->state.pc += (int8_t) imm;
        context->used_cycles += 12;
    }
}


/* LD HL, ** */
static void z80_21_ld_hl_xx (Z80_Context *context)
{
    context->state.l = context->memory_read (context->parent, context->state.pc++);
    context->state.h = context->memory_read (context->parent, context->state.pc++);
    context->used_cycles += 10;
}


/* LD (**), HL */
static void z80_22_ld_xx_hl (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);
    context->memory_write (context->parent, addr.w,     context->state.l);
    context->memory_write (context->parent, addr.w + 1, context->state.h);
    context->used_cycles += 16;
}


/* INC HL */
static void z80_23_inc_hl (Z80_Context *context)
{
    context->state.hl++;
    context->used_cycles += 6;
}


/* INC H */
static void z80_24_inc_h (Z80_Context *context)
{
    context->state.h++;
    SET_FLAGS_INC (context->state.h);
    SET_FLAGS_XY (context->state.h);
    context->used_cycles += 4;
}


/* DEC H */
static void z80_25_dec_h (Z80_Context *context)
{
    context->state.h--;
    SET_FLAGS_DEC (context->state.h);
    SET_FLAGS_XY (context->state.h);
    context->used_cycles += 4;
}


/* LD H, * */
static void z80_26_ld_h_x (Z80_Context *context)
{
    context->state.h = context->memory_read (context->parent, context->state.pc++);
    context->used_cycles += 7;
}


/* DAA */
static void z80_27_daa (Z80_Context *context)
{
    bool set_carry = false;
    bool set_half = false;
    uint8_t diff = 0x00;

    /* Calculate diff to apply */
    switch (context->state.f & (Z80_FLAG_CARRY | Z80_FLAG_HALF))
    {
        case Z80_FLAG_NONE:
                 if ((context->state.a & 0xf0) < 0xa0 && (context->state.a & 0x0f) < 0x0a)    diff = 0x00;
            else if ((context->state.a & 0xf0) < 0x90 && (context->state.a & 0x0f) > 0x09)    diff = 0x06;
            else if ((context->state.a & 0xf0) > 0x90 && (context->state.a & 0x0f) < 0x0a)    diff = 0x60;
            else if ((context->state.a & 0xf0) > 0x80 && (context->state.a & 0x0f) > 0x09)    diff = 0x66;
            break;
        case Z80_FLAG_HALF:
                 if ((context->state.a & 0xf0) < 0xa0 && (context->state.a & 0x0f) < 0x0a)    diff = 0x06;
            else if ((context->state.a & 0xf0) < 0x90 && (context->state.a & 0x0f) > 0x09)    diff = 0x06;
            else if ((context->state.a & 0xf0) > 0x80 && (context->state.a & 0x0f) > 0x09)    diff = 0x66;
            else if ((context->state.a & 0xf0) > 0x90 && (context->state.a & 0x0f) < 0x0a)    diff = 0x66;
            break;
        case Z80_FLAG_CARRY:
                 if (                              (context->state.a & 0x0f) < 0x0a)     diff = 0x60;
            else if (                              (context->state.a & 0x0f) > 0x09)     diff = 0x66;
            break;
        case Z80_FLAG_CARRY | Z80_FLAG_HALF:
                                                                                    diff = 0x66;
            break;
    }

    /* Calculate carry out */
    if (((context->state.a & 0xf0) > 0x80 && (context->state.a & 0x0f) > 0x09) ||
        ((context->state.a & 0xf0) > 0x90 && (context->state.a & 0x0f) < 0x0a) ||
        (context->state.f & Z80_FLAG_CARRY))
    {
        set_carry = true;
    }

    /* Calculate half-carry out */
    if ( (!context->state.flag_sub && (context->state.a & 0x0f) > 0x09) ||
         ( context->state.flag_sub && context->state.flag_half && (context->state.a & 0x0f) < 0x06))
    {
        set_half = true;
    }

    /* Apply diff */
    if (context->state.flag_sub)
    {
        context->state.a -= diff;
    }
    else
    {
        context->state.a += diff;
    }

    context->state.flag_carry = set_carry;
    context->state.flag_parity_overflow = uint8_even_parity [context->state.a];
    context->state.flag_half = set_half;
    context->state.flag_zero = (context->state.a == 0x00);
    context->state.flag_sign = context->state.a >> 7;

    context->used_cycles += 4;
}


/* JR Z */
static void z80_28_jr_z (Z80_Context *context)
{
    uint8_t imm = context->memory_read (context->parent, context->state.pc++);

    if (context->state.flag_zero)
    {
        context->state.pc += (int8_t) imm;
        context->used_cycles += 12;
    }
    else
    {
        context->used_cycles += 7;
    }
}


/* ADD HL, HL */
static void z80_29_add_hl_hl (Z80_Context *context)
{
    SET_FLAGS_ADD_16 (context->state.hl, context->state.hl);
    context->state.hl += context->state.hl;
    SET_FLAGS_XY_16 (context->state.hl);
    context->used_cycles += 11;
}


/* LD, HL, (**) */
static void z80_2a_ld_hl_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);
    context->state.l = context->memory_read (context->parent, addr.w);
    context->state.h = context->memory_read (context->parent, addr.w + 1);
    context->used_cycles += 16;
}


/* DEC HL */
static void z80_2b_dec_hl (Z80_Context *context)
{
    context->state.hl--;
    context->used_cycles += 6;
}


/* INC L */
static void z80_2c_inc_l (Z80_Context *context)
{
    context->state.l++;
    SET_FLAGS_INC (context->state.l);
    SET_FLAGS_XY (context->state.l);
    context->used_cycles += 4;
}


/* DEC L */
static void z80_2d_dec_l (Z80_Context *context)
{
    context->state.l--;
    SET_FLAGS_DEC (context->state.l);
    SET_FLAGS_XY (context->state.l);
    context->used_cycles += 4;
}


/* LD L, * */
static void z80_2e_ld_l_x (Z80_Context *context)
{
    context->state.l = context->memory_read (context->parent, context->state.pc++);
    context->used_cycles += 7;
}


/* CPL */
static void z80_2f_cpl (Z80_Context *context)
{
    context->state.a = ~context->state.a;
    context->state.flag_sub = 1;
    context->state.flag_half = 1;
    context->used_cycles += 4;
}


/* JR NC */
static void z80_30_jr_nc (Z80_Context *context)
{
    uint8_t imm = context->memory_read (context->parent, context->state.pc++);

    if (context->state.flag_carry)
    {
        context->used_cycles += 7;
    }
    else
    {
        context->state.pc += (int8_t) imm;
        context->used_cycles += 12;
    }
}


/* LD SP, ** */
static void z80_31_ld_sp_xx (Z80_Context *context)
{
    context->state.sp_l = context->memory_read (context->parent, context->state.pc++);
    context->state.sp_h = context->memory_read (context->parent, context->state.pc++);
    context->used_cycles += 10;
}


/* LD (**), A */
static void z80_32_ld_xx_a (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);
    context->memory_write (context->parent, addr.w, context->state.a);
    context->used_cycles += 13;
}


/* INC SP */
static void z80_33_inc_sp (Z80_Context *context)
{
    context->state.sp++;
    context->used_cycles += 6;
}


/* INC (HL) */
static void z80_34_inc_hl (Z80_Context *context)
{
    uint8_t value = context->memory_read (context->parent, context->state.hl);
    value++;
    context->memory_write (context->parent, context->state.hl, value);
    SET_FLAGS_INC (value);
    SET_FLAGS_XY (value);
    context->used_cycles += 11;
}


/* DEC (HL) */
static void z80_35_dec_hl (Z80_Context *context)
{
    uint8_t value = context->memory_read (context->parent, context->state.hl);
    value--;
    context->memory_write (context->parent, context->state.hl, value);
    SET_FLAGS_DEC (value);
    SET_FLAGS_XY (value);
    context->used_cycles += 11;
}


/* LD (HL), * */
static void z80_36_ld_hl_x (Z80_Context *context)
{
    context->memory_write (context->parent, context->state.hl, context->memory_read (context->parent, context->state.pc++));
    context->used_cycles += 10;
}


/* SCF */
static void z80_37_scf (Z80_Context *context)
{
    context->state.flag_carry = 1;
    context->state.flag_sub = 0;
    context->state.flag_half = 0;
    context->used_cycles += 4;
}


/* JR C, * */
static void z80_38_jr_c_x (Z80_Context *context)
{
    uint8_t imm = context->memory_read (context->parent, context->state.pc++);

    if (context->state.flag_carry)
    {
        context->state.pc += (int8_t) imm;
        context->used_cycles += 12;
    }
    else
    {
        context->used_cycles += 7;
    }
}


/* ADD HL, SP */
static void z80_39_add_hl_sp (Z80_Context *context)
{
    SET_FLAGS_ADD_16 (context->state.hl, context->state.sp);
    context->state.hl += context->state.sp;
    SET_FLAGS_XY_16 (context->state.hl);
    context->used_cycles += 11;
}


/* LD, A, (**) */
static void z80_3a_ld_a_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);
    context->state.a = context->memory_read (context->parent, addr.w);
    context->used_cycles += 13;
}


/* DEC SP */
static void z80_3b_dec_sp (Z80_Context *context)
{
    context->state.sp--;
    context->used_cycles += 6;
}


/* INC A */
static void z80_3c_inc_a (Z80_Context *context)
{
    context->state.a++;
    SET_FLAGS_INC (context->state.a);
    SET_FLAGS_XY (context->state.a);
    context->used_cycles += 4;
}

/* DEC A */
static void z80_3d_dec_a (Z80_Context *context)
{
    context->state.a--;
    SET_FLAGS_DEC (context->state.a);
    SET_FLAGS_XY (context->state.a);
    context->used_cycles += 4;
}


/* LD A, * */
static void z80_3e_ld_a_x (Z80_Context *context)
{
    context->state.a = context->memory_read (context->parent, context->state.pc++);
    context->used_cycles += 7;
}


/* CCF */
static void z80_3f_ccf (Z80_Context *context)
{
    context->state.flag_sub = 0;
    context->state.flag_half = context->state.flag_carry;
    context->state.flag_carry = ~context->state.flag_carry;
    context->used_cycles += 4;
}


/* LD B, B */
static void z80_40_ld_b_b (Z80_Context *context)
{
    context->used_cycles += 4;
}


/* LD B, C */
static void z80_41_ld_b_c (Z80_Context *context)
{
    context->state.b = context->state.c;
    context->used_cycles += 4;
}


/* LD B, D */
static void z80_42_ld_b_d (Z80_Context *context)
{
    context->state.b = context->state.d;
    context->used_cycles += 4;
}


/* LD B, E */
static void z80_43_ld_b_e (Z80_Context *context)
{
    context->state.b = context->state.e;
    context->used_cycles += 4;
}


/* LD B, H */
static void z80_44_ld_b_h (Z80_Context *context)
{
    context->state.b = context->state.h;
    context->used_cycles += 4;
}


/* LD B, L */
static void z80_45_ld_b_l (Z80_Context *context)
{
    context->state.b = context->state.l;
    context->used_cycles += 4;
}


/* LD B, (HL) */
static void z80_46_ld_b_hl (Z80_Context *context)
{
    context->state.b = context->memory_read (context->parent, context->state.hl);
    context->used_cycles += 7;
}


/* LD B, A */
static void z80_47_ld_b_a (Z80_Context *context)
{
    context->state.b = context->state.a;
    context->used_cycles += 4;
}


/* LD C, B */
static void z80_48_ld_c_b (Z80_Context *context)
{
    context->state.c = context->state.b;
    context->used_cycles += 4;
}


/* LD C, C */
static void z80_49_ld_c_c (Z80_Context *context)
{
    context->used_cycles += 4;
}


/* LD C, D */
static void z80_4a_ld_c_d (Z80_Context *context)
{
    context->state.c = context->state.d;
    context->used_cycles += 4;
}


/* LD C, E */
static void z80_4b_ld_c_e (Z80_Context *context)
{
    context->state.c = context->state.e;
    context->used_cycles += 4;
}


/* LD C, H */
static void z80_4c_ld_c_h (Z80_Context *context)
{
    context->state.c = context->state.h;
    context->used_cycles += 4;
}


/* LD C, L */
static void z80_4d_ld_c_l (Z80_Context *context)
{
    context->state.c = context->state.l;
    context->used_cycles += 4;
}


/* LD C, (HL) */
static void z80_4e_ld_c_hl (Z80_Context *context)
{
    context->state.c = context->memory_read (context->parent, context->state.hl);
    context->used_cycles += 7;
}


/* LD C, A */
static void z80_4f_ld_c_a (Z80_Context *context)
{
    context->state.c = context->state.a;
    context->used_cycles += 4;
}


/* LD D, B */
static void z80_50_ld_d_b (Z80_Context *context)
{
    context->state.d = context->state.b;
    context->used_cycles += 4;
}


/* LD D, C */
static void z80_51_ld_d_c (Z80_Context *context)
{
    context->state.d = context->state.c;
    context->used_cycles += 4;
}


/* LD D, D */
static void z80_52_ld_d_d (Z80_Context *context)
{
    context->used_cycles += 4;
}


/* LD D, E */
static void z80_53_ld_d_e (Z80_Context *context)
{
    context->state.d = context->state.e;
    context->used_cycles += 4;
}


/* LD D, H */
static void z80_54_ld_d_h (Z80_Context *context)
{
    context->state.d = context->state.h;
    context->used_cycles += 4;
}


/* LD D, L */
static void z80_55_ld_d_l (Z80_Context *context)
{
    context->state.d = context->state.l;
    context->used_cycles += 4;
}


/* LD D, (HL) */
static void z80_56_ld_d_hl (Z80_Context *context)
{
    context->state.d = context->memory_read (context->parent, context->state.hl);
    context->used_cycles += 7;
}


/* LD D, A */
static void z80_57_ld_d_a (Z80_Context *context)
{
    context->state.d = context->state.a;
    context->used_cycles += 4;
}


/* LD E, B */
static void z80_58_ld_e_b (Z80_Context *context)
{
    context->state.e = context->state.b;
    context->used_cycles += 4;
}


/* LD E, C */
static void z80_59_ld_e_c (Z80_Context *context)
{
    context->state.e = context->state.c;
    context->used_cycles += 4;
}


/* LD E, D */
static void z80_5a_ld_e_d (Z80_Context *context)
{
    context->state.e = context->state.d;
    context->used_cycles += 4;
}


/* LD E, E */
static void z80_5b_ld_e_e (Z80_Context *context)
{
    context->used_cycles += 4;
}


/* LD E, H */
static void z80_5c_ld_e_h (Z80_Context *context)
{
    context->state.e = context->state.h;
    context->used_cycles += 4;
}


/* LD E, L */
static void z80_5d_ld_e_l (Z80_Context *context)
{
    context->state.e = context->state.l;
    context->used_cycles += 4;
}


/* LD E, (HL) */
static void z80_5e_ld_e_hl (Z80_Context *context)
{
    context->state.e = context->memory_read (context->parent, context->state.hl);
    context->used_cycles += 7;
}


/* LD E, A */
static void z80_5f_ld_e_a (Z80_Context *context)
{
    context->state.e = context->state.a;
    context->used_cycles += 4;
}


/* LD H, B */
static void z80_60_ld_h_b (Z80_Context *context)
{
    context->state.h = context->state.b;
    context->used_cycles += 4;
}


/* LD H, C */
static void z80_61_ld_h_c (Z80_Context *context)
{
    context->state.h = context->state.c;
    context->used_cycles += 4;
}


/* LD H, D */
static void z80_62_ld_h_d (Z80_Context *context)
{
    context->state.h = context->state.d;
    context->used_cycles += 4;
}


/* LD H, E */
static void z80_63_ld_h_e (Z80_Context *context)
{
    context->state.h = context->state.e;
    context->used_cycles += 4;
}


/* LD H, H */
static void z80_64_ld_h_h (Z80_Context *context)
{
    context->used_cycles += 4;
}


/* LD H, L  */
static void z80_65_ld_h_l (Z80_Context *context)
{
    context->state.h = context->state.l;
    context->used_cycles += 4;
}


/* LD H, (HL) */
static void z80_66_ld_h_hl (Z80_Context *context)
{
    context->state.h = context->memory_read (context->parent, context->state.hl);
    context->used_cycles += 7;
}


/* LD H, A */
static void z80_67_ld_h_a (Z80_Context *context)
{
    context->state.h = context->state.a;
    context->used_cycles += 4;
}


/* LD L, B */
static void z80_68_ld_l_b (Z80_Context *context)
{
    context->state.l = context->state.b;
    context->used_cycles += 4;
}


/* LD L, C */
static void z80_69_ld_l_c (Z80_Context *context)
{
    context->state.l = context->state.c;
    context->used_cycles += 4;
}


/* LD L, D */
static void z80_6a_ld_l_d (Z80_Context *context)
{
    context->state.l = context->state.d;
    context->used_cycles += 4;
}


/* LD L, E */
static void z80_6b_ld_l_e (Z80_Context *context)
{
    context->state.l = context->state.e;
    context->used_cycles += 4;
}


/* LD L, H */
static void z80_6c_ld_l_h (Z80_Context *context)
{
    context->state.l = context->state.h;
    context->used_cycles += 4;
}


/* LD L, L */
static void z80_6d_ld_l_l (Z80_Context *context)
{
    context->used_cycles += 4;
}


/* LD L, (HL) */
static void z80_6e_ld_l_hl (Z80_Context *context)
{
    context->state.l = context->memory_read (context->parent, context->state.hl);
    context->used_cycles += 7;
}


/* LD L, A */
static void z80_6f_ld_l_a (Z80_Context *context)
{
    context->state.l = context->state.a;
    context->used_cycles += 4;
}


/* LD (HL), B */
static void z80_70_ld_hl_b (Z80_Context *context)
{
    context->memory_write (context->parent, context->state.hl, context->state.b);
    context->used_cycles += 7;
}


/* LD (HL), C */
static void z80_71_ld_hl_c (Z80_Context *context)
{
    context->memory_write (context->parent, context->state.hl, context->state.c);
    context->used_cycles += 7;
}


/* LD (HL), D */
static void z80_72_ld_hl_d (Z80_Context *context)
{
    context->memory_write (context->parent, context->state.hl, context->state.d);
    context->used_cycles += 7;
}


/* LD (HL), E */
static void z80_73_ld_hl_e (Z80_Context *context)
{
    context->memory_write (context->parent, context->state.hl, context->state.e);
    context->used_cycles += 7;
}


/* LD (HL), H */
static void z80_74_ld_hl_h (Z80_Context *context)
{
    context->memory_write (context->parent, context->state.hl, context->state.h);
    context->used_cycles += 7;
}


/* LD (HL), L */
static void z80_75_ld_hl_l (Z80_Context *context)
{
    context->memory_write (context->parent, context->state.hl, context->state.l);
    context->used_cycles += 7;
}


/* HALT */
static void z80_76_halt (Z80_Context *context)
{
    context->state.pc--;
    context->state.halt = true;
    context->used_cycles += 4;
}


/* LD (HL), A */
static void z80_77_ld_hl_a (Z80_Context *context)
{
    context->memory_write (context->parent, context->state.hl, context->state.a);
    context->used_cycles += 7;
}


/* LD A, B */
static void z80_78_ld_a_b (Z80_Context *context)
{
    context->state.a = context->state.b;
    context->used_cycles += 4;
}


/* LD A, C */
static void z80_79_ld_a_c (Z80_Context *context)
{
    context->state.a = context->state.c;
    context->used_cycles += 4;
}


/* LD A, D */
static void z80_7a_ld_a_d (Z80_Context *context)
{
    context->state.a = context->state.d;
    context->used_cycles += 4;
}


/* LD A, E */
static void z80_7b_ld_a_e (Z80_Context *context)
{
    context->state.a = context->state.e;
    context->used_cycles += 4;
}


/* LD A, H */
static void z80_7c_ld_a_h (Z80_Context *context)
{
    context->state.a = context->state.h;
    context->used_cycles += 4;
}


/* LD A, L */
static void z80_7d_ld_a_l (Z80_Context *context)
{
    context->state.a = context->state.l;
    context->used_cycles += 4;
}


/* LD A, (HL) */
static void z80_7e_ld_a_hl (Z80_Context *context)
{
    context->state.a = context->memory_read (context->parent, context->state.hl);
    context->used_cycles += 7;
}


/* LD A, A */
static void z80_7f_ld_a_a (Z80_Context *context)
{
    context->used_cycles += 4;
}


/* ADD A, B */
static void z80_80_add_a_b (Z80_Context *context)
{
    SET_FLAGS_ADD (context->state.a, context->state.b);
    context->state.a += context->state.b;
    context->used_cycles += 4;
}

/* ADD A, C */
static void z80_81_add_a_c (Z80_Context *context)
{
    SET_FLAGS_ADD (context->state.a, context->state.c);
    context->state.a += context->state.c;
    context->used_cycles += 4;
}


/* ADD A, D */
static void z80_82_add_a_d (Z80_Context *context)
{
    SET_FLAGS_ADD (context->state.a, context->state.d);
    context->state.a += context->state.d;
    context->used_cycles += 4;
}


/* ADD A, E */
static void z80_83_add_a_e (Z80_Context *context)
{
    SET_FLAGS_ADD (context->state.a, context->state.e);
    context->state.a += context->state.e;
    context->used_cycles += 4;
}

/* ADD A, H */
static void z80_84_add_a_h (Z80_Context *context)
{
    SET_FLAGS_ADD (context->state.a, context->state.h);
    context->state.a += context->state.h;
    context->used_cycles += 4;
}


/* ADD A, L */
static void z80_85_add_a_l (Z80_Context *context)
{
    SET_FLAGS_ADD (context->state.a, context->state.l);
    context->state.a += context->state.l;
    context->used_cycles += 4;
}


/* ADD A, (HL) */
static void z80_86_add_a_hl (Z80_Context *context)
{
    uint8_t value = context->memory_read (context->parent, context->state.hl);
    SET_FLAGS_ADD (context->state.a, value);
    context->state.a += value;
    context->used_cycles += 7;
}


/* ADD A, A */
static void z80_87_add_a_a (Z80_Context *context)
{
    SET_FLAGS_ADD (context->state.a, context->state.a);
    context->state.a += context->state.a;
    context->used_cycles += 4;
}


/* ADC A, B */
static void z80_88_adc_a_b (Z80_Context *context)
{
    uint8_t temp = context->state.b + context->state.flag_carry;
    SET_FLAGS_ADC (context->state.b);
    context->state.a += temp;
    context->used_cycles += 4;
}


/* ADC A, C */
static void z80_89_adc_a_c (Z80_Context *context)
{
    uint8_t temp = context->state.c + context->state.flag_carry;
    SET_FLAGS_ADC (context->state.c);
    context->state.a += temp;
    context->used_cycles += 4;
}


/* ADC A, D */
static void z80_8a_adc_a_d (Z80_Context *context)
{
    uint8_t temp = context->state.d + context->state.flag_carry;
    SET_FLAGS_ADC (context->state.d);
    context->state.a += temp;
    context->used_cycles += 4;
}


/* ADC A, E */
static void z80_8b_adc_a_e (Z80_Context *context)
{
    uint8_t temp = context->state.e + context->state.flag_carry;
    SET_FLAGS_ADC (context->state.e);
    context->state.a += temp;
    context->used_cycles += 4;
}


/* ADC A, H */
static void z80_8c_adc_a_h (Z80_Context *context)
{
    uint8_t temp = context->state.h + context->state.flag_carry;
    SET_FLAGS_ADC (context->state.h);
    context->state.a += temp;
    context->used_cycles += 4;
}


/* ADC A, L */
static void z80_8d_adc_a_l (Z80_Context *context)
{
    uint8_t temp = context->state.l + context->state.flag_carry;
    SET_FLAGS_ADC (context->state.l);
    context->state.a += temp;
    context->used_cycles += 4;
}


/* ADC A, (HL) */
static void z80_8e_adc_a_hl (Z80_Context *context)
{
    uint8_t value = context->memory_read (context->parent, context->state.hl);
    uint8_t temp = value + context->state.flag_carry;
    SET_FLAGS_ADC (value);
    context->state.a += temp;
    context->used_cycles += 7;
}


/* ADC A, A */
static void z80_8f_adc_a_a (Z80_Context *context)
{
    uint8_t temp = context->state.a + context->state.flag_carry;
    SET_FLAGS_ADC (context->state.a);
    context->state.a += temp;
    context->used_cycles += 4;
}


/* SUB A, B */
static void z80_90_sub_a_b (Z80_Context *context)
{
    SET_FLAGS_SUB (context->state.a, context->state.b);
    context->state.a -= context->state.b;
    context->used_cycles += 4;
}


/* SUB A, C */
static void z80_91_sub_a_c (Z80_Context *context)
{
    SET_FLAGS_SUB (context->state.a, context->state.c);
    context->state.a -= context->state.c;
    context->used_cycles += 4;
}


/* SUB A, D */
static void z80_92_sub_a_d (Z80_Context *context)
{
    SET_FLAGS_SUB (context->state.a, context->state.d);
    context->state.a -= context->state.d;
    context->used_cycles += 4;
}


/* SUB A, E */
static void z80_93_sub_a_e (Z80_Context *context)
{
    SET_FLAGS_SUB (context->state.a, context->state.e);
    context->state.a -= context->state.e;
    context->used_cycles += 4;
}


/* SUB A, H */
static void z80_94_sub_a_h (Z80_Context *context)
{
    SET_FLAGS_SUB (context->state.a, context->state.h);
    context->state.a -= context->state.h;
    context->used_cycles += 4;
}


/* SUB A, L */
static void z80_95_sub_a_l (Z80_Context *context)
{
    SET_FLAGS_SUB (context->state.a, context->state.l);
    context->state.a -= context->state.l;
    context->used_cycles += 4;
}


/* SUB A, (HL) */
static void z80_96_sub_a_hl (Z80_Context *context)
{
    uint8_t temp = context->memory_read (context->parent, context->state.hl);
    SET_FLAGS_SUB (context->state.a, temp);
    context->state.a -= temp;
    context->used_cycles += 7;
}


/* SUB A, A */
static void z80_97_sub_a_a (Z80_Context *context)
{
    SET_FLAGS_SUB (context->state.a, context->state.a);
    context->state.a -= context->state.a;
    context->used_cycles += 4;
}


/* SBC A, B */
static void z80_98_sbc_a_b (Z80_Context *context)
{
    uint8_t temp = context->state.b + context->state.flag_carry;
    SET_FLAGS_SBC (context->state.b);
    context->state.a -= temp;
    context->used_cycles += 4;
}


/* SBC A, C */
static void z80_99_sbc_a_c (Z80_Context *context)
{
    uint8_t temp = context->state.c + context->state.flag_carry;
    SET_FLAGS_SBC (context->state.c);
    context->state.a -= temp;
    context->used_cycles += 4;
}


/* SBC A, D */
static void z80_9a_sbc_a_d (Z80_Context *context)
{
    uint8_t temp = context->state.d + context->state.flag_carry;
    SET_FLAGS_SBC (context->state.d);
    context->state.a -= temp;
    context->used_cycles += 4;
}


/* SBC A, E */
static void z80_9b_sbc_a_e (Z80_Context *context)
{
    uint8_t temp = context->state.e + context->state.flag_carry;
    SET_FLAGS_SBC (context->state.e);
    context->state.a -= temp;
    context->used_cycles += 4;
}


/* SBC A, H */
static void z80_9c_sbc_a_h (Z80_Context *context)
{
    uint8_t temp = context->state.h + context->state.flag_carry;
    SET_FLAGS_SBC (context->state.h);
    context->state.a -= temp;
    context->used_cycles += 4;
}


/* SBC A, L */
static void z80_9d_sbc_a_l (Z80_Context *context)
{
    uint8_t temp = context->state.l + context->state.flag_carry;
    SET_FLAGS_SBC (context->state.l);
    context->state.a -= temp;
    context->used_cycles += 4;
}


/* SBC A, (HL) */
static void z80_9e_sbc_a_hl (Z80_Context *context)
{
    uint8_t value = context->memory_read (context->parent, context->state.hl);
    uint8_t temp = value + context->state.flag_carry;
    SET_FLAGS_SBC (value);
    context->state.a -= temp;
    context->used_cycles += 7;
}


/* SBC A, A */
static void z80_9f_sbc_a_a (Z80_Context *context)
{
    uint8_t temp = context->state.a + context->state.flag_carry;
    SET_FLAGS_SBC (context->state.a);
    context->state.a -= temp;
    context->used_cycles += 4;
}

/* AND A, B */
static void z80_a0_and_a_b (Z80_Context *context)
{
    context->state.a &= context->state.b;
    SET_FLAGS_AND;
    context->used_cycles += 4;
}


/* AND A, C */
static void z80_a1_and_a_c (Z80_Context *context)
{
    context->state.a &= context->state.c;
    SET_FLAGS_AND;
    context->used_cycles += 4;
}


/* AND A, D */
static void z80_a2_and_a_d (Z80_Context *context)
{
    context->state.a &= context->state.d;
    SET_FLAGS_AND;
    context->used_cycles += 4;
}


/* AND A, E */
static void z80_a3_and_a_e (Z80_Context *context)
{
    context->state.a &= context->state.e;
    SET_FLAGS_AND;
    context->used_cycles += 4;
}


/* AND A, H */
static void z80_a4_and_a_h (Z80_Context *context)
{
    context->state.a &= context->state.h;
    SET_FLAGS_AND;
    context->used_cycles += 4;
}


/* AND A, L */
static void z80_a5_and_a_l (Z80_Context *context)
{
    context->state.a &= context->state.l;
    SET_FLAGS_AND;
    context->used_cycles += 4;
}


/* AND A, (HL) */
static void z80_a6_and_a_hl (Z80_Context *context)
{
    context->state.a &= context->memory_read (context->parent, context->state.hl);
    SET_FLAGS_AND;
    context->used_cycles += 7;
}


/* AND A, A */
static void z80_a7_and_a_a (Z80_Context *context)
{
    SET_FLAGS_AND;
    context->used_cycles += 4;
}


/* XOR A, B */
static void z80_a8_xor_a_b (Z80_Context *context)
{
    context->state.a ^= context->state.b;
    SET_FLAGS_OR_XOR;
    context->used_cycles += 4;
}


/* XOR A, C */
static void z80_a9_xor_a_c (Z80_Context *context)
{
    context->state.a ^= context->state.c;
    SET_FLAGS_OR_XOR;
    context->used_cycles += 4;
}


/* XOR A, D */
static void z80_aa_xor_a_d (Z80_Context *context)
{
    context->state.a ^= context->state.d;
    SET_FLAGS_OR_XOR;
    context->used_cycles += 4;
}


/* XOR A, E */
static void z80_ab_xor_a_e (Z80_Context *context)
{
    context->state.a ^= context->state.e;
    SET_FLAGS_OR_XOR;
    context->used_cycles += 4;
}


/* XOR A, H */
static void z80_ac_xor_a_h (Z80_Context *context)
{
    context->state.a ^= context->state.h;
    SET_FLAGS_OR_XOR;
    context->used_cycles += 4;
}


/* XOR A, L */
static void z80_ad_xor_a_l (Z80_Context *context)
{
    context->state.a ^= context->state.l;
    SET_FLAGS_OR_XOR;
    context->used_cycles += 4;
}


/* XOR A, (HL) */
static void z80_ae_xor_a_hl (Z80_Context *context)
{
    context->state.a ^= context->memory_read (context->parent, context->state.hl);
    SET_FLAGS_OR_XOR;
    context->used_cycles += 7;
}


/* XOR A, A */
static void z80_af_xor_a_a (Z80_Context *context)
{
    context->state.a ^= context->state.a;
    SET_FLAGS_OR_XOR;
    context->used_cycles += 4;
}


/* OR A, B */
static void z80_b0_or_a_b (Z80_Context *context)
{
    context->state.a |= context->state.b;
    SET_FLAGS_OR_XOR;
    context->used_cycles += 4;
}


/* OR A, C */
static void z80_b1_or_a_c (Z80_Context *context)
{
    context->state.a |= context->state.c;
    SET_FLAGS_OR_XOR;
    context->used_cycles += 4;
}


/* OR A, D */
static void z80_b2_or_a_d (Z80_Context *context)
{
    context->state.a |= context->state.d;
    SET_FLAGS_OR_XOR;
    context->used_cycles += 4;
}


/* OR A, E */
static void z80_b3_or_a_e (Z80_Context *context)
{
    context->state.a |= context->state.e;
    SET_FLAGS_OR_XOR;
    context->used_cycles += 4;
}


/* OR A, H */
static void z80_b4_or_a_h (Z80_Context *context)
{
    context->state.a |= context->state.h;
    SET_FLAGS_OR_XOR;
    context->used_cycles += 4;
}


/* OR A, L */
static void z80_b5_or_a_l (Z80_Context *context)
{
    context->state.a |= context->state.l;
    SET_FLAGS_OR_XOR;
    context->used_cycles += 4;
}


/* OR A, (HL) */
static void z80_b6_or_a_hl (Z80_Context *context)
{
    context->state.a |= context->memory_read (context->parent, context->state.hl);
    SET_FLAGS_OR_XOR;
    context->used_cycles += 7;
}


/* OR A, A */
static void z80_b7_or_a_a (Z80_Context *context)
{
    SET_FLAGS_OR_XOR;
    context->used_cycles += 4;
}


/* CP A, B */
static void z80_b8_cp_a_b (Z80_Context *context)
{
    SET_FLAGS_SUB (context->state.a, context->state.b);
    context->used_cycles += 4;
}


/* CP A, C */
static void z80_b9_cp_a_c (Z80_Context *context)
{
    SET_FLAGS_SUB (context->state.a, context->state.c);
    context->used_cycles += 4;
}


/* CP A, D */
static void z80_ba_cp_a_d (Z80_Context *context)
{
    SET_FLAGS_SUB (context->state.a, context->state.d);
    context->used_cycles += 4;
}


/* CP A, E */
static void z80_bb_cp_a_e (Z80_Context *context)
{
    SET_FLAGS_SUB (context->state.a, context->state.e);
    context->used_cycles += 4;
}


/* CP A, H */
static void z80_bc_cp_a_h (Z80_Context *context)
{
    SET_FLAGS_SUB (context->state.a, context->state.h);
    context->used_cycles += 4;
}


/* CP A, L */
static void z80_bd_cp_a_l (Z80_Context *context)
{
    SET_FLAGS_SUB (context->state.a, context->state.l);
    context->used_cycles += 4;
}

/* CP A, (HL) */
static void z80_be_cp_a_hl (Z80_Context *context)
{
    uint8_t value = context->memory_read (context->parent, context->state.hl);
    SET_FLAGS_SUB (context->state.a, value);
    context->used_cycles += 7;
}

/* CP A, A */
static void z80_bf_cp_a_a (Z80_Context *context)
{
    SET_FLAGS_SUB (context->state.a, context->state.a);
    context->used_cycles += 4;
}


/* RET NZ */
static void z80_c0_ret_nz (Z80_Context *context)
{
    if (context->state.flag_zero)
    {
        context->used_cycles += 5;
    }
    else
    {
        context->state.pc_l = context->memory_read (context->parent, context->state.sp++);
        context->state.pc_h = context->memory_read (context->parent, context->state.sp++);
        context->used_cycles += 11;
    }
}


/* POP BC */
static void z80_c1_pop_bc (Z80_Context *context)
{
    context->state.c = context->memory_read (context->parent, context->state.sp++);
    context->state.b = context->memory_read (context->parent, context->state.sp++);
    context->used_cycles += 10;
}


/* JP NZ, ** */
static void z80_c2_jp_nz_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    if (!context->state.flag_zero)
    {
        context->state.pc = addr.w;
    }
    context->used_cycles += 10;
}


/* JP ** */
static void z80_c3_jp_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);
    context->state.pc = addr.w;
    context->used_cycles += 10;
}


/* CALL NZ, ** */
static void z80_c4_call_nz_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    if (context->state.flag_zero)
    {
        context->used_cycles += 10;
    }
    else
    {
        context->memory_write (context->parent, --context->state.sp, context->state.pc_h);
        context->memory_write (context->parent, --context->state.sp, context->state.pc_l);
        context->state.pc = addr.w;
        context->used_cycles += 17;
    }
}


/* PUSH BC */
static void z80_c5_push_bc (Z80_Context *context)
{
    context->memory_write (context->parent, --context->state.sp, context->state.b);
    context->memory_write (context->parent, --context->state.sp, context->state.c);
    context->used_cycles += 11;
}


/* ADD A, * */
static void z80_c6_add_a_x (Z80_Context *context)
{
    uint8_t imm = context->memory_read (context->parent, context->state.pc++);
    /* ADD A,*    */
    SET_FLAGS_ADD (context->state.a, imm);
    context->state.a += imm;
    context->used_cycles += 7;
}


/* RST 00h */
static void z80_c7_rst_00 (Z80_Context *context)
{
    /* RST 00h    */
    context->memory_write (context->parent, --context->state.sp, context->state.pc_h);
    context->memory_write (context->parent, --context->state.sp, context->state.pc_l);
    context->state.pc = 0x0000;
    context->used_cycles += 11;
}


/* RET Z */
static void z80_c8_ret_z (Z80_Context *context)
{
    /* RET Z      */
    if (context->state.flag_zero)
    {
        context->state.pc_l = context->memory_read (context->parent, context->state.sp++);
        context->state.pc_h = context->memory_read (context->parent, context->state.sp++);
        context->used_cycles += 11;
    }
    else
    {
        context->used_cycles += 5;
    }
}


/* RET */
static void z80_c9_ret (Z80_Context *context)
{
    context->state.pc_l = context->memory_read (context->parent, context->state.sp++);
    context->state.pc_h = context->memory_read (context->parent, context->state.sp++);
    context->used_cycles += 10;
}


/* JP Z, ** */
static void z80_ca_jp_z_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    if (context->state.flag_zero)
    {
        context->state.pc = addr.w;
    }
    context->used_cycles += 10;
}


/* BIT PREFIX */
static void z80_cb_prefix (Z80_Context *context)
{
    uint8_t instruction = context->memory_read (context->parent, context->state.pc++);

    switch (instruction & 0x07)
    {
        case 0x00:
            context->state.b = z80_cb_instruction [instruction >> 3] (context, context->state.b);
            context->used_cycles += 8;
            break;

        case 0x01:
            context->state.c = z80_cb_instruction [instruction >> 3] (context, context->state.c);
            context->used_cycles += 8;
            break;

        case 0x02:
            context->state.d = z80_cb_instruction [instruction >> 3] (context, context->state.d);
            context->used_cycles += 8;
            break;

        case 0x03:
            context->state.e = z80_cb_instruction [instruction >> 3] (context, context->state.e);
            context->used_cycles += 8;
            break;

        case 0x04:
            context->state.h = z80_cb_instruction [instruction >> 3] (context, context->state.h);
            context->used_cycles += 8;
            break;

        case 0x05:
            context->state.l = z80_cb_instruction [instruction >> 3] (context, context->state.l);
            context->used_cycles += 8;
            break;

        case 0x06:
            if ((instruction & 0xc0) == 0x40)
            {
                /* The BIT instruction is read-only */
                z80_cb_instruction [instruction >> 3] (context, context->memory_read (context->parent, context->state.hl));
                context->used_cycles += 12;
            }
            else
            {
                context->memory_write (context->parent, context->state.hl, z80_cb_instruction [instruction >> 3] (context, context->memory_read (context->parent, context->state.hl)));
                context->used_cycles += 15;
            }
            break;

        case 0x07:
            context->state.a = z80_cb_instruction [instruction >> 3] (context, context->state.a);
            context->used_cycles += 8;
            break;
    }
}


/* CALL Z, ** */
static void z80_cc_call_z_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    if (context->state.flag_zero)
    {
        context->memory_write (context->parent, --context->state.sp, context->state.pc_h);
        context->memory_write (context->parent, --context->state.sp, context->state.pc_l);
        context->state.pc = addr.w;
        context->used_cycles += 17;
    }
    else
    {
        context->used_cycles += 10;
    }
}


/* CALL ** */
static void z80_cd_call_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);
    context->memory_write (context->parent, --context->state.sp, context->state.pc_h);
    context->memory_write (context->parent, --context->state.sp, context->state.pc_l);
    context->state.pc = addr.w;
    context->used_cycles += 17;
}


/* ADC A, * */
static void z80_ce_adc_a_x (Z80_Context *context)
{
    uint8_t imm = context->memory_read (context->parent, context->state.pc++);
    uint8_t temp = imm + context->state.flag_carry;
    SET_FLAGS_ADC (imm);
    context->state.a += temp;
    context->used_cycles += 7;
}


/* RST 08h */
static void z80_cf_rst_08 (Z80_Context *context)
{
    context->memory_write (context->parent, --context->state.sp, context->state.pc_h);
    context->memory_write (context->parent, --context->state.sp, context->state.pc_l);
    context->state.pc = 0x0008;
    context->used_cycles += 11;
}


/* RET NC */
static void z80_d0_ret_nc (Z80_Context *context)
{
    if (context->state.flag_carry)
    {
        context->used_cycles += 5;
    }
    else
    {
        context->state.pc_l = context->memory_read (context->parent, context->state.sp++);
        context->state.pc_h = context->memory_read (context->parent, context->state.sp++);
        context->used_cycles += 11;
    }
}


/* POP DE */
static void z80_d1_pop_de (Z80_Context *context)
{
    context->state.e = context->memory_read (context->parent, context->state.sp++);
    context->state.d = context->memory_read (context->parent, context->state.sp++);
    context->used_cycles += 10;
}


/* JP NC, ** */
static void z80_d2_jp_nc_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    if (!context->state.flag_carry)
    {
        context->state.pc = addr.w;
    }
    context->used_cycles += 10;
}


/* OUT (*), A */
static void z80_d3_out_x_a (Z80_Context *context)
{
    context->io_write (context->parent, context->memory_read (context->parent, context->state.pc++), context->state.a);
    context->used_cycles += 11;
}


/* CALL NC, ** */
static void z80_d4_call_nc_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    /* CALL NC,** */
    if (context->state.flag_carry)
    {
        context->used_cycles += 10;
    }
    else
    {
        context->memory_write (context->parent, --context->state.sp, context->state.pc_h);
        context->memory_write (context->parent, --context->state.sp, context->state.pc_l);
        context->state.pc = addr.w;
        context->used_cycles += 17;
    }
}


/* PUSH DE */
static void z80_d5_push_de (Z80_Context *context)
{
    context->memory_write (context->parent, --context->state.sp, context->state.d);
    context->memory_write (context->parent, --context->state.sp, context->state.e);
    context->used_cycles += 11;
}


/* SUB A, * */
static void z80_d6_sub_a_x (Z80_Context *context)
{
    uint8_t imm = context->memory_read (context->parent, context->state.pc++);
    SET_FLAGS_SUB (context->state.a, imm);
    context->state.a -= imm;
    context->used_cycles += 7;
}


/* RST 10h */
static void z80_d7_rst_10 (Z80_Context *context)
{
    context->memory_write (context->parent, --context->state.sp, context->state.pc_h);
    context->memory_write (context->parent, --context->state.sp, context->state.pc_l);
    context->state.pc = 0x10;
    context->used_cycles += 11;
}


/* RET C */
static void z80_d8_ret_c (Z80_Context *context)
{
    if (context->state.flag_carry)
    {
        context->state.pc_l = context->memory_read (context->parent, context->state.sp++);
        context->state.pc_h = context->memory_read (context->parent, context->state.sp++);
        context->used_cycles += 11;
    }
    else
    {
        context->used_cycles += 5;
    }
}


/* EXX */
static void z80_d9_exx (Z80_Context *context)
{
    SWAP (uint16_t, context->state.bc, context->state.bc_alt);
    SWAP (uint16_t, context->state.de, context->state.de_alt);
    SWAP (uint16_t, context->state.hl, context->state.hl_alt);
    context->used_cycles += 4;
}


/* JP C, ** */
static void z80_da_jp_c_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    if (context->state.flag_carry)
    {
        context->state.pc = addr.w;
    }
    context->used_cycles += 10;
}


/* IN A, (*) */
static void z80_db_in_a_x (Z80_Context *context)
{
    context->state.a = context->io_read (context->parent, context->memory_read (context->parent, context->state.pc++));
    context->used_cycles += 11;
}


/* CALL C, ** */
static void z80_dc_call_c_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    if (context->state.flag_carry)
    {
        context->memory_write (context->parent, --context->state.sp, context->state.pc_h);
        context->memory_write (context->parent, --context->state.sp, context->state.pc_l);
        context->state.pc = addr.w;
        context->used_cycles += 17;
    }
    else
    {
        context->used_cycles += 10;
    }
}


/* IX PREFIX */
static void z80_dd_ix (Z80_Context *context)
{
    /* Fetch */
    uint8_t instruction = context->memory_read (context->parent, context->state.pc++);

    /* Execute */
    context->state.ix = z80_ix_iy_instruction [instruction] (context, context->state.ix);
}


/* SBC A, * */
static void z80_de_sbc_a_x (Z80_Context *context)
{
    uint8_t imm = context->memory_read (context->parent, context->state.pc++);
    uint8_t temp = imm + context->state.flag_carry;
    SET_FLAGS_SBC (imm);
    context->state.a -= temp;
    context->used_cycles += 7;
}


/* RST 18h */
static void z80_df_rst_18 (Z80_Context *context)
{
    context->memory_write (context->parent, --context->state.sp, context->state.pc_h);
    context->memory_write (context->parent, --context->state.sp, context->state.pc_l);
    context->state.pc = 0x0018;
    context->used_cycles += 11;
}


/* RET PO */
static void z80_e0_ret_po (Z80_Context *context)
{
    if (context->state.flag_parity_overflow)
    {
        context->used_cycles += 5;
    }
    else
    {
        context->state.pc_l = context->memory_read (context->parent, context->state.sp++);
        context->state.pc_h = context->memory_read (context->parent, context->state.sp++);
        context->used_cycles += 11;
    }
}


/* POP HL */
static void z80_e1_pop_hl (Z80_Context *context)
{
    context->state.l = context->memory_read (context->parent, context->state.sp++);
    context->state.h = context->memory_read (context->parent, context->state.sp++);
    context->used_cycles += 10;
}


/* JP PO, ** */
static void z80_e2_jp_po_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    if (!context->state.flag_parity_overflow)
    {
        context->state.pc = addr.w;
    }
    context->used_cycles += 10;
}


/* EX (SP), HL */
static void z80_e3_ex_sp_hl (Z80_Context *context)
{
    uint8_t temp = context->state.l;
    context->state.l = context->memory_read (context->parent, context->state.sp);
    context->memory_write (context->parent, context->state.sp, temp);
    temp = context->state.h;
    context->state.h = context->memory_read (context->parent, context->state.sp + 1);
    context->memory_write (context->parent, context->state.sp + 1, temp);
    context->used_cycles += 19;
}


/* CALL PO, ** */
static void z80_e4_call_po_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    if (context->state.flag_parity_overflow)
    {
        context->used_cycles += 10;
    }
    else
    {
        context->memory_write (context->parent, --context->state.sp, context->state.pc_h);
        context->memory_write (context->parent, --context->state.sp, context->state.pc_l);
        context->state.pc = addr.w;
        context->used_cycles += 17;
    }
}


/* PUSH HL */
static void z80_e5_push_hl (Z80_Context *context)
{
    context->memory_write (context->parent, --context->state.sp, context->state.h);
    context->memory_write (context->parent, --context->state.sp, context->state.l);
    context->used_cycles += 11;
}


/* AND A, * */
static void z80_e6_and_a_x (Z80_Context *context)
{
    context->state.a &= context->memory_read (context->parent, context->state.pc++);
    SET_FLAGS_AND;
    context->used_cycles += 7;
}


/* RST 20h */
static void z80_e7_rst_20 (Z80_Context *context)
{
    context->memory_write (context->parent, --context->state.sp, context->state.pc_h);
    context->memory_write (context->parent, --context->state.sp, context->state.pc_l);
    context->state.pc = 0x0020;
    context->used_cycles += 11;
}


/* RET PE */
static void z80_e8_ret_pe (Z80_Context *context)
{
    if (context->state.flag_parity_overflow)
    {
        context->state.pc_l = context->memory_read (context->parent, context->state.sp++);
        context->state.pc_h = context->memory_read (context->parent, context->state.sp++);
        context->used_cycles += 11;
    }
    else
    {
        context->used_cycles += 5;
    }
}


/* JP (HL) */
static void z80_e9_jp_hl (Z80_Context *context)
{
    context->state.pc = context->state.hl;
    context->used_cycles += 4;
}


/* JP PE, ** */
static void z80_ea_jp_pe_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    if (context->state.flag_parity_overflow)
    {
        context->state.pc = addr.w;
    }
    context->used_cycles += 10;
}


/* EX DE, HL */
static void z80_eb_ex_de_hl (Z80_Context *context)
{
    SWAP (uint16_t, context->state.de, context->state.hl);
    context->used_cycles += 4;
}


/* CALL PE, ** */
static void z80_ec_call_pe_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    if (context->state.flag_parity_overflow)
    {
        context->memory_write (context->parent, --context->state.sp, context->state.pc_h);
        context->memory_write (context->parent, --context->state.sp, context->state.pc_l);
        context->state.pc = addr.w;
        context->used_cycles += 17;
    }
    else
    {
        context->used_cycles += 10;
    }
}


/* EXTENDED PREFIX */
static void z80_ed_prefix (Z80_Context *context)
{
    /* Fetch */
    uint8_t instruction = context->memory_read (context->parent, context->state.pc++);

    /* Execute */
    z80_ed_instruction [instruction] (context);
}


/* XOR A, * */
static void z80_ee_xor_a_x (Z80_Context *context)
{
    context->state.a ^= context->memory_read (context->parent, context->state.pc++);
    SET_FLAGS_OR_XOR;
    context->used_cycles += 7;
}


/* RST 28h */
static void z80_ef_rst_28 (Z80_Context *context)
{
    context->memory_write (context->parent, --context->state.sp, context->state.pc_h);
    context->memory_write (context->parent, --context->state.sp, context->state.pc_l);
    context->state.pc = 0x0028;
    context->used_cycles += 11;
}


/* RET P */
static void z80_f0_ret_p (Z80_Context *context)
{
    if (context->state.flag_sign)
    {
        context->used_cycles += 5;
    }
    else
    {
        context->state.pc_l = context->memory_read (context->parent, context->state.sp++);
        context->state.pc_h = context->memory_read (context->parent, context->state.sp++);
        context->used_cycles += 11;
    }
}


/* POP AF */
static void z80_f1_pop_af (Z80_Context *context)
{
    context->state.f = context->memory_read (context->parent, context->state.sp++);
    context->state.a = context->memory_read (context->parent, context->state.sp++);
    context->used_cycles += 10;
}


/* JP P, ** */
static void z80_f2_jp_p_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    if (!context->state.flag_sign)
    {
        context->state.pc = addr.w;
    }
    context->used_cycles += 10;
}


/* DI */
static void z80_f3_di (Z80_Context *context)
{
    context->state.iff1 = false;
    context->state.iff2 = false;
    context->used_cycles += 4;
}


/* CALL P,** */
static void z80_f4_call_p_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    if (context->state.flag_sign)
    {
        context->used_cycles += 10;
    }
    else
    {
        context->memory_write (context->parent, --context->state.sp, context->state.pc_h);
        context->memory_write (context->parent, --context->state.sp, context->state.pc_l);
        context->state.pc = addr.w;
        context->used_cycles += 17;
    }
}


/* PUSH AF */
static void z80_f5_push_af (Z80_Context *context)
{
    context->memory_write (context->parent, --context->state.sp, context->state.a);
    context->memory_write (context->parent, --context->state.sp, context->state.f);
    context->used_cycles += 11;
}


/* OR A, * */
static void z80_f6_or_a_x (Z80_Context *context)
{
    context->state.a |= context->memory_read (context->parent, context->state.pc++);
    SET_FLAGS_OR_XOR;
    context->used_cycles += 7;
}


/* RST 30h */
static void z80_f7_rst_30 (Z80_Context *context)
{
    context->memory_write (context->parent, --context->state.sp, context->state.pc_h);
    context->memory_write (context->parent, --context->state.sp, context->state.pc_l);
    context->state.pc = 0x0030;
    context->used_cycles += 11;
}


/* RET M */
static void z80_f8_ret_m (Z80_Context *context)
{
    if (context->state.flag_sign)
    {
        context->state.pc_l = context->memory_read (context->parent, context->state.sp++);
        context->state.pc_h = context->memory_read (context->parent, context->state.sp++);
        context->used_cycles += 11;
    }
    else
    {
        context->used_cycles += 5;
    }
}


/* LD SP, HL */
static void z80_f9_ld_sp_hl (Z80_Context *context)
{
    context->state.sp = context->state.hl;
    context->used_cycles += 6;
}


/* JP M, ** */
static void z80_fa_jp_m_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    if (context->state.flag_sign)
    {
        context->state.pc = addr.w;
    }
    context->used_cycles += 10;
}


/* EI */
static void z80_fb_ei (Z80_Context *context)
{
    context->state.iff1 = true;
    context->state.iff2 = true;
    context->state.wait_after_ei = true;
    context->used_cycles += 4;
}


/* CALL M, ** */
static void z80_fc_call_m_xx (Z80_Context *context)
{
    uint16_t_Split addr;
    addr.l = context->memory_read (context->parent, context->state.pc++);
    addr.h = context->memory_read (context->parent, context->state.pc++);

    if (context->state.flag_sign)
    {
        context->memory_write (context->parent, --context->state.sp, context->state.pc_h);
        context->memory_write (context->parent, --context->state.sp, context->state.pc_l);
        context->state.pc = addr.w;
        context->used_cycles += 17;
    }
    else
    {
        context->used_cycles += 10;
    }
}


/* IY PREFIX */
static void z80_fd_prefix (Z80_Context *context)
{
    /* Fetch */
    uint8_t instruction = context->memory_read (context->parent, context->state.pc++);

    /* Execute */
    context->state.iy = z80_ix_iy_instruction [instruction] (context, context->state.iy);
}


/* CP A, * */
static void z80_fe_cp_a_x (Z80_Context *context)
{
    uint8_t imm = context->memory_read (context->parent, context->state.pc++);
    SET_FLAGS_SUB (context->state.a, imm);
    context->used_cycles += 7;
}


/* RST 38h */
static void z80_ff_rst_38 (Z80_Context *context)
{
    context->memory_write (context->parent, --context->state.sp, context->state.pc_h);
    context->memory_write (context->parent, --context->state.sp, context->state.pc_l);
    context->state.pc = 0x0038;
    context->used_cycles += 11;
}


static void (*z80_instruction [256]) (Z80_Context *) = {
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
static void z80_run_instruction (Z80_Context *context)
{
    uint8_t instruction;

    /* TODO: This register should be incremented in more places than just here */
    context->state.r = (context->state.r & 0x80) |((context->state.r + 1) & 0x7f);

    /* Fetch */
    instruction = context->memory_read (context->parent, context->state.pc++);

    /* Execute */
    z80_instruction [instruction] (context);
}


/*
 * Simulate the Z80 for the specified number of clock cycles.
 */
void z80_run_cycles (Z80_Context *context, uint64_t cycles)
{
    cycles += context->state.excess_cycles;

    /* For now, we only run an instruction if we have
     * enough cycles to run any instruction with following interrupt */
    for (context->used_cycles = 0; cycles > 34; cycles -= context->used_cycles, context->cycle_count += context->used_cycles)
    {
        context->used_cycles = 0;

        if (context->state.halt)
        {
            /* NOP */
            context->used_cycles += 4;
        }
        else
        {
            z80_run_instruction (context);
        }

        /* Check for interrupts */
        if (context->state.wait_after_ei)
        {
            context->state.wait_after_ei = false;
        }
        else
        {
            /* First, check for a non-maskable interrupt (edge-triggered) */
            static bool nmi_previous = 0;
            bool nmi = context->get_nmi (context->parent);
            if (nmi && nmi_previous == 0)
            {
                if (context->state.halt)
                {
                    context->state.halt = false;
                    context->state.pc += 1;
                }
                context->state.iff1 = false;
                context->memory_write (context->parent, --context->state.sp, context->state.pc_h);
                context->memory_write (context->parent, --context->state.sp, context->state.pc_l);
                context->state.pc = 0x66;
                context->used_cycles += 11;
            }
            nmi_previous = nmi;

            /* Then check for maskable interrupts */
            if (context->state.iff1 && context->get_int (context->parent))
            {
                if (context->state.halt)
                {
                    context->state.halt = false;
                    context->state.pc += 1;
                }

                context->state.iff1 = false;
                context->state.iff2 = false;

                switch (context->state.im)
                {
                    case 1:
                        context->memory_write (context->parent, --context->state.sp, context->state.pc_h);
                        context->memory_write (context->parent, --context->state.sp, context->state.pc_l);
                        context->state.pc = 0x38;
                        context->used_cycles += 13;
                        break;
                    default:
                        snprintf (state.error_buffer, 79, "Unsupported interrupt mode %d.", context->state.im);
                        snepulator_error ("Z80 Error", state.error_buffer);
                        return;
                }
            }

        }

    }

    context->state.excess_cycles = cycles;
}


/*
 * Export Z80 state.
 */
void z80_state_save (Z80_Context *context)
{
    Z80_State z80_state_be = {
        .af =            htons (context->state.af),
        .bc =            htons (context->state.bc),
        .de =            htons (context->state.de),
        .hl =            htons (context->state.hl),
        .af_alt =        htons (context->state.af_alt),
        .bc_alt =        htons (context->state.bc_alt),
        .de_alt =        htons (context->state.de_alt),
        .hl_alt =        htons (context->state.hl_alt),
        .ir =            htons (context->state.ir),
        .ix =            htons (context->state.ix),
        .iy =            htons (context->state.iy),
        .sp =            htons (context->state.sp),
        .pc =            htons (context->state.pc),
        .im =            context->state.im,
        .iff1 =          context->state.iff1,
        .iff2 =          context->state.iff2,
        .wait_after_ei = context->state.wait_after_ei,
        .halt =          context->state.halt,
        .excess_cycles = htonl (context->state.excess_cycles)
    };

    save_state_section_add (SECTION_ID_Z80, 1, sizeof (z80_state_be), &z80_state_be);
}


/*
 * Import Z80 state.
 */
void z80_state_load (Z80_Context *context, uint32_t version, uint32_t size, void *data)
{
    Z80_State z80_state_be;

    if (size == sizeof (z80_state_be))
    {
        memcpy (&z80_state_be, data, sizeof (z80_state_be));

        context->state.af =            ntohs (z80_state_be.af);
        context->state.bc =            ntohs (z80_state_be.bc);
        context->state.de =            ntohs (z80_state_be.de);
        context->state.hl =            ntohs (z80_state_be.hl);
        context->state.af_alt =        ntohs (z80_state_be.af_alt);
        context->state.bc_alt =        ntohs (z80_state_be.bc_alt);
        context->state.de_alt =        ntohs (z80_state_be.de_alt);
        context->state.hl_alt =        ntohs (z80_state_be.hl_alt);
        context->state.ir =            ntohs (z80_state_be.ir);
        context->state.ix =            ntohs (z80_state_be.ix);
        context->state.iy =            ntohs (z80_state_be.iy);
        context->state.sp =            ntohs (z80_state_be.sp);
        context->state.pc =            ntohs (z80_state_be.pc);
        context->state.im =            z80_state_be.im;
        context->state.iff1 =          z80_state_be.iff1;
        context->state.iff2 =          z80_state_be.iff2;
        context->state.wait_after_ei = z80_state_be.wait_after_ei;
        context->state.halt =          z80_state_be.halt;
        context->state.excess_cycles = ntohl (z80_state_be.excess_cycles);
    }
    else
    {
        snepulator_error ("Error", "Save-state contains incorrect Z80 size");
    }
}
