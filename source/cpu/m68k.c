/*
 * Snepulator
 * Motorola 68000 implementation
 *
 * TODO:
 *  - Instruction timing
 *  - Prefetch
 *  - User/Supervisor
 */

#include <stdlib.h>
#include <stdio.h>

#include "../snepulator.h"
#include "../util.h"
#include "m68k.h"

#define SR_MASK 0xa71f

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
    uint32_split_t value;
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
    uint32_split_t addr;
    addr.w_high = read_extension (context);
    addr.w_low  = read_extension (context);
    return addr.l;
}


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
 * Calculate the address using displacement+index in 16-bit extension.
 *
 * Address is the sum of:
 *  -> An, passed here as a parameter
 *  -> Displacement, from the low byte of the extension
 *  -> Index register, An or Dn register described by the high byte of the extension.
 */
static inline uint32_t address_with_index (M68000_Context *context, uint32_t addr)
{
    uint16_t extension = read_extension (context);
    int8_t displacement = extension & 0xff;

    uint16_t reg = (extension >> 12) & 0x07;
    uint32_t index;

    /* Bit 15 selects Dn or An */
    if ((extension & BIT_15) == 0)
    {
        index = context->state.d [reg].l;
    }
    else
    {
        index = context->state.a [reg];
    }

    /* Bit 11 selects index size, sign-extended word, or long */
    if ((extension & BIT_11) == 0)
    {
        int16_t index_w = index & 0xffff;
        index = index_w;
    }

    return addr + displacement + index;
}


/*
 * Read a byte with indexing from the immediate 16-bit extension.
 */
static inline uint8_t read_byte_with_index (M68000_Context *context, uint32_t addr)
{
    return read_byte (context, address_with_index (context, addr));
}


/*
 * Read a word with indexing from the immediate 16-bit extension.
 */
static inline uint16_t read_word_with_index (M68000_Context *context, uint32_t addr)
{
    return read_word (context, address_with_index (context, addr));
}


/*
 * Read a long with indexing from the immediate 16-bit extension.
 */
static inline uint32_t read_long_with_index (M68000_Context *context, uint32_t addr)
{
    return read_long (context, address_with_index (context, addr));
}


/*
 * Write a byte to the immediate 16-bit address.
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


/*
 * Write a byte from the immediate 16-bit address.
 */
static inline void write_byte_al (M68000_Context *context, uint8_t value)
{
    uint32_t addr = read_extension_long (context);
    write_byte (context, addr, value);
}


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


/*
 * Write a byte with indexing from the immediate 16-bit extension.
 */
static inline void write_byte_with_index (M68000_Context *context, uint32_t addr, uint8_t value)
{
    write_byte (context, address_with_index (context, addr), value);
}


/*
 * Write a word with indexing from the immediate 16-bit extension.
 */
static inline void write_word_with_index (M68000_Context *context, uint32_t addr, uint16_t value)
{
    write_word (context, address_with_index (context, addr), value);
}


/*
 * Write a long with indexing from the immediate 16-bit extension.
 */
static inline void write_long_with_index (M68000_Context *context, uint32_t addr, uint32_t value)
{
    write_long (context, address_with_index (context, addr), value);
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


/*
 * Update flags for add.b instructions.
 */
static inline void m68k_add_b_flags (M68000_Context *context, uint8_t a, uint8_t b, uint8_t result)
{
    context->state.ccr_negative = ((int8_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = (int8_t) a + (int8_t) b > 127 || (int8_t) a + (int8_t) b < -128;
    context->state.ccr_carry = ((uint32_t) a + b) >> 8;
    context->state.ccr_extend = ((uint32_t) a + b) >> 8;
}


/*
 * Update flags for add.w instructions.
 */
static inline void m68k_add_w_flags (M68000_Context *context, uint16_t a, uint16_t b, uint16_t result)
{
    context->state.ccr_negative = ((int16_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = (int16_t) a + (int16_t) b > 32767 || (int16_t) a + (int16_t) b < -32768;
    context->state.ccr_carry = ((uint32_t) a + b) >> 16;
    context->state.ccr_extend = ((uint32_t) a + b) >> 16;
}


/*
 * Update flags for add.l instructions.
 */
static inline void m68k_add_l_flags (M68000_Context *context, uint32_t a, uint32_t b, uint32_t result)
{
    context->state.ccr_negative = ((int32_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = (int64_t)(int32_t) a + (int32_t) b > 2147483647 ||
                                  (int64_t)(int32_t) a + (int32_t) b < -2147483648;
    context->state.ccr_carry = ((uint64_t) a + b) >> 32;
    context->state.ccr_extend = ((uint64_t) a + b) >> 32;
}


/*
 * Update flags for sub.b instructions.
 */
static inline void m68k_sub_b_flags (M68000_Context *context, uint8_t a, uint8_t b, uint8_t result)
{
    context->state.ccr_negative = ((int8_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = (int8_t) a - (int8_t) b > 127 || (int8_t) a - (int8_t) b < -128;
    context->state.ccr_carry = ((uint32_t) a - b) >> 8;
    context->state.ccr_extend = ((uint32_t) a - b) >> 8;
}


/*
 * Update flags for sub.w instructions.
 */
static inline void m68k_sub_w_flags (M68000_Context *context, uint16_t a, uint16_t b, uint16_t result)
{
    context->state.ccr_negative = ((int16_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = (int16_t) a - (int16_t) b > 32767 || (int16_t) a - (int16_t) b < -32768;
    context->state.ccr_carry = ((uint32_t) a - b) >> 16;
    context->state.ccr_extend = ((uint32_t) a - b) >> 16;
}


/*
 * Update flags for sub.l instructions.
 */
static inline void m68k_sub_l_flags (M68000_Context *context, uint32_t a, uint32_t b, uint32_t result)
{
    context->state.ccr_negative = ((int32_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = (int64_t)(int32_t) a - (int32_t) b > 2147483647 ||
                                  (int64_t)(int32_t) a - (int32_t) b < -2147483648;
    context->state.ccr_carry = ((uint64_t) a - b) >> 32;
    context->state.ccr_extend = ((uint64_t) a - b) >> 32;
}


/*
 * Update flags for tst.w instructions.
 */
static inline void m68k_tst_b_flags (M68000_Context *context, uint8_t value)
{
    context->state.ccr_negative = ((int8_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;
}


/*
 * Update flags for tst.w instructions.
 */
static inline void m68k_tst_w_flags (M68000_Context *context, uint16_t value)
{
    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;
}


/*
 * Update flags for tst.w instructions.
 */
static inline void m68k_tst_l_flags (M68000_Context *context, uint32_t value)
{
    context->state.ccr_negative = ((int32_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;
}


/*
 * Update flags for cmp.b instructions.
 */
static inline void m68k_cmp_b_flags (M68000_Context *context, uint8_t a, uint8_t b, uint8_t result)
{
    context->state.ccr_negative = ((int8_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = (int8_t) a - (int8_t) b > 127 || (int8_t) a - (int8_t) b < -128;
    context->state.ccr_carry = ((uint32_t) a - b) >> 8;
}


/*
 * Update flags for cmp.b instructions.
 */
static inline void m68k_cmp_w_flags (M68000_Context *context, uint16_t a, uint16_t b, uint16_t result)
{
    context->state.ccr_negative = ((int16_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = (int16_t) a - (int16_t) b > 32767 || (int16_t) a - (int16_t) b < -32768;
    context->state.ccr_carry = ((uint32_t) a - b) >> 16;
}


/*
 * Update flags for cmp.b instructions.
 */
static inline void m68k_cmp_l_flags (M68000_Context *context, uint32_t a, uint32_t b, uint32_t result)
{
    context->state.ccr_negative = ((int32_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = (int64_t)(int32_t) a - (int32_t) b > 2147483647 ||
                                  (int64_t)(int32_t) a - (int32_t) b < -2147483648;
    context->state.ccr_carry = ((uint64_t) a - b) >> 32;
}


/* ori.b Dn ← Dn | #xxxx */
static uint32_t m68k_0000_ori_b_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint8_t imm = read_extension (context);

    uint8_t result = context->state.d [reg].b | imm;
    context->state.d [reg].b = result;
    m68k_move_b_flags (context, result);

    printf ("ori.b d%d ← #%02x\n", reg, imm);
    return 0;
}


/* ori.b (xxx.w) ← (xxx.w) | #xxxx */
static uint32_t m68k_0038_ori_b_aw (M68000_Context *context, uint16_t instruction)
{
    uint8_t imm = read_extension (context);
    int16_t addr = read_extension (context);

    uint8_t result = read_byte (context, (int32_t) addr) | imm;
    write_byte (context, (int32_t) addr, result);
    m68k_move_b_flags (context, result);

    printf ("ori.b (xxx.w) ← #%02x\n", imm);
    return 0;
}


/* ori.w Dn ← Dn | #xxxx */
static uint32_t m68k_0040_ori_w_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t imm = read_extension (context);

    uint16_t result = context->state.d [reg].w | imm;
    context->state.d [reg].w = result;
    m68k_move_w_flags (context, result);

    printf ("ori.w d%d ← #%04x\n", reg, imm);
    return 0;
}


/* btst.l Dn [Dn] */
static uint32_t m68k_0100_btst_l_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t data_reg = instruction & 0x07;
    uint16_t bit_reg = (instruction >> 9) & 0x07;

    uint32_t value = context->state.d [data_reg].l;
    uint32_t bit = context->state.d [bit_reg].l & 0x1f;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    printf ("btst.l d%d [d%d]\n", data_reg, bit_reg);
    return 0;
}


/* btst.b (An) [Dn] */
static uint32_t m68k_0110_btst_b_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t data_reg = instruction & 0x07;
    uint16_t bit_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte (context, context->state.a [data_reg]);
    uint32_t bit = context->state.d [bit_reg].l & 0x07;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    printf ("btst.b (a%d) [d%d]\n", data_reg, bit_reg);
    return 0;
}


/* bclr.l Dn [Dn] */
static uint32_t m68k_0180_bclr_l_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t data_reg = instruction & 0x07;
    uint16_t bit_reg = (instruction >> 9) & 0x07;

    uint32_t value = context->state.d [data_reg].l;
    uint32_t bit = context->state.d [bit_reg].l & 0x1f;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    value &= ~(1 << bit);
    context->state.d [data_reg].l = value;

    printf ("bclr.l d%d [d%d]\n", data_reg, bit_reg);
    return 0;
}


/* bset.l Dn [Dn] */
static uint32_t m68k_01c0_bset_l_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t data_reg = instruction & 0x07;
    uint16_t bit_reg = (instruction >> 9) & 0x07;

    uint32_t value = context->state.d [data_reg].l;
    uint32_t bit = context->state.d [bit_reg].l & 0x1f;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    value |= 1 << bit;
    context->state.d [data_reg].l = value;

    printf ("bset.l d%d [d%d]\n", data_reg, bit_reg);
    return 0;
}


/* bset.l d(An+Xi) [Dn] */
static uint32_t m68k_01f0_bset_b_danxi_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t data_reg = instruction & 0x07;
    uint16_t bit_reg = (instruction >> 9) & 0x07;

    uint32_t addr = address_with_index (context, context->state.a [data_reg]);

    uint8_t value = read_byte (context, addr);
    uint32_t bit = context->state.d [bit_reg].l & 0x07;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    value |= 1 << bit;
    write_byte (context, addr, value);

    printf ("bset.b d(a%d+Xi) [d%d]\n", data_reg, bit_reg);
    return 0;
}


/* bset.b (xxx.w) [Dn] */
static uint32_t m68k_01f8_bset_b_aw_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t bit_reg = (instruction >> 9) & 0x07;
    int16_t addr = read_extension (context);

    uint8_t value = read_byte (context, addr);
    uint32_t bit = context->state.d [bit_reg].l & 0x07;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    value |= 1 << bit;
    write_byte (context, addr, value);

    printf ("bset.b (xxx.w) [d%d]\n", bit_reg);
    return 0;
}


/* andi.b Dn ← Dn & #xx */
static uint32_t m68k_0200_andi_b_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint8_t imm = read_extension (context);

    uint8_t result = context->state.d [reg].b & imm;
    context->state.d [reg].b = result;
    m68k_move_b_flags (context, result);

    printf ("andi.b d%d ← #%02x\n", reg, imm & 0xff);
    return 0;
}


/* andi.b d(An) ← d(An) & #xx */
static uint32_t m68k_0228_andi_b_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint8_t imm = read_extension (context);
    int16_t displacement = read_extension (context);

    uint8_t result = imm & read_byte (context, context->state.a [reg] + displacement);
    write_byte (context, context->state.a [reg] + displacement, result);
    m68k_move_b_flags (context, result);

    printf ("andi.b d(a%d) ← #%02x\n", reg, imm & 0xff);
    return 0;
}


/* andi.b (xxx.w) ← (xxx.w) & #xx */
static uint32_t m68k_0238_andi_b_aw (M68000_Context *context, uint16_t instruction)
{
    uint8_t imm = read_extension (context);
    int16_t addr = read_extension (context);

    uint8_t result = imm & read_byte (context, (int32_t) addr);
    write_byte (context, (int32_t) addr, result);
    m68k_move_b_flags (context, result);

    printf ("andi.b (xxx.w) ← #%02x\n", imm & 0xff);
    return 0;
}


/* andi.w Dn ← Dn & #xxxx */
static uint32_t m68k_0240_andi_w_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t imm = read_extension (context);

    uint16_t result = context->state.d [reg].w & imm;
    context->state.d [reg].w = result;
    m68k_move_w_flags (context, result);

    printf ("andi.w d%d ← #%04x\n", reg, imm);
    return 0;
}


/* subi.b Dn ← Dn - #xx */
static uint32_t m68k_0400_subi_b_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = read_extension (context);
    uint8_t a = context->state.d [reg].b;

    uint8_t result = a - b;
    context->state.d [reg].b = result;
    m68k_sub_b_flags (context, a, b, result);

    printf ("subi.b d%d ← #%02x\n", reg, b);
    return 0;
}


/* subi.w Dn ← Dn - #xxxx */
static uint32_t m68k_0440_subi_w_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = read_extension (context);
    uint16_t a = context->state.d [reg].w;

    uint16_t result = a - b;
    context->state.d [reg].w = result;
    m68k_sub_w_flags (context, a, b, result);

    printf ("subi.w d%d ← #%04x\n", reg, b);
    return 0;
}


/* subi.w (An)+ ← (An)+ - #xxxx */
static uint32_t m68k_0458_subi_w_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = read_extension (context);
    uint16_t a = read_word (context, context->state.a [reg]);

    uint16_t result = a - b;
    write_word (context, context->state.a [reg], result);
    context->state.a [reg] += 2;
    m68k_sub_w_flags (context, a, b, result);

    printf ("subi.w (a%d)+ ← #%04x\n", reg, b);
    return 0;
}


/* addi.b Dn ← Dn + #xxxx */
static uint32_t m68k_0600_addi_b_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = read_extension (context);
    uint8_t a = context->state.d [reg].b;

    uint8_t result = a + b;
    context->state.d [reg].b = result;
    m68k_add_b_flags (context, a, b, result);

    printf ("addi.b d%d ← #%04x\n", reg, b);
    return 0;
}


/* addi.w Dn ← Dn + #xxxx */
static uint32_t m68k_0640_addi_w_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = read_extension (context);
    uint16_t a = context->state.d [reg].w;

    uint16_t result = a + b;
    context->state.d [reg].w = result;
    m68k_add_w_flags (context, a, b, result);

    printf ("addi.w d%d ← #%04x\n", reg, b);
    return 0;
}


/* addi.w d(An) ← d(An) + #xxxx */
static uint32_t m68k_0668_addi_w_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = read_extension (context);
    int16_t displacement = read_extension (context);
    uint16_t a = read_word (context, context->state.a [reg] + displacement);

    uint16_t result = a + b;
    write_word (context, context->state.a [reg] + displacement, result);
    m68k_add_w_flags (context, a, b, result);

    printf ("addi.w %04x(a%d) ← #%04x\n", displacement, reg, b);
    return 0;
}


/* addi.w (xxx.w) ← (xxx.w) + #xxxx */
static uint32_t m68k_0678_addi_w_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t b = read_extension (context);
    int16_t addr = read_extension (context);
    uint16_t a = read_word (context, addr);

    uint16_t result = a + b;
    write_word (context, addr, result);
    m68k_add_w_flags (context, a, b, result);

    printf ("addi.w (xxx.w) ← #%04x\n", b);
    return 0;
}


/* addi.l Dn ← Dn + #xxxx */
static uint32_t m68k_0680_addi_l_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t b = read_extension_long (context);
    uint32_t a = context->state.d [reg].l;

    uint32_t result = a + b;
    context->state.d [reg].l = result;
    m68k_add_l_flags (context, a, b, result);

    printf ("addi.l d%d ← #%04x\n", reg, b);
    return 0;
}


/* addi.l (An)+ ← (An)+ + #xxxxxxxx */
static uint32_t m68k_0698_addi_l_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t b = read_extension_long (context);
    uint32_t a = read_long (context, context->state.a [reg]);

    uint32_t result = a + b;
    write_long (context, context->state.a [reg], result);
    context->state.a [reg] += 4;
    m68k_add_l_flags (context, a, b, result);

    printf ("addi.w (a%d)+ ← #%08x\n", reg, b);
    return 0;
}


/* btst.l Dn [#xx] */
static uint32_t m68k_0800_btst_l_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t data_reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x1f;
    uint32_t value = context->state.d [data_reg].l;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    printf ("btst.l d%d [#%x]\n", data_reg, bit);
    return 0;
}


/* btst.b (An) [#xx] */
static uint32_t m68k_0810_btst_b_an_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t data_reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x07;
    uint8_t value = read_byte (context, context->state.a [data_reg]);

    context->state.ccr_zero = !((value >> bit) & 0x01);

    printf ("btst.b (a%d) [#%x]\n", data_reg, bit);
    return 0;
}


/* btst.b d(An) [#xx] */
static uint32_t m68k_0828_btst_b_dan_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t data_reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x07;
    int16_t displacement = read_extension (context);
    uint8_t value = read_byte (context, context->state.a [data_reg] + displacement);

    context->state.ccr_zero = !((value >> bit) & 0x01);

    printf ("btst.b %04x(a%d) [#%x]\n", displacement, data_reg, bit);
    return 0;
}


/* btst.b d(An+Xi) [#xx] */
static uint32_t m68k_0830_btst_b_danxi_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t data_reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x07;
    uint8_t value = read_byte_with_index (context, context->state.a [data_reg]);

    context->state.ccr_zero = !((value >> bit) & 0x01);

    printf ("btst.b d(a%d+Xi) [#%x]\n", data_reg, bit);
    return 0;
}


/* btst.b (xxx.w) [#xx] */
static uint32_t m68k_0838_btst_b_aw_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t bit = read_extension (context) & 0x07;
    uint8_t value = read_byte_aw (context);

    context->state.ccr_zero = !((value >> bit) & 0x01);

    printf ("btst.b (xxx.w) [#%x]\n", bit);
    return 0;
}


/* btst.b (xxx.l) [#xx] */
static uint32_t m68k_0839_btst_b_al_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t bit = read_extension (context) & 0x07;
    uint8_t value = read_byte_al (context);

    context->state.ccr_zero = !((value >> bit) & 0x01);

    printf ("btst.b (xxx.l) [#%x]\n", bit);
    return 0;
}


/* bchg.b d(An) [#xx] */
static uint32_t m68k_0868_bcgh_b_dan_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x07;
    int16_t displacement = read_extension (context);

    uint8_t value = read_byte (context, context->state.a [reg] + displacement);
    context->state.ccr_zero = !((value >> bit) & 0x01);

    value ^= 1 << bit;
    write_byte (context, context->state.a [reg] + displacement, value);

    printf ("bchg.b %04x(a%d) [#%x]\n", displacement, reg, bit);
    return 0;
}


/* bclr.l Dn [#xx] */
static uint32_t m68k_0880_bclr_l_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x1f;

    uint32_t value = context->state.d [reg].l;
    context->state.ccr_zero = !((value >> bit) & 0x01);

    value &= ~(1 << bit);
    context->state.d [reg].l = value;

    printf ("bclr.l d%d [#%x]\n", reg, bit);
    return 0;
}


/* bclr.b (An) [#xx] */
static uint32_t m68k_0890_bclr_b_an_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x07;

    uint8_t value = read_byte (context, context->state.a [reg]);
    context->state.ccr_zero = !((value >> bit) & 0x01);

    value &= ~(1 << bit);
    write_byte (context, context->state.a [reg], value);

    printf ("bclr.b (a%d) [#%x]\n", reg, bit);
    return 0;
}


/* bclr.b d(An) [#xx] */
static uint32_t m68k_08a8_bclr_b_dan_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x07;
    int16_t displacement = read_extension (context);

    uint8_t value = read_byte (context, context->state.a [reg] + displacement);
    context->state.ccr_zero = !((value >> bit) & 0x01);

    value &= ~(1 << bit);
    write_byte (context, context->state.a [reg] + displacement, value);

    printf ("bclr.b %04x(a%d) [#%x]\n", displacement, reg, bit);
    return 0;
}


/* bclr.b d(An+Xi) [#xx] */
static uint32_t m68k_08b0_bclr_b_danxi_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x07;

    uint32_t address = address_with_index (context, context->state.a [reg]);

    uint8_t value = read_byte (context, address);
    context->state.ccr_zero = !((value >> bit) & 0x01);

    value &= ~(1 << bit);
    write_byte (context, address, value);

    printf ("bclr.b d(a%d+Xi) [#%x]\n", reg, bit);
    return 0;
}


/* bclr.b (xxx.w) [#xx] */
static uint32_t m68k_08b8_bclr_b_aw_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t bit = read_extension (context) & 0x07;
    uint32_t address = (int16_t) read_extension (context);

    uint8_t value = read_byte (context, address);
    context->state.ccr_zero = !((value >> bit) & 0x01);

    value &= ~(1 << bit);
    write_byte (context, address, value);

    printf ("bclr.b (xxx.w) [#%x]\n", bit);
    return 0;
}


/* bset.b (An) [#xx] */
static uint32_t m68k_08d0_bset_b_an_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x07;

    uint8_t value = read_byte (context, context->state.a [reg]);
    context->state.ccr_zero = !((value >> bit) & 0x01);

    value |= 1 << bit;
    write_byte (context, context->state.a [reg], value);

    printf ("bset.b (a%d) [#%x]\n", reg, bit);
    return 0;
}


/* bset.b d(An) [#xx] */
static uint32_t m68k_08e8_bset_b_dan_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x07;
    int16_t displacement = read_extension (context);

    uint8_t value = read_byte (context, context->state.a [reg] + displacement);
    context->state.ccr_zero = !((value >> bit) & 0x01);

    value |= 1 << bit;
    write_byte (context, context->state.a [reg] + displacement, value);

    printf ("bset.b %04x(a%d) [#%x]\n", displacement, reg, bit);
    return 0;
}


/* bset.b d(An+Xi) [#xx] */
static uint32_t m68k_08f0_bset_b_danxi_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x07;

    uint32_t address = address_with_index (context, context->state.a [reg]);

    uint8_t value = read_byte (context, address);
    context->state.ccr_zero = !((value >> bit) & 0x01);

    value |= 1 << bit;
    write_byte (context, address, value);

    printf ("bset.b d(a%d+Xi) [#%x]\n", reg, bit);
    return 0;
}


/* bset.b (xxx.w) [#xx] */
static uint32_t m68k_08f8_bset_b_aw_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t bit = read_extension (context) & 0x07;
    int16_t addr = read_extension (context);

    uint8_t value = read_byte (context, addr);
    context->state.ccr_zero = !((value >> bit) & 0x01);

    value |= 1 << bit;
    write_byte (context, addr, value);

    printf ("bset.b (xxx.w) [#%x]\n", bit);
    return 0;
}


/* eori.b (xxx.w) ← (xxx.w) ^ #xx */
static uint32_t m68k_0a38_eori_b_aw (M68000_Context *context, uint16_t instruction)
{
    uint8_t imm = read_extension (context);
    int16_t addr = read_extension (context);

    uint8_t result = read_byte (context, (int32_t) addr) ^ imm;
    write_byte (context, (int32_t) addr, result);
    m68k_move_b_flags (context, result);

    printf ("eori.b (xxx.w) ← #%02x\n", imm & 0xff);
    return 0;
}


/* eori.w Dn ← Dn ^ #xxxx */
static uint32_t m68k_0a40_eori_w_dn (M68000_Context *context, uint16_t instruction)
{
    uint32_t imm = read_extension (context);
    uint16_t reg = instruction & 0x07;

    uint16_t result = context->state.d [reg].w ^ imm;
    context->state.d [reg].w = result;
    m68k_move_w_flags (context, result);

    printf ("eori.w d%d ← #%04x\n", reg, imm);
    return 0;
}


/* eori.l Dn ← Dn ^ #xxxxxxxx */
static uint32_t m68k_0a80_eori_l_dn (M68000_Context *context, uint16_t instruction)
{
    uint32_t imm = read_extension_long (context);
    uint16_t reg = instruction & 0x07;

    uint32_t result = context->state.d [reg].l ^ imm;
    context->state.d [reg].l = result;
    m68k_move_l_flags (context, result);

    printf ("eori.l d%d ← #%08x\n", reg, imm);
    return 0;
}


/* cmpi.b Dn - #xx */
static uint32_t m68k_0c00_cmpi_b_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = read_extension (context);
    uint8_t a = context->state.d [reg].b;
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

    printf ("cmpi.b d%d - #%02x\n", reg, b);
    return 0;
}


/* cmpi.b (An) - #xx */
static uint32_t m68k_0c10_cmpi_b_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint8_t b = read_extension (context);

    uint8_t a = read_byte (context, context->state.a [reg]);
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

    printf ("cmpi.b (a%d) - #%02x\n", reg, b);
    return 0;
}


/* cmpi.b d(An) - #xx */
static uint32_t m68k_0c28_cmpi_b_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint8_t b = read_extension (context);
    int16_t displacement = read_extension (context);

    uint8_t a = read_byte (context, context->state.a [reg] + displacement);
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

    printf ("cmpi.b %04x(a%d) - #%02x\n", displacement, reg, b);
    return 0;
}


/* cmpi.b (xxx.w) - #xx */
static uint32_t m68k_0c38_cmpi_b_aw (M68000_Context *context, uint16_t instruction)
{
    uint8_t b = read_extension (context);
    uint8_t a = read_byte_aw (context);
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

    printf ("cmpi.b (xxx.w) - #%02x\n", b);
    return 0;
}


/* cmpi.w Dn - #xxxx */
static uint32_t m68k_0c40_cmpi_w_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = read_extension (context);
    uint16_t a = context->state.d [reg].w;
    uint16_t result = a - b;

    m68k_cmp_w_flags (context, a, b, result);

    printf ("cmpi.w d%d - #%04x\n", reg, b);
    return 0;
}


/* cmpi.w (An) - #xxxx */
static uint32_t m68k_0c50_cmpi_w_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = read_extension (context);
    uint16_t a = read_word (context, context->state.a [reg]);
    uint16_t result = a - b;

    m68k_cmp_w_flags (context, a, b, result);

    printf ("cmpi.w (a%d) - #%04x\n", reg, b);
    return 0;
}


/* cmpi.w d(An) - #xxxx */
static uint32_t m68k_0c68_cmpi_w_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t b = read_extension (context);
    int16_t displacement = read_extension (context);

    uint16_t a = read_word (context, context->state.a [reg] + displacement);
    uint16_t result = a - b;

    m68k_cmp_w_flags (context, a, b, result);

    printf ("cmpi.w %04x(a%d) - #%04x\n", displacement, reg, b);
    return 0;
}


/* cmpi.w (xxx.w) - #xxxx */
static uint32_t m68k_0c78_cmpi_w_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t b = read_extension (context);
    uint16_t a = read_word_aw (context);
    uint16_t result = a - b;

    m68k_cmp_w_flags (context, a, b, result);

    printf ("cmpi.w (xxx.w) - #%02x\n", b);
    return 0;
}


/* cmpi.l (An)+ - #xxxxxxxx */
static uint32_t m68k_0c98_cmpi_l_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t b = read_extension_long (context);
    uint32_t a = read_long (context, context->state.a [reg]);
    context->state.a [reg] += 4;
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    printf ("cmpi.l (a%d)+ - #%04x\n", reg, b);
    return 0;
}


/* move.b Dn ← Dn */
static uint32_t m68k_1000_move_b_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = context->state.d [source_reg].b;
    context->state.d [dest_reg].b = value;

    m68k_move_b_flags (context, value);

    printf ("move.b d%d ← d%d\n", dest_reg, source_reg);
    return 0;
}


/* move.b Dn ← (An) */
static uint32_t m68k_1010_move_b_dn_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    context->state.d [dest_reg].b = value;
    m68k_move_b_flags (context, value);

    printf ("move.b d%d ← (a%d)\n", dest_reg, source_reg);
    return 0;
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


/* move.b Dn ← d(An+Xi) */
static uint32_t m68k_1030_move_b_dn_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t addr = context->state.a [source_reg];
    uint8_t value = read_byte_with_index (context, addr);
    context->state.d [dest_reg].b = value;

    m68k_move_b_flags (context, value);

    printf ("move.b d%d ← d(a%d+Xi)\n", dest_reg, source_reg);
    return 0;
}


/* move.b Dn ← d(PC+Xi) */
static uint32_t m68k_103b_move_b_dn_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_with_index (context, context->state.pc);
    context->state.d [dest_reg].b = value;
    m68k_move_b_flags (context, value);

    printf ("move.b d%d ← d(PC+Xi)\n", dest_reg);
    return 0;
}


/* move.b Dn ← (xxx.w) */
static uint32_t m68k_1038_move_b_dn_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_aw (context);
    context->state.d [dest_reg].b = value;
    m68k_move_b_flags (context, value);

    printf ("move.b d%d ← (xxx.w)\n", dest_reg);
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


/* move.b Dn ← #xx */
static uint32_t m68k_103c_move_b_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_extension (context);
    context->state.d [dest_reg].b = value;
    m68k_move_b_flags (context, value);

    printf ("move.b d%d ← #%02x\n", dest_reg, value);
    return 0;
}


/* move.b (An) ← (An)+ */
static uint32_t m68k_1098_move_b_an_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    context->state.a [source_reg] += (source_reg == 7) ? 2 : 1;
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    printf ("move.b (a%d) ← (a%d)+\n", dest_reg, source_reg);
    return 0;
}


/* move.b (An) ← d(An+Xi) */
static uint32_t m68k_10b0_move_b_an_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte_with_index (context, context->state.a [source_reg]);
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    printf ("move.b (a%d) ← d(a%d+Xi)\n", dest_reg, source_reg);
    return 0;
}


/* move.b (An) ← #xx */
static uint32_t m68k_10bc_move_b_an_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_extension (context);
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    printf ("move.b (a%d) ← #%02x\n", dest_reg, value);
    return 0;
}


/* move.b (An)+ ← Dn */
static uint32_t m68k_10c0_move_b_anp_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = context->state.d [source_reg].b;
    write_byte (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += (dest_reg == 7) ? 2 : 1;
    m68k_move_b_flags (context, value);

    printf ("move.b (a%d)+ ← d%d\n", dest_reg, source_reg);
    return 0;
}


/* move.b (An)+ ← (An)+ */
static uint32_t m68k_10d8_move_b_anp_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    context->state.a [source_reg] += (source_reg == 7) ? 2 : 1;
    write_byte (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += (dest_reg == 7) ? 2 : 1;
    m68k_move_b_flags (context, value);

    printf ("move.b (a%d)+ ← (a%d)+\n", dest_reg, source_reg);
    return 0;
}

/* move.b d(An) ← Dn */
static uint32_t m68k_1140_move_b_dan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    uint8_t value = context->state.d [source_reg].b;
    write_byte (context, context->state.a [dest_reg] + displacement, value);
    m68k_move_b_flags (context, value);

    printf ("move.b %04x(a%d) ← d%d\n", displacement, dest_reg, source_reg);
    return 0;
}


/* move.b d(An) ← (An) */
static uint32_t m68k_1150_move_b_dan_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    write_byte (context, context->state.a [dest_reg] + displacement, value);
    m68k_move_b_flags (context, value);

    printf ("move.b %04x(a%d) ← (a%d)\n", displacement, dest_reg, source_reg);
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


/* move.b d(An) ← d(An) */
static uint32_t m68k_1168_move_b_dan_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    int16_t source_displacement = read_extension (context);
    int16_t dest_displacement = read_extension (context);

    uint8_t value = read_byte (context, context->state.a [source_reg] + source_displacement);
    write_byte (context, context->state.a [dest_reg] + dest_displacement, value);
    m68k_move_b_flags (context, value);

    printf ("move.b %04x(a%d) ← %04x(a%d)\n", dest_displacement, dest_reg, source_displacement, source_reg);
    return 0;
}


/* move.b d(An) ← d(An+Xi) */
static uint32_t m68k_1170_move_b_dan_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte_with_index (context, context->state.a [source_reg]);
    int16_t dest_displacement = read_extension (context);
    write_byte (context, context->state.a [dest_reg] + dest_displacement, value);
    m68k_move_b_flags (context, value);

    printf ("move.b %04x(a%d) ← d(a%d+Xi)\n", dest_displacement, dest_reg, source_reg);
    return 0;
}


/* move.b d(An) ← (xxx.w) */
static uint32_t m68k_1178_move_b_dan_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_aw (context);
    int16_t displacement = read_extension (context);
    write_byte (context, context->state.a [dest_reg] + displacement, value);
    m68k_move_b_flags (context, value);

    printf ("move.b %04x(a%d) ← (xxx.w)\n", displacement, dest_reg);
    return 0;
}


/* move.b d(An) ← Imm */
static uint32_t m68k_117c_move_b_dan_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint8_t value = read_extension (context);
    int16_t displacement = read_extension (context);

    write_byte (context, context->state.a [dest_reg] + displacement, value);
    m68k_move_b_flags (context, value);

    printf ("move.b %04x(a%d) ← #%02x\n", displacement, dest_reg, value);
    return 0;
}


/* move.b d(An+Xi) ← Dn */
static uint32_t m68k_1180_move_b_danxi_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = context->state.d [source_reg].b;
    uint32_t addr = context->state.a [dest_reg];

    write_byte_with_index (context, addr, value);
    m68k_move_b_flags (context, value);

    printf ("move.b d(a%d+Xi) ← d%d\n", dest_reg, source_reg);
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


/* move.b (xxx.w) ← d(An) */
static uint32_t m68k_11e8_move_b_aw_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    uint8_t value = read_byte (context, context->state.a [source_reg] + displacement);
    write_byte_aw (context, value);
    m68k_move_b_flags (context, value);

    printf ("move.b (xxx.w) ← %04x(a%d)\n", displacement, source_reg);
    return 0;
}


/* move.b (xxx.w) ← #xx */
static uint32_t m68k_11fc_move_b_aw_imm (M68000_Context *context, uint16_t instruction)
{
    uint8_t value = read_extension (context);

    write_byte_aw (context, value);
    m68k_move_b_flags (context, value);

    printf ("move.b (xxx.w) ← #%02x\n", value);
    return 0;
}


/* move.b (xxx.l) ← Dn */
static uint32_t m68k_13c0_move_b_al_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = context->state.d [source_reg].b;
    write_byte_al (context, value);
    m68k_move_b_flags (context, value);

    printf ("move.b (xxx.l) ← d%d\n", source_reg);
    return 0;
}


/* move.b (xxx.l) ← d(An) */
static uint32_t m68k_13e8_move_b_al_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    uint8_t value = read_byte (context, context->state.a [source_reg] + displacement);
    write_byte_al (context, value);
    m68k_move_b_flags (context, value);

    printf ("move.b (xxx.l) ← %04x(a%d)\n", displacement, source_reg);
    return 0;
}


/* move.b (xxx.l) ← #xx */
static uint32_t m68k_13fc_move_b_al_imm (M68000_Context *context, uint16_t instruction)
{
    uint8_t value = read_extension (context);

    write_byte_al (context, value);
    m68k_move_b_flags (context, value);

    printf ("move.b (xxx.l) ← #%02x\n", value);
    return 0;
}


/* move.l Dn ← Dn */
static uint32_t m68k_2000_move_l_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = context->state.d [source_reg].l;
    context->state.d [dest_reg].l = value;
    m68k_move_l_flags (context, value);

    printf ("move.l d%d ← d%d\n", dest_reg, source_reg);
    return 0;
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


/* move.l Dn ← d(An) */
static uint32_t m68k_2028_move_l_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    uint32_t value = read_long (context, context->state.a [source_reg] + displacement);
    context->state.d [dest_reg].l = value;
    m68k_move_l_flags (context, value);

    printf ("move.l d%d ← %04x(a%d)\n", dest_reg, displacement, source_reg);
    return 0;
}


/* move.l Dn ← (xxx.w) */
static uint32_t m68k_2038_move_l_dn_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_aw (context);
    context->state.d [dest_reg].l = value;
    m68k_move_l_flags (context, value);

    printf ("move.l d%d ← (xxx.w)\n", dest_reg);
    return 0;
}


/* move.l Dn ← Imm */
static uint32_t m68k_203c_move_l_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint32_t value = read_extension_long (context);

    context->state.d [dest_reg].l = value;
    m68k_move_l_flags (context, value);

    printf ("move.l d%d ← #%08x\n", dest_reg, value);
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


/* movea.l An ← An */
static uint32_t m68k_2048_movea_l_an_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [dest_reg] = context->state.a [source_reg];

    printf ("movea.l a%d ← a%d\n", dest_reg, source_reg);
    return 0;
}


/* movea.l An ← (An)+ */
static uint32_t m68k_2058_movea_l_an_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 4;
    context->state.a [dest_reg] = value;

    printf ("movea.l a%d ← (a%d)+\n", dest_reg, source_reg);
    return 0;
}


/* movea.l An ← d(An) */
static uint32_t m68k_2068_movea_l_an_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    context->state.a [dest_reg] = read_long (context, context->state.a [source_reg] + displacement);

    printf ("movea.l a%d ← %04x(a%d)\n", dest_reg, displacement, source_reg);
    return 0;
}


/* movea.l An ← d(An+Xi) */
static uint32_t m68k_2070_movea_l_an_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [dest_reg] = read_long_with_index (context, context->state.a [source_reg]);

    printf ("movea.l a%d ← d(a%d+Xi)\n", dest_reg, source_reg);
    return 0;
}


/* movea.l An ← (xxx.w) */
static uint32_t m68k_2078_movea_l_an_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint32_t value = read_long_aw (context);

    context->state.a [dest_reg] = value;

    printf ("movea.l a%d ← (xxx.w)\n", dest_reg);
    return 0;
}


/* movea.l An ← (xxx.l) */
static uint32_t m68k_2079_movea_l_an_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint32_t value = read_long_al (context);

    context->state.a [dest_reg] = value;

    printf ("movea.l a%d ← (xxx.l)\n", dest_reg);
    return 0;
}


/* movea.l An ← d(PC+Xi) */
static uint32_t m68k_207b_movea_l_an_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    context->state.a [dest_reg] = read_long_with_index (context, context->state.pc);

    printf ("movea.l a%d ← d(PC+Xi)+\n", dest_reg);
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


/* move.l (An) ← d(An+Xi) */
static uint32_t m68k_20b0_move_l_an_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long_with_index (context, context->state.a [source_reg]);
    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    printf ("move.l (a%d) ← d(a%d+Xi)\n", dest_reg, source_reg);
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


/* move.l (An)+ ← (An)+ */
static uint32_t m68k_20d8_move_l_anp_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 4;
    write_long (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 4;
    m68k_move_l_flags (context, value);

    printf ("move.l (a%d)+ ← (a%d)+\n", dest_reg, source_reg);
    return 0;
}


/* move.l (An)+ ← d(An) */
static uint32_t m68k_20e8_move_l_anp_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    uint32_t value = read_long (context, context->state.a [source_reg] + displacement);
    write_long (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 4;
    m68k_move_l_flags (context, value);

    printf ("move.l (a%d)+ ← %04x(a%d)\n", dest_reg, displacement, source_reg);
    return 0;
}


/* move.l (An)+ ← d(An+Xi) */
static uint32_t m68k_20f0_move_l_anp_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long_with_index (context, context->state.a [source_reg]);
    write_long (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 4;
    m68k_move_l_flags (context, value);

    printf ("move.l (a%d)+ ← d(a%d+Xi)\n", dest_reg, source_reg);
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


/* move.l -(An) ← An */
static uint32_t m68k_2108_move_l_pan_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = context->state.a [source_reg];
    context->state.a [dest_reg] -= 4;
    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    printf ("move.l -(a%d) ← a%d\n", dest_reg, source_reg);
    return 0;
}


/* move.l d(An) ← Dn */
static uint32_t m68k_2140_move_l_dan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = context->state.d [source_reg].l;
    int16_t displacement = read_extension (context);

    write_long (context, context->state.a [dest_reg] + displacement, value);
    m68k_move_l_flags (context, value);

    printf ("move.l %04x(a%d) ← d%d\n", displacement, dest_reg, source_reg);
    return 0;
}


/* move.l d(An) ← An */
static uint32_t m68k_2148_move_l_dan_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = context->state.a [source_reg];
    int16_t displacement = read_extension (context);

    write_long (context, context->state.a [dest_reg] + displacement, value);
    m68k_move_l_flags (context, value);

    printf ("move.l %04x(a%d) ← a%d\n", displacement, dest_reg, source_reg);
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


/* move.l d(An+Xi) ← An */
static uint32_t m68k_2188_move_l_danxi_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = context->state.a [source_reg];
    uint32_t addr = context->state.a [dest_reg];

    write_long_with_index (context, addr, value);
    m68k_move_l_flags (context, value);

    printf ("move.l d(a%d+Xi) ← a%d\n", dest_reg, source_reg);
    return 0;
}


/* move.l d(An+Xi) ← #xxxxxxxx */
static uint32_t m68k_21bc_move_l_danxi_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_extension_long (context);
    uint32_t addr = context->state.a [dest_reg];

    write_long_with_index (context, addr, value);
    m68k_move_l_flags (context, value);

    printf ("move.l d(a%d+Xi) ← #%08x\n", dest_reg, value);
    return 0;
}


/* move.l (xxx.w) ← Dn */
static uint32_t m68k_21c0_move_l_aw_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = context->state.d [source_reg].l;
    write_long_aw (context, value);
    m68k_move_l_flags (context, value);

    printf ("move.l (xxx.w) ← d%d\n", source_reg);
    return 0;
}


/* move.l (xxx.w) ← An */
static uint32_t m68k_21c8_move_l_aw_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = context->state.a [source_reg];
    write_long_aw (context, value);
    m68k_move_l_flags (context, value);

    printf ("move.l (xxx.w) ← a%d\n", source_reg);
    return 0;
}


/* move.l (xxx.w) ← d(PC+Xi) */
static uint32_t m68k_21fb_move_l_aw_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint32_t value = read_long_with_index (context, context->state.pc);

    write_long_aw (context, value);
    m68k_move_l_flags (context, value);

    printf ("move.w (xxx.w) ← d(PC+Xi)\n");
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


/* move.l (xxx.l) ← (xxx.w) */
static uint32_t m68k_23f8_move_l_al_aw (M68000_Context *context, uint16_t instruction)
{
    uint32_t value = read_long_aw (context);

    write_long_al (context, value);
    m68k_move_l_flags (context, value);

    printf ("move.l (xxx.l) ← (xxx.w)\n");
    return 0;
}


/* move.l (xxx.l) ← #xxxx */
static uint32_t m68k_23fc_move_l_al_imm (M68000_Context *context, uint16_t instruction)
{
    uint32_t value = read_extension_long (context);

    write_long_al (context, value);
    m68k_move_l_flags (context, value);

    printf ("move.l (xxx.l) ← #%08x\n", value);
    return 0;
}


/* move.w Dn ← Dn */
static uint32_t m68k_3000_move_w_dn_dn (M68000_Context *context, uint16_t instruction)
{
    /* TODO: Consider, instruction union/struct, to pick out the fields */
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = context->state.d [source_reg].w;
    context->state.d [dest_reg].w = value;
    m68k_move_w_flags (context, value);

    printf ("move.w d%d ← d%d\n", dest_reg, source_reg);
    return 0;
}


/* move.w Dn ← An */
static uint32_t m68k_3008_move_w_dn_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = context->state.a [source_reg];
    context->state.d [dest_reg].w = value;
    m68k_move_w_flags (context, value);

    printf ("move.w d%d ← a%d\n", dest_reg, source_reg);
    return 0;
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


/* move.w Dn ← (An)+ */
static uint32_t m68k_3018_move_w_dn_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 2;
    context->state.d [dest_reg].w = value;
    m68k_move_w_flags (context, value);

    printf ("move.w d%d ← (a%d)+\n", dest_reg, source_reg);
    return 0;
}


/* move.w Dn ← d(An) */
static uint32_t m68k_3028_move_w_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    uint16_t value = read_word (context, context->state.a [source_reg] + displacement);
    context->state.d [dest_reg].w = value;
    m68k_move_w_flags (context, value);

    printf ("move.w d%d ← %04x(a%d)\n", dest_reg, displacement, source_reg);
    return 0;
}


/* move.w Dn ← d(An+Xi) */
static uint32_t m68k_3030_move_w_dn_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t addr = context->state.a [source_reg];
    uint16_t value = read_word_with_index (context, addr);
    context->state.d [dest_reg].w = value;

    m68k_move_w_flags (context, value);

    printf ("move.w d%d ← d(a%d+Xi)\n", dest_reg, source_reg);
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


/* move.w Dn ← (xxx.w) */
static uint32_t m68k_3038_move_w_dn_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_aw (context);
    context->state.d [dest_reg].w = value;
    m68k_move_w_flags (context, value);

    printf ("move.w d%d ← (xxx.w)\n", dest_reg);
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


/* move.w Dn ← d(PC+Xi) */
static uint32_t m68k_303b_move_w_dn_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_with_index (context, context->state.pc);
    context->state.d [dest_reg].w = value;
    m68k_move_w_flags (context, value);

    printf ("move.w d%d ← d(PC+Xi)\n", dest_reg);
    return 0;
}


/* movea.w An ← Dn */
static uint32_t m68k_3040_movea_w_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [dest_reg] = (int16_t) context->state.d [source_reg].w;

    printf ("movea.w a%d ← d%d\n", dest_reg, source_reg);
    return 0;
}


/* movea.w An ← (An)+ */
static uint32_t m68k_3058_movea_w_an_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 2;
    context->state.a [dest_reg] = (int16_t) value;

    printf ("movea.w a%d ← (a%d)+\n", dest_reg, source_reg);
    return 0;
}


/* movea.w An ← d(An) */
static uint32_t m68k_3068_movea_w_an_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    context->state.a [dest_reg] = (int16_t) read_word (context, context->state.a [source_reg] + displacement);

    printf ("movea.w a%d ← %04x(a%d)\n", dest_reg, displacement, source_reg);
    return 0;
}


/* movea.w An ← d(An+Xi) */
static uint32_t m68k_3074_movea_w_an_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word_with_index (context, context->state.a [source_reg]);
    context->state.a [dest_reg] = (int16_t) value;

    printf ("movea.w a%d ← d(a%d+Xi)+\n", dest_reg, source_reg);
    return 0;
}


/* movea.w An ← #xxxx */
static uint32_t m68k_307c_movea_w_an_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_extension (context);
    context->state.a [dest_reg] = (int16_t) value;

    printf ("movea.w a%d ← #%04x\n", dest_reg, value);
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


/* move.w (An) ← An */
static uint32_t m68k_3088_move_w_an_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = context->state.a [source_reg];
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    printf ("move.w (a%d) ← a%d\n", dest_reg, source_reg);
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


/* move.w (An) ← (xxx.w) */
static uint32_t m68k_30b8_move_w_an_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_aw (context);
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    printf ("move.w (a%d) ← (xxx.w)\n", dest_reg);
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


/* move.w (An)+ ← Dn */
static uint32_t m68k_30c0_move_w_anp_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = context->state.d [source_reg].w;
    write_word (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 2;
    m68k_move_w_flags (context, value);

    printf ("move.w (a%d)+ ← d%d\n", dest_reg, source_reg);
    return 0;
}


/* move.w (An)+ ← An */
static uint32_t m68k_30c8_move_w_anp_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = context->state.a [source_reg];
    write_word (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 2;
    m68k_move_w_flags (context, value);

    printf ("move.w (a%d)+ ← a%d\n", dest_reg, source_reg);
    return 0;
}


/* move.w (An)+ ← (An)+ */
static uint32_t m68k_30d8_move_w_anp_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 2;
    write_word (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 2;
    m68k_move_w_flags (context, value);

    printf ("move.w (a%d)+ ← (a%d)+\n", dest_reg, source_reg);
    return 0;
}


/* move.w (An)+ ← d(An) */
static uint32_t m68k_30e8_move_w_anp_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    uint16_t value = read_word (context, context->state.a [source_reg] + displacement);
    write_word (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 2;
    m68k_move_w_flags (context, value);

    printf ("move.w (a%d)+ ← %04x(a%d)\n", dest_reg, displacement, source_reg);
    return 0;
}


/* move.w (An)+ ← #xxxx */
static uint32_t m68k_30fc_move_w_anp_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_extension (context);
    write_word (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 2;
    m68k_move_w_flags (context, value);

    printf ("move.w (a%d)+ ← #%04x\n", dest_reg, value);
    return 0;
}


/* move.w -(An) ← Dn */
static uint32_t m68k_3100_move_w_pan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = context->state.d [source_reg].w;
    context->state.a [dest_reg] -= 2;
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    printf ("move.w -(a%d) ← d%d\n", dest_reg, source_reg);
    return 0;
}


/* move.w d(An) ← Dn */
static uint32_t m68k_3140_move_w_dan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    uint16_t value = context->state.d [source_reg].w;
    write_word (context, context->state.a [dest_reg] + displacement, value);
    m68k_move_w_flags (context, value);

    printf ("move.w %04x(a%d) ← d%d\n", displacement, dest_reg, source_reg);
    return 0;
}


/* move.w d(An) ← (An) */
static uint32_t m68k_3150_move_w_dan_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    uint16_t value = read_word (context, context->state.a [source_reg]);
    write_word (context, context->state.a [dest_reg] + displacement, value);
    m68k_move_w_flags (context, value);

    printf ("move.w %04x(a%d) ← (a%d)\n", displacement, dest_reg, source_reg);
    return 0;
}


/* move.w d(An) ← (An)+ */
static uint32_t m68k_3158_move_w_dan_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    uint16_t value = read_word (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 2;
    write_word (context, context->state.a [dest_reg] + displacement, value);
    m68k_move_w_flags (context, value);

    printf ("move.w %04x(a%d) ← (a%d)+\n", displacement, dest_reg, source_reg);
    return 0;
}


/* move.w d(An) ← d(An) */
static uint32_t m68k_3168_move_w_dan_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    int16_t source_displacement = read_extension (context);
    int16_t dest_displacement = read_extension (context);

    uint16_t value = read_word (context, context->state.a [source_reg] + source_displacement);
    write_word (context, context->state.a [dest_reg] + dest_displacement, value);
    m68k_move_w_flags (context, value);

    printf ("move.w %04x(a%d) ← %04x(a%d)\n", dest_displacement, dest_reg, source_displacement, source_reg);
    return 0;
}


/* move.w d(An) ← d(An+Xi) */
static uint32_t m68k_3170_move_b_dan_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word_with_index (context, context->state.a [source_reg]);
    int16_t dest_displacement = read_extension (context);
    write_word (context, context->state.a [dest_reg] + dest_displacement, value);
    m68k_move_w_flags (context, value);

    printf ("move.w %04x(a%d) ← d(a%d+Xi)\n", dest_displacement, dest_reg, source_reg);
    return 0;
}


/* move.w d(An) ← Imm */
static uint32_t m68k_317c_move_w_dan_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t value = read_extension (context);
    int16_t displacement = read_extension (context);

    write_word (context, context->state.a [dest_reg] + displacement, value);
    m68k_move_w_flags (context, value);

    printf ("move.w %04x(a%d) ← #%02x\n", displacement, dest_reg, value);
    return 0;
}


/* move.w d(An+Xi) ← Dn */
static uint32_t m68k_3180_move_w_danxi_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = context->state.d [source_reg].w;
    uint32_t addr = context->state.a [dest_reg];

    write_word_with_index (context, addr, value);
    m68k_move_w_flags (context, value);

    printf ("move.w d(a%d+Xi) ← d%d\n", dest_reg, source_reg);
    return 0;
}


/* move.w d(An+Xi) ← (An) */
static uint32_t m68k_3190_move_w_danxi_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    uint32_t addr = context->state.a [dest_reg];

    write_word_with_index (context, addr, value);
    m68k_move_w_flags (context, value);

    printf ("move.w d(a%d+Xi) ← (a%d)\n", dest_reg, source_reg);
    return 0;
}


/* move.w d(An+Xi) ← (An)+ */
static uint32_t m68k_3198_move_w_danxi_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 2;
    uint32_t addr = context->state.a [dest_reg];

    write_word_with_index (context, addr, value);
    m68k_move_w_flags (context, value);

    printf ("move.w d(a%d+Xi) ← (a%d)+\n", dest_reg, source_reg);
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


/* move.w (xxx.w) ← (xxx.w) */
static uint32_t m68k_31f8_move_w_aw_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t value = read_word_aw (context);
    write_word_aw (context, value);
    m68k_move_w_flags (context, value);

    printf ("move.w (xxx.w) ← (xxx.w)\n");
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


/* move.w (xxx.l) ← Dn */
static uint32_t m68k_33c0_move_w_al_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = context->state.d [source_reg].w;
    write_word_al (context, value);
    m68k_move_w_flags (context, value);

    printf ("move.w (xxx.l) ← d%d\n", source_reg);
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


/* move Dn ← sr */
static uint32_t m68k_40c0_move_dn_sr (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = instruction & 0x07;

    context->state.d [dest_reg].w = context->state.sr;

    printf ("move d%d ← sr\n", dest_reg);
    return 0;
}


/* lea An ← d(An) */
static uint32_t m68k_41e8_lea_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    int16_t displacement = read_extension (context);

    context->state.a [dest_reg] = context->state.a [source_reg] + displacement;

    printf ("lea a%d ← %04x(a%d)\n", dest_reg, displacement, source_reg);
    return 0;
}


/* lea An ← d(An+Xi) */
static uint32_t m68k_41f0_lea_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    context->state.a [dest_reg] = address_with_index (context, context->state.a [source_reg]);

    printf ("lea a%d ← d(a%d+Xi)\n", dest_reg, source_reg);
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
static uint32_t m68k_41f9_lea_al (M68000_Context *context, uint16_t instruction)
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

    printf ("lea a%d ← %+d(pc)\n", dest_reg, displacement);
    return 0;
}


/* lea An ← d(PC+Xi) */
static uint32_t m68k_41fb_lea_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint32_t pc = context->state.pc;
    uint16_t extension = read_extension (context);

    int8_t displacement = extension & 0xff;
    uint16_t index_reg = (extension >> 12) & 0x07;
    uint32_t index;

    /* Bit 15 selects Dn or An */
    if ((extension & BIT_15) == 0)
    {
        index = context->state.d [index_reg].l;
    }
    else
    {
        index = context->state.a [index_reg];
    }

    /* Bit 11 selects index size, sign-extended word, or long */
    if ((extension & BIT_11) == 0)
    {
        int16_t index_w = index & 0xffff;
        index = index_w;
    }

    context->state.a [dest_reg] = pc + displacement + index;

    printf ("lea a%d ← d(PC+Xi)\n", dest_reg);
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


/* clr.b (An)+ */
static uint32_t m68k_4218_clr_b_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    write_byte (context, context->state.a [reg], 0x00);
    context->state.a [reg] += (reg == 7) ? 2 : 1;

    m68k_clr_flags (context);

    printf ("clr.b (a%d)+\n", reg);
    return 0;
}


/* clr.b d(An) */
static uint32_t m68k_4228_clr_b_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    write_byte (context, context->state.a [reg] + displacement, 0x00);
    m68k_clr_flags (context);

    printf ("clr.b %04x(a%d)\n", displacement, reg);
    return 0;
}


/* clr.b (xxx.w) */
static uint32_t m68k_4238_clr_b_aw (M68000_Context *context, uint16_t instruction)
{
    write_byte_aw (context, 0x00);
    m68k_clr_flags (context);

    printf ("clr.b (xxx.w)\n");
    return 0;
}


/* clr.w d(An) */
static uint32_t m68k_4268_clr_w_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    write_word (context, context->state.a [reg] + displacement, 0x00);
    m68k_clr_flags (context);

    printf ("clr.w %04x(a%d)\n", displacement, reg);
    return 0;
}


/* clr.w (xxx.w) */
static uint32_t m68k_4278_clr_w_aw (M68000_Context *context, uint16_t instruction)
{
    write_word_aw (context, 0x0000);
    m68k_clr_flags (context);

    printf ("clr.w (xxx.w)\n");
    return 0;
}


/* clr.l (An)+ */
static uint32_t m68k_4298_clr_l_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    write_long (context, context->state.a [reg], 0x00000000);
    context->state.a [reg] += 4;

    m68k_clr_flags (context);

    printf ("clr.l (a%d)+\n", reg);
    return 0;
}


/* clr.l (xxx.w) */
static uint32_t m68k_42b8_clr_l_aw (M68000_Context *context, uint16_t instruction)
{
    write_long_aw (context, 0x00000000);
    m68k_clr_flags (context);

    printf ("clr.l (xxx.w)\n");
    return 0;
}


/* neg.b Dn */
static uint32_t m68k_4400_neg_b_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t value = context->state.d [reg].b;
    uint8_t result = 0 - value;

    context->state.d [reg].b = result;

    context->state.ccr_negative = ((int8_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = ((int8_t) value == -128);
    context->state.ccr_carry = (result != 0);
    context->state.ccr_extend = (result != 0);

    printf ("neg.b d%d\n", reg);
    return 0;
}


/* neg.b (An) */
static uint32_t m68k_4410_neg_b_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t value = read_byte (context, context->state.a [reg]);
    uint8_t result = 0 - value;

    write_byte (context, context->state.a [reg], result);

    context->state.ccr_negative = ((int8_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = ((int8_t) value == -128);
    context->state.ccr_carry = (result != 0);
    context->state.ccr_extend = (result != 0);

    printf ("neg.b (a%d)\n", reg);
    return 0;
}


/* neg.b d(An) */
static uint32_t m68k_4428_neg_b_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    uint8_t value = read_byte (context, context->state.a [reg] + displacement);
    uint8_t result = 0 - value;

    write_byte (context, context->state.a [reg] + displacement, result);

    context->state.ccr_negative = ((int8_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = ((int8_t) value == -128);
    context->state.ccr_carry = (result != 0);
    context->state.ccr_extend = (result != 0);

    printf ("neg.b %04x(a%d)\n", displacement, reg);
    return 0;
}


/* neg.w Dn */
static uint32_t m68k_4440_neg_w_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t value = context->state.d [reg].w;
    uint16_t result = 0 - value;

    context->state.d [reg].w = result;

    context->state.ccr_negative = ((int16_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = ((int16_t) value == -32768);
    context->state.ccr_carry = (result != 0);
    context->state.ccr_extend = (result != 0);

    printf ("neg.w d%d\n", reg);
    return 0;
}


/* move ccr ← Dn */
static uint32_t m68k_44c0_move_ccr_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t value = context->state.d [reg].w;

    context->state.ccr = value;

    printf ("move ccr ← d%d\n", reg);
    return 0;
}


/* not.b Dn */
static uint32_t m68k_4600_not_b_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint8_t value = context->state.d [reg].b;

    uint8_t result = ~value;
    context->state.d [reg].b = result;
    m68k_move_b_flags (context, result);

    printf ("not.b d%d\n", reg);
    return 0;
}


/* not.w Dn */
static uint32_t m68k_4640_not_w_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t value = context->state.d [reg].w;

    uint16_t result = ~value;
    context->state.d [reg].w = result;
    m68k_move_w_flags (context, result);

    printf ("not.w d%d\n", reg);
    return 0;
}


/* not.l Dn */
static uint32_t m68k_4680_not_l_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t value = context->state.d [reg].l;

    uint32_t result = ~value;
    context->state.d [reg].l = result;
    m68k_move_l_flags (context, result);

    printf ("not.l d%d\n", reg);
    return 0;
}


/* move sr ← #xxxx */
/* TODO: Privileged instruction. */
static uint32_t m68k_46fc_move_sr_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t value = read_extension (context);

    context->state.sr = value & SR_MASK;

    if (!context->state.sr_supervisor)
    {
        snepulator_error ("M68000 Error", "User-mode not implemented");
    }

    printf ("move sr ← #%04x\n", value);
    return 0;
}


/* swap Dn */
static uint32_t m68k_4840_swap_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t value = context->state.d [reg].l;
    uint32_t result = (value << 16) | (value >> 16);
    context->state.d [reg].l = result;

    context->state.ccr_negative = ((int32_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;

    printf ("swap d%d\n", reg);
    return 0;
}


/* ext.w Dn */
static uint32_t m68k_4880_ext_w_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    int8_t value = context->state.d [reg].b;
    context->state.d [reg].w = value;

    context->state.ccr_negative = (value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;

    printf ("ext.w d%d\n", reg);
    return 0;
}


/* ext.l Dn */
static uint32_t m68k_48c0_ext_l_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    int16_t value = context->state.d [reg].w;
    context->state.d [reg].l = value;

    context->state.ccr_negative = (value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;

    printf ("ext.l d%d\n", reg);
    return 0;
}


/* movem.l (An) ← <registers> */
static uint32_t m68k_48d0_movem_l_an_regs (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = instruction & 0x07;
    uint16_t mask = read_extension (context);
    uint32_t address = context->state.a [dest_reg];

    for (uint32_t i = 0; i < 16; i++)
    {
        if (mask & (1 << i))
        {
            if (i < 8)
            {
                write_long (context, address, context->state.d [i].l);
                address += 4;
            }
            else
            {
                write_long (context, address, context->state.a [i - 8]);
                address += 4;
            }
        }
    }

    printf ("movem.l (a%d) ← <registers>\n", dest_reg);
    return 0;
}


/* movem.l -(An) ← <registers> */
static uint32_t m68k_48e0_movem_l_pan_regs (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t mask = read_extension (context);

    /* For -(An), the bit-mask and order is reversed */
    for (int32_t i = 0; i < 16; i++)
    {
        if (mask & (1 << i))
        {
            if (i < 8)
            {
                context->state.a [reg] -= 4;
                write_long (context, context->state.a [reg], context->state.a [7 - i]);
            }
            else
            {
                context->state.a [reg] -= 4;
                write_long (context, context->state.a [reg], context->state.d [15 - i].l);
            }
        }
    }

    printf ("movem.l -(a%d) ← <registers>\n", reg);
    return 0;
}


/* movem.l (xxx.w) ← <registers> */
static uint32_t m68k_48f8_movem_l_aw_regs (M68000_Context *context, uint16_t instruction)
{
    uint16_t mask = read_extension (context);
    uint32_t address = (int16_t) read_extension (context);

    for (uint32_t i = 0; i < 16; i++)
    {
        if (mask & (1 << i))
        {
            if (i < 8)
            {
                write_long (context, address, context->state.d [i].l);
                address += 4;
            }
            else
            {
                write_long (context, address, context->state.a [i - 8]);
                address += 4;
            }
        }
    }

    printf ("movem.l (xxx.w) ← <registers>\n");
    return 0;
}


/* tst.b Dn */
static uint32_t m68k_4a00_tst_b_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint8_t value = context->state.d [reg].b;

    m68k_tst_b_flags (context, value);

    printf ("tst.b d%d\n", reg);
    return 0;
}


/* tst.b (An) */
static uint32_t m68k_4a10_tst_b_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint8_t value = read_byte (context, context->state.a [reg]);

    m68k_tst_b_flags (context, value);

    printf ("tst.b (a%d)\n", reg);
    return 0;
}


/* tst.b d(An) */
static uint32_t m68k_4a28_tst_b_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    int16_t displacement = read_extension (context);
    uint8_t value = read_byte (context, context->state.a [reg] + displacement);

    m68k_tst_b_flags (context, value);

    printf ("tst.b %04x(a%d)\n", displacement, reg);
    return 0;
}


/* tst.b d(An+Xi) */
static uint32_t m68k_4a30_tst_b_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint8_t value = read_byte_with_index (context, context->state.a [reg]);

    m68k_tst_b_flags (context, value);

    printf ("tst.b d(a%d+Xi)\n", reg);
    return 0;
}


/* tst.b (xxx.w) */
static uint32_t m68k_4a38_tst_b_aw (M68000_Context *context, uint16_t instruction)
{
    uint8_t value = read_byte_aw (context);

    m68k_tst_b_flags (context, value);

    printf ("tst.b (xxx.w)\n");
    return 0;
}


/* tst.w Dn */
static uint32_t m68k_4a40_tst_w_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t value = context->state.d [reg].w;

    m68k_tst_w_flags (context, value);

    printf ("tst.w d%d\n", reg);
    return 0;
}


/* tst.w (An) */
static uint32_t m68k_4a50_tst_w_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t value = read_word (context, context->state.a [reg]);

    m68k_tst_w_flags (context, value);

    printf ("tst.w (a%d)\n", reg);
    return 0;
}


/* tst.w d(An) */
static uint32_t m68k_4a68_tst_w_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    int16_t displacement = read_extension (context);
    uint16_t value = read_word (context, context->state.a [reg] + displacement);

    m68k_tst_w_flags (context, value);

    printf ("tst.w %04x(a%d)\n", displacement, reg);
    return 0;
}


/* tst.w (xxx.w) */
static uint32_t m68k_4a78_tst_w_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t value = read_word_aw (context);

    m68k_tst_w_flags (context, value);

    printf ("tst.w (xxx.w)\n");
    return 0;
}


/* tst.w (xxx.l) */
static uint32_t m68k_4a79_tst_w_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t value = read_word_al (context);

    m68k_tst_w_flags (context, value);

    printf ("tst.w (xxx.l)\n");
    return 0;
}


/* tst.l (An) */
static uint32_t m68k_4a90_tst_l_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t value = read_long (context, context->state.a [reg]);

    m68k_tst_l_flags (context, value);

    printf ("tst.l (a%d)\n", reg);
    return 0;
}


/* tst.l (xxx.w) */
static uint32_t m68k_4ab8_tst_l_aw (M68000_Context *context, uint16_t instruction)
{
    uint32_t value = read_long_aw (context);

    m68k_tst_l_flags (context, value);

    printf ("tst.l (xxx.w)\n");
    return 0;
}


/* tst.l (xxx.l) */
static uint32_t m68k_4ab9_tst_l_al (M68000_Context *context, uint16_t instruction)
{
    uint32_t value = read_long_al (context);

    m68k_tst_l_flags (context, value);

    printf ("tst.l (xxx.l)\n");
    return 0;
}


/* movem.w (An)+ ← <registers> */
static uint32_t m68k_4c98_movem_w_regs_anp (M68000_Context *context, uint16_t instruction)
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

    printf ("movem.w <registers> ← (a%d)+\n", reg);
    return 0;
}


/* movem.l <registers> ← (An) */
static uint32_t m68k_4cd0_movem_l_regs_an (M68000_Context *context, uint16_t instruction)
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
static uint32_t m68k_4cd8_movem_l_regs_anp (M68000_Context *context, uint16_t instruction)
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


/* movem.l <registers> ← (xxx.w) */
static uint32_t m68k_4cf8_movem_l_regs_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t mask = read_extension (context);
    uint32_t address = (int16_t) read_extension (context);

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

    printf ("movem.l <registers> ← (xxx.w)\n");
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


/* rte */
static uint32_t m68k_4e73_rte (M68000_Context *context, uint16_t instruction)
{
    context->state.sr = read_word (context, context->state.a [7]) & SR_MASK;
    context->state.a[7] += 2;

    context->state.pc = read_long (context, context->state.a [7]);
    context->state.a[7] += 4;

    printf ("rte\n");
    return 0;
}


/* rts */
static uint32_t m68k_4e75_rts (M68000_Context *context, uint16_t instruction)
{
    context->state.pc = read_long (context, context->state.a [7]);
    context->state.a[7] += 4;

    printf ("rts\n");
    return 0;
}


/* jsr (An) */
static uint32_t m68k_4e90_jsr_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    /* Push the current PC to the stack */
    context->state.a [7] -= 4;
    write_long (context, context->state.a [7], context->state.pc);

    /* Update the PC */
    context->state.pc = context->state.a [reg];

    printf ("jsr (a%d)\n", reg);
    return 0;
}


/* jsr (xxx.l) */
static uint32_t m68k_4eb9_jsr_al (M68000_Context *context, uint16_t instruction)
{
    uint32_t addr = read_extension_long (context);

    /* Push the current PC to the stack */
    context->state.a [7] -= 4;
    write_long (context, context->state.a [7], context->state.pc);

    /* Update the PC */
    context->state.pc = addr;

    printf ("jsr (xxx.l)\n");
    return 0;
}


/* jsr d(PC) */
static uint32_t m68k_4eba_jsr_dpc (M68000_Context *context, uint16_t instruction)
{
    int16_t displacement = read_extension (context);

    /* Push the current PC to the stack */
    context->state.a [7] -= 4;
    write_long (context, context->state.a [7], context->state.pc);

    /* Update the PC, subtract 2, the jump is from the location of the extension. */
    context->state.pc += displacement - 2;

    printf ("jsr %+d(pc)\n", displacement);
    return 0;
}


/* jsr d(PC, Xi) */
static uint32_t m68k_4ebb_jsr_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t extension = read_extension (context);
    int8_t displacement = extension & 0xff;

    uint16_t reg = (extension >> 12) & 0x07;
    uint32_t index;

    /* Bit 15 selects Dn or An */
    if ((extension & BIT_15) == 0)
    {
        index = context->state.d [reg].l;
    }
    else
    {
        index = context->state.a [reg];
    }

    /* Bit 11 selects index size, sign-extended word, or long */
    if ((extension & BIT_11) == 0)
    {
        int16_t index_w = index & 0xffff;
        index = index_w;
    }

    /* Push the current PC to the stack */
    context->state.a [7] -= 4;
    write_long (context, context->state.a [7], context->state.pc);

    /* Update the PC, subtract 2, the jump is from the location of the extension. */
    context->state.pc += displacement + index - 2;

    printf ("jsr d(PC+Xi)\n");
    return 0;
}


/* jmp (An) */
static uint32_t m68k_4ed0_jmp_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    context->state.pc = context->state.a [reg];

    printf ("jmp (a%d)\n", reg);
    return 0;
}


/* jmp (xxx.l) */
static uint32_t m68k_4ef9_jmp_al (M68000_Context *context, uint16_t instruction)
{
    uint32_t addr = read_extension_long (context);

    context->state.pc = addr;

    printf ("jmp (xxx.l)\n");
    return 0;
}


/* jmp d(PC+Xi) */
static uint32_t m68k_4efb_jmp_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t extension = read_extension (context);
    int8_t displacement = extension & 0xff;

    uint16_t reg = (extension >> 12) & 0x07;
    uint32_t index;

    /* Bit 15 selects Dn or An */
    if ((extension & BIT_15) == 0)
    {
        index = context->state.d [reg].l;
    }
    else
    {
        index = context->state.a [reg];
    }

    /* Bit 11 selects index size, sign-extended word, or long */
    if ((extension & BIT_11) == 0)
    {
        int16_t index_w = index & 0xffff;
        index = index_w;
    }

    /* Update the PC, subtract 2, the jump is from the location of the extension. */
    context->state.pc += displacement + index - 2;

    printf ("jmp d(PC+Xi)\n");
    return 0;
}


/* addq.b Dn, #xx */
static uint32_t m68k_5000_addq_b_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint8_t a = context->state.d [reg].b;

    uint8_t result = a + b;
    context->state.d [reg].b = result;
    m68k_add_b_flags (context, a, b, result);

    printf ("addq.b d%d, #%x\n", reg, b);
    return 0;
}


/* addq.b (An), #xx */
static uint32_t m68k_5010_addq_b_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint8_t a = read_byte (context, context->state.a [reg]);

    uint8_t result = a + b;
    write_byte (context, context->state.a [reg], result);
    m68k_add_b_flags (context, a, b, result);

    printf ("addq.b (a%d), #%x\n", reg, b);
    return 0;
}


/* addq.b -(An), #xx */
static uint32_t m68k_5020_addq_b_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    context->state.a [reg] -= (reg == 7) ? 2 : 1;
    uint8_t a = read_byte (context, context->state.a [reg]);

    uint8_t result = a + b;
    write_byte (context, context->state.a [reg], result);
    m68k_add_b_flags (context, a, b, result);

    printf ("addq.b -(a%d), #%x\n", reg, b);
    return 0;
}


/* addq.b d(An), #xx */
static uint32_t m68k_5028_addq_b_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    int16_t displacement = read_extension (context);
    uint32_t address = context->state.a [reg] + displacement;

    uint8_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint8_t a = read_byte (context, address);

    uint8_t result = a + b;
    write_byte (context, address, result);
    m68k_add_b_flags (context, a, b, result);

    printf ("addq.b %04x(a%d), #%x\n", displacement, reg, b);
    return 0;
}


/* addq.b (xxx.w), #xx */
static uint32_t m68k_5038_addq_b_aw (M68000_Context *context, uint16_t instruction)
{
    int16_t addr = read_extension (context);

    uint8_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint8_t a = read_byte (context, (int32_t) addr);

    uint8_t result = a + b;
    write_byte (context, (int32_t) addr, result);
    m68k_add_b_flags (context, a, b, result);

    printf ("addq.b (xxx.w), #%x\n", b);
    return 0;
}


/* addq.w Dn, #xx */
static uint32_t m68k_5040_addq_w_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t a = context->state.d [reg].w;

    uint16_t result = a + b;
    context->state.d [reg].w = result;
    m68k_add_w_flags (context, a, b, result);

    printf ("addq.w d%d, #%x\n", reg, b);
    return 0;
}


/* addq.w An, #xx */
static uint32_t m68k_5048_addq_w_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint32_t a = context->state.a [reg];

    uint32_t result = a + b;
    context->state.a [reg] = result;

    printf ("addq.w a%d, #%x\n", reg, b);
    return 0;
}


/* addq.w (An), #xx */
static uint32_t m68k_5050_addq_w_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t a = read_word (context, context->state.a [reg]);

    uint16_t result = a + b;
    write_word (context, context->state.a [reg], result);
    m68k_add_w_flags (context, a, b, result);

    printf ("addq.w (a%d), #%x\n", reg, b);
    return 0;
}


/* addq.w (An)+, #xx */
static uint32_t m68k_5058_addq_w_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t a = read_word (context, context->state.a [reg]);

    uint16_t result = a + b;
    write_word (context, context->state.a [reg], result);
    context->state.a [reg] += 2;
    m68k_add_w_flags (context, a, b, result);

    printf ("addq.w (a%d)+, #%x\n", reg, b);
    return 0;
}


/* addq.w d(An), #xx */
static uint32_t m68k_5068_addq_w_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    int16_t displacement = read_extension (context);
    uint32_t address = context->state.a [reg] + displacement;

    uint16_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t a = read_word (context, address);

    uint16_t result = a + b;
    write_word (context, address, result);
    m68k_add_w_flags (context, a, b, result);

    printf ("addq.w %04x(a%d), #%x\n", displacement, reg, b);
    return 0;
}


/* addq.w (xxx.w), #xx */
static uint32_t m68k_5078_addq_w_aw (M68000_Context *context, uint16_t instruction)
{
    int16_t addr = read_extension (context);

    uint16_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t a = read_word (context, (int32_t) addr);

    uint16_t result = a + b;
    write_word (context, (int32_t) addr, result);
    m68k_add_w_flags (context, a, b, result);

    printf ("addq.w (xxx.w), #%x\n", b);
    return 0;
}


/* addq.l An, #xx */
static uint32_t m68k_5088_addq_l_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint32_t a = context->state.a [reg];

    uint32_t result = a + b;
    context->state.a [reg] = result;

    printf ("addq.l a%d, #%x\n", reg, b);
    return 0;
}


/* addq.l (xxx.w), #xx */
static uint32_t m68k_50b8_addq_l_aw (M68000_Context *context, uint16_t instruction)
{
    int16_t addr = read_extension (context);

    uint32_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint32_t a = read_long (context, (int32_t) addr);

    uint32_t result = a + b;
    write_long (context, (int32_t) addr, result);
    m68k_add_l_flags (context, a, b, result);

    printf ("addq.l (xxx.w), #%x\n", b);
    return 0;
}


/* subq.b Dn, #xx */
static uint32_t m68k_5100_subq_b_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint8_t a = context->state.d [reg].b;

    uint8_t result = a - b;
    context->state.d [reg].b = result;
    m68k_sub_b_flags (context, a, b, result);

    printf ("subq.b d%d, #%x\n", reg, b);
    return 0;
}


/* subq.b d(An), #xx */
static uint32_t m68k_5128_subq_b_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    int16_t displacement = read_extension (context);
    uint32_t address = context->state.a [reg] + displacement;

    uint8_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint8_t a = read_byte (context, address);

    uint8_t result = a - b;
    write_byte (context, address, result);
    m68k_sub_b_flags (context, a, b, result);

    printf ("subq.b %04x(a%d), #%x\n", displacement, reg, b);
    return 0;
}


/* subq.b d(An+Xi), #xx */
static uint32_t m68k_5130_subq_b_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t address = address_with_index (context, context->state.a [reg]);

    uint8_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint8_t a = read_byte (context, address);

    uint8_t result = a - b;
    write_byte (context, address, result);
    m68k_sub_b_flags (context, a, b, result);

    printf ("subq.b d(a%d+Xi), #%x\n", reg, b);

    return 0;
}


/* subq.b (xxx.w), #xx */
static uint32_t m68k_5138_subq_b_aw (M68000_Context *context, uint16_t instruction)
{
    int16_t addr = read_extension (context);

    uint8_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint8_t a = read_byte (context, (int32_t) addr);

    uint8_t result = a - b;
    write_byte (context, (int32_t) addr, result);
    m68k_sub_b_flags (context, a, b, result);
    printf ("subq.b (xxx.w), #%x\n", b);
    return 0;
}


/* subq.w Dn, #xx */
static uint32_t m68k_5140_subq_w_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t a = context->state.d [reg].w;

    uint16_t result = a - b;
    context->state.d [reg].w = result;
    m68k_sub_w_flags (context, a, b, result);

    printf ("subq.w d%d, %d\n", reg, b);
    return 0;
}


/* subq.w An, #xx */
static uint32_t m68k_5148_subq_w_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint32_t a = context->state.a [reg];

    uint32_t result = a - b;
    context->state.a [reg] = result;

    printf ("subq.w a%d, %d\n", reg, b);
    return 0;
}


/* subq.w (An), #xx */
static uint32_t m68k_5150_subq_w_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t a = read_word (context, context->state.a [reg]);

    uint16_t result = a - b;
    write_word (context, context->state.a [reg], result);
    m68k_sub_w_flags (context, a, b, result);

    printf ("subq.w (a%d), %d\n", reg, b);
    return 0;
}


/* subq.w (An)+, #xx */
static uint32_t m68k_5158_subq_w_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t a = read_word (context, context->state.a [reg]);

    uint16_t result = a - b;
    write_word (context, context->state.a [reg], result);
    context->state.a [reg] += 2;
    m68k_sub_w_flags (context, a, b, result);

    printf ("subq.w (a%d)+, %d\n", reg, b);
    return 0;
}


/* subq.w d(An), #xx */
static uint32_t m68k_5168_subq_w_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    int16_t displacement = read_extension (context);
    uint32_t address = context->state.a [reg] + displacement;

    uint16_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t a = read_word (context, address);

    uint16_t result = a - b;
    write_word (context, address, result);
    m68k_sub_w_flags (context, a, b, result);

    printf ("subq.w %04x(a%d), #%x\n", displacement, reg, b);
    return 0;
}


/* subq.w (xxx.w), #xx */
static uint32_t m68k_5178_subq_w_aw (M68000_Context *context, uint16_t instruction)
{
    int16_t addr = read_extension (context);

    uint16_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t a = read_word (context, (int32_t) addr);

    uint16_t result = a - b;
    write_word (context, (int32_t) addr, result);
    m68k_sub_w_flags (context, a, b, result);

    printf ("subq.w (xxx.w), #%x\n", b);
    return 0;
}


/* subq.l An, #xx */
static uint32_t m68k_5188_subq_l_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint32_t a = context->state.a [reg];

    uint32_t result = a - b;
    context->state.a [reg] = result;

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
    context->state.pc += displacement - 2;
    printf ("bra.w %+d\n", displacement);
    return 0;
}


/* bra.s */
static uint32_t m68k_6001_bra_s (M68000_Context *context, uint16_t instruction)
{
    int8_t displacement = instruction & 0xff;
    context->state.pc += displacement;
    printf ("bra.s %+d\n", displacement);
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


/* bhi.w */
static uint32_t m68k_6200_bhi_w (M68000_Context *context, uint16_t instruction)
{
    int16_t displacement = read_extension (context);

    if (!context->state.ccr_carry && !context->state.ccr_zero)
    {
        context->state.pc += displacement - 2;
        printf ("bhi.w %+d\n", displacement);
    }
    else
    {
        printf ("bhi.w (not taken).\n");
    }

    return 0;
}


/* bhi.s */
static uint32_t m68k_6201_bhi_s (M68000_Context *context, uint16_t instruction)
{
    if (!context->state.ccr_carry && !context->state.ccr_zero)
    {
        int8_t displacement = instruction & 0xff;
        context->state.pc += displacement;
        printf ("bhi.s %+d\n", displacement);
    }
    else
    {
        printf ("bhi.s (not taken).\n");
    }

    return 0;
}


/* bls.w */
static uint32_t m68k_6300_bcc_w (M68000_Context *context, uint16_t instruction)
{
    int16_t displacement = read_extension (context);

    if (context->state.ccr_carry || context->state.ccr_zero)
    {
        context->state.pc += displacement - 2;
        printf ("bls.w %+d\n", displacement);
    }
    else
    {
        printf ("bls.w (not taken).\n");
    }

    return 0;
}


/* bls.s */
static uint32_t m68k_6301_bcc_s (M68000_Context *context, uint16_t instruction)
{
    if (context->state.ccr_carry || context->state.ccr_zero)
    {
        int8_t displacement = instruction & 0xff;
        context->state.pc += displacement;
        printf ("bls.s %+d\n", displacement);
    }
    else
    {
        printf ("bls.s (not taken).\n");
    }

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


/* bcs.w / blo.w */
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


/* bcs.s / blo.w */
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


/* bpl.w */
static uint32_t m68k_6a00_bpl_w (M68000_Context *context, uint16_t instruction)
{
    int16_t displacement = read_extension (context);

    if (!context->state.ccr_negative)
    {
        context->state.pc += displacement - 2;
        printf ("bpl.w %+d\n", displacement);
    }
    else
    {
        printf ("bpl.w (not taken).\n");
    }

    return 0;
}


/* bpl.s */
static uint32_t m68k_6a01_bpl_s (M68000_Context *context, uint16_t instruction)
{
    if (!context->state.ccr_negative)
    {
        int8_t displacement = instruction & 0xff;
        context->state.pc += displacement;
        printf ("bpl.s %+d\n", displacement);
    }
    else
    {
        printf ("bpl.s (not taken).\n");
    }

    return 0;
}


/* bmi.w */
static uint32_t m68k_6b00_bmi_w (M68000_Context *context, uint16_t instruction)
{
    int16_t displacement = read_extension (context);

    if (context->state.ccr_negative)
    {
        context->state.pc += displacement - 2;
        printf ("bmi.w %+d\n", displacement);
    }
    else
    {
        printf ("bmi.w (not taken).\n");
    }

    return 0;
}


/* bmi.s */
static uint32_t m68k_6b01_bmi_s (M68000_Context *context, uint16_t instruction)
{
    if (context->state.ccr_negative)
    {
        int8_t displacement = instruction & 0xff;
        context->state.pc += displacement;
        printf ("bmi.s %+d\n", displacement);
    }
    else
    {
        printf ("bmi.s (not taken).\n");
    }

    return 0;
}


/* bge.w */
static uint32_t m68k_6c00_bge_w (M68000_Context *context, uint16_t instruction)
{
    int16_t displacement = read_extension (context);

    if ((context->state.ccr_negative && context->state.ccr_overflow) ||
        (!context->state.ccr_negative && !context->state.ccr_overflow))
    {
        context->state.pc += displacement - 2;
        printf ("bge.w %+d\n", displacement);
    }
    else
    {
        printf ("bge.w (not taken).\n");
    }

    return 0;
}


/* bge.s */
static uint32_t m68k_6c01_bge_s (M68000_Context *context, uint16_t instruction)
{
    if ((context->state.ccr_negative && context->state.ccr_overflow) ||
        (!context->state.ccr_negative && !context->state.ccr_overflow))
    {
        int8_t displacement = instruction & 0xff;
        context->state.pc += displacement;
        printf ("bge.s %+d\n", displacement);
    }
    else
    {
        printf ("bge.s (not taken).\n");
    }

    return 0;
}


/* blt.w */
static uint32_t m68k_6d00_blt_w (M68000_Context *context, uint16_t instruction)
{
    int16_t displacement = read_extension (context);

    if ((context->state.ccr_negative && !context->state.ccr_overflow) ||
        (!context->state.ccr_negative && context->state.ccr_overflow))
    {
        context->state.pc += displacement - 2;
        printf ("blt.w %+d\n", displacement);
    }
    else
    {
        printf ("blt.w (not taken).\n");
    }

    return 0;
}


/* blt.s */
static uint32_t m68k_6d01_blt_s (M68000_Context *context, uint16_t instruction)
{
    if ((context->state.ccr_negative && !context->state.ccr_overflow) ||
        (!context->state.ccr_negative && context->state.ccr_overflow))
    {
        int8_t displacement = instruction & 0xff;
        context->state.pc += displacement;
        printf ("blt.s %+d\n", displacement);
    }
    else
    {
        printf ("blt.s (not taken).\n");
    }

    return 0;
}


/* bgt.w */
static uint32_t m68k_6e00_bgt_w (M68000_Context *context, uint16_t instruction)
{
    int16_t displacement = read_extension (context);

    if ((context->state.ccr_negative && context->state.ccr_overflow && !context->state.ccr_zero) ||
        (!context->state.ccr_negative && !context->state.ccr_overflow && !context->state.ccr_zero))
    {
        context->state.pc += displacement - 2;
        printf ("bgt.w %+d\n", displacement);
    }
    else
    {
        printf ("bgt.w (not taken).\n");
    }

    return 0;
}


/* bgt.s */
static uint32_t m68k_6e01_bgt_s (M68000_Context *context, uint16_t instruction)
{
    if ((context->state.ccr_negative && context->state.ccr_overflow && !context->state.ccr_zero) ||
        (!context->state.ccr_negative && !context->state.ccr_overflow && !context->state.ccr_zero))
    {
        int8_t displacement = instruction & 0xff;
        context->state.pc += displacement;
        printf ("bgt.s %+d\n", displacement);
    }
    else
    {
        printf ("bgt.s (not taken).\n");
    }

    return 0;
}


/* ble.w */
static uint32_t m68k_6f00_ble_w (M68000_Context *context, uint16_t instruction)
{
    int16_t displacement = read_extension (context);

    if (context->state.ccr_zero ||
        (context->state.ccr_negative && !context->state.ccr_overflow) ||
        (!context->state.ccr_negative && context->state.ccr_overflow))
    {
        context->state.pc += displacement - 2;
        printf ("ble.w %+d\n", displacement);
    }
    else
    {
        printf ("ble.w (not taken).\n");
    }

    return 0;
}


/* ble.s */
static uint32_t m68k_6f01_ble_s (M68000_Context *context, uint16_t instruction)
{
    if (context->state.ccr_zero ||
        (context->state.ccr_negative && !context->state.ccr_overflow) ||
        (!context->state.ccr_negative && context->state.ccr_overflow))
    {
        int8_t displacement = instruction & 0xff;
        context->state.pc += displacement;
        printf ("ble.s %+d\n", displacement);
    }
    else
    {
        printf ("ble.s (not taken).\n");
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


/* or.b Dn ← Dn | Dn */
static uint32_t m68k_8000_or_b_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a | b;

    context->state.d [dest_reg].b = result;
    m68k_move_b_flags (context, result);

    printf ("or.b d%d ← d%d\n", dest_reg, source_reg);
    return 0;
}


/* or.b Dn ← Dn | d(An) */
static uint32_t m68k_8028_or_b_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    uint8_t b = read_byte (context, context->state.a [source_reg] + displacement);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a | b;

    context->state.d [dest_reg].b = result;
    m68k_move_b_flags (context, result);

    printf ("or.b d%d ← %04x(a%d)\n", dest_reg, displacement, source_reg);
    return 0;
}


/* or.w Dn ← Dn | Dn */
static uint32_t m68k_8040_or_w_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t b = context->state.d [source_reg].w;
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a | b;

    context->state.d [dest_reg].w = result;
    m68k_move_w_flags (context, result);

    printf ("or.w d%d ← d%d\n", dest_reg, source_reg);
    return 0;
}


/* divu.w Dn ← Dn ÷ Dn */
static uint32_t m68k_80c0_divu_w_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t dividend = context->state.d [dest_reg].l;
    uint16_t divisor = context->state.d [source_reg].w;

    if (divisor == 0)
    {
        printf ("[%s] Division by zero - Trap not implemented.\n", __func__);
        return 0;
    }

    int32_t quotient = dividend / divisor;
    uint16_t remainder = dividend % divisor;

    context->state.ccr_negative = (quotient < 0);
    context->state.ccr_zero = (quotient == 0);
    context->state.ccr_overflow = quotient > 32767 || quotient < -32768;
    context->state.ccr_carry = 0;

    if (!context->state.ccr_overflow)
    {
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

    printf ("divs.w d%d ← d%d ÷ d%d\n", dest_reg, dest_reg, source_reg);
    return 0;
}


/* or.b d(An) ← d(An) | Dn */
static uint32_t m68k_8128_or_b_dan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint16_t dest_reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = read_byte (context, context->state.a [dest_reg] + displacement);
    uint8_t result = a | b;

    write_byte (context, context->state.a [dest_reg] + displacement, result);
    m68k_move_b_flags (context, result);

    printf ("or.b %04x(a%d) ← d%d\n", displacement, dest_reg, source_reg);
    return 0;
}


/* divu.w Dn ← Dn ÷ #xxxx */
static uint32_t m68k_81fc_divs_w_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    int32_t dividend = context->state.d [dest_reg].l;
    int16_t divisor = read_extension (context);

    if (divisor == 0)
    {
        printf ("[%s] Division by zero - Trap not implemented.\n", __func__);
        return 0;
    }

    int32_t quotient = dividend / divisor;
    int16_t remainder = dividend % divisor;

    context->state.ccr_negative = (quotient < 0);
    context->state.ccr_zero = (quotient == 0);
    context->state.ccr_overflow = quotient > 32767 || quotient < -32768;
    context->state.ccr_carry = 0;

    if (!context->state.ccr_overflow)
    {
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

    printf ("divs.w d%d ← d%d ÷ %d\n", dest_reg, dest_reg, divisor);
    return 0;
}


/* sub.b Dn ← Dn - Dn */
static uint32_t m68k_9000_sub_b_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    context->state.d [dest_reg].b = result;
    m68k_sub_b_flags (context, a, b, result);

    printf ("sub.b d%d ← d%d - d%d\n", dest_reg, dest_reg, source_reg);
    return 0;
}


/* sub.b Dn ← Dn - d(PC+Xi) */
static uint32_t m68k_903b_sub_b_dn_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t b = read_byte_with_index (context, context->state.pc);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    context->state.d [dest_reg].b = result;
    m68k_sub_b_flags (context, a, b, result);

    printf ("sub.b d%d ← d%d - d(PC+Xi)\n", dest_reg, dest_reg);
    return 0;
}


/* sub.w Dn ← Dn - Dn */
static uint32_t m68k_9040_sub_w_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t b = context->state.d [source_reg].w;
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a - b;

    context->state.d [dest_reg].w = result;
    m68k_sub_w_flags (context, a, b, result);

    printf ("sub.w d%d ← d%d - d%d\n", dest_reg, dest_reg, source_reg);
    return 0;
}


/* sub.w Dn ← Dn - An */
static uint32_t m68k_9048_sub_w_dn_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t b = context->state.a [source_reg];
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a - b;

    context->state.d [dest_reg].w = result;
    m68k_sub_w_flags (context, a, b, result);

    printf ("sub.w d%d ← d%d - a%d\n", dest_reg, dest_reg, source_reg);
    return 0;
}


/* sub.w Dn ← Dn - (An) */
static uint32_t m68k_9050_sub_w_dn_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t b = read_word (context, context->state.a [source_reg]);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a - b;

    context->state.d [dest_reg].w = result;
    m68k_sub_w_flags (context, a, b, result);

    printf ("sub.w d%d ← d%d - (a%d)\n", dest_reg, dest_reg, source_reg);
    return 0;
}


/* sub.w Dn ← Dn - d(An) */
static uint32_t m68k_9068_sub_w_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    uint16_t b = read_word (context, context->state.a [source_reg] + displacement);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a - b;

    context->state.d [dest_reg].w = result;
    m68k_sub_w_flags (context, a, b, result);

    printf ("sub.w d%d ← d%d - %04x(a%d)\n", dest_reg, dest_reg, displacement, source_reg);
    return 0;
}


/* sub.w Dn ← Dn - (xxx.w) */
static uint32_t m68k_9078_sub_w_dn_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t b = read_word_aw (context);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a - b;

    context->state.d [dest_reg].w = result;
    m68k_sub_w_flags (context, a, b, result);

    printf ("sub.w d%d ← d%d - (xxx.w)\n", dest_reg, dest_reg);
    return 0;
}


/* sub.l Dn ← Dn - Dn */
static uint32_t m68k_9080_sub_l_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t b = context->state.d [source_reg].l;
    uint32_t a = context->state.d [dest_reg].l;
    uint32_t result = a - b;

    context->state.d [dest_reg].l = result;
    m68k_sub_l_flags (context, a, b, result);

    printf ("sub.l d%d ← d%d - d%d\n", dest_reg, dest_reg, source_reg);
    return 0;
}


/* sub.l Dn ← Dn - (xxx.w) */
static uint32_t m68k_90b8_sub_l_dn_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_long_aw (context);
    uint32_t a = context->state.d [dest_reg].l;
    uint32_t result = a - b;

    context->state.d [dest_reg].l = result;
    m68k_sub_l_flags (context, a, b, result);

    printf ("sub.l d%d ← d%d - (xxx.w)\n", dest_reg, dest_reg);
    return 0;
}


/* sub.b d(An) ← d(An) - Dn */
static uint32_t m68k_9128_sub_b_dan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = instruction & 0x07;
    uint16_t source_reg = (instruction >> 9) & 0x07;
    int16_t displacement = read_extension (context);

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = read_byte (context, context->state.a [dest_reg] + displacement);
    uint8_t result = a - b;

    write_byte (context, context->state.a [dest_reg] + displacement, result);
    m68k_sub_b_flags (context, a, b, result);

    printf ("sub.b d(a%d) ← d(a%d) - d%d\n", dest_reg, dest_reg, source_reg);
    return 0;
}


/* cmp.b Dn - Dn */
static uint32_t m68k_b000_cmp_b_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

    printf ("cmp.b d%d - d%d\n", dest_reg, source_reg);
    return 0;
}


/* cmp.b Dn - (An) */
static uint32_t m68k_b010_cmp_b_dn_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t b = read_byte (context, context->state.a [source_reg]);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

    printf ("cmp.b d%d - (a%d)\n", dest_reg, source_reg);
    return 0;
}


/* cmp.b Dn - d(An) */
static uint32_t m68k_b028_cmp_b_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    uint8_t b = read_byte (context, context->state.a [source_reg] + displacement);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

    printf ("cmp.b d%d - %04x(a%d)\n", dest_reg, displacement, source_reg);
    return 0;
}


/* cmp.b Dn - (xxx.w) */
static uint32_t m68k_b038_cmp_b_dn_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t b = read_byte_aw (context);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

    printf ("cmp.b d%d - (xxx.w)\n", dest_reg);
    return 0;
}


/* cmp.w Dn - Dn */
static uint32_t m68k_b040_cmp_w_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t b = context->state.d [source_reg].w;
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a - b;

    m68k_cmp_w_flags (context, a, b, result);

    printf ("cmp.w d%d - d%d\n", dest_reg, source_reg);
    return 0;
}


/* cmp.w Dn - (An) */
static uint32_t m68k_b050_cmp_w_dn_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t b = read_word (context, context->state.a [source_reg]);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a - b;

    m68k_cmp_w_flags (context, a, b, result);

    printf ("cmp.w d%d - (a%d)\n", dest_reg, source_reg);
    return 0;
}


/* cmp.w Dn - d(An) */
static uint32_t m68k_b068_cmp_w_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    uint16_t b = read_word (context, context->state.a [source_reg] + displacement);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a - b;

    m68k_cmp_w_flags (context, a, b, result);

    printf ("cmp.w d%d - %04x(a%d)\n", dest_reg, displacement, source_reg);
    return 0;
}


/* cmp.w Dn - (xxx.w) */
static uint32_t m68k_b078_cmp_w_dn_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t b = read_word_aw (context);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a - b;

    m68k_cmp_w_flags (context, a, b, result);

    printf ("cmp.w d%d - (xxx.w)\n", dest_reg);
    return 0;
}


/* cmp.l Dn - An */
static uint32_t m68k_b088_cmp_l_dn_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t b = context->state.a [source_reg];
    uint32_t a = context->state.d [dest_reg].l;
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    printf ("cmp.l d%d - a%d\n", dest_reg, source_reg);
    return 0;
}


/* eor.b Dn ← Dn ^ Dn */
static uint32_t m68k_b100_eor_b_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint16_t dest_reg = instruction & 0x07;

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a ^ b;

    context->state.d [dest_reg].b = result;
    m68k_move_b_flags (context, result);

    printf ("eor.b d%d ← d%d\n", dest_reg, source_reg);
    return 0;
}


/* eor.w Dn ← Dn ^ Dn */
static uint32_t m68k_b140_eor_w_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint16_t dest_reg = instruction & 0x07;

    uint16_t b = context->state.d [source_reg].w;
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a ^ b;

    context->state.d [dest_reg].w = result;
    m68k_move_w_flags (context, result);

    printf ("eor.w d%d ← d%d\n", dest_reg, source_reg);
    return 0;
}


/* eor.l Dn ← Dn ^ Dn */
static uint32_t m68k_b180_eor_l_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint16_t dest_reg = instruction & 0x07;

    uint32_t b = context->state.d [source_reg].l;
    uint32_t a = context->state.d [dest_reg].l;
    uint32_t result = a ^ b;

    context->state.d [dest_reg].l = result;
    m68k_move_l_flags (context, result);

    printf ("eor.l d%d ← d%d\n", dest_reg, source_reg);
    return 0;
}


/* and.b Dn ← Dn & Dn */
static uint32_t m68k_c000_and_b_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a & b;

    context->state.d [dest_reg].b = result;
    m68k_move_b_flags (context, result);

    printf ("and.b d%d ← d%d\n", dest_reg, source_reg);
    return 0;
}


/* and.w Dn ← Dn & Dn */
static uint32_t m68k_c040_and_w_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t b = context->state.d [source_reg].w;
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a & b;

    context->state.d [dest_reg].w = result;
    m68k_move_w_flags (context, result);

    printf ("and.w d%d ← d%d\n", dest_reg, source_reg);
    return 0;
}


/* and.w Dn ← Dn & d(PC+Xi) */
static uint32_t m68k_c07b_and_w_dn_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t b = read_word_with_index (context, context->state.pc);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a & b;

    context->state.d [dest_reg].w = result;
    m68k_move_w_flags (context, result);

    printf ("and.w d%d ← d(PC+Xi)\n", dest_reg);
    return 0;
}


/* and.l Dn ← Dn & Dn */
static uint32_t m68k_c080_and_l_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t b = context->state.d [source_reg].l;
    uint32_t a = context->state.d [dest_reg].l;
    uint32_t result = a & b;

    context->state.d [dest_reg].l = result;
    m68k_move_l_flags (context, result);

    printf ("and.l d%d ← d%d\n", dest_reg, source_reg);
    return 0;
}


/* exg.l Dn ←→ Dn */
static uint32_t m68k_c140_exg_l_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg_x = (instruction >> 9) & 0x07;
    uint16_t reg_y = instruction & 0x07;

    uint32_t temp = context->state.d [reg_x].l;
    context->state.d [reg_x].l = context->state.d [reg_y].l;
    context->state.d [reg_y].l = temp;

    printf ("exg.l d%d ←→ %d.\n", reg_x, reg_x);
    return 0;
}


/* muls.w Dn ← Dn × Dn */
static uint32_t m68k_c1c0_muls_w_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    int16_t b = context->state.d [source_reg].w;
    int16_t a = context->state.d [dest_reg].w;
    int32_t result = a * b;

    context->state.d [dest_reg].l = result;

    context->state.ccr_negative = (result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;

    printf ("muls.w d%d ← d%d\n", dest_reg, source_reg);
    return 0;
}


/* muls.w Dn ← Dn × d(An) */
static uint32_t m68k_c1e8_muls_w_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    int16_t displacement = read_extension (context);

    int16_t b = read_word (context, context->state.a [source_reg] + displacement);
    int16_t a = context->state.d [dest_reg].w;
    int32_t result = a * b;

    context->state.d [dest_reg].l = result;

    context->state.ccr_negative = (result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;

    printf ("muls.w d%d ← d(a%d)\n", dest_reg, source_reg);
    return 0;
}


/* muls.w Dn ← Dn × #xxxx */
static uint32_t m68k_c1fc_muls_w_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    int16_t b = read_extension (context);
    int16_t a = context->state.d [dest_reg].w;
    int32_t result = a * b;

    context->state.d [dest_reg].l = result;

    context->state.ccr_negative = (result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;

    printf ("muls.w d%d ← #%04x\n", dest_reg, b);
    return 0;
}


/* add.b Dn ← Dn + Dn */
static uint32_t m68k_d000_add_b_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a + b;

    context->state.d [dest_reg].b = result;
    m68k_add_b_flags (context, a, b, result);

    printf ("add.b d%d ← d%d + d%d\n", dest_reg, dest_reg, source_reg);
    return 0;
}


/* add.b Dn ← Dn + d(An) */
static uint32_t m68k_d028_add_b_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    uint8_t b = read_byte (context, context->state.a [source_reg] + displacement);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a + b;

    context->state.d [dest_reg].b = result;
    m68k_add_b_flags (context, a, b, result);

    printf ("add.b d%d ← d%d + %04x(a%d)\n", dest_reg, dest_reg, displacement, source_reg);
    return 0;
}


/* add.b Dn ← Dn + (xxx.w) */
static uint32_t m68k_d038_add_b_dn_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t b = read_byte_aw (context);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a + b;

    context->state.d [dest_reg].b = result;
    m68k_add_b_flags (context, a, b, result);

    printf ("add.b d%d ← d%d + (xxx.w)\n", dest_reg, dest_reg);
    return 0;
}


/* add.w Dn ← Dn + Dn */
static uint32_t m68k_d040_add_w_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t b = context->state.d [source_reg].w;
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a + b;

    context->state.d [dest_reg].w = result;
    m68k_add_w_flags (context, a, b, result);

    printf ("add.w d%d ← d%d + d%d\n", dest_reg, dest_reg, source_reg);
    return 0;
}


/* add.w Dn ← Dn + An */
static uint32_t m68k_d048_add_w_dn_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t b = context->state.a [source_reg];
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a + b;

    context->state.d [dest_reg].w = result;
    m68k_add_w_flags (context, a, b, result);

    printf ("add.w d%d ← d%d + a%d\n", dest_reg, dest_reg, source_reg);
    return 0;
}


/* add.w Dn ← Dn + (An) */
static uint32_t m68k_d050_add_w_dn_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t b = read_word (context, context->state.a [source_reg]);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a + b;

    context->state.d [dest_reg].w = result;
    m68k_add_w_flags (context, a, b, result);

    printf ("add.w d%d ← d%d + (a%d)\n", dest_reg, dest_reg, source_reg);
    return 0;
}


/* add.w Dn ← Dn + (An)+ */
static uint32_t m68k_d058_add_w_dn_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t b = read_word (context, context->state.a [source_reg]);
    uint16_t a = context->state.d [dest_reg].w;
    context->state.a [source_reg] += 2;
    uint16_t result = a + b;

    context->state.d [dest_reg].w = result;
    m68k_add_w_flags (context, a, b, result);

    printf ("add.w d%d ← d%d + (a%d)+\n", dest_reg, dest_reg, source_reg);
    return 0;
}


/* add.w Dn ← Dn + d(An) */
static uint32_t m68k_d068_add_w_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    int16_t displacement = read_extension (context);

    uint16_t b = read_word (context, context->state.a [source_reg] + displacement);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a + b;

    context->state.d [dest_reg].w = result;
    m68k_add_w_flags (context, a, b, result);

    printf ("add.w d%d ← d%d + %04x(a%d)\n", dest_reg, dest_reg, displacement, source_reg);
    return 0;
}


/* add.w Dn ← Dn + (xxx.w) */
static uint32_t m68k_d078_add_w_dn_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t b = read_word_aw (context);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a + b;

    context->state.d [dest_reg].w = result;
    m68k_add_w_flags (context, a, b, result);

    printf ("add.w d%d ← d%d + (xxx.w)\n", dest_reg, dest_reg);
    return 0;
}


/* add.l Dn ← Dn + Dn */
static uint32_t m68k_d080_add_l_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t b = context->state.d [source_reg].l;
    uint32_t a = context->state.d [dest_reg].l;
    uint32_t result = a + b;

    context->state.d [dest_reg].l = result;
    m68k_add_l_flags (context, a, b, result);

    printf ("add.l d%d ← d%d + d%d\n", dest_reg, dest_reg, source_reg);
    return 0;
}


/* add.l Dn ← Dn + An */
static uint32_t m68k_d088_add_l_dn_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t b = context->state.a [source_reg];
    uint32_t a = context->state.d [dest_reg].l;
    uint32_t result = a + b;

    context->state.d [dest_reg].l = result;
    m68k_add_l_flags (context, a, b, result);

    printf ("add.l d%d ← d%d + a%d\n", dest_reg, dest_reg, source_reg);
    return 0;
}


/* add.l Dn ← Dn + (xxx.w) */
static uint32_t m68k_d0b8_add_l_dn_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_long_aw (context);
    uint32_t a = context->state.d [dest_reg].l;
    uint32_t result = a + b;

    context->state.d [dest_reg].l = result;
    m68k_add_l_flags (context, a, b, result);

    printf ("add.l d%d ← d%d + (xxx.w)\n", dest_reg, dest_reg);
    return 0;
}


/* adda.w An ← An + Dn */
static uint32_t m68k_d0c0_adda_w_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [dest_reg] += (int16_t) context->state.d [source_reg].w;

    printf ("adda.w a%d ← a%d + d%d\n", dest_reg, dest_reg, source_reg);
    return 0;
}


/* adda.w An ← An + An */
static uint32_t m68k_d0c8_adda_w_an_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [dest_reg] += (int16_t) (uint16_t) context->state.a [source_reg];

    printf ("adda.w a%d ← a%d + a%d\n", dest_reg, dest_reg, source_reg);
    return 0;
}


/* adda.w An ← An + (An) */
static uint32_t m68k_d0d0_adda_w_an_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [dest_reg] += (int16_t) read_word (context, context->state.a [source_reg]);

    printf ("adda.w a%d ← a%d + (a%d)\n", dest_reg, dest_reg, source_reg);
    return 0;
}


/* adda.w An ← An + #xxxx */
static uint32_t m68k_d0fc_adda_w_an_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t imm = read_extension (context);

    context->state.a [dest_reg] += (int16_t) imm;

    printf ("adda.w a%d ← a%d + #%04x\n", dest_reg, dest_reg, imm);
    return 0;
}


/* adda.w An ← An + d(An+Xi) */
static uint32_t m68k_d0f0_adda_w_an_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [dest_reg] += (int16_t) read_word_with_index (context, context->state.a [source_reg]);

    printf ("adda.w a%d ← a%d + d(a%d+Xi)\n", dest_reg, dest_reg, source_reg);
    return 0;
}


/* add.b d(An) ← d(An) + Dn */
static uint32_t m68k_d128_add_b_dan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = instruction & 0x07;
    uint16_t source_reg = (instruction >> 9) & 0x07;
    int16_t displacement = read_extension (context);

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = read_byte (context, context->state.a [dest_reg] + displacement);
    uint8_t result = a + b;

    write_byte (context, context->state.a [dest_reg] + displacement, result);
    m68k_add_b_flags (context, a, b, result);

    printf ("add.b d(a%d) ← d(a%d) + d%d\n", dest_reg, dest_reg, source_reg);
    return 0;
}


/* add.w d(An) ← d(An) + Dn */
static uint32_t m68k_d168_add_w_dan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = instruction & 0x07;
    uint16_t source_reg = (instruction >> 9) & 0x07;
    int16_t displacement = read_extension (context);

    uint16_t b = context->state.d [source_reg].w;
    uint16_t a = read_word (context, context->state.a [dest_reg] + displacement);
    uint16_t result = a + b;

    write_word (context, context->state.a [dest_reg] + displacement, result);
    m68k_add_w_flags (context, a, b, result);

    printf ("add.w d(a%d) ← d(a%d) + d%d\n", dest_reg, dest_reg, source_reg);
    return 0;
}


/* add.l (xxx.w) ← (xxx.w) + Dn */
static uint32_t m68k_d1b8_add_l_aw_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    int16_t addr = read_extension (context);

    uint32_t b = context->state.d [source_reg].l;
    uint32_t a = read_long (context, addr);
    uint32_t result = a + b;

    write_long (context, addr, result);
    m68k_add_l_flags (context, a, b, result);

    printf ("add.l (xxx.w) ← (xxx.w) + d%d\n", source_reg);
    return 0;
}


/* adda.l An ← An + Dn */
static uint32_t m68k_d1c0_adda_l_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [dest_reg] += context->state.d [source_reg].l;

    printf ("adda.l a%d ← a%d + d%d\n", dest_reg, dest_reg, source_reg);
    return 0;
}


/* lsr.b Dn ← Dn >> #xx */
static uint32_t m68k_e008_lsr_b_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t count = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t reg = instruction & 0x07;
    uint8_t value = context->state.d [reg].b;

    context->state.ccr_carry = 0;

    for (uint32_t i = 0; i < count; i++)
    {
        context->state.ccr_carry = value & 0x01;
        context->state.ccr_extend = value & 0x01;
        value = value >> 1;
    }

    context->state.ccr_negative = ((int8_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    context->state.d [reg].b = value;

    printf ("lsr.b d%d >> %d\n", reg, count);
    return 0;
}


/* asr.w Dn ← Dn >> #xx */
static uint32_t m68k_e040_asr_w_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t count = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t reg = instruction & 0x07;
    uint16_t value = context->state.d [reg].w;

    context->state.ccr_carry = 0;

    for (uint32_t i = 0; i < count; i++)
    {
        context->state.ccr_carry = value & 0x0001;
        context->state.ccr_extend = value & 0x0001;

        value = (value >> 1) | (value & 0x8000);
    }

    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    context->state.d [reg].w = value;

    printf ("asr.w d%d >> %d\n", reg, count);
    return 0;
}


/* lsr.w Dn ← Dn >> #xx */
static uint32_t m68k_e048_lsr_w_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t count = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t reg = instruction & 0x07;
    uint16_t value = context->state.d [reg].w;

    context->state.ccr_carry = 0;

    for (uint32_t i = 0; i < count; i++)
    {
        context->state.ccr_carry = value & 0x0001;
        context->state.ccr_extend = value & 0x0001;
        value = value >> 1;
    }

    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    context->state.d [reg].w = value;

    printf ("lsr.w d%d >> %d\n", reg, count);
    return 0;
}


/* ror.w Dn ← Dn >> #xx */
static uint32_t m68k_e058_ror_w_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t count = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t reg = instruction & 0x07;
    uint16_t value = context->state.d [reg].w;

    for (uint32_t i = 0; i < count; i++)
    {
        context->state.ccr_carry = value & 0x0001;
        value = (value >> 1) | (value << 15);
    }

    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    context->state.d [reg].w = value;

    printf ("ror.w d%d >> %d\n", reg, count);
    return 0;
}


/* lsr.w Dn ← Dn >> Dn */
static uint32_t m68k_e068_lsr_w_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t count_reg = (instruction >> 9) & 0x07;
    uint16_t reg = instruction & 0x07;
    uint16_t value = context->state.d [reg].w;

    uint8_t count = context->state.d [count_reg].b & 0x3f;

    context->state.ccr_carry = 0;

    for (uint32_t i = 0; i < count; i++)
    {
        context->state.ccr_carry = value & 0x0001;
        context->state.ccr_extend = value & 0x0001;
        value = value >> 1;
    }

    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    context->state.d [reg].w = value;

    printf ("lsr.w d%d >> d%d\n", reg, count_reg);
    return 0;
}


/* asr.l Dn ← Dn >> #xx */
static uint32_t m68k_e080_asr_l_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t count = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t reg = instruction & 0x07;
    uint32_t value = context->state.d [reg].l;

    context->state.ccr_carry = 0;

    for (uint32_t i = 0; i < count; i++)
    {
        context->state.ccr_carry = value & 0x00000001;
        context->state.ccr_extend = value & 0x00000001;
        value = (value >> 1) | (value & 0x80000000);
    }

    context->state.ccr_negative = ((int32_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    context->state.d [reg].l = value;

    printf ("asr.l d%d >> %d\n", reg, count);
    return 0;
}


/* ror.l Dn ← Dn >> #xx */
static uint32_t m68k_e098_ror_l_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t count = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t reg = instruction & 0x07;
    uint32_t value = context->state.d [reg].l;

    for (uint32_t i = 0; i < count; i++)
    {
        context->state.ccr_carry = value & 0x00000001;
        value = (value >> 1) | (value << 31);
    }

    context->state.ccr_negative = ((int32_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    context->state.d [reg].l = value;

    printf ("ror.l d%d >> %d\n", reg, count);
    return 0;
}


/* lsl.b Dn ← Dn << #xx */
static uint32_t m68k_e108_lsl_b_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t count = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t reg = instruction & 0x07;
    uint8_t value = context->state.d [reg].b;

    context->state.ccr_carry = 0;

    for (uint32_t i = 0; i < count; i++)
    {
        context->state.ccr_carry = value >> 7;
        context->state.ccr_extend = value >> 7;
        value = value << 1;
    }

    context->state.ccr_negative = ((int8_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    context->state.d [reg].b = value;

    printf ("lsl.b d%d << %d\n", reg, count);
    return 0;
}


/* rol.b Dn ← Dn << #xx */
static uint32_t m68k_e118_rol_b_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t count = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t reg = instruction & 0x07;
    uint8_t value = context->state.d [reg].b;

    context->state.ccr_carry = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        value = (value << 1) | (value >> 7);
        context->state.ccr_carry = value & 0x01;
    }

    context->state.ccr_negative = ((int8_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    context->state.d [reg].b = value;

    printf ("rol.b d%d << %d\n", reg, count);
    return 0;
}


/* rol.b Dn ← Dn << Dn */
static uint32_t m68k_e138_rol_b_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t count_reg = (instruction >> 9) & 0x07;
    uint8_t count = context->state.d [count_reg].b & 0x3f;
    uint16_t reg = instruction & 0x07;
    uint8_t value = context->state.d [reg].b;

    context->state.ccr_carry = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        value = (value << 1) | (value >> 7);
        context->state.ccr_carry = value & 0x01;
    }

    context->state.ccr_negative = ((int8_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    context->state.d [reg].b = value;

    printf ("rol.b d%d << d%d\n", reg, count_reg);
    return 0;
}


/* asl.w Dn ← Dn << #xx */
static uint32_t m68k_e140_asl_w_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t count = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t reg = instruction & 0x07;
    uint16_t value = context->state.d [reg].w;

    uint16_t initial_value = value;
    bool sign_changed = false;
    context->state.ccr_carry = 0;

    for (uint32_t i = 0; i < count; i++)
    {
        context->state.ccr_carry = value >> 15;
        context->state.ccr_extend = value >> 15;

        value = value << 1;
        sign_changed |= (value ^ initial_value) >> 15;
    }

    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = sign_changed;

    context->state.d [reg].w = value;

    printf ("asl.w d%d << %d\n", reg, count);
    return 0;
}


/* lsl.w Dn ← Dn << #xx */
static uint32_t m68k_e148_lsl_w_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t count = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t reg = instruction & 0x07;
    uint16_t value = context->state.d [reg].w;

    context->state.ccr_carry = 0;

    for (uint32_t i = 0; i < count; i++)
    {
        context->state.ccr_carry = value >> 15;
        context->state.ccr_extend = value >> 15;
        value = value << 1;
    }

    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    context->state.d [reg].w = value;

    printf ("lsl.w d%d << %d\n", reg, count);
    return 0;
}


/* roxl.w Dn ← Dn << #xx */
static uint32_t m68k_e150_roxl_w_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t count = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t reg = instruction & 0x07;
    uint16_t value = context->state.d [reg].w;

    for (uint32_t i = 0; i < count; i++)
    {
        context->state.ccr_carry = value >> 15;
        value = (value << 1) | context->state.ccr_extend;
        context->state.ccr_extend = context->state.ccr_carry;
    }

    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    context->state.d [reg].w = value;

    printf ("roxl.w d%d << %d\n", reg, count);
    return 0;
}


/* rol.w Dn ← Dn << #xx */
static uint32_t m68k_e158_rol_w_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t count = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t reg = instruction & 0x07;
    uint16_t value = context->state.d [reg].w;

    for (uint32_t i = 0; i < count; i++)
    {
        value = (value << 1) | (value >> 15);
        context->state.ccr_carry = value & 0x01;
    }

    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    context->state.d [reg].w = value;

    printf ("rol.w d%d << %d\n", reg, count);
    return 0;
}


/* lsl.w Dn ← Dn << Dn */
static uint32_t m68k_e168_lsl_w_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t count_reg = (instruction >> 9) & 0x07;
    uint16_t reg = instruction & 0x07;
    uint16_t value = context->state.d [reg].w;

    uint8_t count = context->state.d [count_reg].b & 0x3f;

    context->state.ccr_carry = 0;

    for (uint32_t i = 0; i < count; i++)
    {
        context->state.ccr_carry = value >> 15;
        context->state.ccr_extend = value >> 15;

        value = value << 1;
    }

    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    context->state.d [reg].w = value;

    printf ("lsl.w d%d << d%d\n", reg, count_reg);
    return 0;
}


/* asl.l Dn ← Dn << #xx */
static uint32_t m68k_e180_asl_l_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t count = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t reg = instruction & 0x07;
    uint32_t value = context->state.d [reg].l;

    uint32_t initial_value = value;
    bool sign_changed = false;
    context->state.ccr_carry = 0;

    for (uint32_t i = 0; i < count; i++)
    {
        context->state.ccr_carry = value >> 31;
        context->state.ccr_extend = value >> 31;

        value = value << 1;
        sign_changed |= (value ^ initial_value) >> 31;
    }

    context->state.ccr_negative = ((int32_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = sign_changed;

    context->state.d [reg].l = value;

    printf ("asl.l d%d << %d\n", reg, count);
    return 0;
}


/* lsl.l Dn ← Dn << #xx */
static uint32_t m68k_e188_lsl_l_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t count = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t reg = instruction & 0x07;
    uint32_t value = context->state.d [reg].l;

    uint32_t result = value << count;
    bool last_out = !! ((value << (count - 1)) & 0x80000000);

    context->state.ccr_negative = ((int32_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = last_out;
    context->state.ccr_extend = last_out;

    context->state.d [reg].l = result;

    printf ("lsl.l d%d << %d\n", reg, count);
    return 0;
}


/*
 * Initialise the instruction array.
 */
static void m68k_init_instructions (void)
{
    /* TODO: Consider something that reads closer to the datasheet.
     *       Eg, add_instruction ("0001 xxx 000 011 xxx", m68k_1018_move_b_dn_anp); */

    /* Bit Instructions */
    for (uint16_t data_reg = 0; data_reg < 8; data_reg++)
    {
        for (uint16_t bit_reg = 0; bit_reg < 8; bit_reg++)
        {
            m68k_instruction [0x0100 | (bit_reg << 9) | data_reg] = m68k_0100_btst_l_dn_dn;
            m68k_instruction [0x0110 | (bit_reg << 9) | data_reg] = m68k_0110_btst_b_an_dn;
            m68k_instruction [0x0180 | (bit_reg << 9) | data_reg] = m68k_0180_bclr_l_dn_dn;
            m68k_instruction [0x01c0 | (bit_reg << 9) | data_reg] = m68k_01c0_bset_l_dn_dn;
            m68k_instruction [0x01f0 | (bit_reg << 9) | data_reg] = m68k_01f0_bset_b_danxi_dn;
        }
        /* TODO: actually the bit-register, but loop currently structured to iterate
         *       over data register or both.. So not a great fit for absolute-word. */
        m68k_instruction [0x01f8 | (data_reg << 9)] = m68k_01f8_bset_b_aw_dn;
        m68k_instruction [0x0800 | data_reg] = m68k_0800_btst_l_dn_imm;
        m68k_instruction [0x0810 | data_reg] = m68k_0810_btst_b_an_imm;
        m68k_instruction [0x0828 | data_reg] = m68k_0828_btst_b_dan_imm;
        m68k_instruction [0x0830 | data_reg] = m68k_0830_btst_b_danxi_imm;
        m68k_instruction [0x0868 | data_reg] = m68k_0868_bcgh_b_dan_imm;
        m68k_instruction [0x0880 | data_reg] = m68k_0880_bclr_l_dn_imm;
        m68k_instruction [0x0890 | data_reg] = m68k_0890_bclr_b_an_imm;
        m68k_instruction [0x08a8 | data_reg] = m68k_08a8_bclr_b_dan_imm;
        m68k_instruction [0x08b0 | data_reg] = m68k_08b0_bclr_b_danxi_imm;
        m68k_instruction [0x08d0 | data_reg] = m68k_08d0_bset_b_an_imm;
        m68k_instruction [0x08e8 | data_reg] = m68k_08e8_bset_b_dan_imm;
        m68k_instruction [0x08f0 | data_reg] = m68k_08f0_bset_b_danxi_imm;
    }

    m68k_instruction [0x0838] = m68k_0838_btst_b_aw_imm;
    m68k_instruction [0x0839] = m68k_0839_btst_b_al_imm;
    m68k_instruction [0x08b8] = m68k_08b8_bclr_b_aw_imm;
    m68k_instruction [0x08f8] = m68k_08f8_bset_b_aw_imm;

    /* immediate */
    for (uint16_t reg = 0; reg < 8; reg++)
    {
        m68k_instruction [0x0000 | reg] = m68k_0000_ori_b_dn;
        m68k_instruction [0x0040 | reg] = m68k_0040_ori_w_dn;
        m68k_instruction [0x0200 | reg] = m68k_0200_andi_b_dn;
        m68k_instruction [0x0228 | reg] = m68k_0228_andi_b_dan;
        m68k_instruction [0x0240 | reg] = m68k_0240_andi_w_dn;
        m68k_instruction [0x0400 | reg] = m68k_0400_subi_b_dn;
        m68k_instruction [0x0440 | reg] = m68k_0440_subi_w_dn;
        m68k_instruction [0x0458 | reg] = m68k_0458_subi_w_anp;
        m68k_instruction [0x0600 | reg] = m68k_0600_addi_b_dn;
        m68k_instruction [0x0640 | reg] = m68k_0640_addi_w_dn;
        m68k_instruction [0x0668 | reg] = m68k_0668_addi_w_dan;
        m68k_instruction [0x0680 | reg] = m68k_0680_addi_l_dn;
        m68k_instruction [0x0698 | reg] = m68k_0698_addi_l_anp;
        m68k_instruction [0x0a40 | reg] = m68k_0a40_eori_w_dn;
        m68k_instruction [0x0a80 | reg] = m68k_0a80_eori_l_dn;
        m68k_instruction [0x0c00 | reg] = m68k_0c00_cmpi_b_dn;
        m68k_instruction [0x0c10 | reg] = m68k_0c10_cmpi_b_an;
        m68k_instruction [0x0c28 | reg] = m68k_0c28_cmpi_b_dan;
        m68k_instruction [0x0c40 | reg] = m68k_0c40_cmpi_w_dn;
        m68k_instruction [0x0c50 | reg] = m68k_0c50_cmpi_w_an;
        m68k_instruction [0x0c68 | reg] = m68k_0c68_cmpi_w_dan;
        m68k_instruction [0x0c98 | reg] = m68k_0c98_cmpi_l_anp;
    }
    m68k_instruction [0x0038] = m68k_0038_ori_b_aw;
    m68k_instruction [0x0238] = m68k_0238_andi_b_aw;
    m68k_instruction [0x0678] = m68k_0678_addi_w_aw;
    m68k_instruction [0x0a38] = m68k_0a38_eori_b_aw;
    m68k_instruction [0x0c38] = m68k_0c38_cmpi_b_aw;
    m68k_instruction [0x0c78] = m68k_0c78_cmpi_w_aw;

    /* move */
    for (uint16_t reg_a = 0; reg_a < 8; reg_a++)
    {
        for (uint16_t reg_b = 0; reg_b < 8; reg_b++)
        {
            m68k_instruction [0x1000 | (reg_a << 9) | reg_b] = m68k_1000_move_b_dn_dn;
            m68k_instruction [0x1010 | (reg_a << 9) | reg_b] = m68k_1010_move_b_dn_an;
            m68k_instruction [0x1018 | (reg_a << 9) | reg_b] = m68k_1018_move_b_dn_anp;
            m68k_instruction [0x1028 | (reg_a << 9) | reg_b] = m68k_1028_move_b_dn_dan;
            m68k_instruction [0x1030 | (reg_a << 9) | reg_b] = m68k_1030_move_b_dn_danxi;
            m68k_instruction [0x1098 | (reg_a << 9) | reg_b] = m68k_1098_move_b_an_anp;
            m68k_instruction [0x10b0 | (reg_a << 9) | reg_b] = m68k_10b0_move_b_an_danxi;
            m68k_instruction [0x10c0 | (reg_a << 9) | reg_b] = m68k_10c0_move_b_anp_dn;
            m68k_instruction [0x10d8 | (reg_a << 9) | reg_b] = m68k_10d8_move_b_anp_anp;
            m68k_instruction [0x1140 | (reg_a << 9) | reg_b] = m68k_1140_move_b_dan_dn;
            m68k_instruction [0x1150 | (reg_a << 9) | reg_b] = m68k_1150_move_b_dan_an;
            m68k_instruction [0x1158 | (reg_a << 9) | reg_b] = m68k_1158_move_b_dan_anp;
            m68k_instruction [0x1168 | (reg_a << 9) | reg_b] = m68k_1168_move_b_dan_dan;
            m68k_instruction [0x1170 | (reg_a << 9) | reg_b] = m68k_1170_move_b_dan_danxi;
            m68k_instruction [0x1180 | (reg_a << 9) | reg_b] = m68k_1180_move_b_danxi_dn;
            m68k_instruction [0x2000 | (reg_a << 9) | reg_b] = m68k_2000_move_l_dn_dn;
            m68k_instruction [0x2010 | (reg_a << 9) | reg_b] = m68k_2010_move_l_dn_an;
            m68k_instruction [0x2018 | (reg_a << 9) | reg_b] = m68k_2018_move_l_dn_anp;
            m68k_instruction [0x2028 | (reg_a << 9) | reg_b] = m68k_2028_move_l_dn_dan;
            m68k_instruction [0x2040 | (reg_a << 9) | reg_b] = m68k_2040_movea_l_an_dn;
            m68k_instruction [0x2048 | (reg_a << 9) | reg_b] = m68k_2048_movea_l_an_an;
            m68k_instruction [0x2058 | (reg_a << 9) | reg_b] = m68k_2058_movea_l_an_anp;
            m68k_instruction [0x2068 | (reg_a << 9) | reg_b] = m68k_2068_movea_l_an_dan;
            m68k_instruction [0x2070 | (reg_a << 9) | reg_b] = m68k_2070_movea_l_an_danxi;
            m68k_instruction [0x2080 | (reg_a << 9) | reg_b] = m68k_2080_move_l_an_dn;
            m68k_instruction [0x2098 | (reg_a << 9) | reg_b] = m68k_2098_move_l_an_anp;
            m68k_instruction [0x20b0 | (reg_a << 9) | reg_b] = m68k_20b0_move_l_an_danxi;
            m68k_instruction [0x20c0 | (reg_a << 9) | reg_b] = m68k_20c0_move_l_anp_dn;
            m68k_instruction [0x20d8 | (reg_a << 9) | reg_b] = m68k_20d8_move_l_anp_anp;
            m68k_instruction [0x20e8 | (reg_a << 9) | reg_b] = m68k_20e8_move_l_anp_dan;
            m68k_instruction [0x20f0 | (reg_a << 9) | reg_b] = m68k_20f0_move_l_anp_danxi;
            m68k_instruction [0x2100 | (reg_a << 9) | reg_b] = m68k_2100_move_l_pan_dn;
            m68k_instruction [0x2108 | (reg_a << 9) | reg_b] = m68k_2108_move_l_pan_an;
            m68k_instruction [0x2140 | (reg_a << 9) | reg_b] = m68k_2140_move_l_dan_dn;
            m68k_instruction [0x2148 | (reg_a << 9) | reg_b] = m68k_2148_move_l_dan_an;
            m68k_instruction [0x2188 | (reg_a << 9) | reg_b] = m68k_2188_move_l_danxi_an;
            m68k_instruction [0x3000 | (reg_a << 9) | reg_b] = m68k_3000_move_w_dn_dn;
            m68k_instruction [0x3008 | (reg_a << 9) | reg_b] = m68k_3008_move_w_dn_an;
            m68k_instruction [0x3010 | (reg_a << 9) | reg_b] = m68k_3010_move_w_dn_an;
            m68k_instruction [0x3018 | (reg_a << 9) | reg_b] = m68k_3018_move_w_dn_anp;
            m68k_instruction [0x3028 | (reg_a << 9) | reg_b] = m68k_3028_move_w_dn_dan;
            m68k_instruction [0x3030 | (reg_a << 9) | reg_b] = m68k_3030_move_w_dn_danxi;
            m68k_instruction [0x3040 | (reg_a << 9) | reg_b] = m68k_3040_movea_w_an_dn;
            m68k_instruction [0x3058 | (reg_a << 9) | reg_b] = m68k_3058_movea_w_an_anp;
            m68k_instruction [0x3068 | (reg_a << 9) | reg_b] = m68k_3068_movea_w_an_dan;
            m68k_instruction [0x3074 | (reg_a << 9) | reg_b] = m68k_3074_movea_w_an_dan;
            m68k_instruction [0x3080 | (reg_a << 9) | reg_b] = m68k_3080_move_w_an_dn;
            m68k_instruction [0x3088 | (reg_a << 9) | reg_b] = m68k_3088_move_w_an_an;
            m68k_instruction [0x3098 | (reg_a << 9) | reg_b] = m68k_3098_move_w_an_anp;
            m68k_instruction [0x30c0 | (reg_a << 9) | reg_b] = m68k_30c0_move_w_anp_dn;
            m68k_instruction [0x30c8 | (reg_a << 9) | reg_b] = m68k_30c8_move_w_anp_an;
            m68k_instruction [0x30d8 | (reg_a << 9) | reg_b] = m68k_30d8_move_w_anp_anp;
            m68k_instruction [0x30e8 | (reg_a << 9) | reg_b] = m68k_30e8_move_w_anp_dan;
            m68k_instruction [0x3100 | (reg_a << 9) | reg_b] = m68k_3100_move_w_pan_dn;
            m68k_instruction [0x3140 | (reg_a << 9) | reg_b] = m68k_3140_move_w_dan_dn;
            m68k_instruction [0x3150 | (reg_a << 9) | reg_b] = m68k_3150_move_w_dan_an;
            m68k_instruction [0x3158 | (reg_a << 9) | reg_b] = m68k_3158_move_w_dan_anp;
            m68k_instruction [0x3168 | (reg_a << 9) | reg_b] = m68k_3168_move_w_dan_dan;
            m68k_instruction [0x3170 | (reg_a << 9) | reg_b] = m68k_3170_move_b_dan_danxi;
            m68k_instruction [0x3180 | (reg_a << 9) | reg_b] = m68k_3180_move_w_danxi_dn;
            m68k_instruction [0x3190 | (reg_a << 9) | reg_b] = m68k_3190_move_w_danxi_an;
            m68k_instruction [0x3198 | (reg_a << 9) | reg_b] = m68k_3198_move_w_danxi_anp;
        }
        m68k_instruction [0x1038 | (reg_a << 9)] = m68k_1038_move_b_dn_aw;
        m68k_instruction [0x1039 | (reg_a << 9)] = m68k_1039_move_b_dn_al;
        m68k_instruction [0x103b | (reg_a << 9)] = m68k_103b_move_b_dn_dpcxi;
        m68k_instruction [0x103c | (reg_a << 9)] = m68k_103c_move_b_dn_imm;
        m68k_instruction [0x10bc | (reg_a << 9)] = m68k_10bc_move_b_an_imm;
        m68k_instruction [0x1178 | (reg_a << 9)] = m68k_1178_move_b_dan_aw;
        m68k_instruction [0x117c | (reg_a << 9)] = m68k_117c_move_b_dan_imm;
        m68k_instruction [0x11c0 | reg_a       ] = m68k_11c0_move_b_aw_dn;
        m68k_instruction [0x11e8 | reg_a       ] = m68k_11e8_move_b_aw_dan;
        m68k_instruction [0x13c0 | reg_a       ] = m68k_13c0_move_b_al_dn;
        m68k_instruction [0x13e8 | reg_a       ] = m68k_13e8_move_b_al_dan;
        m68k_instruction [0x2038 | (reg_a << 9)] = m68k_2038_move_l_dn_aw;
        m68k_instruction [0x203c | (reg_a << 9)] = m68k_203c_move_l_dn_imm;
        m68k_instruction [0x2078 | (reg_a << 9)] = m68k_2078_movea_l_an_aw;
        m68k_instruction [0x2079 | (reg_a << 9)] = m68k_2079_movea_l_an_al;
        m68k_instruction [0x207b | (reg_a << 9)] = m68k_207b_movea_l_an_dpcxi;
        m68k_instruction [0x207c | (reg_a << 9)] = m68k_207c_movea_l_an_imm;
        m68k_instruction [0x20bc | (reg_a << 9)] = m68k_20bc_move_l_an_imm;
        m68k_instruction [0x217c | (reg_a << 9)] = m68k_217c_move_l_dan_imm;
        m68k_instruction [0x21bc | (reg_a << 9)] = m68k_21bc_move_l_danxi_imm;
        m68k_instruction [0x21c0 | reg_a       ] = m68k_21c0_move_l_aw_dn;
        m68k_instruction [0x21c8 | reg_a       ] = m68k_21c8_move_l_aw_an;
        m68k_instruction [0x3038 | (reg_a << 9)] = m68k_3038_move_w_dn_aw;
        m68k_instruction [0x3039 | (reg_a << 9)] = m68k_3039_move_w_dn_al;
        m68k_instruction [0x303b | (reg_a << 9)] = m68k_303b_move_w_dn_dpcxi;
        m68k_instruction [0x303c | (reg_a << 9)] = m68k_303c_move_w_dn_imm;
        m68k_instruction [0x307c | (reg_a << 9)] = m68k_307c_movea_w_an_imm;
        m68k_instruction [0x30b8 | (reg_a << 9)] = m68k_30b8_move_w_an_aw;
        m68k_instruction [0x30bc | (reg_a << 9)] = m68k_30bc_move_w_an_imm;
        m68k_instruction [0x30fc | (reg_a << 9)] = m68k_30fc_move_w_anp_imm;
        m68k_instruction [0x317c | (reg_a << 9)] = m68k_317c_move_w_dan_imm;
        m68k_instruction [0x31c0 | reg_a       ] = m68k_31c0_move_w_aw_dn;
        m68k_instruction [0x33c0 | reg_a       ] = m68k_33c0_move_w_al_dn;
        m68k_instruction [0x40c0 | reg_a       ] = m68k_40c0_move_dn_sr;
        m68k_instruction [0x44c0 | reg_a       ] = m68k_44c0_move_ccr_dn;
        m68k_instruction [0x4e60 | reg_a       ] = m68k_4e60_move_an_usp;
        m68k_instruction [0x4e68 | reg_a       ] = m68k_4e68_move_usp_an;
    }
    m68k_instruction [0x11fc] = m68k_11fc_move_b_aw_imm;
    m68k_instruction [0x13fc] = m68k_13fc_move_b_al_imm;
    m68k_instruction [0x21fb] = m68k_21fb_move_l_aw_dpcxi;
    m68k_instruction [0x21fc] = m68k_21fc_move_l_aw_imm;
    m68k_instruction [0x23f8] = m68k_23f8_move_l_al_aw;
    m68k_instruction [0x23fc] = m68k_23fc_move_l_al_imm;
    m68k_instruction [0x31f8] = m68k_31f8_move_w_aw_aw;
    m68k_instruction [0x31fc] = m68k_31fc_move_w_aw_imm;
    m68k_instruction [0x33fc] = m68k_33fc_move_w_al_imm;

    /* not */
    for (uint16_t reg = 0; reg < 8; reg++)
    {
        m68k_instruction [0x4600 | reg] = m68k_4600_not_b_dn;
        m68k_instruction [0x4640 | reg] = m68k_4640_not_w_dn;
        m68k_instruction [0x4680 | reg] = m68k_4680_not_l_dn;
    }

    /* misc */
    m68k_instruction [0x46fc] = m68k_46fc_move_sr_imm;

    /* tst */
    for (uint16_t reg = 0; reg < 8; reg++)
    {
        m68k_instruction [0x4a00 | reg] = m68k_4a00_tst_b_dn;
        m68k_instruction [0x4a10 | reg] = m68k_4a10_tst_b_an;
        m68k_instruction [0x4a28 | reg] = m68k_4a28_tst_b_dan;
        m68k_instruction [0x4a30 | reg] = m68k_4a30_tst_b_danxi;
        m68k_instruction [0x4a40 | reg] = m68k_4a40_tst_w_dn;
        m68k_instruction [0x4a50 | reg] = m68k_4a50_tst_w_an;
        m68k_instruction [0x4a68 | reg] = m68k_4a68_tst_w_dan;
        m68k_instruction [0x4a90 | reg] = m68k_4a90_tst_l_an;
    }
    m68k_instruction [0x4a38] = m68k_4a38_tst_b_aw;
    m68k_instruction [0x4a78] = m68k_4a78_tst_w_aw;
    m68k_instruction [0x4a79] = m68k_4a79_tst_w_al;
    m68k_instruction [0x4ab8] = m68k_4ab8_tst_l_aw;
    m68k_instruction [0x4ab9] = m68k_4ab9_tst_l_al;


    m68k_instruction [0x4e71] = m68k_4e71_nop;
    m68k_instruction [0x4e73] = m68k_4e73_rte;
    m68k_instruction [0x4e75] = m68k_4e75_rts;

    /* jsr / jmp */
    for (uint16_t reg = 0; reg < 8; reg++)
    {
        m68k_instruction [0x4e90 | reg] = m68k_4e90_jsr_an;
        m68k_instruction [0x4ed0 | reg] = m68k_4ed0_jmp_an;
    }
    m68k_instruction [0x4eb9] = m68k_4eb9_jsr_al;
    m68k_instruction [0x4eba] = m68k_4eba_jsr_dpc;
    m68k_instruction [0x4ebb] = m68k_4ebb_jsr_dpcxi;
    m68k_instruction [0x4ef9] = m68k_4ef9_jmp_al;
    m68k_instruction [0x4efb] = m68k_4efb_jmp_dpcxi;

    /* movem */
    for (uint16_t an = 0; an < 8; an++)
    {
        m68k_instruction [0x48d0 | an] = m68k_48d0_movem_l_an_regs;
        m68k_instruction [0x48e0 | an] = m68k_48e0_movem_l_pan_regs;
        m68k_instruction [0x4c98 | an] = m68k_4c98_movem_w_regs_anp;
        m68k_instruction [0x4cd0 | an] = m68k_4cd0_movem_l_regs_an;
        m68k_instruction [0x4cd8 | an] = m68k_4cd8_movem_l_regs_anp;
    }
    m68k_instruction [0x48f8] = m68k_48f8_movem_l_aw_regs;
    m68k_instruction [0x4cf8] = m68k_4cf8_movem_l_regs_aw;

    /* lea */
    for (uint16_t an = 0; an < 8; an++)
    {
        for (uint16_t reg = 0; reg < 8; reg++)
        {
            m68k_instruction [0x41e8 | (an << 9) | reg] = m68k_41e8_lea_dan;
            m68k_instruction [0x41f0 | (an << 9) | reg] = m68k_41f0_lea_danxi;
        }
        m68k_instruction [0x41f8 | (an << 9)] = m68k_41f8_lea_aw;
        m68k_instruction [0x41f9 | (an << 9)] = m68k_41f9_lea_al;
        m68k_instruction [0x41fa | (an << 9)] = m68k_41fa_lea_dpc;
        m68k_instruction [0x41fb | (an << 9)] = m68k_41fb_lea_dpcxi;
    }

    /* clr */
    for (uint16_t reg = 0; reg < 8; reg++)
    {
        m68k_instruction [0x4218 | reg] = m68k_4218_clr_b_anp;
        m68k_instruction [0x4228 | reg] = m68k_4228_clr_b_dan;
        m68k_instruction [0x4268 | reg] = m68k_4268_clr_w_dan;
        m68k_instruction [0x4298 | reg] = m68k_4298_clr_l_anp;
    }
    m68k_instruction [0x4238] = m68k_4238_clr_b_aw;
    m68k_instruction [0x4278] = m68k_4278_clr_w_aw;
    m68k_instruction [0x42b8] = m68k_42b8_clr_l_aw;

    /* neg / swap / ext */
    for (uint16_t reg = 0; reg < 8; reg++)
    {
        m68k_instruction [0x4400 | reg] = m68k_4400_neg_b_dn;
        m68k_instruction [0x4410 | reg] = m68k_4410_neg_b_an;
        m68k_instruction [0x4428 | reg] = m68k_4428_neg_b_dan;
        m68k_instruction [0x4440 | reg] = m68k_4440_neg_w_dn;
        m68k_instruction [0x4840 | reg] = m68k_4840_swap_dn;
        m68k_instruction [0x4880 | reg] = m68k_4880_ext_w_dn;
        m68k_instruction [0x48c0 | reg] = m68k_48c0_ext_l_dn;
    }

    /* addq / subq */
    for (uint16_t data = 0; data < 8; data++)
    {
        for (uint16_t reg = 0; reg < 8; reg++)
        {
            m68k_instruction [0x5000 | (data << 9) | reg] = m68k_5000_addq_b_dn;
            m68k_instruction [0x5010 | (data << 9) | reg] = m68k_5010_addq_b_an;
            m68k_instruction [0x5020 | (data << 9) | reg] = m68k_5020_addq_b_pan;
            m68k_instruction [0x5028 | (data << 9) | reg] = m68k_5028_addq_b_dan;
            m68k_instruction [0x5040 | (data << 9) | reg] = m68k_5040_addq_w_dn;
            m68k_instruction [0x5048 | (data << 9) | reg] = m68k_5048_addq_w_an;
            m68k_instruction [0x5050 | (data << 9) | reg] = m68k_5050_addq_w_an;
            m68k_instruction [0x5058 | (data << 9) | reg] = m68k_5058_addq_w_anp;
            m68k_instruction [0x5068 | (data << 9) | reg] = m68k_5068_addq_w_dan;
            m68k_instruction [0x5088 | (data << 9) | reg] = m68k_5088_addq_l_an;
            m68k_instruction [0x5100 | (data << 9) | reg] = m68k_5100_subq_b_dn;
            m68k_instruction [0x5128 | (data << 9) | reg] = m68k_5128_subq_b_dan;
            m68k_instruction [0x5130 | (data << 9) | reg] = m68k_5130_subq_b_danxi;
            m68k_instruction [0x5140 | (data << 9) | reg] = m68k_5140_subq_w_dn;
            m68k_instruction [0x5148 | (data << 9) | reg] = m68k_5148_subq_w_an;
            m68k_instruction [0x5150 | (data << 9) | reg] = m68k_5150_subq_w_an;
            m68k_instruction [0x5158 | (data << 9) | reg] = m68k_5158_subq_w_anp;
            m68k_instruction [0x5168 | (data << 9) | reg] = m68k_5168_subq_w_dan;
            m68k_instruction [0x5188 | (data << 9) | reg] = m68k_5188_subq_l_an;
        }
        m68k_instruction [0x5038 | (data << 9)] = m68k_5038_addq_b_aw;
        m68k_instruction [0x5078 | (data << 9)] = m68k_5078_addq_w_aw;
        m68k_instruction [0x50b8 | (data << 9)] = m68k_50b8_addq_l_aw;
        m68k_instruction [0x5138 | (data << 9)] = m68k_5138_subq_b_aw;
        m68k_instruction [0x5178 | (data << 9)] = m68k_5178_subq_w_aw;
    }

    /* dbcc */
    for (uint16_t dn = 0; dn < 8; dn++)
    {
        m68k_instruction [0x51c8 | dn] = m68k_51c8_dbf;
    }

    /* Bcc/BSR/BRA with 16-bit displacement */
    m68k_instruction [0x6000] = m68k_6000_bra_w;
    m68k_instruction [0x6100] = m68k_6100_bsr_w;
    m68k_instruction [0x6200] = m68k_6200_bhi_w;
    m68k_instruction [0x6300] = m68k_6300_bcc_w;
    m68k_instruction [0x6400] = m68k_6400_bcc_w;
    m68k_instruction [0x6500] = m68k_6500_bcs_w;
    m68k_instruction [0x6600] = m68k_6600_bne_w;
    m68k_instruction [0x6700] = m68k_6700_beq_w;
    m68k_instruction [0x6a00] = m68k_6a00_bpl_w;
    m68k_instruction [0x6b00] = m68k_6b00_bmi_w;
    m68k_instruction [0x6c00] = m68k_6c00_bge_w;
    m68k_instruction [0x6d00] = m68k_6d00_blt_w;
    m68k_instruction [0x6e00] = m68k_6e00_bgt_w;
    m68k_instruction [0x6f00] = m68k_6f00_ble_w;

    /* Bcc/BSR/BRA with 8-bit displacement */
    for (uint16_t d = 0x01; d <= 0xff; d++)
    {
        m68k_instruction [0x6000 | d] = m68k_6001_bra_s;
        m68k_instruction [0x6100 | d] = m68k_6101_bsr_s;
        m68k_instruction [0x6200 | d] = m68k_6201_bhi_s;
        m68k_instruction [0x6300 | d] = m68k_6301_bcc_s;
        m68k_instruction [0x6400 | d] = m68k_6401_bcc_s;
        m68k_instruction [0x6500 | d] = m68k_6501_bcs_s;
        m68k_instruction [0x6600 | d] = m68k_6601_bne_s;
        m68k_instruction [0x6700 | d] = m68k_6701_beq_s;
        m68k_instruction [0x6a00 | d] = m68k_6a01_bpl_s;
        m68k_instruction [0x6b00 | d] = m68k_6b01_bmi_s;
        m68k_instruction [0x6c00 | d] = m68k_6c01_bge_s;
        m68k_instruction [0x6d00 | d] = m68k_6d01_blt_s;
        m68k_instruction [0x6e00 | d] = m68k_6e01_bgt_s;
        m68k_instruction [0x6f00 | d] = m68k_6f01_ble_s;
    }

    /* moveq */
    for (uint16_t dn = 0; dn < 8; dn++)
    {
        for (uint16_t data = 0x00; data <= 0xff; data++)
        {
            m68k_instruction [0x7000 | (dn << 9) | data] = m68k_7000_moveq;
        }
    }

    /* or */
    for (uint16_t reg_a = 0; reg_a < 8; reg_a++)
    {
        for (uint16_t reg_b = 0; reg_b < 8; reg_b++)
        {
            m68k_instruction [0x8000 | (reg_a << 9) | reg_b] = m68k_8000_or_b_dn_dn;
            m68k_instruction [0x8028 | (reg_a << 9) | reg_b] = m68k_8028_or_b_dn_dan;
            m68k_instruction [0x8040 | (reg_a << 9) | reg_b] = m68k_8040_or_w_dn_dn;
            m68k_instruction [0x8128 | (reg_a << 9) | reg_b] = m68k_8128_or_b_dan_dn;
        }
    }

    /* divs / divu */
    for (uint16_t reg = 0; reg < 8; reg++)
    {
        for (uint16_t ea = 0; ea < 8; ea++)
        {
            m68k_instruction [0x80c0 | (reg << 9) | ea] = m68k_80c0_divu_w_dn_dn;
        }
        m68k_instruction [0x81fc | (reg << 9)] = m68k_81fc_divs_w_dn_imm;
    }

    /* sub */
    for (uint16_t reg_a = 0; reg_a < 8; reg_a++)
    {
        for (uint16_t reg_b = 0; reg_b < 8; reg_b++)
        {
            m68k_instruction [0x9000 | (reg_a << 9) | reg_b] = m68k_9000_sub_b_dn_dn;
            m68k_instruction [0x9040 | (reg_a << 9) | reg_b] = m68k_9040_sub_w_dn_dn;
            m68k_instruction [0x9048 | (reg_a << 9) | reg_b] = m68k_9048_sub_w_dn_an;
            m68k_instruction [0x9050 | (reg_a << 9) | reg_b] = m68k_9050_sub_w_dn_an;
            m68k_instruction [0x9068 | (reg_a << 9) | reg_b] = m68k_9068_sub_w_dn_dan;
            m68k_instruction [0x9080 | (reg_a << 9) | reg_b] = m68k_9080_sub_l_dn_dn;
            m68k_instruction [0x9128 | (reg_a << 9) | reg_b] = m68k_9128_sub_b_dan_dn;
        }
        m68k_instruction [0x903b | (reg_a << 9)] = m68k_903b_sub_b_dn_dpcxi;
        m68k_instruction [0x9078 | (reg_a << 9)] = m68k_9078_sub_w_dn_aw;
        m68k_instruction [0x90b8 | (reg_a << 9)] = m68k_90b8_sub_l_dn_aw;
    }

    /* cmp */
    for (uint16_t reg = 0; reg < 8; reg++)
    {
        for (uint16_t ea = 0; ea < 8; ea++)
        {
            m68k_instruction [0xb000 | (reg << 9) | ea] = m68k_b000_cmp_b_dn_dn;
            m68k_instruction [0xb010 | (reg << 9) | ea] = m68k_b010_cmp_b_dn_an;
            m68k_instruction [0xb028 | (reg << 9) | ea] = m68k_b028_cmp_b_dn_dan;
            m68k_instruction [0xb040 | (reg << 9) | ea] = m68k_b040_cmp_w_dn_dn;
            m68k_instruction [0xb050 | (reg << 9) | ea] = m68k_b050_cmp_w_dn_an;
            m68k_instruction [0xb068 | (reg << 9) | ea] = m68k_b068_cmp_w_dn_dan;
            m68k_instruction [0xb088 | (reg << 9) | ea] = m68k_b088_cmp_l_dn_an;
        }
        m68k_instruction [0xb038 | (reg << 9)] = m68k_b038_cmp_b_dn_aw;
        m68k_instruction [0xb078 | (reg << 9)] = m68k_b078_cmp_w_dn_aw;
    }

    /* eor */
    for (uint16_t reg_a = 0; reg_a < 8; reg_a++)
    {
        for (uint16_t reg_b = 0; reg_b < 8; reg_b++)
        {
            m68k_instruction [0xb100 | (reg_a << 9) | reg_b] = m68k_b100_eor_b_dn_dn;
            m68k_instruction [0xb140 | (reg_a << 9) | reg_b] = m68k_b140_eor_w_dn_dn;
            m68k_instruction [0xb180 | (reg_a << 9) | reg_b] = m68k_b180_eor_l_dn_dn;
        }
    }

    /* and */
    for (uint16_t reg_a = 0; reg_a < 8; reg_a++)
    {
        for (uint16_t reg_b = 0; reg_b < 8; reg_b++)
        {
            m68k_instruction [0xc000 | (reg_a << 9) | reg_b] = m68k_c000_and_b_dn_dn;
            m68k_instruction [0xc040 | (reg_a << 9) | reg_b] = m68k_c040_and_w_dn_dn;
            m68k_instruction [0xc080 | (reg_a << 9) | reg_b] = m68k_c080_and_l_dn_dn;
        }
        m68k_instruction [0xc07b | (reg_a << 9)] = m68k_c07b_and_w_dn_dpcxi;
    }

    /* exg */
    for (uint16_t reg_x = 0; reg_x < 8; reg_x++)
    {
        for (uint16_t reg_y = 0; reg_y < 8; reg_y++)
        {
            m68k_instruction [0xc140 | (reg_x << 9) | reg_y] = m68k_c140_exg_l_dn_dn;
        }
    }

    /* muls */
    for (uint16_t reg = 0; reg < 8; reg++)
    {
        for (uint16_t ea = 0; ea < 8; ea++)
        {
            m68k_instruction [0xc1c0 | (reg << 9) | ea] = m68k_c1c0_muls_w_dn_dn;
            m68k_instruction [0xc1e8 | (reg << 9) | ea] = m68k_c1e8_muls_w_dn_dan;
        }
        m68k_instruction [0xc1fc | (reg << 9)] = m68k_c1fc_muls_w_dn_imm;
    }

    /* add */
    for (uint16_t reg = 0; reg < 8; reg++)
    {
        for (uint16_t ea = 0; ea < 8; ea++)
        {
            m68k_instruction [0xd000 | (reg << 9) | ea] = m68k_d000_add_b_dn_dn;
            m68k_instruction [0xd028 | (reg << 9) | ea] = m68k_d028_add_b_dn_dan;
            m68k_instruction [0xd040 | (reg << 9) | ea] = m68k_d040_add_w_dn_dn;
            m68k_instruction [0xd048 | (reg << 9) | ea] = m68k_d048_add_w_dn_an;
            m68k_instruction [0xd050 | (reg << 9) | ea] = m68k_d050_add_w_dn_an;
            m68k_instruction [0xd058 | (reg << 9) | ea] = m68k_d058_add_w_dn_anp;
            m68k_instruction [0xd068 | (reg << 9) | ea] = m68k_d068_add_w_dn_dan;
            m68k_instruction [0xd080 | (reg << 9) | ea] = m68k_d080_add_l_dn_dn;
            m68k_instruction [0xd088 | (reg << 9) | ea] = m68k_d088_add_l_dn_an;
            m68k_instruction [0xd0c0 | (reg << 9) | ea] = m68k_d0c0_adda_w_an_dn;
            m68k_instruction [0xd0c8 | (reg << 9) | ea] = m68k_d0c8_adda_w_an_an;
            m68k_instruction [0xd0d0 | (reg << 9) | ea] = m68k_d0d0_adda_w_an_an;
            m68k_instruction [0xd0f0 | (reg << 9) | ea] = m68k_d0f0_adda_w_an_danxi;
            m68k_instruction [0xd128 | (reg << 9) | ea] = m68k_d128_add_b_dan_dn;
            m68k_instruction [0xd168 | (reg << 9) | ea] = m68k_d168_add_w_dan_dn;
            m68k_instruction [0xd1c0 | (reg << 9) | ea] = m68k_d1c0_adda_l_an_dn;
        }
        m68k_instruction [0xd038 | (reg << 9)] = m68k_d038_add_b_dn_aw;
        m68k_instruction [0xd078 | (reg << 9)] = m68k_d078_add_w_dn_aw;
        m68k_instruction [0xd0b8 | (reg << 9)] = m68k_d0b8_add_l_dn_aw;
        m68k_instruction [0xd0fc | (reg << 9)] = m68k_d0fc_adda_w_an_imm;
        m68k_instruction [0xd1b8 | (reg << 9)] = m68k_d1b8_add_l_aw_dn;
    }

    /* shift */
    for (uint16_t reg = 0; reg < 8; reg++)
    {
        for (uint16_t count = 0; count < 8; count++)
        {
            m68k_instruction [0xe008 | (count << 9) | reg] = m68k_e008_lsr_b_dn_imm;
            m68k_instruction [0xe040 | (count << 9) | reg] = m68k_e040_asr_w_dn_imm;
            m68k_instruction [0xe048 | (count << 9) | reg] = m68k_e048_lsr_w_dn_imm;
            m68k_instruction [0xe058 | (count << 9) | reg] = m68k_e058_ror_w_dn_imm;
            m68k_instruction [0xe068 | (count << 9) | reg] = m68k_e068_lsr_w_dn_dn;
            m68k_instruction [0xe080 | (count << 9) | reg] = m68k_e080_asr_l_dn_imm;
            m68k_instruction [0xe098 | (count << 9) | reg] = m68k_e098_ror_l_dn_imm;
            m68k_instruction [0xe108 | (count << 9) | reg] = m68k_e108_lsl_b_dn_imm;
            m68k_instruction [0xe118 | (count << 9) | reg] = m68k_e118_rol_b_dn_imm;
            m68k_instruction [0xe138 | (count << 9) | reg] = m68k_e138_rol_b_dn_dn;
            m68k_instruction [0xe140 | (count << 9) | reg] = m68k_e140_asl_w_dn_imm;
            m68k_instruction [0xe148 | (count << 9) | reg] = m68k_e148_lsl_w_dn_imm;
            m68k_instruction [0xe150 | (count << 9) | reg] = m68k_e150_roxl_w_dn_imm;
            m68k_instruction [0xe158 | (count << 9) | reg] = m68k_e158_rol_w_dn_imm;
            m68k_instruction [0xe168 | (count << 9) | reg] = m68k_e168_lsl_w_dn_dn;
            m68k_instruction [0xe180 | (count << 9) | reg] = m68k_e180_asl_l_dn_imm;
            m68k_instruction [0xe188 | (count << 9) | reg] = m68k_e188_lsl_l_dn_imm;
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
    static uint32_t count = 0;
    count++;
    printf ("[%06x (%.4d)]: ", context->state.pc, count);

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
        snepulator_error ("M68000 Error", "Unknown %s instruction: %04x.",
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

        uint8_t interrupt = context->get_int (context->parent);
        if (interrupt > context->state.sr_interrupt_priority)
        {
            uint16_t sr_was = context->state.sr;

            context->state.sr_supervisor = 1;
            context->state.sr_trace = 0;
            context->state.sr_interrupt_priority = interrupt;

            /* Push the current PC to the stack */
            /* TODO: Consider inline helpers for push/pop operations */
            context->state.a [7] -= 4;
            write_long (context, context->state.a [7], context->state.pc);

            /* Save status */
            context->state.a [7] -= 2;
            write_word (context, context->state.a [7], sr_was);

            /* Update PC from vector table */
            context->state.pc    = read_long (context, (24 + interrupt) << 2);

            context->clock_cycles += 10; /* TODO - Work out the real timing */

            /* TODO: Investigate behaviour of interrupt acknowledgement. Right
             *       now, the VDP implementation just treats get_interrupt as
             *       clear-on-read. */
        }
        else
        {
            context->clock_cycles -= m68k_run_instruction (context);
        }
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
    context->state.sr    = 0x2700;
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

    static bool first = true;
    if (first)
    {
        /* Once-off initialisations */
        first = false;
        m68k_init_instructions ();
    }

    return context;
}
