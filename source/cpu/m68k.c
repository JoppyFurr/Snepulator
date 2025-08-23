/*
 * Snepulator
 * Motorola 68000 implementation
 */

#include <stdlib.h>
#include <stdio.h>

#include "../snepulator.h"
#include "../util.h"
#include "m68k.h"


static uint32_t (*m68k_instruction [SIZE_64K]) (M68000_Context *, uint16_t) = { };


/*
 * Read an 8-bit byte from memory.
 */
static inline uint8_t read_byte (M68000_Context *context, uint32_t addr)
{
    return context->memory_read_8 (context->parent, addr & 0x00ffffff);
}


/*
 * Read a 16-bit word from memory, converting it to little-endian.
 */
static inline uint16_t read_word (M68000_Context *context, uint32_t addr)
{
    return context->memory_read_16 (context->parent, addr & 0x00ffffff);
}


/*
 * Read a 32-bit dword from memory, converting it to little-endian.
 */
static inline uint32_t read_long (M68000_Context *context, uint32_t addr)
{
    uint32_reg value;
    value.w_high = read_word (context, addr);
    value.w_low  = read_word (context, addr + 2);
    return value.l;
}


/*
 * Write an 8-bit byte from memory.
 */
static inline void write_byte (M68000_Context *context, uint32_t addr, uint8_t data)
{
    context->memory_write_8 (context->parent, addr & 0x00ffffff, data);
}


/*
 * Write a 16-bit word from memory, converting it to little-endian.
 */
static inline void write_word (M68000_Context *context, uint32_t addr, uint16_t data)
{
    context->memory_write_16 (context->parent, addr & 0x00ffffff, data);
}


/*
 * Write a 32-bit dword from memory, converting it to little-endian.
 */
static inline void write_long (M68000_Context *context, uint32_t addr, uint32_t data)
{
    write_word (context, addr,     data >> 16);
    write_word (context, addr + 2, data & 0xffff);
}


/*
 * Read a 16-bit word from PC. Increments PC.
 */
static inline uint16_t read_extension (M68000_Context *context)
{
    uint16_t value = read_word (context, context->state.pc);
    context->state.pc += 2;
    return value;
}


/*
 * Read a 32-bit dword from PC. Increments PC.
 */
static inline uint32_t read_extension_long (M68000_Context *context)
{
    uint32_reg addr;
    addr.w_high = read_extension (context);
    addr.w_low  = read_extension (context);
    return addr.l;
}


#if 0 /* UNUSED */
/*
 * Read a byte from the immediate 16-bit address.
 */
static inline uint8_t read_byte_aw (M68000_Context *context)
{
    int16_t addr = read_extension (context);
    return read_byte (context, (int32_t) addr);
}


/*
 * Read a word from the immediate 16-bit address.
 */
static inline uint16_t read_word_aw (M68000_Context *context)
{
    int16_t addr = read_extension (context);
    return read_word (context, (int32_t) addr);
}


/*
 * Read a long from the immediate 16-bit address.
 */
static inline uint32_t read_long_aw (M68000_Context *context)
{
    int16_t addr = read_extension (context);
    return read_long (context, (int32_t) addr);
}
#endif


/*
 * Read a byte from the immediate 32-bit address.
 */
static inline uint8_t read_byte_al (M68000_Context *context)
{
    uint32_t addr = read_extension_long (context);
    return read_byte (context, addr);
}


/*
 * Read a word from the immediate 32-bit address.
 */
static inline uint16_t read_word_al (M68000_Context *context)
{
    uint32_t addr = read_extension_long (context);
    return read_word (context, addr);
}


/*
 * Read a long from the immediate 32-bit address.
 */
static inline uint32_t read_long_al (M68000_Context *context)
{
    uint32_t addr = read_extension_long (context);
    return read_long (context, addr);
}


/*
 * Write a byte from the immediate 16-bit address.
 */
static inline void write_byte_aw (M68000_Context *context, uint8_t value)
{
    int16_t addr = read_extension (context);
    write_byte (context, (int32_t) addr, value);
}


/*
 * Write a word from the immediate 16-bit address.
 */
static inline void write_word_aw (M68000_Context *context, uint16_t value)
{
    int16_t addr = read_extension (context);
    write_word (context, (int32_t) addr, value);
}


/*
 * Write a long from the immediate 16-bit address.
 */
static inline void write_long_aw (M68000_Context *context, uint32_t value)
{
    int16_t addr = read_extension (context);
    write_long (context, (int32_t) addr, value);
}


#if 0 /* UNUSED */
/*
 * Write a byte from the immediate 16-bit address.
 */
static inline void write_byte_al (M68000_Context *context, uint8_t value)
{
    uint32_t addr = read_extension_long (context);
    write_byte (context, addr, value);
}
#endif


/*
 * Write a word from the immediate 16-bit address.
 */
static inline void write_word_al (M68000_Context *context, uint16_t value)
{
    uint32_t addr = read_extension_long (context);
    write_word (context, addr, value);
}


/*
 * Write a long from the immediate 16-bit address.
 */
static inline void write_long_al (M68000_Context *context, uint32_t value)
{
    uint32_t addr = read_extension_long (context);
    write_long (context, addr, value);
}


/* btst.b (An) [Dn] */
static uint32_t m68k_0110_btst_b_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t bit_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    uint32_t bit = context->state.d [bit_reg].l & 0x07;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    printf ("btst.b (a%d) [d%d]\n", source_reg, bit_reg);
    return 0;
}


/* andi.b Dn ← #xxxx */
static uint32_t m68k_0200_andi_b_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t imm = read_extension (context);

    uint8_t result = imm & context->state.d [reg].b;
    context->state.d [reg].b = result;

    context->state.ccr_negative = ((int8_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;

    printf ("andi.b d%d ← #%02x\n", reg, imm & 0xff);
    return 0;
}


/* btst.l Dn [imm] */
static uint32_t m68k_0800_btst_l_imm_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x1f;
    uint32_t value = context->state.d [source_reg].l;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    printf ("btst.b d%d [#%x]\n", source_reg, bit);
    return 0;
}


/* btst.b (xxx.l) [imm] */
static uint32_t m68k_0839_btst_b_imm_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t bit = read_extension (context) & 0x07;
    uint8_t value = read_byte_al (context);

    context->state.ccr_zero = !((value >> bit) & 0x01);

    printf ("btst.b (xxx.l) [#%x]\n", bit);
    return 0;
}


/*
 * Update flags for move.b instructions.
 */
static inline void m68k_move_b_flags (M68000_Context *context, uint8_t value)
{
    context->state.ccr_negative = ((int8_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;
}


/* move.b Dn ← (An)+ */
static uint32_t m68k_1018_move_b_dn_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    context->state.a [source_reg] += (source_reg == 7) ? 2 : 1;
    context->state.d [dest_reg].b = value;
    m68k_move_b_flags (context, value);

    printf ("move.b d%d ← (a%d)+\n", dest_reg, source_reg);
    return 0;
}


/* move.b Dn ← d(An) */
static uint32_t m68k_1028_move_b_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    uint8_t value = read_byte (context, context->state.a [source_reg] + displacement);
    context->state.d [dest_reg].b = value;
    m68k_move_b_flags (context, value);

    printf ("move.b d%d ← %04x(a%d)\n", dest_reg, displacement, source_reg);
    return 0;
}


/* move.b Dn ← (xxx.l) */
static uint32_t m68k_1039_move_b_dn_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_al (context);
    context->state.d [dest_reg].b = value;
    m68k_move_b_flags (context, value);

    printf ("move.b d%d ← (xxx.l)\n", dest_reg);
    return 0;
}


/* move.b (An)+ ← (An)+ */
static uint32_t m68k_10d8_move_b_anp_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    write_byte (context, context->state.a [dest_reg], value);
    context->state.a [source_reg] += (source_reg == 7) ? 2 : 1;
    context->state.a [dest_reg] += (dest_reg == 7) ? 2 : 1;
    m68k_move_b_flags (context, value);

    printf ("move.b (a%d)+ ← (a%d)+\n", dest_reg, source_reg);
    return 0;
}


/* move.b d(An) ← (An)+ */
static uint32_t m68k_1158_move_b_dan_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    context->state.a [source_reg] += (source_reg == 7) ? 2 : 1;
    write_byte (context, context->state.a [dest_reg] + displacement, value);
    m68k_move_b_flags (context, value);

    printf ("move.b %04x(a%d) ← (a%d)+\n", displacement, dest_reg, source_reg);
    return 0;
}


/* move.b (xxx.w) ← Dn */
static uint32_t m68k_11c0_move_b_aw_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = context->state.d [source_reg].b;
    write_byte_aw (context, value);
    m68k_move_b_flags (context, value);

    printf ("move.b (xxx.w) ← d%d\n", source_reg);
    return 0;
}


/*
 * Update flags for move.l instructions.
 */
static inline void m68k_move_l_flags (M68000_Context *context, uint32_t value)
{
    context->state.ccr_negative = ((int32_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;
}


/* move.l Dn ← (An) */
static uint32_t m68k_2010_move_l_dn_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    context->state.d [dest_reg].l = value;
    m68k_move_l_flags (context, value);

    printf ("move.l d%d ← (a%d)\n", dest_reg, source_reg);
    return 0;
}


/* move.l Dn ← (An)+ */
static uint32_t m68k_2018_move_l_dn_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 4;
    context->state.d [dest_reg].l = value;
    m68k_move_l_flags (context, value);

    printf ("move.l d%d ← (a%d)+\n", dest_reg, source_reg);
    return 0;
}


/* move.l (xxx.l) ← Imm */
static uint32_t m68k_23fc_move_l_al_imm (M68000_Context *context, uint16_t instruction)
{
    uint32_t value = read_extension_long (context);

    write_long_al (context, value);
    m68k_move_l_flags (context, value);

    printf ("move.l (xxx.l) ← #%06x\n", value);
    return 0;
}


/* movea.l An ← Dn */
static uint32_t m68k_2040_movea_l_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [dest_reg] = context->state.d [source_reg].l;

    printf ("movea.l a%d ← d%d\n", dest_reg, source_reg);
    return 0;
}


/* movea.l An ← #xxxx */
static uint32_t m68k_207c_movea_l_an_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint32_t imm = read_extension_long (context);

    context->state.a [dest_reg] = imm;

    printf ("movea.l a%d ← #%06x\n", dest_reg, imm);
    return 0;
}


/* move.l (An) ← Dn */
static uint32_t m68k_2080_move_l_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = context->state.d [source_reg].l;
    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    printf ("move.l (a%d) ← d%d\n", dest_reg, source_reg);
    return 0;
}


/* move.l (An) ← (An)+ */
static uint32_t m68k_2098_move_l_an_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 4;

    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    printf ("move.l (a%d) ← (a%d)+\n", dest_reg, source_reg);
    return 0;
}


/* move.l (An) ← #xxxx */
static uint32_t m68k_20bc_move_l_an_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint32_t value = read_extension_long (context);

    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    printf ("move.l (a%d) ← #%08x\n", dest_reg, value);
    return 0;
}


/* move.l (An)+ ← Dn */
static uint32_t m68k_20c0_move_l_anp_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = context->state.d [source_reg].l;
    write_long (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 4;
    m68k_move_l_flags (context, value);

    printf ("move.l (a%d)+ ← d%d\n", dest_reg, source_reg);
    return 0;
}


/* move.l -(An) ← Dn */
static uint32_t m68k_2100_move_l_pan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = context->state.d [source_reg].l;
    context->state.a [dest_reg] -= 4;
    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    printf ("move.l -(a%d) ← d%d\n", dest_reg, source_reg);
    return 0;
}


/* move.l d(An) ← #xxxx */
static uint32_t m68k_217c_move_l_dan_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint32_t value = read_extension_long (context);
    int16_t displacement = read_extension (context);

    write_long (context, context->state.a [dest_reg] + displacement, value);
    m68k_move_l_flags (context, value);

    printf ("move.l %04x(a%d) ← #%08x\n", displacement, dest_reg, value);
    return 0;
}


/* move.l (xxx.w) ← #xxxx */
static uint32_t m68k_21fc_move_l_aw_imm (M68000_Context *context, uint16_t instruction)
{
    uint32_t value = read_extension_long (context);

    write_long_aw (context, value);
    m68k_move_l_flags (context, value);

    printf ("move.l (xxx.w) ← #%08x\n", value);
    return 0;
}


/*
 * Update flags for move.w instructions.
 */
static inline void m68k_move_w_flags (M68000_Context *context, uint16_t value)
{
    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;
}


/* move.w Dn ← (An) */
static uint32_t m68k_3010_move_w_dn_an (M68000_Context *context, uint16_t instruction)
{
    /* TODO: Consider, instruction union/struct, to pick out the fields */
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    context->state.d [dest_reg].w = value;
    m68k_move_w_flags (context, value);

    printf ("move.w d%d ← (a%d)\n", dest_reg, source_reg);
    return 0;
}


/* move.w Dn ← Imm */
static uint32_t m68k_303c_move_w_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_extension (context);
    context->state.d [dest_reg].w = value;
    m68k_move_w_flags (context, value);

    printf ("move.w d%d ← #%04x\n", dest_reg, value);
    return 0;
}


/* move.w Dn ← (xxx.l) */
static uint32_t m68k_3039_move_w_dn_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_al (context);
    context->state.d [dest_reg].w = value;
    m68k_move_w_flags (context, value);

    printf ("move.w d%d ← (xxx.l)\n", dest_reg);
    return 0;
}


/* move.w (An) ← Dn */
static uint32_t m68k_3080_move_w_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = context->state.d [source_reg].w;
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    printf ("move.w (a%d) ← d%d\n", dest_reg, source_reg);
    return 0;
}


/* move.w (xxx.l) ← #xxxx */
static uint32_t m68k_33fc_move_w_al_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t value = read_extension (context);
    write_word_al (context, value);
    m68k_move_w_flags (context, value);

    printf ("move.w (xxx.l) ← #%04x\n", value);
    return 0;
}


/* move.w (An) ← (An)+ */
static uint32_t m68k_3098_move_w_an_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 2;
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    printf ("move.w (a%d) ← (a%d)+\n", dest_reg, source_reg);
    return 0;
}


/* move.w (An) ← #xxxx */
static uint32_t m68k_30bc_move_w_an_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t value = read_extension (context);

    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    printf ("move.w (a%d) ← #%04x\n", dest_reg, value);
    return 0;
}


/* move.w (xxx.w) ← Dn */
static uint32_t m68k_31c0_move_w_aw_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = context->state.d [source_reg].w;
    write_word_aw (context, value);
    m68k_move_w_flags (context, value);

    printf ("move.w (xxx.w) ← d%d\n", source_reg);
    return 0;
}


/* move.w (xxx.w) ← Imm */
static uint32_t m68k_31fc_move_w_aw_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t value = read_extension (context);

    write_word_aw (context, value);
    m68k_move_w_flags (context, value);

    printf ("move.w (xxx.w) ← #%04x\n", value);
    return 0;
}


/* tst.w (xxx.l) */
static uint32_t m68k_4a79_tst_w_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t value = read_word_al (context);

    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;

    printf ("tst.w (xxx.l)\n");
    return 0;
}


/* tst.l (xxx.l) */
static uint32_t m68k_4ab9_tst_l_al (M68000_Context *context, uint16_t instruction)
{
    uint32_t value = read_long_al (context);

    context->state.ccr_negative = ((int32_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;

    printf ("tst.l (xxx.l)\n");
    return 0;
}


/* lea An ← (xxx.w) */
static uint32_t m68k_41f8_lea_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    int16_t addr = read_extension (context);

    context->state.a [dest_reg] = (int32_t) addr;

    printf ("lea a%d ← (%04x.w)\n", dest_reg, (uint16_t) addr);
    return 0;
}


/* lea An ← (xxx.l) */
static uint32_t m68k_41f9_lea_an_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint32_t addr = read_extension_long (context);

    context->state.a [dest_reg] = addr;

    printf ("lea a%d ← (%06x.l)\n", dest_reg, addr);
    return 0;
}


/* lea An ← d(PC) */
static uint32_t m68k_41fa_lea_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint32_t pc = context->state.pc;
    int16_t displacement = read_extension (context);

    context->state.a [dest_reg] = pc + displacement;

    printf ("lea a%d ← d(pc)\n", dest_reg);
    return 0;
}


/*
 * Update flags for clr instructions.
 */
static inline void m68k_clr_flags (M68000_Context *context)
{
    context->state.ccr_negative = 0;
    context->state.ccr_zero = 1;
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;
}


/* clr.l (xxx.w) */
static uint32_t m68k_42b8_clr_l_aw (M68000_Context *context, uint16_t instruction)
{
    write_long_aw (context, 0x00000000);
    m68k_clr_flags (context);

    printf ("clr.l (xxx.w)\n");
    return 0;
}


/* move sr ← #xxxx */
/* TODO: Privileged instruction. */
static uint32_t m68k_46fc_move_sr_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t value = read_extension (context);

    context->state.sr = value;

    printf ("move sr ← #%04x\n", value);
    return 0;
}


/* movem.w memory-to-register (An)+ */
static uint32_t m68k_4c98_movem_w_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t mask = read_extension (context);

    /* Data registers first */
    for (uint32_t i = 0; i < 16; i++)
    {
        if (mask & (1 << i))
        {
            if (i < 8)
            {
                context->state.d [i].l = (int16_t) read_word (context, context->state.a [reg]);
                context->state.a [reg] += 2;
            }
            else
            {
                context->state.a [i - 8] = (int16_t) read_word (context, context->state.a [reg]);
                context->state.a [reg] += 2;
            }
        }
    }

    printf ("movem.w (a%d)+\n", reg);
    return 0;
}


/* movem.l <registers> ← (An) */
static uint32_t m68k_4cd0_movem_l_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t mask = read_extension (context);
    uint32_t address = context->state.a [source_reg];

    for (uint32_t i = 0; i < 16; i++)
    {
        if (mask & (1 << i))
        {
            if (i < 8)
            {
                context->state.d [i].l = read_long (context, address);
                address += 4;
            }
            else
            {
                context->state.a [i - 8] = read_long (context, address);
                address += 4;
            }
        }
    }

    printf ("movem.l <registers> ← (a%d)\n", source_reg);
    return 0;
}


/* movem.l <registers> ← (An)+ */
static uint32_t m68k_4cd8_movem_l_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t mask = read_extension (context);
    uint32_t address = context->state.a [source_reg];

    for (uint32_t i = 0; i < 16; i++)
    {
        if (mask & (1 << i))
        {
            if (i < 8)
            {
                context->state.d [i].l = read_long (context, address);
                address += 4;
            }
            else
            {
                context->state.a [i - 8] = read_long (context, address);
                address += 4;
            }
        }
    }

    context->state.a [source_reg] = address;

    printf ("movem.l <registers> ← (a%d)+\n", source_reg);
    return 0;
}


/* move usp ← An */
/* TODO: Privileged instruction. */
static uint32_t m68k_4e60_move_an_usp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    context->state.usp = context->state.a [reg];

    printf ("move usp ← A%d\n", reg);
    return 0;
}


/* move An ← usp */
/* TODO: Privileged instruction. */
static uint32_t m68k_4e68_move_usp_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    context->state.a [reg] = context->state.usp;

    printf ("move A%d ← usp\n", reg);
    return 0;
}


/* nop */
static uint32_t m68k_4e71_nop (M68000_Context *context, uint16_t instruction)
{
    printf ("nop\n");
    return 0;
}


/* rts */
static uint32_t m68k_4e75_rts (M68000_Context *context, uint16_t instruction)
{
    context->state.pc = read_long (context, context->state.a[7]);
    context->state.a[7] += 4;

    printf ("rts\n");
    return 0;
}


/* subq.l an, #xx */
static uint32_t m68k_5188_subq_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t a = context->state.a [reg];
    uint32_t b = (instruction >> 9) & 0x07;

    if (b == 0)
    {
        b = 8;
    }

    uint32_t result = a - b;
    context->state.a [reg] = result;

    context->state.ccr_negative = ((int32_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = (int64_t)(int32_t) a - (int32_t) b > 2147483647 || (int64_t)(int32_t) a - (int32_t) b < -2147483648;
    context->state.ccr_carry = ((uint64_t) a - b) >> 32;

    printf ("subq.l a%d, %d\n", reg, b);
    return 0;
}


/* dbf Dn, #xxxx */
static uint32_t m68k_51c8_dbf (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    context->state.d [reg].w--;
    if (context->state.d [reg].w != 0xffff)
    {
        context->state.pc = context->state.pc - 2 + displacement;
        printf ("dbf d%d, %+d\n", reg, displacement);
    }
    else
    {
        printf ("dbf d%d (termination)\n", reg);
    }

    return 0;
}


/* bra.w */
static uint32_t m68k_6000_bra_w (M68000_Context *context, uint16_t instruction)
{
    int16_t displacement = read_extension (context);
    context->state.pc += displacement;
    printf ("bra %+d\n", displacement);
    return 0;
}

/* bra.s */
static uint32_t m68k_6001_bra_s (M68000_Context *context, uint16_t instruction)
{
    int8_t displacement = instruction & 0xff;
    context->state.pc += displacement;
    printf ("bra %+d\n", displacement);
    return 0;
}


/* bsr.w */
static uint32_t m68k_6100_bsr_w (M68000_Context *context, uint16_t instruction)
{
    int16_t displacement = read_extension (context);

    context->state.a[7] -= 4;
    write_long (context, context->state.a[7], context->state.pc);

    context->state.pc += displacement - 2;
    printf ("bsr.w %+d\n", displacement);
    return 0;
}


/* bsr.s */
static uint32_t m68k_6101_bsr_s (M68000_Context *context, uint16_t instruction)
{
    int8_t displacement = instruction & 0xff;

    context->state.a[7] -= 4;
    write_long (context, context->state.a[7], context->state.pc);

    context->state.pc += displacement;
    printf ("bsr.s %+d\n", displacement);
    return 0;
}


/* bcc.w / bhs.w */
static uint32_t m68k_6400_bcc_w (M68000_Context *context, uint16_t instruction)
{
    int16_t displacement = read_extension (context);

    if (!context->state.ccr_carry)
    {
        context->state.pc += displacement - 2;
        printf ("bcc.w %+d\n", displacement);
    }
    else
    {
        printf ("bcc.w (not taken).\n");
    }

    return 0;
}


/* bcc.s / bhs.s */
static uint32_t m68k_6401_bcc_s (M68000_Context *context, uint16_t instruction)
{
    if (!context->state.ccr_carry)
    {
        int8_t displacement = instruction & 0xff;
        context->state.pc += displacement;
        printf ("bcc.s %+d\n", displacement);
    }
    else
    {
        printf ("bcc.s (not taken).\n");
    }

    return 0;
}


/* bcs.w */
static uint32_t m68k_6500_bcs_w (M68000_Context *context, uint16_t instruction)
{
    int16_t displacement = read_extension (context);

    if (context->state.ccr_carry)
    {
        context->state.pc += displacement - 2;
        printf ("bcs.w %+d\n", displacement);
    }
    else
    {
        printf ("bcs.w (not taken).\n");
    }

    return 0;
}


/* bcs.s */
static uint32_t m68k_6501_bcs_s (M68000_Context *context, uint16_t instruction)
{
    if (context->state.ccr_carry)
    {
        int8_t displacement = instruction & 0xff;
        context->state.pc += displacement;
        printf ("bcs.s %+d\n", displacement);
    }
    else
    {
        printf ("bcs.s (not taken).\n");
    }

    return 0;
}


/* bne.w */
static uint32_t m68k_6600_bne_w (M68000_Context *context, uint16_t instruction)
{
    int16_t displacement = read_extension (context);

    if (!context->state.ccr_zero)
    {
        context->state.pc += displacement - 2;
        printf ("bne.w %+d\n", displacement);
    }
    else
    {
        printf ("bne.w (not taken).\n");
    }

    return 0;
}


/* bne.s */
static uint32_t m68k_6601_bne_s (M68000_Context *context, uint16_t instruction)
{
    if (!context->state.ccr_zero)
    {
        int8_t displacement = instruction & 0xff;
        context->state.pc += displacement;
        printf ("bne.s %+d\n", displacement);
    }
    else
    {
        printf ("bne.s (not taken).\n");
    }

    return 0;
}


/* beq.w */
static uint32_t m68k_6700_beq_w (M68000_Context *context, uint16_t instruction)
{
    int16_t displacement = read_extension (context);

    if (context->state.ccr_zero)
    {
        context->state.pc += displacement - 2;
        printf ("beq.w %+d\n", displacement);
    }
    else
    {
        printf ("beq.w (not taken).\n");
    }

    return 0;
}


/* beq.s */
static uint32_t m68k_6701_beq_s (M68000_Context *context, uint16_t instruction)
{
    if (context->state.ccr_zero)
    {
        int8_t displacement = instruction & 0xff;
        context->state.pc += displacement;
        printf ("beq.s %+d\n", displacement);
    }
    else
    {
        printf ("beq.s (not taken).\n");
    }

    return 0;
}


/* moveq Dn ← #xxxx */
static uint32_t m68k_7000_moveq (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = (instruction >> 9) & 0x07;
    int8_t data = instruction & 0xff;

    context->state.d [reg].l = data;

    context->state.ccr_negative = (data < 0);
    context->state.ccr_zero = (data == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;

    printf ("moveq d%d ← #%08x\n", reg, (uint32_t) (int32_t) data);
    return 0;
}


/* cmp.w Dn - (An) */
static uint32_t m68k_b050_cmp_w_dn_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t a = context->state.d [dest_reg].w;
    uint16_t b = read_word (context, context->state.a [source_reg]);
    uint16_t result = a - b;

    context->state.ccr_negative = ((int16_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = (int16_t) a - (int16_t) b > 32767 || (int16_t) a - (int16_t) b < -32768;
    context->state.ccr_carry = ((uint32_t) a - b) >> 16;

    printf ("cmp.w d%d - (a%d)\n", dest_reg, source_reg);
    return 0;
}


/* cmp.l Dn - An */
static uint32_t m68k_b088_cmp_l_dn_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t a = context->state.d [dest_reg].l;
    uint32_t b = context->state.a [source_reg];
    uint32_t result = a - b;

    context->state.ccr_negative = ((int32_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = (int64_t)(int32_t) a - (int32_t) b > 2147483647 || (int64_t)(int32_t) a - (int32_t) b < -2147483648;
    context->state.ccr_carry = ((uint64_t) a - b) >> 32;

    printf ("cmp.l d%d - a%d\n", dest_reg, source_reg);
    return 0;
}


/* add.w Dn ← Dn + Dn */
static uint32_t m68k_d040_add_w_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t a = context->state.d [dest_reg].w;
    uint16_t b = context->state.d [source_reg].w;
    uint16_t result = a + b;

    context->state.ccr_negative = ((int16_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = (int16_t) a + (int16_t) b > 32767 || (int16_t) a + (int16_t) b < -32768;
    context->state.ccr_carry = ((uint32_t) a + b) >> 16;

    context->state.d [dest_reg].w = result;

    printf ("add.w d%d ← d%d + d%d\n", dest_reg, dest_reg, source_reg);
    return 0;
}


/* add.w Dn ← Dn + (An)+ */
static uint32_t m68k_d058_add_w_dn_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t a = context->state.d [dest_reg].w;
    uint16_t b = read_word (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 2;
    uint16_t result = a + b;

    context->state.ccr_negative = ((int16_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = (int16_t) a + (int16_t) b > 32767 || (int16_t) a + (int16_t) b < -32768;
    context->state.ccr_carry = ((uint32_t) a + b) >> 16;

    context->state.d [dest_reg].w = result;

    printf ("add.w d%d ← d%d + a(%d)+\n", dest_reg, dest_reg, source_reg);
    return 0;
}


/*
 * Initialise the instruction array.
 * TODO: This only needs to happen once
 */
static void m68k_init_instructions (void)
{
    /* TODO: Consider something that reads closer to the datasheet.
     *       Eg, add_instruction ("0001 xxx 000 011 xxx", m68k_1018_move_b_dn_anp); */

    /* Bit Instructions */
    for (uint16_t reg_a = 0; reg_a < 8; reg_a++)
    {
        for (uint16_t reg_b = 0; reg_b < 8; reg_b++)
        {
            m68k_instruction [0x0110 | (reg_a << 9) | reg_b] = m68k_0110_btst_b_an_dn;
        }
        m68k_instruction [0x0800 | reg_a] = m68k_0800_btst_l_imm_dn;
    }

    m68k_instruction [0x0839] = m68k_0839_btst_b_imm_al;

    /* andi */
    for (uint16_t dn = 0; dn < 8; dn++)
    {
        m68k_instruction [0x0200 | dn] = m68k_0200_andi_b_dn;
    }

    /* move */
    for (uint16_t reg_a = 0; reg_a < 8; reg_a++)
    {
        for (uint16_t reg_b = 0; reg_b < 8; reg_b++)
        {
            m68k_instruction [0x1018 | (reg_a << 9) | reg_b] = m68k_1018_move_b_dn_anp;
            m68k_instruction [0x1028 | (reg_a << 9) | reg_b] = m68k_1028_move_b_dn_dan;
            m68k_instruction [0x10d8 | (reg_a << 9) | reg_b] = m68k_10d8_move_b_anp_anp;
            m68k_instruction [0x1158 | (reg_a << 9) | reg_b] = m68k_1158_move_b_dan_anp;
            m68k_instruction [0x2010 | (reg_a << 9) | reg_b] = m68k_2010_move_l_dn_an;
            m68k_instruction [0x2018 | (reg_a << 9) | reg_b] = m68k_2018_move_l_dn_anp;
            m68k_instruction [0x2040 | (reg_a << 9) | reg_b] = m68k_2040_movea_l_an_dn;
            m68k_instruction [0x2080 | (reg_a << 9) | reg_b] = m68k_2080_move_l_an_dn;
            m68k_instruction [0x2098 | (reg_a << 9) | reg_b] = m68k_2098_move_l_an_anp;
            m68k_instruction [0x20c0 | (reg_a << 9) | reg_b] = m68k_20c0_move_l_anp_dn;
            m68k_instruction [0x2100 | (reg_a << 9) | reg_b] = m68k_2100_move_l_pan_dn;
            m68k_instruction [0x3010 | (reg_a << 9) | reg_b] = m68k_3010_move_w_dn_an;
            m68k_instruction [0x3080 | (reg_a << 9) | reg_b] = m68k_3080_move_w_an_dn;
            m68k_instruction [0x3098 | (reg_a << 9) | reg_b] = m68k_3098_move_w_an_anp;
        }
        m68k_instruction [0x1039 | (reg_a << 9)] = m68k_1039_move_b_dn_al;
        m68k_instruction [0x11c0 | reg_a       ] = m68k_11c0_move_b_aw_dn;
        m68k_instruction [0x207c | (reg_a << 9)] = m68k_207c_movea_l_an_imm;
        m68k_instruction [0x20bc | (reg_a << 9)] = m68k_20bc_move_l_an_imm;
        m68k_instruction [0x217c | (reg_a << 9)] = m68k_217c_move_l_dan_imm;
        m68k_instruction [0x3039 | (reg_a << 9)] = m68k_3039_move_w_dn_al;
        m68k_instruction [0x303c | (reg_a << 9)] = m68k_303c_move_w_dn_imm;
        m68k_instruction [0x30bc | (reg_a << 9)] = m68k_30bc_move_w_an_imm;
        m68k_instruction [0x31c0 | reg_a       ] = m68k_31c0_move_w_aw_dn;
        m68k_instruction [0x4e60 | reg_a       ] = m68k_4e60_move_an_usp;
        m68k_instruction [0x4e68 | reg_a       ] = m68k_4e68_move_usp_an;
    }
    m68k_instruction [0x21fc] = m68k_21fc_move_l_aw_imm;
    m68k_instruction [0x23fc] = m68k_23fc_move_l_al_imm;
    m68k_instruction [0x31fc] = m68k_31fc_move_w_aw_imm;
    m68k_instruction [0x33fc] = m68k_33fc_move_w_al_imm;

    /* misc */
    m68k_instruction [0x46fc] = m68k_46fc_move_sr_imm;
    m68k_instruction [0x4a79] = m68k_4a79_tst_w_al;
    m68k_instruction [0x4ab9] = m68k_4ab9_tst_l_al;
    m68k_instruction [0x4e71] = m68k_4e71_nop;
    m68k_instruction [0x4e75] = m68k_4e75_rts;

    /* movem */
    for (uint16_t an = 0; an < 8; an++)
    {
        m68k_instruction [0x4c98 | an] = m68k_4c98_movem_w_anp;
        m68k_instruction [0x4cd0 | an] = m68k_4cd0_movem_l_an;
        m68k_instruction [0x4cd8 | an] = m68k_4cd8_movem_l_anp;
    }

    /* lea */
    for (uint16_t an = 0; an < 8; an++)
    {
        m68k_instruction [0x41f8 | (an << 9)] = m68k_41f8_lea_aw;
        m68k_instruction [0x41f9 | (an << 9)] = m68k_41f9_lea_an_al;
        m68k_instruction [0x41fa | (an << 9)] = m68k_41fa_lea_dpc;
    }

    /* clr */
    m68k_instruction [0x42b8] = m68k_42b8_clr_l_aw;

    /* addq / subq */
    for (uint16_t value = 0; value < 8; value++)
    {
        for (uint16_t reg = 0; reg < 8; reg++)
        {
            m68k_instruction [0x5188 | (value << 9) | reg] = m68k_5188_subq_an;
        }
    }

    /* dbcc */
    for (uint16_t dn = 0; dn < 8; dn++)
    {
        m68k_instruction [0x51c8 | dn] = m68k_51c8_dbf;
    }

    /* Bcc/BSR/BRA with 16-bit displacement */
    m68k_instruction [0x6000] = m68k_6000_bra_w;
    m68k_instruction [0x6100] = m68k_6100_bsr_w;
    m68k_instruction [0x6400] = m68k_6400_bcc_w;
    m68k_instruction [0x6500] = m68k_6500_bcs_w;
    m68k_instruction [0x6600] = m68k_6600_bne_w;
    m68k_instruction [0x6700] = m68k_6700_beq_w;

    /* Bcc/BSR/BRA with 8-bit displacement */
    for (uint16_t d = 0x01; d <= 0xff; d++)
    {
        m68k_instruction [0x6000 | d] = m68k_6001_bra_s;
        m68k_instruction [0x6100 | d] = m68k_6101_bsr_s;
        m68k_instruction [0x6400 | d] = m68k_6401_bcc_s;
        m68k_instruction [0x6500 | d] = m68k_6501_bcs_s;
        m68k_instruction [0x6600 | d] = m68k_6601_bne_s;
        m68k_instruction [0x6700 | d] = m68k_6701_beq_s;
    }

    /* moveq */
    for (uint16_t dn = 0; dn < 8; dn++)
    {
        for (uint16_t data = 0x00; data < 0xff; data++)
        {
            m68k_instruction [0x7000 | (dn << 9) | data] = m68k_7000_moveq;
        }
    }

    /* cmp */
    for (uint16_t reg_a = 0; reg_a < 8; reg_a++)
    {
        for (uint16_t reg_b = 0; reg_b < 8; reg_b++)
        {
            m68k_instruction [0xb050 | (reg_a << 9) | reg_b] = m68k_b050_cmp_w_dn_an;
            m68k_instruction [0xb088 | (reg_a << 9) | reg_b] = m68k_b088_cmp_l_dn_an;
        }
    }

    /* add */
    for (uint16_t dn = 0; dn < 8; dn++)
    {
        for (uint16_t ea = 0; ea < 8; ea++)
        {
            m68k_instruction [0xd040 | (dn << 9) | ea] = m68k_d040_add_w_dn_dn;
            m68k_instruction [0xd058 | (dn << 9) | ea] = m68k_d058_add_w_dn_anp;
        }
    }

    /* An estimate of progress, note, opcodes beginning with 1010 and 1111
     * are unassigned, so are subtracted from the total number.. */
    uint32_t populated = 0;
    for (uint32_t i = 0; i < SIZE_64K; i++)
    {
        if (m68k_instruction [i] != NULL)
        {
            populated++;
        }
    }
    printf ("[%s] %d of %d opcodes populated. (%2.1f%%)\n", __func__,
            populated, SIZE_64K - 8192,
            100.0 * (populated / (float) (SIZE_64K - 8192)));
}


/*
 * Execute a single M68000 instruction.
 * Returns the number of clock cycles used.
 */
static uint32_t m68k_run_instruction (M68000_Context *context)
{
    /* Fetch */
    uint16_t instruction = read_word (context, context->state.pc);
    context->state.pc += 2;

    /* Execute */
    if (m68k_instruction [instruction] != NULL)
    {
        m68k_instruction [instruction] (context, instruction);
    }
    else
    {
        /* TODO: varargs for snepulator_error. */
        char buf [80];
        sprintf (buf, "Unknown %s instruction: %04x.",
                 (instruction & 0xf000) == 0x0000 ? "bit/movep/immediate" :
                 (instruction & 0xf000) == 0x1000 ? "move.b" :
                 (instruction & 0xf000) == 0x2000 ? "move.l" :
                 (instruction & 0xf000) == 0x3000 ? "move.w" :
                 (instruction & 0xf000) == 0x4000 ? "misc" :
                 (instruction & 0xf000) == 0x5000 ? "addq/subq/Scc/DBcc" :
                 (instruction & 0xf000) == 0x6000 ? "Bcc/bsr" :
                 (instruction & 0xf000) == 0x7000 ? "moveq" :
                 (instruction & 0xf000) == 0x8000 ? "or/div/sbcd" :
                 (instruction & 0xf000) == 0x9000 ? "sub/subx" :
                 (instruction & 0xf000) == 0xa000 ? "unassigned" :
                 (instruction & 0xf000) == 0xb000 ? "cmp/eor" :
                 (instruction & 0xf000) == 0xc000 ? "and/mul/abcd/exg" :
                 (instruction & 0xf000) == 0xd000 ? "add/addx" :
                 (instruction & 0xf000) == 0xe000 ? "shift/rotate" : "unassigned",
                 instruction);
        snepulator_error ("M68000 Error", buf);
        return 150000; /* Enough to end the frame */
    }

    /* TODO: Instruction timing */
    return 10; /* placeholder */
}


/*
 * Run the 68000 for the specified number of clock cycles.
 */
/* XXX DEBUG XXX */ extern Snepulator_State state;
void m68k_run_cycles (M68000_Context *context, int64_t cycles)
{
    /* Account for cycles used during the last run */
    context->clock_cycles += cycles;

    /* As long as we have a positive number of cycles, run an instruction */
    while (context->clock_cycles > 0)
    {
        /* XXX DEBUG XXX: Don't finish the call if we hit something unimplemented */
        if (state.run != RUN_STATE_RUNNING)
        {
            return;
        }

        context->clock_cycles -= m68k_run_instruction (context);
    }
}


/*
 * Operations performed when taking the chip out of reset.
 */
void m68k_reset (M68000_Context *context)
{
    /*       Current plan is: In-memory, big-endian
     *                        everywhere else, host-endian.
     *                        Convert when reading/writing. */
    context->state.a [7] = read_long (context, 0x0000);
    context->state.pc    = read_long (context, 0x0004);
}


/*
 * Create the 68000 context with power-on defaults.
 */
M68000_Context *m68k_init (void *parent,
                           uint16_t (* memory_read_16)  (void *, uint32_t),
                           void     (* memory_write_16) (void *, uint32_t, uint16_t),
                           uint8_t  (* memory_read_8)   (void *, uint32_t),
                           void     (* memory_write_8)  (void *, uint32_t, uint8_t),
                           uint8_t  (* get_int)     (void *))
{
    M68000_Context *context;

    context = calloc (1, sizeof (M68000_Context));
    if (context == NULL)
    {
        snepulator_error ("Error", "Unable to allocate memory for M68000_Context");
        return NULL;
    }

    context->parent          = parent;
    context->memory_read_16  = memory_read_16;
    context->memory_write_16 = memory_write_16;
    context->memory_read_8   = memory_read_8;
    context->memory_write_8  = memory_write_8;
    context->get_int         = get_int;

    m68k_init_instructions ();

    return context;
}
