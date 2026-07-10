/*
 * Snepulator
 * Motorola 68000 implementation
 *
 * TODO:
 *  - Instruction timing
 *  - Prefetch
 */

#include <stdlib.h>
#include <stdio.h>

#include "../snepulator.h"
#include "../util.h"
#include "m68k.h"

#define SR_MASK 0xa71f

static uint32_t (*m68k_instruction [SIZE_64K]) (M68000_Context *, uint16_t) = { };


/*
 * Save the current stack-pointer from state.a [7] into either state.ssp
 * or state.usp. This is done before altering the supervisor bit.
 */
static inline void m68k_store_stack_pointer (M68000_Context *context)
{
    if (context->state.sr_supervisor)
    {
        context->state.ssp = context->state.a [7];
    }
    else
    {
        context->state.usp = context->state.a [7];
    }
}


/*
 * Load the current stack-pointer from either state.ssp or state.usp
 * into state.a [7]. This is done after altering the supervisor bit.
 */
static inline void m68k_load_stack_pointer (M68000_Context *context)
{
    context->state.a [7] = (context->state.sr_supervisor) ? context->state.ssp
                                                          : context->state.usp;
}


/*
 * Read an 8-bit byte from memory.
 */
static inline uint8_t read_byte (M68000_Context *context, uint32_t address)
{
    return context->memory_read_8 (context->parent, address & 0x00ffffff);
}


/*
 * Read a 16-bit word from memory, converting it to little-endian.
 */
static inline uint16_t read_word (M68000_Context *context, uint32_t address)
{
    return context->memory_read_16 (context->parent, address & 0x00ffffff);
}


/*
 * Read a 32-bit dword from memory, converting it to little-endian.
 */
static inline uint32_t read_long (M68000_Context *context, uint32_t address)
{
    uint32_split_t value;
    value.w_high = read_word (context, address);
    value.w_low  = read_word (context, address + 2);
    return value.l;
}


/*
 * Write an 8-bit byte from memory.
 */
static inline void write_byte (M68000_Context *context, uint32_t address, uint8_t data)
{
    context->memory_write_8 (context->parent, address & 0x00ffffff, data);
}


/*
 * Write a 16-bit word from memory, converting it to little-endian.
 */
static inline void write_word (M68000_Context *context, uint32_t address, uint16_t data)
{
    context->memory_write_16 (context->parent, address & 0x00ffffff, data);
}


/*
 * Write a 32-bit dword from memory, converting it to little-endian.
 */
static inline void write_long (M68000_Context *context, uint32_t address, uint32_t data)
{
    write_word (context, address,     data >> 16);
    write_word (context, address + 2, data & 0xffff);
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
    uint32_split_t address;
    address.w_high = read_extension (context);
    address.w_low  = read_extension (context);
    return address.l;
}


/*
 * Calculate the address using the displacement in a 16-bit extension.
 *
 * Address is the sum of:
 *  -> An, passed here as a parameter
 *  -> Displacement, from the extension
 */
static inline uint32_t address_with_displacement (M68000_Context *context, uint32_t address)
{
    int16_t displacement = read_extension (context);
    return address + displacement;
}


/*
 * Read a byte with indexing from the immediate 16-bit extension.
 */
static inline uint8_t read_byte_with_displacement (M68000_Context *context, uint32_t address)
{
    return read_byte (context, address_with_displacement (context, address));
}


/*
 * Write a byte with indexing from the immediate 16-bit extension.
 */
static inline void write_byte_with_displacement (M68000_Context *context, uint32_t address, uint8_t value)
{
    write_byte (context, address_with_displacement (context, address), value);
}


/*
 * Read a word with indexing from the immediate 16-bit extension.
 */
static inline uint16_t read_word_with_displacement (M68000_Context *context, uint32_t address)
{
    return read_word (context, address_with_displacement (context, address));
}


/*
 * Write a word with indexing from the immediate 16-bit extension.
 */
static inline void write_word_with_displacement (M68000_Context *context, uint32_t address, uint16_t value)
{
    write_word (context, address_with_displacement (context, address), value);
}


/*
 * Read a long with indexing from the immediate 16-bit extension.
 */
static inline uint32_t read_long_with_displacement (M68000_Context *context, uint32_t address)
{
    return read_long (context, address_with_displacement (context, address));
}


/*
 * Write a long with indexing from the immediate 16-bit extension.
 */
static inline void write_long_with_displacement (M68000_Context *context, uint32_t address, uint32_t value)
{
    write_long (context, address_with_displacement (context, address), value);
}


/*
 * Calculate the address using displacement+index in 16-bit extension.
 *
 * Address is the sum of:
 *  -> An, passed here as a parameter
 *  -> Displacement, from the low byte of the extension
 *  -> Index register, An or Dn register described by the high byte of the extension.
 */
static inline uint32_t address_with_index (M68000_Context *context, uint32_t address)
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

    return address + displacement + index;
}


/*
 * Read a byte with indexing from the immediate 16-bit extension.
 */
static inline uint8_t read_byte_with_index (M68000_Context *context, uint32_t address)
{
    return read_byte (context, address_with_index (context, address));
}


/*
 * Write a byte with indexing from the immediate 16-bit extension.
 */
static inline void write_byte_with_index (M68000_Context *context, uint32_t address, uint8_t value)
{
    write_byte (context, address_with_index (context, address), value);
}


/*
 * Read a word with indexing from the immediate 16-bit extension.
 */
static inline uint16_t read_word_with_index (M68000_Context *context, uint32_t address)
{
    return read_word (context, address_with_index (context, address));
}


/*
 * Write a word with indexing from the immediate 16-bit extension.
 */
static inline void write_word_with_index (M68000_Context *context, uint32_t address, uint16_t value)
{
    write_word (context, address_with_index (context, address), value);
}


/*
 * Read a long with indexing from the immediate 16-bit extension.
 */
static inline uint32_t read_long_with_index (M68000_Context *context, uint32_t address)
{
    return read_long (context, address_with_index (context, address));
}


/*
 * Write a long with indexing from the immediate 16-bit extension.
 */
static inline void write_long_with_index (M68000_Context *context, uint32_t address, uint32_t value)
{
    write_long (context, address_with_index (context, address), value);
}


/*
 * Read a byte from the immediate 16-bit address.
 */
static inline uint8_t read_byte_aw (M68000_Context *context)
{
    uint32_t address = (int16_t) read_extension (context);
    return read_byte (context, address);
}


/*
 * Write a byte to the immediate 16-bit address.
 */
static inline void write_byte_aw (M68000_Context *context, uint8_t value)
{
    uint32_t address = (int16_t) read_extension (context);
    write_byte (context, address, value);
}


/*
 * Read a word from the immediate 16-bit address.
 */
static inline uint16_t read_word_aw (M68000_Context *context)
{
    uint32_t address = (int16_t) read_extension (context);
    return read_word (context, address);
}


/*
 * Write a word from the immediate 16-bit address.
 */
static inline void write_word_aw (M68000_Context *context, uint16_t value)
{
    uint32_t address = (int16_t) read_extension (context);
    write_word (context, address, value);
}


/*
 * Read a long from the immediate 16-bit address.
 */
static inline uint32_t read_long_aw (M68000_Context *context)
{
    uint32_t address = (int16_t) read_extension (context);
    return read_long (context, address);
}


/*
 * Write a long from the immediate 16-bit address.
 */
static inline void write_long_aw (M68000_Context *context, uint32_t value)
{
    uint32_t address = (int16_t) read_extension (context);
    write_long (context, address, value);
}


/*
 * Read a byte from the immediate 32-bit address.
 */
static inline uint8_t read_byte_al (M68000_Context *context)
{
    uint32_t address = read_extension_long (context);
    return read_byte (context, address);
}


/*
 * Write a byte from the immediate 16-bit address.
 */
static inline void write_byte_al (M68000_Context *context, uint8_t value)
{
    uint32_t address = read_extension_long (context);
    write_byte (context, address, value);
}


/*
 * Read a word from the immediate 32-bit address.
 */
static inline uint16_t read_word_al (M68000_Context *context)
{
    uint32_t address = read_extension_long (context);
    return read_word (context, address);
}


/*
 * Write a word from the immediate 16-bit address.
 */
static inline void write_word_al (M68000_Context *context, uint16_t value)
{
    uint32_t address = read_extension_long (context);
    write_word (context, address, value);
}


/*
 * Read a long from the immediate 32-bit address.
 */
static inline uint32_t read_long_al (M68000_Context *context)
{
    uint32_t address = read_extension_long (context);
    return read_long (context, address);
}


/*
 * Write a long from the immediate 16-bit address.
 */
static inline void write_long_al (M68000_Context *context, uint32_t value)
{
    uint32_t address = read_extension_long (context);
    write_long (context, address, value);
}


/*
 * Handle an m68k exception.
 */
static inline void m68k_exception (M68000_Context *context, uint32_t vector)
{
    uint16_t sr_was = context->state.sr;

    m68k_store_stack_pointer (context);
    context->state.sr_supervisor = 1;
    m68k_load_stack_pointer (context);
    context->state.sr_trace = 0;

    /* Push the current PC to the stack */
    context->state.a [7] -= 4;
    write_long (context, context->state.a [7], context->state.pc);

    /* Push the status register to the stack */
    context->state.a [7] -= 2;
    write_word (context, context->state.a [7], sr_was);

    /* Update PC from vector table */
    context->state.pc = read_long (context, vector);

    context->clock_cycles -= 10; /* TODO - Work out the real timing */
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

    return 0;
}


/* ori.b d(An) ← d(An) | #xxxx */
static uint32_t m68k_0028_ori_b_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint8_t imm = read_extension (context);
    uint32_t address = address_with_displacement (context, context->state.a [reg]);

    uint8_t result = read_byte (context, address) | imm;
    write_byte (context, address, result);
    m68k_move_b_flags (context, result);

    return 0;
}


/* ori.b (xxx.w) ← (xxx.w) | #xxxx */
static uint32_t m68k_0038_ori_b_aw (M68000_Context *context, uint16_t instruction)
{
    uint8_t imm = read_extension (context);
    uint32_t address = (int16_t) read_extension (context);

    uint8_t result = read_byte (context, address) | imm;
    write_byte (context, address, result);
    m68k_move_b_flags (context, result);

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

    return 0;
}


/* bclr.b (An) [Dn] */
static uint32_t m68k_0190_bclr_b_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t data_reg = instruction & 0x07;
    uint16_t bit_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte (context, context->state.a [data_reg]);
    uint32_t bit = context->state.d [bit_reg].l & 0x07;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    value &= ~(1 << bit);
    write_byte (context, context->state.a [data_reg], value);

    return 0;
}


/* bclr.b (An+) [Dn] */
static uint32_t m68k_0198_bclr_b_anp_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t data_reg = instruction & 0x07;
    uint16_t bit_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte (context, context->state.a [data_reg]);
    uint32_t bit = context->state.d [bit_reg].l & 0x07;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    value &= ~(1 << bit);
    write_byte (context, context->state.a [data_reg], value);
    context->state.a [data_reg] += (data_reg == 7) ? 2 : 1;

    return 0;
}


/* bclr.b (-An) [Dn] */
static uint32_t m68k_01a0_bclr_b_pan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t data_reg = instruction & 0x07;
    uint16_t bit_reg = (instruction >> 9) & 0x07;

    context->state.a [data_reg] -= (data_reg == 7) ? 2 : 1;
    uint8_t value = read_byte (context, context->state.a [data_reg]);
    uint32_t bit = context->state.d [bit_reg].l & 0x07;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    value &= ~(1 << bit);
    write_byte (context, context->state.a [data_reg], value);

    return 0;
}


/* bclr.b d(An) [Dn] */
static uint32_t m68k_01a8_bclr_b_dan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t data_reg = instruction & 0x07;
    uint16_t bit_reg = (instruction >> 9) & 0x07;
    uint32_t address = address_with_displacement (context, context->state.a [data_reg]);

    uint8_t value = read_byte (context, address);
    uint32_t bit = context->state.d [bit_reg].l & 0x07;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    value &= ~(1 << bit);
    write_byte (context, address, value);

    return 0;
}


/* bclr.b d(An+Xi) [Dn] */
static uint32_t m68k_01b0_bclr_b_danxi_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t data_reg = instruction & 0x07;
    uint16_t bit_reg = (instruction >> 9) & 0x07;
    uint32_t address = address_with_index (context, context->state.a [data_reg]);

    uint8_t value = read_byte (context, address);
    uint32_t bit = context->state.d [bit_reg].l & 0x07;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    value &= ~(1 << bit);
    write_byte (context, address, value);

    return 0;
}


/* bclr.b (xxx.w) [Dn] */
static uint32_t m68k_01b8_bclr_b_aw_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t bit_reg = (instruction >> 9) & 0x07;
    uint32_t address = (int16_t) read_extension (context);

    uint8_t value = read_byte (context, address);
    uint32_t bit = context->state.d [bit_reg].l & 0x07;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    value &= ~(1 << bit);
    write_byte (context, address, value);

    return 0;
}


/* bclr.b (xxx.l) [Dn] */
static uint32_t m68k_01b9_bclr_b_al_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t bit_reg = (instruction >> 9) & 0x07;
    uint32_t address = read_extension_long (context);

    uint8_t value = read_byte (context, address);
    uint32_t bit = context->state.d [bit_reg].l & 0x07;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    value &= ~(1 << bit);
    write_byte (context, address, value);

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

    return 0;
}


/* bset.b (An) [Dn] */
static uint32_t m68k_01d0_bset_b_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t data_reg = instruction & 0x07;
    uint16_t bit_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte (context, context->state.a [data_reg]);
    uint32_t bit = context->state.d [bit_reg].l & 0x07;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    value |= 1 << bit;
    write_byte (context, context->state.a [data_reg], value);

    return 0;
}


/* bset.b (An+) [Dn] */
static uint32_t m68k_01d8_bset_b_anp_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t data_reg = instruction & 0x07;
    uint16_t bit_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte (context, context->state.a [data_reg]);
    uint32_t bit = context->state.d [bit_reg].l & 0x07;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    value |= 1 << bit;
    write_byte (context, context->state.a [data_reg], value);
    context->state.a [data_reg] += (data_reg == 7) ? 2 : 1;

    return 0;
}


/* bset.b (-An) [Dn] */
static uint32_t m68k_01e0_bset_b_pan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t data_reg = instruction & 0x07;
    uint16_t bit_reg = (instruction >> 9) & 0x07;

    context->state.a [data_reg] -= (data_reg == 7) ? 2 : 1;
    uint8_t value = read_byte (context, context->state.a [data_reg]);
    uint32_t bit = context->state.d [bit_reg].l & 0x07;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    value |= 1 << bit;
    write_byte (context, context->state.a [data_reg], value);

    return 0;
}


/* bset.b d(An) [Dn] */
static uint32_t m68k_01e8_bset_b_dan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t data_reg = instruction & 0x07;
    uint16_t bit_reg = (instruction >> 9) & 0x07;

    uint32_t address = address_with_displacement (context, context->state.a [data_reg]);

    uint8_t value = read_byte (context, address);
    uint32_t bit = context->state.d [bit_reg].l & 0x07;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    value |= 1 << bit;
    write_byte (context, address, value);

    return 0;
}


/* bset.b d(An+Xi) [Dn] */
static uint32_t m68k_01f0_bset_b_danxi_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t data_reg = instruction & 0x07;
    uint16_t bit_reg = (instruction >> 9) & 0x07;

    uint32_t address = address_with_index (context, context->state.a [data_reg]);

    uint8_t value = read_byte (context, address);
    uint32_t bit = context->state.d [bit_reg].l & 0x07;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    value |= 1 << bit;
    write_byte (context, address, value);

    return 0;
}


/* bset.b (xxx.w) [Dn] */
static uint32_t m68k_01f8_bset_b_aw_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t bit_reg = (instruction >> 9) & 0x07;
    uint32_t address = (int16_t) read_extension (context);

    uint8_t value = read_byte (context, address);
    uint32_t bit = context->state.d [bit_reg].l & 0x07;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    value |= 1 << bit;
    write_byte (context, address, value);

    return 0;
}


/* bset.b (xxx.l) [Dn] */
static uint32_t m68k_01f9_bset_b_al_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t bit_reg = (instruction >> 9) & 0x07;
    uint32_t address = read_extension_long (context);

    uint8_t value = read_byte (context, address);
    uint32_t bit = context->state.d [bit_reg].l & 0x07;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    value |= 1 << bit;
    write_byte (context, address, value);

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

    return 0;
}


/* andi.b d(An) ← d(An) & #xx */
static uint32_t m68k_0228_andi_b_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint8_t imm = read_extension (context);
    uint32_t address = address_with_displacement (context, context->state.a [reg]);

    uint8_t result = imm & read_byte (context, address);
    write_byte (context, address, result);
    m68k_move_b_flags (context, result);

    return 0;
}


/* andi.b (xxx.w) ← (xxx.w) & #xx */
static uint32_t m68k_0238_andi_b_aw (M68000_Context *context, uint16_t instruction)
{
    uint8_t imm = read_extension (context);
    uint32_t address = (int16_t) read_extension (context);

    uint8_t result = imm & read_byte (context, address);
    write_byte (context, address, result);
    m68k_move_b_flags (context, result);

    return 0;
}


/* andi.w Dn ← Dn & #xxxx */
static uint32_t m68k_0240_andi_w_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = read_extension (context);
    uint16_t a = context->state.d [reg].w;

    uint16_t result = a & b;
    context->state.d [reg].w = result;
    m68k_move_w_flags (context, result);

    return 0;
}


/* andi.w (An) ← (An) & #xxxx */
static uint32_t m68k_0250_andi_w_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = read_extension (context);
    uint16_t a = read_word (context, context->state.a [reg]);

    uint16_t result = a & b;
    write_word (context, context->state.a [reg], result);
    m68k_move_w_flags (context, result);

    return 0;
}


/* andi.w (An+) ← (An+) & #xxxx */
static uint32_t m68k_0258_andi_w_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = read_extension (context);
    uint16_t a = read_word (context, context->state.a [reg]);

    uint16_t result = a & b;
    write_word (context, context->state.a [reg], result);
    context->state.a [reg] += 2;
    m68k_move_w_flags (context, result);

    return 0;
}


/* andi.w (-An) ← (-An) & #xxxx */
static uint32_t m68k_0260_andi_w_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    context->state.a [reg] -= 2;

    uint16_t b = read_extension (context);
    uint16_t a = read_word (context, context->state.a [reg]);

    uint16_t result = a & b;
    write_word (context, context->state.a [reg], result);
    m68k_move_w_flags (context, result);

    return 0;
}


/* andi.w d(An) ← d(An) & #xxxx */
static uint32_t m68k_0268_andi_w_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = read_extension (context);
    uint32_t address = address_with_displacement (context, context->state.a [reg]);
    uint16_t a = read_word (context, address);

    uint16_t result = a & b;
    write_word (context, address, result);
    m68k_move_w_flags (context, result);

    return 0;
}


/* andi.w d(An+Xi) ← d(An+Xi) & #xxxx */
static uint32_t m68k_0270_andi_w_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = read_extension (context);
    uint32_t address = address_with_index (context, context->state.a [reg]);
    uint16_t a = read_word (context, address);

    uint16_t result = a & b;
    write_word (context, address, result);
    m68k_move_w_flags (context, result);

    return 0;
}


/* andi.w (xxx.w) ← (xxx.w) & #xxxx */
static uint32_t m68k_0278_andi_w_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t b = read_extension (context);
    uint32_t address = (int16_t) read_extension (context);
    uint16_t a = read_word (context, address);

    uint16_t result = a & b;
    write_word (context, address, result);
    m68k_move_w_flags (context, result);

    return 0;
}


/* andi.w (xxx.l) ← (xxx.l) & #xxxx */
static uint32_t m68k_0279_andi_w_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t b = read_extension (context);
    uint32_t address = read_extension_long (context);
    uint16_t a = read_word (context, address);

    uint16_t result = a & b;
    write_word (context, address, result);
    m68k_move_w_flags (context, result);

    return 0;
}


/* andi.l Dn ← Dn & #xxxxxxxx */
static uint32_t m68k_0280_andi_l_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t b = read_extension_long (context);
    uint32_t a = context->state.d [reg].l;

    uint32_t result = a & b;
    context->state.d [reg].l = result;
    m68k_move_l_flags (context, result);

    return 0;
}


/* andi.l (An) ← (An) & #xxxxxxxx */
static uint32_t m68k_0290_andi_l_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t b = read_extension_long (context);
    uint32_t a = read_long (context, context->state.a [reg]);

    uint32_t result = a & b;
    write_long (context, context->state.a [reg], result);
    m68k_move_l_flags (context, result);

    return 0;
}


/* andi.l (An+) ← (An+) & #xxxxxxxx */
static uint32_t m68k_0298_andi_l_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t b = read_extension_long (context);
    uint32_t a = read_long (context, context->state.a [reg]);

    uint32_t result = a & b;
    write_long (context, context->state.a [reg], result);
    context->state.a [reg] += 4;
    m68k_move_l_flags (context, result);

    return 0;
}


/* andi.l (-An) ← (-An) & #xxxxxxxx */
static uint32_t m68k_02a0_andi_l_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t b = read_extension_long (context);
    context->state.a [reg] -= 4;
    uint32_t a = read_long (context, context->state.a [reg]);

    uint32_t result = a & b;
    write_long (context, context->state.a [reg], result);
    m68k_move_l_flags (context, result);

    return 0;
}


/* andi.l d(An) ← d(An) & #xxxxxxxx */
static uint32_t m68k_02a8_andi_l_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t b = read_extension_long (context);
    uint32_t address = address_with_displacement (context, context->state.a [reg]);
    uint32_t a = read_long (context, address);

    uint32_t result = a & b;
    write_long (context, address, result);
    m68k_move_l_flags (context, result);

    return 0;
}


/* andi.l d(An+Xi) ← d(An+Xi) & #xxxxxxxx */
static uint32_t m68k_02b0_andi_l_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t b = read_extension_long (context);
    uint32_t address = address_with_index (context, context->state.a [reg]);
    uint32_t a = read_long (context, address);

    uint32_t result = a & b;
    write_long (context, address, result);
    m68k_move_l_flags (context, result);

    return 0;
}


/* andi.l (xxx.w) ← d(xxx.w) & #xxxxxxxx */
static uint32_t m68k_02b8_andi_l_aw (M68000_Context *context, uint16_t instruction)
{
    uint32_t b = read_extension_long (context);
    uint32_t address = (int16_t) read_extension (context);
    uint32_t a = read_long (context, address);

    uint32_t result = a & b;
    write_long (context, address, result);
    m68k_move_l_flags (context, result);

    return 0;
}


/* andi.l (xxx.l) ← d(xxx.l) & #xxxxxxxx */
static uint32_t m68k_02b9_andi_l_al (M68000_Context *context, uint16_t instruction)
{
    uint32_t b = read_extension_long (context);
    uint32_t address = (int16_t) read_extension (context);
    uint32_t a = read_long (context, address);

    uint32_t result = a & b;
    write_long (context, address, result);
    m68k_move_l_flags (context, result);

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

    return 0;
}

/* subi.b (An) ← (An) - #xx */
static uint32_t m68k_0410_subi_b_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = read_extension (context);
    uint8_t a = read_byte (context, context->state.a [reg]);

    uint8_t result = a - b;
    write_byte (context, context->state.a [reg], result);
    m68k_sub_b_flags (context, a, b, result);

    return 0;
}


/* subi.b (An+) ← (An+) - #xx */
static uint32_t m68k_0418_subi_b_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = read_extension (context);
    uint8_t a = read_byte (context, context->state.a [reg]);

    uint8_t result = a - b;
    write_byte (context, context->state.a [reg], result);
    context->state.a [reg] += (reg == 7) ? 2 : 1;
    m68k_sub_b_flags (context, a, b, result);

    return 0;
}

/* subi.b (-An) ← (-An) - #xx */
static uint32_t m68k_0420_subi_b_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    context->state.a [reg] -= (reg == 7) ? 2 : 1;

    uint8_t b = read_extension (context);
    uint8_t a = read_byte (context, context->state.a [reg]);

    uint8_t result = a - b;
    write_byte (context, context->state.a [reg], result);
    m68k_sub_b_flags (context, a, b, result);

    return 0;
}


/* subi.b d(An) ← d(An) - #xx */
static uint32_t m68k_0428_subi_b_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = read_extension (context);
    uint32_t address = context->state.a [reg] + (int16_t) read_extension (context);
    uint8_t a = read_byte (context, address);

    uint8_t result = a - b;
    write_byte (context, address, result);

    m68k_sub_b_flags (context, a, b, result);

    return 0;
}


/* subi.b d(An+Xi) ← d(An+Xi) - #xx */
static uint32_t m68k_0430_subi_b_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = read_extension (context);
    uint32_t address = address_with_index (context, context->state.a [reg]);
    uint8_t a = read_byte (context, address);

    uint8_t result = a - b;
    write_byte (context, address, result);

    m68k_sub_b_flags (context, a, b, result);

    return 0;
}


/* subi.b (xxx.w) ← (xxx.w) - #xx */
static uint32_t m68k_0438_subi_b_aw (M68000_Context *context, uint16_t instruction)
{
    uint8_t b = read_extension (context);
    uint32_t address = (int16_t) read_extension (context);
    uint8_t a = read_byte (context, address);

    uint8_t result = a - b;
    write_byte (context, address, result);

    m68k_sub_b_flags (context, a, b, result);

    return 0;
}


/* subi.b (xxx.l) ← (xxx.l) - #xx */
static uint32_t m68k_0439_subi_b_al (M68000_Context *context, uint16_t instruction)
{
    uint8_t b = read_extension (context);
    uint32_t address = read_extension_long (context);
    uint8_t a = read_byte (context, address);

    uint8_t result = a - b;
    write_byte (context, address, result);

    m68k_sub_b_flags (context, a, b, result);

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

    return 0;
}


/* subi.w (An) ← (An) - #xxxx */
static uint32_t m68k_0450_subi_w_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = read_extension (context);
    uint16_t a = read_word (context, context->state.a [reg]);

    uint16_t result = a - b;
    write_word (context, context->state.a [reg], result);
    m68k_sub_w_flags (context, a, b, result);

    return 0;
}


/* subi.w (An+) ← (An+) - #xxxx */
static uint32_t m68k_0458_subi_w_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = read_extension (context);
    uint16_t a = read_word (context, context->state.a [reg]);

    uint16_t result = a - b;
    write_word (context, context->state.a [reg], result);
    context->state.a [reg] += 2;
    m68k_sub_w_flags (context, a, b, result);

    return 0;
}


/* subi.w (-An) ← (-An) - #xxxx */
static uint32_t m68k_0460_subi_w_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    context->state.a [reg] -= 2;

    uint16_t b = read_extension (context);
    uint16_t a = read_word (context, context->state.a [reg]);

    uint16_t result = a - b;
    write_word (context, context->state.a [reg], result);
    m68k_sub_w_flags (context, a, b, result);

    return 0;
}


/* subi.w d(An) ← d(An) - #xxxx */
static uint32_t m68k_0468_subi_w_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = read_extension (context);
    uint32_t address = address_with_displacement (context, context->state.a [reg]);
    uint16_t a = read_word (context, address);

    uint16_t result = a - b;
    write_word (context, address, result);
    m68k_sub_w_flags (context, a, b, result);

    return 0;
}


/* subi.w d(An+Xi) ← d(An+Xi) - #xxxx */
static uint32_t m68k_0470_subi_w_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = read_extension (context);
    uint32_t address = address_with_index (context, context->state.a [reg]);
    uint16_t a = read_word (context, address);

    uint16_t result = a - b;
    write_word (context, address, result);

    m68k_sub_w_flags (context, a, b, result);

    return 0;
}


/* subi.w (xxx.w) ← (xxx.w) - #xxxx */
static uint32_t m68k_0478_subi_w_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t b = read_extension (context);
    uint32_t address = (int16_t) read_extension (context);
    uint16_t a = read_word (context, address);

    uint16_t result = a - b;
    write_word (context, address, result);

    m68k_sub_w_flags (context, a, b, result);

    return 0;
}


/* subi.w (xxx.l) ← (xxx.l) - #xxxx */
static uint32_t m68k_0479_subi_w_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t b = read_extension (context);
    uint32_t address = read_extension_long (context);
    uint16_t a = read_word (context, address);

    uint16_t result = a - b;
    write_word (context, address, result);

    m68k_sub_w_flags (context, a, b, result);

    return 0;
}


/* subi.l Dn ← Dn - #xxxxxxxx */
static uint32_t m68k_0480_subi_l_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t b = read_extension_long (context);
    uint32_t a = context->state.d [reg].l;

    uint32_t result = a - b;
    context->state.d [reg].l = result;
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* subi.l (An) ← (An) - #xxxxxxxx */
static uint32_t m68k_0490_subi_l_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t b = read_extension_long (context);
    uint32_t a = read_long (context, context->state.a [reg]);

    uint32_t result = a - b;
    write_long (context, context->state.a [reg], result);
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* subi.l (An+) ← (An+) - #xxxxxxxx */
static uint32_t m68k_0498_subi_l_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t b = read_extension_long (context);
    uint32_t a = read_long (context, context->state.a [reg]);

    uint32_t result = a - b;
    write_long (context, context->state.a [reg], result);
    context->state.a [reg] += 4;
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* subi.l (-An) ← (-An) - #xxxxxxxx */
static uint32_t m68k_04a0_subi_l_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    context->state.a [reg] -= 4;

    uint32_t b = read_extension_long (context);
    uint32_t a = read_long (context, context->state.a [reg]);

    uint32_t result = a - b;
    write_long (context, context->state.a [reg], result);
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* subi.l d(An) ← d(An) - #xxxxxxxx */
static uint32_t m68k_04a8_subi_l_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t b = read_extension_long (context);
    uint32_t address = address_with_displacement (context, context->state.a [reg]);
    uint32_t a = read_long (context, address);

    uint32_t result = a - b;
    write_long (context, address, result);
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* subi.l d(An+Xi) ← d(An+Xi) - #xxxxxxxx */
static uint32_t m68k_04b0_subi_l_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t b = read_extension_long (context);
    uint32_t address = address_with_index (context, context->state.a [reg]);
    uint32_t a = read_long (context, address);

    uint32_t result = a - b;
    write_long (context, address, result);
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* subi.l (xxx.w) ← (xxx.w) - #xxxxxxxx */
static uint32_t m68k_04b8_subi_l_aw (M68000_Context *context, uint16_t instruction)
{
    uint32_t b = read_extension_long (context);
    uint32_t address = (int16_t) read_extension (context);
    uint32_t a = read_long (context, address);

    uint32_t result = a - b;
    write_long (context, address, result);
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* subi.l (xxx.l) ← (xxx.l) - #xxxxxxxx */
static uint32_t m68k_04b9_subi_l_al (M68000_Context *context, uint16_t instruction)
{
    uint32_t b = read_extension_long (context);
    uint32_t address = read_extension_long (context);
    uint32_t a = read_long (context, address);

    uint32_t result = a - b;
    write_long (context, address, result);
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* addi.b Dn ← Dn + #xx */
static uint32_t m68k_0600_addi_b_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = read_extension (context);
    uint8_t a = context->state.d [reg].b;

    uint8_t result = a + b;
    context->state.d [reg].b = result;
    m68k_add_b_flags (context, a, b, result);

    return 0;
}


/* addi.b (An) ← (An) + #xx */
static uint32_t m68k_0610_addi_b_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = read_extension (context);
    uint8_t a = read_byte (context, context->state.a [reg]);

    uint8_t result = a + b;
    write_byte (context, context->state.a [reg], result);
    m68k_add_b_flags (context, a, b, result);

    return 0;
}


/* addi.b (An+) ← (An+) + #xx */
static uint32_t m68k_0618_addi_b_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = read_extension (context);
    uint8_t a = read_byte (context, context->state.a [reg]);

    uint8_t result = a + b;
    write_byte (context, context->state.a [reg], result);
    context->state.a [reg] += (reg == 7) ? 2 : 1;

    m68k_add_b_flags (context, a, b, result);

    return 0;
}


/* addi.b (-An) ← (-An) + #xx */
static uint32_t m68k_0620_addi_b_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    context->state.a [reg] -= (reg == 7) ? 2 : 1;

    uint8_t b = read_extension (context);
    uint8_t a = read_byte (context, context->state.a [reg]);

    uint8_t result = a + b;
    write_byte (context, context->state.a [reg], result);

    m68k_add_b_flags (context, a, b, result);

    return 0;
}


/* addi.b d(An) ← d(An) + #xx */
static uint32_t m68k_0628_addi_b_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = read_extension (context);
    uint32_t address = context->state.a [reg] + (int16_t) read_extension (context);
    uint8_t a = read_byte (context, address);

    uint8_t result = a + b;
    write_byte (context, address, result);

    m68k_add_b_flags (context, a, b, result);

    return 0;
}


/* addi.b d(An+Xi) ← d(An+Xi) + #xx */
static uint32_t m68k_0630_addi_b_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = read_extension (context);
    uint32_t address = address_with_index (context, context->state.a [reg]);
    uint8_t a = read_byte (context, address);

    uint8_t result = a + b;
    write_byte (context, address, result);

    m68k_add_b_flags (context, a, b, result);

    return 0;
}


/* addi.b (xxx.w) ← (xxx.w) + #xx */
static uint32_t m68k_0638_addi_b_aw (M68000_Context *context, uint16_t instruction)
{
    uint8_t b = read_extension (context);
    uint32_t address = (int16_t) read_extension (context);
    uint8_t a = read_byte (context, address);

    uint8_t result = a + b;
    write_byte (context, address, result);

    m68k_add_b_flags (context, a, b, result);

    return 0;
}


/* addi.b (xxx.l) ← (xxx.l) + #xx */
static uint32_t m68k_0639_addi_b_al (M68000_Context *context, uint16_t instruction)
{
    uint8_t b = read_extension (context);
    uint32_t address = read_extension_long (context);
    uint8_t a = read_byte (context, address);

    uint8_t result = a + b;
    write_byte (context, address, result);

    m68k_add_b_flags (context, a, b, result);

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

    return 0;
}


/* addi.w (An) ← (An) + #xxxx */
static uint32_t m68k_0650_addi_w_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = read_extension (context);
    uint16_t a = read_word (context, context->state.a [reg]);

    uint16_t result = a + b;
    write_word (context, context->state.a [reg], result);
    m68k_add_w_flags (context, a, b, result);

    return 0;
}


/* addi.w (An+) ← (An+) + #xxxx */
static uint32_t m68k_0658_addi_w_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = read_extension (context);
    uint16_t a = read_word (context, context->state.a [reg]);

    uint16_t result = a + b;
    write_word (context, context->state.a [reg], result);
    context->state.a [reg] += 2;
    m68k_add_w_flags (context, a, b, result);

    return 0;
}


/* addi.w (-An) ← (-An) + #xxxx */
static uint32_t m68k_0660_addi_w_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    context->state.a [reg] -= 2;

    uint16_t b = read_extension (context);
    uint16_t a = read_word (context, context->state.a [reg]);

    uint16_t result = a + b;
    write_word (context, context->state.a [reg], result);
    m68k_add_w_flags (context, a, b, result);

    return 0;
}


/* addi.w d(An) ← d(An) + #xxxx */
static uint32_t m68k_0668_addi_w_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = read_extension (context);
    uint32_t address = address_with_displacement (context, context->state.a [reg]);
    uint16_t a = read_word (context, address);

    uint16_t result = a + b;
    write_word (context, address, result);
    m68k_add_w_flags (context, a, b, result);

    return 0;
}


/* addi.w d(An+Xi) ← d(An+Xi) + #xxxx */
static uint32_t m68k_0670_addi_w_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = read_extension (context);
    uint32_t address = address_with_index (context, context->state.a [reg]);
    uint16_t a = read_word (context, address);

    uint16_t result = a + b;
    write_word (context, address, result);
    m68k_add_w_flags (context, a, b, result);

    return 0;
}


/* addi.w (xxx.w) ← (xxx.w) + #xxxx */
static uint32_t m68k_0678_addi_w_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t b = read_extension (context);
    uint32_t address = (int16_t) read_extension (context);
    uint16_t a = read_word (context, address);

    uint16_t result = a + b;
    write_word (context, address, result);
    m68k_add_w_flags (context, a, b, result);

    return 0;
}


/* addi.w (xxx.l) ← (xxx.l) + #xxxx */
static uint32_t m68k_0679_addi_w_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t b = read_extension (context);
    int32_t address = read_extension_long (context);
    uint16_t a = read_word (context, address);

    uint16_t result = a + b;
    write_word (context, address, result);
    m68k_add_w_flags (context, a, b, result);

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

    return 0;
}


/* addi.l (An) ← (An) + #xxxxxxxx */
static uint32_t m68k_0690_addi_l_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t b = read_extension_long (context);
    uint32_t a = read_long (context, context->state.a [reg]);

    uint32_t result = a + b;
    write_long (context, context->state.a [reg], result);
    m68k_add_l_flags (context, a, b, result);

    return 0;
}


/* addi.l (An+) ← (An+) + #xxxxxxxx */
static uint32_t m68k_0698_addi_l_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t b = read_extension_long (context);
    uint32_t a = read_long (context, context->state.a [reg]);

    uint32_t result = a + b;
    write_long (context, context->state.a [reg], result);
    context->state.a [reg] += 4;
    m68k_add_l_flags (context, a, b, result);

    return 0;
}


/* addi.l (-An) ← (-An) + #xxxxxxxx */
static uint32_t m68k_06a0_addi_l_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t b = read_extension_long (context);
    context->state.a [reg] -= 4;
    uint32_t a = read_long (context, context->state.a [reg]);

    uint32_t result = a + b;
    write_long (context, context->state.a [reg], result);
    m68k_add_l_flags (context, a, b, result);

    return 0;
}


/* addi.l d(An) ← d(An) + #xxxxxxxx */
static uint32_t m68k_06a8_addi_l_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t b = read_extension_long (context);
    uint32_t address = address_with_displacement (context, context->state.a [reg]);
    uint32_t a = read_long (context, address);

    uint32_t result = a + b;
    write_long (context, address, result);
    m68k_add_l_flags (context, a, b, result);

    return 0;
}


/* addi.l d(An+Xi) ← d(An+Xi) + #xxxxxxxx */
static uint32_t m68k_06b0_addi_l_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t b = read_extension_long (context);
    uint32_t address = address_with_index (context, context->state.a [reg]);
    uint32_t a = read_long (context, address);

    uint32_t result = a + b;
    write_long (context, address, result);
    m68k_add_l_flags (context, a, b, result);

    return 0;
}


/* addi.l (xxx.w) ← (xxx.w) + #xxxxxxxx */
static uint32_t m68k_06b8_addi_l_aw (M68000_Context *context, uint16_t instruction)
{
    uint32_t b = read_extension_long (context);
    uint32_t address = (int16_t) read_extension (context);
    uint32_t a = read_long (context, address);

    uint32_t result = a + b;
    write_long (context, address, result);
    m68k_add_l_flags (context, a, b, result);

    return 0;
}


/* addi.l (xxx.l) ← (xxx.l) + #xxxxxxxx */
static uint32_t m68k_06b9_addi_l_al (M68000_Context *context, uint16_t instruction)
{
    uint32_t b = read_extension_long (context);
    uint32_t address = read_extension_long (context);
    uint32_t a = read_long (context, address);

    uint32_t result = a + b;
    write_long (context, address, result);
    m68k_add_l_flags (context, a, b, result);

    return 0;
}


/* btst.l Dn [#xx] */
static uint32_t m68k_0800_btst_l_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t data_reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x1f;
    uint32_t value = context->state.d [data_reg].l;

    context->state.ccr_zero = !((value >> bit) & 0x01);

    return 0;
}


/* btst.b (An) [#xx] */
static uint32_t m68k_0810_btst_b_an_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t data_reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x07;
    uint8_t value = read_byte (context, context->state.a [data_reg]);

    context->state.ccr_zero = !((value >> bit) & 0x01);

    return 0;
}


/* btst.b d(An) [#xx] */
static uint32_t m68k_0828_btst_b_dan_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t data_reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x07;
    uint8_t value = read_byte_with_displacement (context, context->state.a [data_reg]);

    context->state.ccr_zero = !((value >> bit) & 0x01);

    return 0;
}


/* btst.b d(An+Xi) [#xx] */
static uint32_t m68k_0830_btst_b_danxi_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t data_reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x07;
    uint8_t value = read_byte_with_index (context, context->state.a [data_reg]);

    context->state.ccr_zero = !((value >> bit) & 0x01);

    return 0;
}


/* btst.b (xxx.w) [#xx] */
static uint32_t m68k_0838_btst_b_aw_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t bit = read_extension (context) & 0x07;
    uint8_t value = read_byte_aw (context);

    context->state.ccr_zero = !((value >> bit) & 0x01);

    return 0;
}


/* btst.b (xxx.l) [#xx] */
static uint32_t m68k_0839_btst_b_al_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t bit = read_extension (context) & 0x07;
    uint8_t value = read_byte_al (context);

    context->state.ccr_zero = !((value >> bit) & 0x01);

    return 0;
}


/* bchg.l Dn [#xx] */
static uint32_t m68k_0840_bchg_l_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x1f;

    uint32_t value = context->state.d [reg].l;
    context->state.ccr_zero = !((value >> bit) & 0x01);

    value ^= 1 << bit;
    context->state.d [reg].l = value;

    return 0;
}


/* bchg.b d(An) [#xx] */
static uint32_t m68k_0868_bchg_b_dan_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x07;
    uint32_t address = address_with_displacement (context, context->state.a [reg]);

    uint8_t value = read_byte (context, address);
    context->state.ccr_zero = !((value >> bit) & 0x01);

    value ^= 1 << bit;
    write_byte (context, address, value);

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

    return 0;
}


/* bclr.b (An+) [#xx] */
static uint32_t m68k_0898_bclr_b_anp_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x07;

    uint8_t value = read_byte (context, context->state.a [reg]);
    context->state.ccr_zero = !((value >> bit) & 0x01);

    value &= ~(1 << bit);
    write_byte (context, context->state.a [reg], value);
    context->state.a [reg] += (reg == 7) ? 2 : 1;

    return 0;
}


/* bclr.b (-An) [#xx] */
static uint32_t m68k_08a0_bclr_b_pan_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x07;

    context->state.a [reg] -= (reg == 7) ? 2 : 1;
    uint8_t value = read_byte (context, context->state.a [reg]);
    context->state.ccr_zero = !((value >> bit) & 0x01);

    value &= ~(1 << bit);
    write_byte (context, context->state.a [reg], value);

    return 0;
}


/* bclr.b d(An) [#xx] */
static uint32_t m68k_08a8_bclr_b_dan_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x07;
    uint32_t address = address_with_displacement (context, context->state.a [reg]);

    uint8_t value = read_byte (context, address);
    context->state.ccr_zero = !((value >> bit) & 0x01);

    value &= ~(1 << bit);
    write_byte (context, address, value);

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

    return 0;
}


/* bclr.b (xxx.l) [#xx] */
static uint32_t m68k_08b9_bclr_b_al_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t bit = read_extension (context) & 0x07;
    uint32_t address = read_extension_long (context);

    uint8_t value = read_byte (context, address);
    context->state.ccr_zero = !((value >> bit) & 0x01);

    value &= ~(1 << bit);
    write_byte (context, address, value);

    return 0;
}


/* bset.l Dn [#xx] */
static uint32_t m68k_08c0_bset_l_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x1f;

    uint32_t value = context->state.d [reg].l;
    context->state.ccr_zero = !((value >> bit) & 0x00000001);

    value |= 1 << bit;
    context->state.d [reg].l = value;

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

    return 0;
}


/* bset.b (An+) [#xx] */
static uint32_t m68k_08d8_bset_b_anp_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x07;

    uint8_t value = read_byte (context, context->state.a [reg]);
    context->state.ccr_zero = !((value >> bit) & 0x01);

    value |= 1 << bit;
    write_byte (context, context->state.a [reg], value);
    context->state.a [reg] += (reg == 7) ? 2 : 1;

    return 0;
}


/* bset.b (-An) [#xx] */
static uint32_t m68k_08e0_bset_b_pan_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x07;

    context->state.a [reg] -= (reg == 7) ? 2 : 1;
    uint8_t value = read_byte (context, context->state.a [reg]);
    context->state.ccr_zero = !((value >> bit) & 0x01);

    value |= 1 << bit;
    write_byte (context, context->state.a [reg], value);

    return 0;
}


/* bset.b d(An) [#xx] */
static uint32_t m68k_08e8_bset_b_dan_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t bit = read_extension (context) & 0x07;
    uint32_t address = address_with_displacement (context, context->state.a [reg]);

    uint8_t value = read_byte (context, address);
    context->state.ccr_zero = !((value >> bit) & 0x01);

    value |= 1 << bit;
    write_byte (context, address, value);

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

    return 0;
}


/* bset.b (xxx.w) [#xx] */
static uint32_t m68k_08f8_bset_b_aw_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t bit = read_extension (context) & 0x07;
    uint32_t address = (int16_t) read_extension (context);

    uint8_t value = read_byte (context, address);
    context->state.ccr_zero = !((value >> bit) & 0x01);

    value |= 1 << bit;
    write_byte (context, address, value);

    return 0;
}


/* bset.b (xxx.l) [#xx] */
static uint32_t m68k_08f9_bset_b_al_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t bit = read_extension (context) & 0x07;
    uint32_t address = read_extension_long (context);

    uint8_t value = read_byte (context, address);
    context->state.ccr_zero = !((value >> bit) & 0x01);

    value |= 1 << bit;
    write_byte (context, address, value);

    return 0;
}


/* eori.b Dn ← Dn ^ #xx */
static uint32_t m68k_0a00_eori_b_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = read_extension (context);
    uint8_t a = context->state.d [reg].b;

    uint8_t result = a ^ b;
    context->state.d [reg].b = result;
    m68k_move_b_flags (context, result);

    return 0;
}


/* eori.b (An) ← (An) ^ #xx */
static uint32_t m68k_0a10_eori_b_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = read_extension (context);
    uint8_t a = read_byte (context, context->state.a [reg]);

    uint8_t result = a ^ b;
    write_byte (context, context->state.a [reg], result);
    m68k_move_b_flags (context, result);

    return 0;
}


/* eori.b (An+) ← (An+) ^ #xx */
static uint32_t m68k_0a18_eori_b_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = read_extension (context);
    uint8_t a = read_byte (context, context->state.a [reg]);

    uint8_t result = a ^ b;
    write_byte (context, context->state.a [reg], result);
    context->state.a [reg] += (reg == 7) ? 2 : 1;
    m68k_move_b_flags (context, result);

    return 0;
}


/* eori.b (-An) ← (-An) ^ #xx */
static uint32_t m68k_0a20_eori_b_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    context->state.a [reg] -= (reg == 7) ? 2 : 1;

    uint8_t b = read_extension (context);
    uint8_t a = read_byte (context, context->state.a [reg]);

    uint8_t result = a ^ b;
    write_byte (context, context->state.a [reg], result);
    m68k_move_b_flags (context, result);

    return 0;
}


/* eori.b d(An) ← d(An) ^ #xx */
static uint32_t m68k_0a28_eori_b_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = read_extension (context);
    uint32_t address = context->state.a [reg] + (int16_t) read_extension (context);
    uint8_t a = read_byte (context, address);

    uint8_t result = a ^ b;
    write_byte (context, address, result);
    m68k_move_b_flags (context, result);

    return 0;
}


/* eori.b d(An+Xi) ← d(An+Xi) ^ #xx */
static uint32_t m68k_0a30_eori_b_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = read_extension (context);
    uint32_t address = address_with_index (context, context->state.a [reg]);
    uint8_t a = read_byte (context, address);

    uint8_t result = a ^ b;
    write_byte (context, address, result);
    m68k_move_b_flags (context, result);

    return 0;
}


/* eori.b (xxx.w) ← (xxx.w) ^ #xx */
static uint32_t m68k_0a38_eori_b_aw (M68000_Context *context, uint16_t instruction)
{
    uint8_t b = read_extension (context);
    uint32_t address = (int16_t) read_extension (context);
    uint8_t a = read_byte (context, address);

    uint8_t result = a ^ b;
    write_byte (context, address, result);
    m68k_move_b_flags (context, result);

    return 0;
}


/* eori.b (xxx.l) ← (xxx.l) ^ #xx */
static uint32_t m68k_0a39_eori_b_al (M68000_Context *context, uint16_t instruction)
{
    uint8_t b = read_extension (context);
    uint32_t address = read_extension_long (context);
    uint8_t a = read_byte (context, address);

    uint8_t result = a ^ b;
    write_byte (context, address, result);
    m68k_move_b_flags (context, result);

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

    return 0;
}


/* cmpi.b (An+) - #xx */
static uint32_t m68k_0c18_cmpi_b_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = read_extension (context);
    uint8_t a = read_byte (context, context->state.a [reg]);
    context->state.a [reg] += (reg == 7) ? 2 : 1;
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

    return 0;
}


/* cmpi.b (-An) - #xx */
static uint32_t m68k_0c20_cmpi_b_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    context->state.a [reg] -= (reg == 7) ? 2 : 1;
    uint8_t b = read_extension (context);
    uint8_t a = read_byte (context, context->state.a [reg]);
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

    return 0;
}


/* cmpi.b d(An) - #xx */
static uint32_t m68k_0c28_cmpi_b_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = read_extension (context);
    uint8_t a = read_byte_with_displacement (context, context->state.a [reg]);
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

    return 0;
}


/* cmpi.b d(An+Xi) - #xx */
static uint32_t m68k_0c30_cmpi_b_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = read_extension (context);
    uint8_t a = read_byte_with_index (context, context->state.a [reg]);
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

    return 0;
}


/* cmpi.b (xxx.w) - #xx */
static uint32_t m68k_0c38_cmpi_b_aw (M68000_Context *context, uint16_t instruction)
{
    uint8_t b = read_extension (context);
    uint8_t a = read_byte_aw (context);
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

    return 0;
}


/* cmpi.b (xxx.l) - #xx */
static uint32_t m68k_0c39_cmpi_b_al (M68000_Context *context, uint16_t instruction)
{
    uint8_t b = read_extension (context);
    uint8_t a = read_byte_al (context);
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

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

    return 0;
}


/* cmpi.w d(An) - #xxxx */
static uint32_t m68k_0c68_cmpi_w_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t b = read_extension (context);

    uint16_t a = read_word_with_displacement (context, context->state.a [reg]);
    uint16_t result = a - b;

    m68k_cmp_w_flags (context, a, b, result);

    return 0;
}


/* cmpi.w (xxx.w) - #xxxx */
static uint32_t m68k_0c78_cmpi_w_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t b = read_extension (context);
    uint16_t a = read_word_aw (context);
    uint16_t result = a - b;

    m68k_cmp_w_flags (context, a, b, result);

    return 0;
}


/* cmpi.l (An+) - #xxxxxxxx */
static uint32_t m68k_0c98_cmpi_l_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t b = read_extension_long (context);
    uint32_t a = read_long (context, context->state.a [reg]);
    context->state.a [reg] += 4;
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

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

    return 0;
}


/* move.b Dn ← (An+) */
static uint32_t m68k_1018_move_b_dn_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    context->state.a [source_reg] += (source_reg == 7) ? 2 : 1;
    context->state.d [dest_reg].b = value;
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b Dn ← (-An) */
static uint32_t m68k_1020_move_b_dn_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    context->state.a [source_reg] -= (source_reg == 7) ? 2 : 1;

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    context->state.d [dest_reg].b = value;
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b Dn ← d(An) */
static uint32_t m68k_1028_move_b_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte_with_displacement (context, context->state.a [source_reg]);
    context->state.d [dest_reg].b = value;
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b Dn ← d(An+Xi) */
static uint32_t m68k_1030_move_b_dn_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_with_index (context, context->state.a [source_reg]);
    context->state.d [dest_reg].b = value;

    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b Dn ← (xxx.w) */
static uint32_t m68k_1038_move_b_dn_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_aw (context);
    context->state.d [dest_reg].b = value;
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b Dn ← (xxx.l) */
static uint32_t m68k_1039_move_b_dn_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_al (context);
    context->state.d [dest_reg].b = value;
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b Dn ← d(PC) */
static uint32_t m68k_103a_move_b_dn_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_with_displacement (context, context->state.pc);
    context->state.d [dest_reg].b = value;
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b Dn ← d(PC+Xi) */
static uint32_t m68k_103b_move_b_dn_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_with_index (context, context->state.pc);
    context->state.d [dest_reg].b = value;
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b Dn ← #xx */
static uint32_t m68k_103c_move_b_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_extension (context);
    context->state.d [dest_reg].b = value;
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (An) ← Dn */
static uint32_t m68k_1080_move_b_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = context->state.d [source_reg].b;
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (An) ← (An) */
static uint32_t m68k_1090_move_b_an_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (An) ← (An+) */
static uint32_t m68k_1098_move_b_an_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    context->state.a [source_reg] += (source_reg == 7) ? 2 : 1;
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (An) ← (-An) */
static uint32_t m68k_10a0_move_b_an_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [source_reg] -= (source_reg == 7) ? 2 : 1;
    uint8_t value = read_byte (context, context->state.a [source_reg]);
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (An) ← d(An) */
static uint32_t m68k_10a8_move_b_an_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte_with_displacement (context, context->state.a [source_reg]);
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

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

    return 0;
}


/* move.b (An) ← (xxx.w) */
static uint32_t m68k_10b8_move_b_an_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_aw (context);
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (An) ← (xxx.l) */
static uint32_t m68k_10b9_move_b_an_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_al (context);
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (An) ← d(PC) */
static uint32_t m68k_10ba_move_b_an_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_with_displacement (context, context->state.pc);
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (An) ← d(PC+Xi) */
static uint32_t m68k_10bb_move_b_an_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_with_index (context, context->state.pc);
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (An) ← #xx */
static uint32_t m68k_10bc_move_b_an_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_extension (context);
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (An+) ← Dn */
static uint32_t m68k_10c0_move_b_anp_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = context->state.d [source_reg].b;
    write_byte (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += (dest_reg == 7) ? 2 : 1;
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (An+) ← (An) */
static uint32_t m68k_10d0_move_b_anp_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    write_byte (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += (dest_reg == 7) ? 2 : 1;
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (An+) ← (An+) */
static uint32_t m68k_10d8_move_b_anp_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    context->state.a [source_reg] += (source_reg == 7) ? 2 : 1;
    write_byte (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += (dest_reg == 7) ? 2 : 1;
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (An+) ← (-An) */
static uint32_t m68k_10e0_move_b_anp_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [source_reg] -= (source_reg == 7) ? 2 : 1;
    uint8_t value = read_byte (context, context->state.a [source_reg]);
    write_byte (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += (dest_reg == 7) ? 2 : 1;
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (An+) ← d(An) */
static uint32_t m68k_10e8_move_b_anp_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte_with_displacement (context, context->state.a [source_reg]);
    write_byte (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += (dest_reg == 7) ? 2 : 1;
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (An+) ← d(An+Xi) */
static uint32_t m68k_10f0_move_b_anp_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte_with_index (context, context->state.a [source_reg]);
    write_byte (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += (dest_reg == 7) ? 2 : 1;
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (An+) ← (xxx.w) */
static uint32_t m68k_10f8_move_b_anp_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_aw (context);
    write_byte (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += (dest_reg == 7) ? 2 : 1;
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (An+) ← (xxx.l) */
static uint32_t m68k_10f9_move_b_anp_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_al (context);
    write_byte (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += (dest_reg == 7) ? 2 : 1;
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (An+) ← d(PC) */
static uint32_t m68k_10fa_move_b_anp_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_with_displacement (context, context->state.pc);
    write_byte (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += (dest_reg == 7) ? 2 : 1;
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (An+) ← d(PC+Xi) */
static uint32_t m68k_10fb_move_b_anp_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_with_index (context, context->state.pc);
    write_byte (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += (dest_reg == 7) ? 2 : 1;
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (An+) ← #xxxx */
static uint32_t m68k_10fc_move_b_anp_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_extension (context);
    write_byte (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += (dest_reg == 7) ? 2 : 1;
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (-An) ← Dn */
static uint32_t m68k_1100_move_b_pan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = context->state.d [source_reg].b;
    context->state.a [dest_reg] -= (dest_reg == 7) ? 2 : 1;
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (-An) ← (An) */
static uint32_t m68k_1110_move_b_pan_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    context->state.a [dest_reg] -= (dest_reg == 7) ? 2 : 1;
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (-An) ← (An+) */
static uint32_t m68k_1118_move_b_pan_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    context->state.a [source_reg] += (source_reg == 7) ? 2 : 1;
    context->state.a [dest_reg] -= (dest_reg == 7) ? 2 : 1;
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (-An) ← (-An) */
static uint32_t m68k_1120_move_b_pan_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [source_reg] -= (source_reg == 7) ? 2 : 1;
    uint8_t value = read_byte (context, context->state.a [source_reg]);
    context->state.a [dest_reg] -= (dest_reg == 7) ? 2 : 1;
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (-An) ← d(An) */
static uint32_t m68k_1128_move_b_pan_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte_with_displacement (context, context->state.a [source_reg]);
    context->state.a [dest_reg] -= (dest_reg == 7) ? 2 : 1;
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (-An) ← d(An+Xi) */
static uint32_t m68k_1130_move_b_pan_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte_with_index (context, context->state.a [source_reg]);
    context->state.a [dest_reg] -= (dest_reg == 7) ? 2 : 1;
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (-An) ← (xxx.w) */
static uint32_t m68k_1138_move_b_pan_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_aw (context);
    context->state.a [dest_reg] -= (dest_reg == 7) ? 2 : 1;
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (-An) ← (xxx.l) */
static uint32_t m68k_1139_move_b_pan_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_al (context);
    context->state.a [dest_reg] -= (dest_reg == 7) ? 2 : 1;
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (-An) ← d(PC) */
static uint32_t m68k_113a_move_b_pan_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_with_displacement (context, context->state.pc);
    context->state.a [dest_reg] -= (dest_reg == 7) ? 2 : 1;
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (-An) ← d(PC+Xi) */
static uint32_t m68k_113b_move_b_pan_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_with_index (context, context->state.pc);
    context->state.a [dest_reg] -= (dest_reg == 7) ? 2 : 1;
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (-An) ← #xx */
static uint32_t m68k_113c_move_b_pan_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_extension (context);
    context->state.a [dest_reg] -= (dest_reg == 7) ? 2 : 1;
    write_byte (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b d(An) ← Dn */
static uint32_t m68k_1140_move_b_dan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = context->state.d [source_reg].b;
    write_byte_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b d(An) ← (An) */
static uint32_t m68k_1150_move_b_dan_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    write_byte_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b d(An) ← (An+) */
static uint32_t m68k_1158_move_b_dan_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    context->state.a [source_reg] += (source_reg == 7) ? 2 : 1;
    write_byte_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b d(An) ← (-An) */
static uint32_t m68k_1160_move_b_dan_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [source_reg] -= (source_reg == 7) ? 2 : 1;
    uint8_t value = read_byte (context, context->state.a [source_reg]);
    write_byte_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b d(An) ← d(An) */
static uint32_t m68k_1168_move_b_dan_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte_with_displacement (context, context->state.a [source_reg]);
    write_byte_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b d(An) ← d(An+Xi) */
static uint32_t m68k_1170_move_b_dan_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte_with_index (context, context->state.a [source_reg]);
    write_byte_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b d(An) ← (xxx.w) */
static uint32_t m68k_1178_move_b_dan_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_aw (context);
    write_byte_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b d(An) ← (xxx.l) */
static uint32_t m68k_1179_move_b_dan_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_al (context);
    write_byte_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b d(An) ← d(PC) */
static uint32_t m68k_117a_move_b_dan_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_with_displacement (context, context->state.pc);
    write_byte_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b d(An) ← d(PC+Xi) */
static uint32_t m68k_117b_move_b_dan_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_with_index (context, context->state.pc);
    write_byte_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b d(An) ← Imm */
static uint32_t m68k_117c_move_b_dan_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint8_t value = read_extension (context);

    write_byte_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b d(An+Xi) ← Dn */
static uint32_t m68k_1180_move_b_danxi_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = context->state.d [source_reg].b;
    uint32_t address = context->state.a [dest_reg];

    write_byte_with_index (context, address, value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b d(An+Xi) ← (An) */
static uint32_t m68k_1190_move_b_danxi_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    write_byte_with_index (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b d(An+Xi) ← (An+) */
static uint32_t m68k_1198_move_b_danxi_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    context->state.a [source_reg] += (source_reg == 7) ? 2 : 1;
    write_byte_with_index (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b d(An+Xi) ← (-An) */
static uint32_t m68k_11a0_move_b_danxi_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [source_reg] -= (source_reg == 7) ? 2 : 1;
    uint8_t value = read_byte (context, context->state.a [source_reg]);
    write_byte_with_index (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b d(An+Xi) ← d(An) */
static uint32_t m68k_11a8_move_b_danxi_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte_with_displacement (context, context->state.a [source_reg]);
    write_byte_with_index (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b d(An+Xi) ← d(An+Xi) */
static uint32_t m68k_11b0_move_b_danxi_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte_with_index (context, context->state.a [source_reg]);
    write_byte_with_index (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b d(An+Xi) ← (xxx.w) */
static uint32_t m68k_11b8_move_b_danxi_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_aw (context);
    write_byte_with_index (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b d(An+Xi) ← (xxx.l) */
static uint32_t m68k_11b9_move_b_danxi_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_al (context);
    write_byte_with_index (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b d(An+Xi) ← d(PC) */
static uint32_t m68k_11ba_move_b_danxi_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_with_displacement (context, context->state.pc);
    write_byte_with_index (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b d(An+Xi) ← d(PC+Xi) */
static uint32_t m68k_11bb_move_b_danxi_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_byte_with_index (context, context->state.pc);
    write_byte_with_index (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b d(An+Xi) ← #xx */
static uint32_t m68k_11bc_move_b_danxi_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t value = read_extension (context);
    write_byte_with_index (context, context->state.a [dest_reg], value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (xxx.w) ← Dn */
static uint32_t m68k_11c0_move_b_aw_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = context->state.d [source_reg].b;
    write_byte_aw (context, value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (xxx.w) ← (An) */
static uint32_t m68k_11d0_move_b_aw_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    write_byte_aw (context, value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (xxx.w) ← (An+) */
static uint32_t m68k_11d8_move_b_aw_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    context->state.a [source_reg] += (source_reg == 7) ? 2 : 1;
    write_byte_aw (context, value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (xxx.w) ← (-An) */
static uint32_t m68k_11e0_move_b_aw_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    context->state.a [source_reg] -= (source_reg == 7) ? 2 : 1;
    uint8_t value = read_byte (context, context->state.a [source_reg]);
    write_byte_aw (context, value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (xxx.w) ← d(An) */
static uint32_t m68k_11e8_move_b_aw_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte_with_displacement (context, context->state.a [source_reg]);
    write_byte_aw (context, value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (xxx.w) ← d(An+Xi) */
static uint32_t m68k_11f0_move_b_aw_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte_with_index (context, context->state.a [source_reg]);
    write_byte_aw (context, value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (xxx.w) ← (xxx.w) */
static uint32_t m68k_11f8_move_b_aw_aw (M68000_Context *context, uint16_t instruction)
{
    uint8_t value = read_byte_aw (context);
    write_byte_aw (context, value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (xxx.w) ← (xxx.l) */
static uint32_t m68k_11f9_move_b_aw_al (M68000_Context *context, uint16_t instruction)
{
    uint8_t value = read_byte_al (context);
    write_byte_aw (context, value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (xxx.w) ← d(PC) */
static uint32_t m68k_11fa_move_b_aw_dpc (M68000_Context *context, uint16_t instruction)
{
    uint8_t value = read_byte_with_displacement (context, context->state.pc);
    write_byte_aw (context, value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (xxx.w) ← d(PC+Xi) */
static uint32_t m68k_11fb_move_b_aw_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint8_t value = read_byte_with_index (context, context->state.pc);
    write_byte_aw (context, value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (xxx.w) ← #xx */
static uint32_t m68k_11fc_move_b_aw_imm (M68000_Context *context, uint16_t instruction)
{
    uint8_t value = read_extension (context);

    write_byte_aw (context, value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (xxx.l) ← Dn */
static uint32_t m68k_13c0_move_b_al_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = context->state.d [source_reg].b;
    write_byte_al (context, value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (xxx.l) ← (An) */
static uint32_t m68k_13d0_move_b_al_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    write_byte_al (context, value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (xxx.l) ← (An+) */
static uint32_t m68k_13d8_move_b_al_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte (context, context->state.a [source_reg]);
    context->state.a [source_reg] += (source_reg == 7) ? 2 : 1;
    write_byte_al (context, value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (xxx.l) ← (-An) */
static uint32_t m68k_13e0_move_b_al_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    context->state.a [source_reg] -= (source_reg == 7) ? 2 : 1;
    uint8_t value = read_byte (context, context->state.a [source_reg]);
    write_byte_al (context, value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (xxx.l) ← d(An) */
static uint32_t m68k_13e8_move_b_al_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte_with_displacement (context, context->state.a [source_reg]);
    write_byte_al (context, value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (xxx.l) ← d(An+Xi) */
static uint32_t m68k_13f0_move_b_al_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint8_t value = read_byte_with_index (context, context->state.a [source_reg]);
    write_byte_al (context, value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (xxx.l) ← (xxx.w) */
static uint32_t m68k_13f8_move_b_al_aw (M68000_Context *context, uint16_t instruction)
{
    uint8_t value = read_byte_aw (context);

    write_byte_al (context, value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (xxx.l) ← (xxx.l) */
static uint32_t m68k_13f9_move_b_al_al (M68000_Context *context, uint16_t instruction)
{
    uint8_t value = read_byte_al (context);

    write_byte_al (context, value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (xxx.l) ← d(PC) */
static uint32_t m68k_13fa_move_b_al_dpc (M68000_Context *context, uint16_t instruction)
{
    uint8_t value = read_byte_with_displacement (context, context->state.pc);

    write_byte_al (context, value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (xxx.l) ← d(PC+Xi) */
static uint32_t m68k_13fb_move_b_al_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint8_t value = read_byte_with_index (context, context->state.pc);

    write_byte_al (context, value);
    m68k_move_b_flags (context, value);

    return 0;
}


/* move.b (xxx.l) ← #xx */
static uint32_t m68k_13fc_move_b_al_imm (M68000_Context *context, uint16_t instruction)
{
    uint8_t value = read_extension (context);

    write_byte_al (context, value);
    m68k_move_b_flags (context, value);

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

    return 0;
}


/* move.l Dn ← An */
static uint32_t m68k_2008_move_l_dn_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = context->state.a [source_reg];
    context->state.d [dest_reg].l = value;
    m68k_move_l_flags (context, value);

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

    return 0;
}


/* move.l Dn ← (An+) */
static uint32_t m68k_2018_move_l_dn_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 4;
    context->state.d [dest_reg].l = value;
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l Dn ← (-An) */
static uint32_t m68k_2020_move_l_dn_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    context->state.a [source_reg] -= 4;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    context->state.d [dest_reg].l = value;
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l Dn ← d(An) */
static uint32_t m68k_2028_move_l_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long_with_displacement (context, context->state.a [source_reg]);
    context->state.d [dest_reg].l = value;
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l Dn ← d(An+Xi) */
static uint32_t m68k_2030_move_l_dn_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long_with_index (context, context->state.a [source_reg]);
    context->state.d [dest_reg].l = value;
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l Dn ← (xxx.w) */
static uint32_t m68k_2038_move_l_dn_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_aw (context);
    context->state.d [dest_reg].l = value;
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l Dn ← (xxx.l) */
static uint32_t m68k_2039_move_l_dn_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_al (context);
    context->state.d [dest_reg].l = value;
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l Dn ← d(PC) */
static uint32_t m68k_203a_move_l_dn_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_with_displacement (context, context->state.pc);
    context->state.d [dest_reg].l = value;
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l Dn ← d(PC+Xi) */
static uint32_t m68k_203b_move_l_dn_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_with_index (context, context->state.pc);
    context->state.d [dest_reg].l = value;
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l Dn ← Imm */
static uint32_t m68k_203c_move_l_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint32_t value = read_extension_long (context);

    context->state.d [dest_reg].l = value;
    m68k_move_l_flags (context, value);

    return 0;
}


/* movea.l An ← Dn */
static uint32_t m68k_2040_movea_l_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [dest_reg] = context->state.d [source_reg].l;

    return 0;
}


/* movea.l An ← An */
static uint32_t m68k_2048_movea_l_an_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [dest_reg] = context->state.a [source_reg];

    return 0;
}


/* movea.l An ← (An) */
static uint32_t m68k_2050_movea_l_an_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    context->state.a [dest_reg] = value;

    return 0;
}


/* movea.l An ← (An+) */
static uint32_t m68k_2058_movea_l_an_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 4;
    context->state.a [dest_reg] = value;

    return 0;
}


/* movea.l An ← (-An) */
static uint32_t m68k_2060_movea_l_an_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [source_reg] -= 4;
    uint32_t value = read_long (context, context->state.a [source_reg]);
    context->state.a [dest_reg] = value;

    return 0;
}


/* movea.l An ← d(An) */
static uint32_t m68k_2068_movea_l_an_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [dest_reg] = read_long_with_displacement (context, context->state.a [source_reg]);

    return 0;
}


/* movea.l An ← d(An+Xi) */
static uint32_t m68k_2070_movea_l_an_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [dest_reg] = read_long_with_index (context, context->state.a [source_reg]);

    return 0;
}


/* movea.l An ← (xxx.w) */
static uint32_t m68k_2078_movea_l_an_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint32_t value = read_long_aw (context);

    context->state.a [dest_reg] = value;

    return 0;
}


/* movea.l An ← (xxx.l) */
static uint32_t m68k_2079_movea_l_an_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint32_t value = read_long_al (context);

    context->state.a [dest_reg] = value;

    return 0;
}


/* movea.l An ← d(PC) */
static uint32_t m68k_207a_movea_l_an_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_with_displacement (context, context->state.pc);
    context->state.a [dest_reg] = value;

    return 0;
}


/* movea.l An ← d(PC+Xi) */
static uint32_t m68k_207b_movea_l_an_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    context->state.a [dest_reg] = read_long_with_index (context, context->state.pc);

    return 0;
}


/* movea.l An ← #xxxx */
static uint32_t m68k_207c_movea_l_an_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint32_t imm = read_extension_long (context);

    context->state.a [dest_reg] = imm;

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

    return 0;
}


/* move.l (An) ← (An) */
static uint32_t m68k_2090_move_l_an_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long (context, context->state.a [source_reg]);

    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (An) ← (An+) */
static uint32_t m68k_2098_move_l_an_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 4;

    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (An) ← (-An) */
static uint32_t m68k_20a0_move_l_an_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    context->state.a [source_reg] -= 4;

    uint32_t value = read_long (context, context->state.a [source_reg]);

    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (An) ← d(An) */
static uint32_t m68k_20a8_move_l_an_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long_with_displacement (context, context->state.a [source_reg]);

    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

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

    return 0;
}


/* move.l (An) ← (xxx.w) */
static uint32_t m68k_20b8_move_l_an_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_aw (context);
    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (An) ← (xxx.l) */
static uint32_t m68k_20b9_move_l_an_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_al (context);
    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (An) ← d(PC) */
static uint32_t m68k_20ba_move_l_an_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_with_displacement (context, context->state.pc);
    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (An) ← d(PC+Xi) */
static uint32_t m68k_20bb_move_l_an_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_with_index (context, context->state.pc);
    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (An) ← #xxxx */
static uint32_t m68k_20bc_move_l_an_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint32_t value = read_extension_long (context);

    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (An+) ← Dn */
static uint32_t m68k_20c0_move_l_anp_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = context->state.d [source_reg].l;
    write_long (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 4;
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (An+) ← An */
static uint32_t m68k_20c8_move_l_anp_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = context->state.a [source_reg];
    write_long (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 4;
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (An+) ← (An) */
static uint32_t m68k_20d0_move_l_anp_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    write_long (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 4;
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (An+) ← (An+) */
static uint32_t m68k_20d8_move_l_anp_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 4;
    write_long (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 4;
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (An+) ← (-An) */
static uint32_t m68k_20e0_move_l_anp_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [source_reg] -= 4;
    uint32_t value = read_long (context, context->state.a [source_reg]);
    write_long (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 4;
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (An+) ← d(An) */
static uint32_t m68k_20e8_move_l_anp_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long_with_displacement (context, context->state.a [source_reg]);
    write_long (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 4;
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (An+) ← d(An+Xi) */
static uint32_t m68k_20f0_move_l_anp_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long_with_index (context, context->state.a [source_reg]);
    write_long (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 4;
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (An+) ← (xxx.w) */
static uint32_t m68k_20f8_move_l_anp_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_aw (context);
    write_long (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 4;
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (An+) ← (xxx.l) */
static uint32_t m68k_20f9_move_l_anp_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_al (context);
    write_long (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 4;
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (An+) ← d(PC) */
static uint32_t m68k_20fa_move_l_anp_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_with_displacement (context, context->state.pc);
    write_long (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 4;
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (An+) ← d(PC+Xi) */
static uint32_t m68k_20fb_move_l_anp_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_with_index (context, context->state.pc);
    write_long (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 4;
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (An+) ← #xxxxxxxx */
static uint32_t m68k_20fc_move_l_anp_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_extension_long (context);
    write_long (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 4;
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (-An) ← Dn */
static uint32_t m68k_2100_move_l_pan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = context->state.d [source_reg].l;
    context->state.a [dest_reg] -= 4;
    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (-An) ← An */
static uint32_t m68k_2108_move_l_pan_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = context->state.a [source_reg];
    context->state.a [dest_reg] -= 4;
    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (-An) ← (An) */
static uint32_t m68k_2110_move_l_pan_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    context->state.a [dest_reg] -= 4;
    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (-An) ← (An+) */
static uint32_t m68k_2118_move_l_pan_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 4;
    context->state.a [dest_reg] -= 4;
    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (-An) ← (-An) */
static uint32_t m68k_2120_move_l_pan_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [source_reg] -= 4;
    uint32_t value = read_long (context, context->state.a [source_reg]);
    context->state.a [dest_reg] -= 4;
    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (-An) ← d(An) */
static uint32_t m68k_2128_move_l_pan_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long_with_displacement (context, context->state.a [source_reg]);
    context->state.a [dest_reg] -= 4;
    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (-An) ← d(An+Xi) */
static uint32_t m68k_2130_move_l_pan_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long_with_index (context, context->state.a [source_reg]);
    context->state.a [dest_reg] -= 4;
    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (-An) ← (xxx.w) */
static uint32_t m68k_2138_move_l_pan_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint32_t address = (int16_t) read_extension (context);

    uint32_t value = read_long (context, address);
    context->state.a [dest_reg] -= 4;
    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (-An) ← (xxx.l) */
static uint32_t m68k_2139_move_l_pan_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint32_t address = read_extension_long (context);

    uint32_t value = read_long (context, address);
    context->state.a [dest_reg] -= 4;
    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (-An) ← d(PC) */
static uint32_t m68k_213a_move_l_pan_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_with_displacement (context, context->state.pc);
    context->state.a [dest_reg] -= 4;
    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (-An) ← d(PC+Xi) */
static uint32_t m68k_213b_move_l_pan_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_with_index (context, context->state.pc);
    context->state.a [dest_reg] -= 4;
    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (-An) ← #xxxxxxxx */
static uint32_t m68k_213c_move_l_pan_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_extension_long (context);
    context->state.a [dest_reg] -= 4;
    write_long (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An) ← Dn */
static uint32_t m68k_2140_move_l_dan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = context->state.d [source_reg].l;

    write_long_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An) ← An */
static uint32_t m68k_2148_move_l_dan_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = context->state.a [source_reg];

    write_long_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An) ← (An) */
static uint32_t m68k_2150_move_l_dan_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    write_long_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An) ← (An+) */
static uint32_t m68k_2158_move_l_dan_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 4;
    write_long_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An) ← (-An) */
static uint32_t m68k_2160_move_l_dan_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    context->state.a [source_reg] -= 4;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    write_long_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An) ← d(An) */
static uint32_t m68k_2168_move_l_dan_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long_with_displacement (context, context->state.a [source_reg]);
    write_long_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An) ← d(An+Xi) */
static uint32_t m68k_2170_move_l_dan_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long_with_index (context, context->state.a [source_reg]);
    write_long_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An) ← (xxx.w) */
static uint32_t m68k_2178_move_l_dan_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_aw (context);
    write_long_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An) ← (xxx.l) */
static uint32_t m68k_2179_move_l_dan_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_al (context);
    write_long_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An) ← d(PC) */
static uint32_t m68k_217a_move_l_dan_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_with_displacement (context, context->state.pc);
    write_long_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An) ← d(PC+Xi) */
static uint32_t m68k_217b_move_l_dan_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_with_index (context, context->state.pc);
    write_long_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An) ← #xxxx */
static uint32_t m68k_217c_move_l_dan_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint32_t value = read_extension_long (context);

    write_long_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An+Xi) ← Dn */
static uint32_t m68k_2180_move_l_danxi_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = context->state.d [source_reg].l;
    write_long_with_index (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An+Xi) ← An */
static uint32_t m68k_2188_move_l_danxi_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = context->state.a [source_reg];
    write_long_with_index (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An+Xi) ← (An) */
static uint32_t m68k_2190_move_l_danxi_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    write_long_with_index (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An+Xi) ← (An+) */
static uint32_t m68k_2198_move_l_danxi_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 4;
    write_long_with_index (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An+Xi) ← (-An) */
static uint32_t m68k_21a0_move_l_danxi_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    context->state.a [source_reg] -= 4;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    write_long_with_index (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An+Xi) ← d(An) */
static uint32_t m68k_21a8_move_l_danxi_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_with_displacement (context, context->state.a [source_reg]);
    write_long_with_index (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An+Xi) ← d(An+Xi) */
static uint32_t m68k_21b0_move_l_danxi_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_with_index (context, context->state.a [source_reg]);
    write_long_with_index (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An+Xi) ← (xxx.w) */
static uint32_t m68k_21b8_move_l_danxi_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_aw (context);
    write_long_with_index (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An+Xi) ← (xxx.l) */
static uint32_t m68k_21b9_move_l_danxi_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_al (context);
    write_long_with_index (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An+Xi) ← d(PC) */
static uint32_t m68k_21ba_move_l_danxi_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_with_displacement (context, context->state.pc);
    write_long_with_index (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An+Xi) ← d(PC+Xi) */
static uint32_t m68k_21bb_move_l_danxi_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_long_with_index (context, context->state.pc);
    write_long_with_index (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l d(An+Xi) ← #xxxxxxxx */
static uint32_t m68k_21bc_move_l_danxi_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t value = read_extension_long (context);
    write_long_with_index (context, context->state.a [dest_reg], value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.w) ← Dn */
static uint32_t m68k_21c0_move_l_aw_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = context->state.d [source_reg].l;
    write_long_aw (context, value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.w) ← An */
static uint32_t m68k_21c8_move_l_aw_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = context->state.a [source_reg];
    write_long_aw (context, value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.w) ← (An) */
static uint32_t m68k_21d0_move_l_aw_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    write_long_aw (context, value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.w) ← (An+) */
static uint32_t m68k_21d8_move_l_aw_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 4;
    write_long_aw (context, value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.w) ← (-An) */
static uint32_t m68k_21e0_move_l_aw_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    context->state.a [source_reg] -= 4;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    write_long_aw (context, value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.w) ← d(An) */
static uint32_t m68k_21e8_move_l_aw_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long_with_displacement (context, context->state.a [source_reg]);
    write_long_aw (context, value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.w) ← d(An+Xi) */
static uint32_t m68k_21f0_move_l_aw_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long_with_index (context, context->state.a [source_reg]);
    write_long_aw (context, value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.w) ← (xxx.w) */
static uint32_t m68k_21f8_move_l_aw_aw (M68000_Context *context, uint16_t instruction)
{
    uint32_t value = read_long_aw (context);

    write_long_aw (context, value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.w) ← (xxx.l) */
static uint32_t m68k_21f9_move_l_aw_al (M68000_Context *context, uint16_t instruction)
{
    uint32_t value = read_long_al (context);

    write_long_aw (context, value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.w) ← d(PC) */
static uint32_t m68k_21fa_move_l_aw_dpc (M68000_Context *context, uint16_t instruction)
{
    uint32_t value = read_long_with_displacement (context, context->state.pc);

    write_long_aw (context, value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.w) ← d(PC+Xi) */
static uint32_t m68k_21fb_move_l_aw_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint32_t value = read_long_with_index (context, context->state.pc);

    write_long_aw (context, value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.w) ← #xxxxxxxx */
static uint32_t m68k_21fc_move_l_aw_imm (M68000_Context *context, uint16_t instruction)
{
    uint32_t value = read_extension_long (context);

    write_long_aw (context, value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.l) ← Dn */
static uint32_t m68k_23c0_move_l_al_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = context->state.d [source_reg].l;
    write_long_al (context, value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.l) ← An */
static uint32_t m68k_23c8_move_l_al_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = context->state.a [source_reg];
    write_long_al (context, value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.l) ← (An) */
static uint32_t m68k_23d0_move_l_al_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    write_long_al (context, value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.l) ← (An+) */
static uint32_t m68k_23d8_move_l_al_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 4;
    write_long_al (context, value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.l) ← (-An) */
static uint32_t m68k_23e0_move_l_al_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    context->state.a [source_reg] -= 4;

    uint32_t value = read_long (context, context->state.a [source_reg]);
    write_long_al (context, value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.l) ← d(An) */
static uint32_t m68k_23e8_move_l_al_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long_with_displacement (context, context->state.a [source_reg]);
    write_long_al (context, value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.l) ← d(An+Xi) */
static uint32_t m68k_23f0_move_l_al_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint32_t value = read_long_with_index (context, context->state.a [source_reg]);
    write_long_al (context, value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.l) ← (xxx.w) */
static uint32_t m68k_23f8_move_l_al_aw (M68000_Context *context, uint16_t instruction)
{
    uint32_t value = read_long_aw (context);

    write_long_al (context, value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.l) ← (xxx.l) */
static uint32_t m68k_23f9_move_l_al_al (M68000_Context *context, uint16_t instruction)
{
    uint32_t value = read_long_al (context);

    write_long_al (context, value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.l) ← d(PC) */
static uint32_t m68k_23fa_move_l_al_dpc (M68000_Context *context, uint16_t instruction)
{
    uint32_t value = read_long_with_displacement (context, context->state.pc);

    write_long_al (context, value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.l) ← d(PC+Xi) */
static uint32_t m68k_23fb_move_l_al_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint32_t value = read_long_with_index (context, context->state.pc);

    write_long_al (context, value);
    m68k_move_l_flags (context, value);

    return 0;
}


/* move.l (xxx.l) ← #xxxx */
static uint32_t m68k_23fc_move_l_al_imm (M68000_Context *context, uint16_t instruction)
{
    uint32_t value = read_extension_long (context);

    write_long_al (context, value);
    m68k_move_l_flags (context, value);

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

    return 0;
}


/* move.w Dn ← (An+) */
static uint32_t m68k_3018_move_w_dn_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 2;
    context->state.d [dest_reg].w = value;
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w Dn ← (-An)- */
static uint32_t m68k_3020_move_w_dn_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    context->state.a [source_reg] -= 2;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    context->state.d [dest_reg].w = value;
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w Dn ← d(An) */
static uint32_t m68k_3028_move_w_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word_with_displacement (context, context->state.a [source_reg]);
    context->state.d [dest_reg].w = value;
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w Dn ← d(An+Xi) */
static uint32_t m68k_3030_move_w_dn_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_with_index (context, context->state.a [source_reg]);
    context->state.d [dest_reg].w = value;

    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w Dn ← (xxx.w) */
static uint32_t m68k_3038_move_w_dn_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_aw (context);
    context->state.d [dest_reg].w = value;
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w Dn ← (xxx.l) */
static uint32_t m68k_3039_move_w_dn_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_al (context);
    context->state.d [dest_reg].w = value;
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w Dn ← d(PC) */
static uint32_t m68k_303a_move_w_dn_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_with_displacement (context, context->state.pc);
    context->state.d [dest_reg].w = value;
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w Dn ← d(PC+Xi) */
static uint32_t m68k_303b_move_w_dn_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_with_index (context, context->state.pc);
    context->state.d [dest_reg].w = value;
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w Dn ← Imm */
static uint32_t m68k_303c_move_w_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_extension (context);
    context->state.d [dest_reg].w = value;
    m68k_move_w_flags (context, value);

    return 0;
}


/* movea.w An ← Dn */
static uint32_t m68k_3040_movea_w_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [dest_reg] = (int16_t) context->state.d [source_reg].w;

    return 0;
}


/* movea.w An ← An */
static uint32_t m68k_3048_movea_w_an_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = context->state.a [source_reg];
    context->state.a [dest_reg] = (int16_t) value;

    return 0;
}


/* movea.w An ← (An) */
static uint32_t m68k_3050_movea_w_an_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    context->state.a [dest_reg] = (int16_t) value;

    return 0;
}


/* movea.w An ← (An+) */
static uint32_t m68k_3058_movea_w_an_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 2;
    context->state.a [dest_reg] = (int16_t) value;

    return 0;
}


/* movea.w An ← (-An) */
static uint32_t m68k_3060_movea_w_an_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [source_reg] -= 2;
    uint16_t value = read_word (context, context->state.a [source_reg]);
    context->state.a [dest_reg] = (int16_t) value;

    return 0;
}


/* movea.w An ← d(An) */
static uint32_t m68k_3068_movea_w_an_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [dest_reg] = (int16_t) read_word_with_displacement (context, context->state.a [source_reg]);

    return 0;
}


/* movea.w An ← d(An+Xi) */
static uint32_t m68k_3070_movea_w_an_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word_with_index (context, context->state.a [source_reg]);
    context->state.a [dest_reg] = (int16_t) value;

    return 0;
}


/* movea.w An ← (xxx.w) */
static uint32_t m68k_3078_movea_w_an_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t value = read_word_aw (context);

    context->state.a [dest_reg] = (int16_t) value;

    return 0;
}


/* movea.w An ← (xxx.l) */
static uint32_t m68k_3079_movea_w_an_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t value = read_word_al (context);

    context->state.a [dest_reg] = (int16_t) value;

    return 0;
}


/* movea.w An ← d(PC) */
static uint32_t m68k_307a_movea_w_an_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_with_displacement (context, context->state.pc);
    context->state.a [dest_reg] = (int16_t) value;

    return 0;
}


/* movea.w An ← d(PC+Xi) */
static uint32_t m68k_307b_movea_w_an_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    context->state.a [dest_reg] = (int16_t) read_word_with_index (context, context->state.pc);

    return 0;
}


/* movea.w An ← #xxxx */
static uint32_t m68k_307c_movea_w_an_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_extension (context);
    context->state.a [dest_reg] = (int16_t) value;

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

    return 0;
}


/* move.w (An) ← (An) */
static uint32_t m68k_3090_move_w_an_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);

    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (An) ← (An+) */
static uint32_t m68k_3098_move_w_an_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 2;
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (An) ← (-An) */
static uint32_t m68k_30a0_move_w_an_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    context->state.a [source_reg] -= 2;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (An) ← d(An) */
static uint32_t m68k_30a8_move_w_an_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word_with_displacement (context, context->state.a [source_reg]);
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (An) ← d(An+Xi) */
static uint32_t m68k_30b0_move_w_an_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word_with_index (context, context->state.a [source_reg]);
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (An) ← (xxx.w) */
static uint32_t m68k_30b8_move_w_an_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_aw (context);
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (An) ← (xxx.l) */
static uint32_t m68k_30b9_move_w_an_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_al (context);
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (An) ← d(PC) */
static uint32_t m68k_30ba_move_w_an_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_with_displacement (context, context->state.pc);
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (An) ← d(PC+Xi) */
static uint32_t m68k_30bb_move_w_an_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_with_index (context, context->state.pc);
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (An) ← #xxxx */
static uint32_t m68k_30bc_move_w_an_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t value = read_extension (context);

    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (An+) ← Dn */
static uint32_t m68k_30c0_move_w_anp_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = context->state.d [source_reg].w;
    write_word (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 2;
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (An+) ← An */
static uint32_t m68k_30c8_move_w_anp_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = context->state.a [source_reg];
    write_word (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 2;
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (An+) ← (An) */
static uint32_t m68k_30d0_move_w_anp_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    write_word (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 2;
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (An+) ← (An+) */
static uint32_t m68k_30d8_move_w_anp_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 2;
    write_word (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 2;
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (An+) ← (-An) */
static uint32_t m68k_30e0_move_w_anp_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    context->state.a [source_reg] -= 2;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    write_word (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 2;
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (An+) ← d(An) */
static uint32_t m68k_30e8_move_w_anp_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word_with_displacement (context, context->state.a [source_reg]);
    write_word (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 2;
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (An+) ← d(An+Xi) */
static uint32_t m68k_30f0_move_w_anp_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word_with_index (context, context->state.a [source_reg]);
    write_word (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 2;
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (An+) ← (xxx.w) */
static uint32_t m68k_30f8_move_w_anp_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_aw (context);
    write_word (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 2;
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (An+) ← (xxx.l) */
static uint32_t m68k_30f9_move_w_anp_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_al (context);
    write_word (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 2;
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (An+) ← d(PC) */
static uint32_t m68k_30fa_move_w_anp_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_with_displacement (context, context->state.pc);
    write_word (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 2;
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (An+) ← d(PC+Xi) */
static uint32_t m68k_30fb_move_w_anp_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_with_index (context, context->state.pc);
    write_word (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 2;
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (An+) ← #xxxx */
static uint32_t m68k_30fc_move_w_anp_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_extension (context);
    write_word (context, context->state.a [dest_reg], value);
    context->state.a [dest_reg] += 2;
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (-An) ← Dn */
static uint32_t m68k_3100_move_w_pan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = context->state.d [source_reg].w;
    context->state.a [dest_reg] -= 2;
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (-An) ← An */
static uint32_t m68k_3108_move_w_pan_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = context->state.a [source_reg];
    context->state.a [dest_reg] -= 2;
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (-An) ← (An) */
static uint32_t m68k_3110_move_w_pan_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    context->state.a [dest_reg] -= 2;
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (-An) ← (An+) */
static uint32_t m68k_3118_move_w_pan_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 2;
    context->state.a [dest_reg] -= 2;
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (-An) ← (-An) */
static uint32_t m68k_3120_move_w_pan_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [source_reg] -= 2;
    uint16_t value = read_word (context, context->state.a [source_reg]);
    context->state.a [dest_reg] -= 2;
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (-An) ← d(An) */
static uint32_t m68k_3128_move_w_pan_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word_with_displacement (context, context->state.a [source_reg]);
    context->state.a [dest_reg] -= 2;
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (-An) ← d(An+Xi) */
static uint32_t m68k_3130_move_w_pan_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word_with_index (context, context->state.a [source_reg]);
    context->state.a [dest_reg] -= 2;
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (-An) ← (xxx.w) */
static uint32_t m68k_3138_move_w_pan_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_aw (context);
    context->state.a [dest_reg] -= 2;
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (-An) ← (xxx.l) */
static uint32_t m68k_3139_move_w_pan_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_al (context);
    context->state.a [dest_reg] -= 2;
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (-An) ← d(PC) */
static uint32_t m68k_313a_move_w_pan_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_with_displacement (context, context->state.pc);
    context->state.a [dest_reg] -= 2;
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (-An) ← d(PC+Xi) */
static uint32_t m68k_313b_move_w_pan_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_with_index (context, context->state.pc);
    context->state.a [dest_reg] -= 2;
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (-An) ← #xxxx */
static uint32_t m68k_313c_move_w_pan_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_extension (context);
    context->state.a [dest_reg] -= 2;
    write_word (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An) ← Dn */
static uint32_t m68k_3140_move_w_dan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = context->state.d [source_reg].w;
    write_word_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An) ← An */
static uint32_t m68k_3148_move_w_dan_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = context->state.a [source_reg];
    write_word_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An) ← (An) */
static uint32_t m68k_3150_move_w_dan_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    write_word_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An) ← (An+) */
static uint32_t m68k_3158_move_w_dan_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 2;
    write_word_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An) ← (-An) */
static uint32_t m68k_3160_move_w_dan_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [source_reg] -= 2;
    uint16_t value = read_word (context, context->state.a [source_reg]);
    write_word_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An) ← d(An) */
static uint32_t m68k_3168_move_w_dan_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word_with_displacement (context, context->state.a [source_reg]);
    write_word_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An) ← d(An+Xi) */
static uint32_t m68k_3170_move_w_dan_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word_with_index (context, context->state.a [source_reg]);
    write_word_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An) ← (xxx.w) */
static uint32_t m68k_3178_move_w_dan_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_aw (context);
    write_word_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An) ← (xxx.l) */
static uint32_t m68k_3179_move_w_dan_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_al (context);
    write_word_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An) ← d(PC) */
static uint32_t m68k_317a_move_w_dan_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_with_displacement (context, context->state.pc);
    write_word_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An) ← d(PC+Xi) */
static uint32_t m68k_317b_move_w_dan_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_with_index (context, context->state.pc);
    write_word_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An) ← Imm */
static uint32_t m68k_317c_move_w_dan_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t value = read_extension (context);

    write_word_with_displacement (context, context->state.a [dest_reg], value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An+Xi) ← Dn */
static uint32_t m68k_3180_move_w_danxi_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = context->state.d [source_reg].w;
    uint32_t address = context->state.a [dest_reg];

    write_word_with_index (context, address, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An+Xi) ← An */
static uint32_t m68k_3188_move_w_danxi_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = context->state.a [source_reg];
    uint32_t address = context->state.a [dest_reg];

    write_word_with_index (context, address, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An+Xi) ← (An) */
static uint32_t m68k_3190_move_w_danxi_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    uint32_t address = context->state.a [dest_reg];

    write_word_with_index (context, address, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An+Xi) ← (An+) */
static uint32_t m68k_3198_move_w_danxi_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 2;
    uint32_t address = context->state.a [dest_reg];

    write_word_with_index (context, address, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An+Xi) ← (-An) */
static uint32_t m68k_31a0_move_w_danxi_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    context->state.a [source_reg] -= 2;
    uint16_t value = read_word (context, context->state.a [source_reg]);
    uint32_t address = context->state.a [dest_reg];

    write_word_with_index (context, address, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An+Xi) ← d(An) */
static uint32_t m68k_31a8_move_w_danxi_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_with_displacement (context, context->state.a [source_reg]);
    uint32_t address = context->state.a [dest_reg];

    write_word_with_index (context, address, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An+Xi) ← d(An+Xi) */
static uint32_t m68k_31b0_move_w_danxi_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_with_index (context, context->state.a [source_reg]);
    uint32_t address = context->state.a [dest_reg];

    write_word_with_index (context, address, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An+Xi) ← (xxx.w) */
static uint32_t m68k_31b8_move_w_danxi_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_aw (context);
    uint32_t address = context->state.a [dest_reg];

    write_word_with_index (context, address, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An+Xi) ← (xxx.l) */
static uint32_t m68k_31b9_move_w_danxi_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_al (context);
    uint32_t address = context->state.a [dest_reg];

    write_word_with_index (context, address, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An+Xi) ← d(PC) */
static uint32_t m68k_31ba_move_w_danxi_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_with_displacement (context, context->state.pc);
    uint32_t address = context->state.a [dest_reg];

    write_word_with_index (context, address, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An+Xi) ← d(PC+Xi) */
static uint32_t m68k_31bb_move_w_danxi_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_word_with_index (context, context->state.pc);
    uint32_t address = context->state.a [dest_reg];

    write_word_with_index (context, address, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w d(An+Xi) ← #xxxx */
static uint32_t m68k_31bc_move_w_danxi_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t value = read_extension (context);
    uint32_t address = context->state.a [dest_reg];

    write_word_with_index (context, address, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.w) ← Dn */
static uint32_t m68k_31c0_move_w_aw_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = context->state.d [source_reg].w;
    write_word_aw (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.w) ← An */
static uint32_t m68k_31c8_move_w_aw_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = context->state.a [source_reg];
    write_word_aw (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.w) ← (An) */
static uint32_t m68k_31d0_move_w_aw_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);

    write_word_aw (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.w) ← (An+) */
static uint32_t m68k_31d8_move_w_aw_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 2;
    write_word_aw (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.w) ← (-An) */
static uint32_t m68k_31e0_move_w_aw_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    context->state.a [source_reg] -= 2;
    uint16_t value = read_word (context, context->state.a [source_reg]);
    write_word_aw (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.w) ← d(An) */
static uint32_t m68k_31e8_move_w_aw_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word_with_displacement (context, context->state.a [source_reg]);
    write_word_aw (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.w) ← d(An+Xi) */
static uint32_t m68k_31f0_move_w_aw_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word_with_index (context, context->state.a [source_reg]);
    write_word_aw (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.w) ← (xxx.w) */
static uint32_t m68k_31f8_move_w_aw_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t value = read_word_aw (context);
    write_word_aw (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.w) ← (xxx.l) */
static uint32_t m68k_31f9_move_w_aw_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t value = read_word_al (context);
    write_word_aw (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.w) ← d(PC) */
static uint32_t m68k_31fa_move_w_aw_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t value = read_word_with_displacement (context, context->state.pc);
    write_word_aw (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.w) ← d(PC+Xi) */
static uint32_t m68k_31fb_move_w_aw_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t value = read_word_with_index (context, context->state.pc);
    write_word_aw (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.w) ← Imm */
static uint32_t m68k_31fc_move_w_aw_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t value = read_extension (context);

    write_word_aw (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.l) ← Dn */
static uint32_t m68k_33c0_move_w_al_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = context->state.d [source_reg].w;
    write_word_al (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.l) ← An */
static uint32_t m68k_33c8_move_w_al_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = context->state.a [source_reg];
    write_word_al (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.l) ← (An) */
static uint32_t m68k_33d0_move_w_al_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    write_word_al (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.l) ← (An+) */
static uint32_t m68k_33d8_move_w_al_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 2;
    write_word_al (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.l) ← (-An) */
static uint32_t m68k_33e0_move_w_al_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    context->state.a [source_reg] -= 2;
    uint16_t value = read_word (context, context->state.a [source_reg]);
    write_word_al (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.l) ← d(An) */
static uint32_t m68k_33e8_move_w_al_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word_with_displacement (context, context->state.a [source_reg]);
    write_word_al (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.l) ← d(An+Xi) */
static uint32_t m68k_33f0_move_w_al_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;

    uint16_t value = read_word_with_index (context, context->state.a [source_reg]);
    write_word_al (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.l) ← (xxx.w) */
static uint32_t m68k_33f8_move_w_al_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t value = read_word_aw (context);
    write_word_al (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.l) ← (xxx.l) */
static uint32_t m68k_33f9_move_w_al_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t value = read_word_al (context);
    write_word_al (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.l) ← d(PC) */
static uint32_t m68k_33fa_move_w_al_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t value = read_word_with_displacement (context, context->state.pc);
    write_word_al (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.l) ← d(PC+Xi) */
static uint32_t m68k_33fb_move_w_al_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t value = read_word_with_index (context, context->state.pc);
    write_word_al (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move.w (xxx.l) ← #xxxx */
static uint32_t m68k_33fc_move_w_al_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t value = read_extension (context);
    write_word_al (context, value);
    m68k_move_w_flags (context, value);

    return 0;
}


/* move Dn ← sr */
static uint32_t m68k_40c0_move_dn_sr (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = instruction & 0x07;

    context->state.d [dest_reg].w = context->state.sr;

    return 0;
}


/* move (An) ← sr */
static uint32_t m68k_40d0_move_an_sr (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = instruction & 0x07;

    write_word (context, context->state.a [dest_reg], context->state.sr);

    return 0;
}


/* move (An+) ← sr */
static uint32_t m68k_40d8_move_anp_sr (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = instruction & 0x07;

    write_word (context, context->state.a [dest_reg], context->state.sr);
    context->state.a [dest_reg] += 2;

    return 0;
}


/* move (-An) ← sr */
static uint32_t m68k_40e0_move_pan_sr (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = instruction & 0x07;

    context->state.a [dest_reg] -= 2;
    write_word (context, context->state.a [dest_reg], context->state.sr);

    return 0;
}


/* move d(An) ← sr */
static uint32_t m68k_40e8_move_dan_sr (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = instruction & 0x07;

    write_word_with_displacement (context, context->state.a [dest_reg], context->state.sr);

    return 0;
}


/* move d(An+Xi) ← sr */
static uint32_t m68k_40f0_move_danxi_sr (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = instruction & 0x07;

    write_word_with_index (context, context->state.a [dest_reg], context->state.sr);

    return 0;
}


/* move (xxx.w) ← sr */
static uint32_t m68k_40f8_move_aw_sr (M68000_Context *context, uint16_t instruction)
{
    write_word_aw (context, context->state.sr);

    return 0;
}


/* move (xxx.l) ← sr */
static uint32_t m68k_40f9_move_al_sr (M68000_Context *context, uint16_t instruction)
{
    write_word_al (context, context->state.sr);

    return 0;
}


/* lea An ← (An) */
static uint32_t m68k_41d0_lea_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    context->state.a [dest_reg] = context->state.a [source_reg];

    return 0;
}


/* lea An ← d(An) */
static uint32_t m68k_41e8_lea_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    context->state.a [dest_reg] = address_with_displacement (context, context->state.a [source_reg]);

    return 0;
}


/* lea An ← d(An+Xi) */
static uint32_t m68k_41f0_lea_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    context->state.a [dest_reg] = address_with_index (context, context->state.a [source_reg]);

    return 0;
}


/* lea An ← (xxx.w) */
static uint32_t m68k_41f8_lea_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint32_t address = (int16_t) read_extension (context);

    context->state.a [dest_reg] = address;

    return 0;
}


/* lea An ← (xxx.l) */
static uint32_t m68k_41f9_lea_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint32_t address = read_extension_long (context);

    context->state.a [dest_reg] = address;

    return 0;
}


/* lea An ← d(PC) */
static uint32_t m68k_41fa_lea_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    context->state.a [dest_reg] = address_with_displacement (context, context->state.pc);

    return 0;
}


/* lea An ← d(PC+Xi) */
static uint32_t m68k_41fb_lea_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    context->state.a [dest_reg] = address_with_index (context, context->state.pc);

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


/* clr.b Dn */
static uint32_t m68k_4200_clr_d_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    context->state.d [reg].b = 0x00;
    m68k_clr_flags (context);

    return 0;
}


/* clr.b (An) */
static uint32_t m68k_4210_clr_b_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    write_byte (context, context->state.a [reg], 0x00);
    m68k_clr_flags (context);

    return 0;
}


/* clr.b (An+) */
static uint32_t m68k_4218_clr_b_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    write_byte (context, context->state.a [reg], 0x00);
    context->state.a [reg] += (reg == 7) ? 2 : 1;
    m68k_clr_flags (context);

    return 0;
}


/* clr.b (-An) */
static uint32_t m68k_4220_clr_b_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    context->state.a [reg] -= (reg == 7) ? 2 : 1;
    write_byte (context, context->state.a [reg], 0x00);
    m68k_clr_flags (context);

    return 0;
}


/* clr.b d(An) */
static uint32_t m68k_4228_clr_b_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    write_byte_with_displacement (context, context->state.a [reg], 0x00);
    m68k_clr_flags (context);

    return 0;
}


/* clr.b d(An+Xi) */
static uint32_t m68k_4230_clr_b_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    write_byte_with_index (context, context->state.a [reg], 0x00);
    m68k_clr_flags (context);

    return 0;
}


/* clr.b (xxx.w) */
static uint32_t m68k_4238_clr_b_aw (M68000_Context *context, uint16_t instruction)
{
    write_byte_aw (context, 0x00);
    m68k_clr_flags (context);

    return 0;
}


/* clr.b (xxx.l) */
static uint32_t m68k_4239_clr_b_al (M68000_Context *context, uint16_t instruction)
{
    write_byte_al (context, 0x00);
    m68k_clr_flags (context);

    return 0;
}


/* clr.w d(An) */
static uint32_t m68k_4268_clr_w_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    write_word_with_displacement (context, context->state.a [reg], 0x00);
    m68k_clr_flags (context);

    return 0;
}


/* clr.w (xxx.w) */
static uint32_t m68k_4278_clr_w_aw (M68000_Context *context, uint16_t instruction)
{
    write_word_aw (context, 0x0000);
    m68k_clr_flags (context);

    return 0;
}


/* clr.l Dn */
static uint32_t m68k_4280_clr_l_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    context->state.d [reg].l = 0x00000000;
    m68k_clr_flags (context);

    return 0;
}


/* clr.l (An) */
static uint32_t m68k_4290_clr_l_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    write_long (context, context->state.a [reg], 0x00000000);
    m68k_clr_flags (context);

    return 0;
}


/* clr.l (An+) */
static uint32_t m68k_4298_clr_l_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    write_long (context, context->state.a [reg], 0x00000000);
    context->state.a [reg] += 4;
    m68k_clr_flags (context);

    return 0;
}


/* clr.l (-An) */
static uint32_t m68k_42a0_clr_l_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    context->state.a [reg] -= 4;

    write_long (context, context->state.a [reg], 0x00000000);
    m68k_clr_flags (context);

    return 0;
}


/* clr.l d(An) */
static uint32_t m68k_42a8_clr_l_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    write_long_with_displacement (context, context->state.a [reg], 0x00000000);
    m68k_clr_flags (context);

    return 0;
}


/* clr.l d(An+Xi) */
static uint32_t m68k_42b0_clr_l_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    write_long_with_index (context, context->state.a [reg], 0x00000000);
    m68k_clr_flags (context);

    return 0;
}


/* clr.l (xxx.w) */
static uint32_t m68k_42b8_clr_l_aw (M68000_Context *context, uint16_t instruction)
{
    write_long_aw (context, 0x00000000);
    m68k_clr_flags (context);

    return 0;
}


/* clr.l (xxx.l) */
static uint32_t m68k_42b9_clr_l_al (M68000_Context *context, uint16_t instruction)
{
    write_long_al (context, 0x00000000);
    m68k_clr_flags (context);

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

    return 0;
}


/* neg.b d(An) */
static uint32_t m68k_4428_neg_b_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t address = address_with_displacement (context, context->state.a [reg]);

    uint8_t value = read_byte (context, address);
    uint8_t result = 0 - value;

    write_byte (context, address, result);

    context->state.ccr_negative = ((int8_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = ((int8_t) value == -128);
    context->state.ccr_carry = (result != 0);
    context->state.ccr_extend = (result != 0);

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

    return 0;
}


/* neg.w (An) */
static uint32_t m68k_4450_neg_w_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t value = read_word (context, context->state.a [reg]);
    uint16_t result = 0 - value;

    write_word (context, context->state.a [reg], result);

    context->state.ccr_negative = ((int16_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = ((int16_t) value == -32768);
    context->state.ccr_carry = (result != 0);
    context->state.ccr_extend = (result != 0);

    return 0;
}


/* neg.w (An+) */
static uint32_t m68k_4458_neg_w_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t value = read_word (context, context->state.a [reg]);
    uint16_t result = 0 - value;

    write_word (context, context->state.a [reg], result);
    context->state.a [reg] += 2;

    context->state.ccr_negative = ((int16_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = ((int16_t) value == -32768);
    context->state.ccr_carry = (result != 0);
    context->state.ccr_extend = (result != 0);

    return 0;
}


/* neg.w (-An) */
static uint32_t m68k_4460_neg_w_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    context->state.a [reg] -= 2;

    uint16_t value = read_word (context, context->state.a [reg]);
    uint16_t result = 0 - value;

    write_word (context, context->state.a [reg], result);

    context->state.ccr_negative = ((int16_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = ((int16_t) value == -32768);
    context->state.ccr_carry = (result != 0);
    context->state.ccr_extend = (result != 0);

    return 0;
}


/* neg.w d(An) */
static uint32_t m68k_4468_neg_w_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t address = address_with_displacement (context, context->state.a [reg]);

    uint16_t value = read_word (context, address);
    uint16_t result = 0 - value;

    write_word (context, address, result);

    context->state.ccr_negative = ((int16_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = ((int16_t) value == -32768);
    context->state.ccr_carry = (result != 0);
    context->state.ccr_extend = (result != 0);

    return 0;
}


/* neg.w d(An+Xi) */
static uint32_t m68k_4470_neg_w_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t address = address_with_index (context, context->state.a [reg]);

    uint16_t value = read_word (context, address);
    uint16_t result = 0 - value;

    write_word (context, address, result);

    context->state.ccr_negative = ((int16_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = ((int16_t) value == -32768);
    context->state.ccr_carry = (result != 0);
    context->state.ccr_extend = (result != 0);

    return 0;
}


/* neg.w (xxx.w) */
static uint32_t m68k_4478_neg_w_aw (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = (int16_t) read_extension (context);

    uint16_t value = read_word (context, address);
    uint16_t result = 0 - value;

    write_word (context, address, result);

    context->state.ccr_negative = ((int16_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = ((int16_t) value == -32768);
    context->state.ccr_carry = (result != 0);
    context->state.ccr_extend = (result != 0);

    return 0;
}


/* neg.w (xxx.l) */
static uint32_t m68k_4479_neg_w_al (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = read_extension_long (context);

    uint16_t value = read_word (context, address);
    uint16_t result = 0 - value;

    write_word (context, address, result);

    context->state.ccr_negative = ((int16_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = ((int16_t) value == -32768);
    context->state.ccr_carry = (result != 0);
    context->state.ccr_extend = (result != 0);

    return 0;
}


/* move ccr ← Dn */
static uint32_t m68k_44c0_move_ccr_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t value = context->state.d [reg].w;

    context->state.ccr = value;

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

    return 0;
}


/* move sr ← Dn */
static uint32_t m68k_46c0_move_sr_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    if (context->state.sr_supervisor)
    {
        uint16_t value = context->state.d [reg].w;
        m68k_store_stack_pointer (context);
        context->state.sr = value & SR_MASK;
        m68k_load_stack_pointer (context);
    }
    else
    {
        context->state.pc -= 2;
        m68k_exception (context, 0x20);
    }

    return 0;
}


/* move sr ← (An) */
static uint32_t m68k_46d0_move_sr_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    if (context->state.sr_supervisor)
    {
        uint16_t value = read_word (context, context->state.a [reg]);
        m68k_store_stack_pointer (context);
        context->state.sr = value & SR_MASK;
        m68k_load_stack_pointer (context);
    }
    else
    {
        context->state.pc -= 2;
        m68k_exception (context, 0x20);
    }

    return 0;
}


/* move sr ← (An+) */
static uint32_t m68k_46d8_move_sr_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    if (context->state.sr_supervisor)
    {
        uint16_t value = read_word (context, context->state.a [reg]);
        context->state.a [reg] += 2;
        m68k_store_stack_pointer (context);
        context->state.sr = value & SR_MASK;
        m68k_load_stack_pointer (context);
    }
    else
    {
        context->state.pc -= 2;
        m68k_exception (context, 0x20);
    }

    return 0;
}


/* move sr ← (-An) */
static uint32_t m68k_46e0_move_sr_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    if (context->state.sr_supervisor)
    {
        context->state.a [reg] -= 2;
        uint16_t value = read_word (context, context->state.a [reg]);
        m68k_store_stack_pointer (context);
        context->state.sr = value & SR_MASK;
        m68k_load_stack_pointer (context);
    }
    else
    {
        context->state.pc -= 2;
        m68k_exception (context, 0x20);
    }

    return 0;
}


/* move sr ← d(An) */
static uint32_t m68k_46e8_move_sr_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    if (context->state.sr_supervisor)
    {
        uint16_t value = read_word_with_displacement (context, context->state.a [reg]);
        m68k_store_stack_pointer (context);
        context->state.sr = value & SR_MASK;
        m68k_load_stack_pointer (context);
    }
    else
    {
        context->state.pc -= 2;
        m68k_exception (context, 0x20);
    }

    return 0;
}


/* move sr ← d(An+Xi) */
static uint32_t m68k_46f0_move_sr_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    if (context->state.sr_supervisor)
    {
        uint16_t value = read_word_with_index (context, context->state.a [reg]);
        m68k_store_stack_pointer (context);
        context->state.sr = value & SR_MASK;
        m68k_load_stack_pointer (context);
    }
    else
    {
        context->state.pc -= 2;
        m68k_exception (context, 0x20);
    }

    return 0;
}


/* move sr ← (xxx.w) */
static uint32_t m68k_46f8_move_sr_aw (M68000_Context *context, uint16_t instruction)
{
    if (context->state.sr_supervisor)
    {
        uint16_t value = read_word_aw (context);
        m68k_store_stack_pointer (context);
        context->state.sr = value & SR_MASK;
        m68k_load_stack_pointer (context);
    }
    else
    {
        context->state.pc -= 2;
        m68k_exception (context, 0x20);
    }

    return 0;
}


/* move sr ← (xxx.l) */
static uint32_t m68k_46f9_move_sr_al (M68000_Context *context, uint16_t instruction)
{
    if (context->state.sr_supervisor)
    {
        uint16_t value = read_word_al (context);
        m68k_store_stack_pointer (context);
        context->state.sr = value & SR_MASK;
        m68k_load_stack_pointer (context);
    }
    else
    {
        context->state.pc -= 2;
        m68k_exception (context, 0x20);
    }

    return 0;
}


/* move sr ← d(PC) */
static uint32_t m68k_46fa_move_sr_dpc (M68000_Context *context, uint16_t instruction)
{
    if (context->state.sr_supervisor)
    {
        uint16_t value = read_word_with_displacement (context, context->state.pc);
        m68k_store_stack_pointer (context);
        context->state.sr = value & SR_MASK;
        m68k_load_stack_pointer (context);
    }
    else
    {
        context->state.pc -= 2;
        m68k_exception (context, 0x20);
    }

    return 0;
}


/* move sr ← d(PC+Xi) */
static uint32_t m68k_46fb_move_sr_dpcxi (M68000_Context *context, uint16_t instruction)
{
    if (context->state.sr_supervisor)
    {
        uint16_t value = read_word_with_index (context, context->state.pc);
        m68k_store_stack_pointer (context);
        context->state.sr = value & SR_MASK;
        m68k_load_stack_pointer (context);
    }
    else
    {
        context->state.pc -= 2;
        m68k_exception (context, 0x20);
    }

    return 0;
}


/* move sr ← #xxxx */
static uint32_t m68k_46fc_move_sr_imm (M68000_Context *context, uint16_t instruction)
{
    if (context->state.sr_supervisor)
    {
        uint16_t value = read_extension (context);
        m68k_store_stack_pointer (context);
        context->state.sr = value & SR_MASK;
        m68k_load_stack_pointer (context);
    }
    else
    {
        context->state.pc -= 2;
        m68k_exception (context, 0x20);
    }

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

    return 0;
}


/* movem.w (-An) ← <registers> */
static uint32_t m68k_48a0_movem_w_pan_regs (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t mask = read_extension (context);

    uint32_t address = context->state.a [reg];

    /* For (-An), the bit-mask and order is reversed */
    for (int32_t i = 0; i < 16; i++)
    {
        if (mask & (1 << i))
        {
            if (i < 8)
            {
                address -= 2;
                write_word (context, address, context->state.a [7 - i]);
            }
            else
            {
                address -= 2;
                write_word (context, address, context->state.d [15 - i].w);
            }
        }
    }

    context->state.a [reg] = address;

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

    return 0;
}


/* movem.l (-An) ← <registers> */
static uint32_t m68k_48e0_movem_l_pan_regs (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t mask = read_extension (context);

    uint32_t address = context->state.a [reg];

    /* For (-An), the bit-mask and order is reversed */
    for (int32_t i = 0; i < 16; i++)
    {
        if (mask & (1 << i))
        {
            if (i < 8)
            {
                address -= 4;
                write_long (context, address, context->state.a [7 - i]);
            }
            else
            {
                address -= 4;
                write_long (context, address, context->state.d [15 - i].l);
            }
        }
    }

    context->state.a [reg] = address;

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

    return 0;
}


/* tst.b Dn */
static uint32_t m68k_4a00_tst_b_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint8_t value = context->state.d [reg].b;

    m68k_tst_b_flags (context, value);

    return 0;
}


/* tst.b (An) */
static uint32_t m68k_4a10_tst_b_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint8_t value = read_byte (context, context->state.a [reg]);

    m68k_tst_b_flags (context, value);

    return 0;
}


/* tst.b d(An) */
static uint32_t m68k_4a28_tst_b_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint8_t value = read_byte_with_displacement (context, context->state.a [reg]);

    m68k_tst_b_flags (context, value);

    return 0;
}


/* tst.b d(An+Xi) */
static uint32_t m68k_4a30_tst_b_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint8_t value = read_byte_with_index (context, context->state.a [reg]);

    m68k_tst_b_flags (context, value);

    return 0;
}


/* tst.b (xxx.w) */
static uint32_t m68k_4a38_tst_b_aw (M68000_Context *context, uint16_t instruction)
{
    uint8_t value = read_byte_aw (context);

    m68k_tst_b_flags (context, value);

    return 0;
}


/* tst.w Dn */
static uint32_t m68k_4a40_tst_w_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t value = context->state.d [reg].w;

    m68k_tst_w_flags (context, value);

    return 0;
}


/* tst.w (An) */
static uint32_t m68k_4a50_tst_w_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t value = read_word (context, context->state.a [reg]);

    m68k_tst_w_flags (context, value);

    return 0;
}


/* tst.w d(An) */
static uint32_t m68k_4a68_tst_w_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t value = read_word_with_displacement (context, context->state.a [reg]);

    m68k_tst_w_flags (context, value);

    return 0;
}


/* tst.w (xxx.w) */
static uint32_t m68k_4a78_tst_w_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t value = read_word_aw (context);

    m68k_tst_w_flags (context, value);

    return 0;
}


/* tst.w (xxx.l) */
static uint32_t m68k_4a79_tst_w_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t value = read_word_al (context);

    m68k_tst_w_flags (context, value);

    return 0;
}


/* tst.l Dn */
static uint32_t m68k_4a80_tst_l_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t value = context->state.d [reg].l;

    m68k_tst_l_flags (context, value);

    return 0;
}


/* tst.l (An) */
static uint32_t m68k_4a90_tst_l_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t value = read_long (context, context->state.a [reg]);

    m68k_tst_l_flags (context, value);

    return 0;
}


/* tst.l (An+) */
static uint32_t m68k_4a98_tst_l_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t value = read_long (context, context->state.a [reg]);
    context->state.a [reg] += 4;

    m68k_tst_l_flags (context, value);

    return 0;
}


/* tst.l (-An) */
static uint32_t m68k_4aa0_tst_l_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    context->state.a [reg] -= 4;
    uint32_t value = read_long (context, context->state.a [reg]);

    m68k_tst_l_flags (context, value);

    return 0;
}


/* tst.l d(An) */
static uint32_t m68k_4aa8_tst_l_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t value = read_long_with_displacement (context, context->state.a [reg]);

    m68k_tst_l_flags (context, value);

    return 0;
}


/* tst.l d(An+Xi) */
static uint32_t m68k_4ab0_tst_l_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t value = read_long_with_index (context, context->state.a [reg]);

    m68k_tst_l_flags (context, value);

    return 0;
}


/* tst.l (xxx.w) */
static uint32_t m68k_4ab8_tst_l_aw (M68000_Context *context, uint16_t instruction)
{
    uint32_t value = read_long_aw (context);

    m68k_tst_l_flags (context, value);

    return 0;
}


/* tst.l (xxx.l) */
static uint32_t m68k_4ab9_tst_l_al (M68000_Context *context, uint16_t instruction)
{
    uint32_t value = read_long_al (context);

    m68k_tst_l_flags (context, value);

    return 0;
}


/* movem.w <registers> ← (An+) */
static uint32_t m68k_4c98_movem_w_regs_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t mask = read_extension (context);

    uint32_t address = context->state.a [reg];

    /* Data registers first */
    for (uint32_t i = 0; i < 16; i++)
    {
        if (mask & (1 << i))
        {
            if (i < 8)
            {
                context->state.d [i].l = (int16_t) read_word (context, address);
                address += 2;
            }
            else
            {
                context->state.a [i - 8] = (int16_t) read_word (context, address);
                address += 2;
            }
        }
    }

    context->state.a [reg] = address;

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

    return 0;
}


/* movem.l <registers> ← (An+) */
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

    return 0;
}


/* trap */
static uint32_t m68k_4e40_trap (M68000_Context *context, uint16_t instruction)
{
    uint16_t vector = instruction & 0x0f;

    m68k_exception (context, 0x80 + (vector << 2));

    return 0;
}


/* move usp ← An */
static uint32_t m68k_4e60_move_an_usp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    if (context->state.sr_supervisor)
    {
        context->state.usp = context->state.a [reg];
    }
    else
    {
        context->state.pc -= 2;
        m68k_exception (context, 0x20);
    }

    return 0;
}


/* move An ← usp */
static uint32_t m68k_4e68_move_usp_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    if (context->state.sr_supervisor)
    {
        context->state.a [reg] = context->state.usp;
    }
    else
    {
        context->state.pc -= 2;
        m68k_exception (context, 0x20);
    }

    return 0;
}


/* nop */
static uint32_t m68k_4e71_nop (M68000_Context *context, uint16_t instruction)
{
    return 0;
}


/* rte */
static uint32_t m68k_4e73_rte (M68000_Context *context, uint16_t instruction)
{
    if (context->state.sr_supervisor)
    {
        uint16_t new_sr = read_word (context, context->state.a [7]) & SR_MASK;
        context->state.a [7] += 2;

        context->state.pc = read_long (context, context->state.a [7]);
        context->state.a [7] += 4;

        m68k_store_stack_pointer (context);
        context->state.sr = new_sr;
        m68k_load_stack_pointer (context);
    }
    else
    {
        context->state.pc -= 2;
        m68k_exception (context, 0x20);
    }

    return 0;
}


/* rts */
static uint32_t m68k_4e75_rts (M68000_Context *context, uint16_t instruction)
{
    context->state.pc = read_long (context, context->state.a [7]);
    context->state.a[7] += 4;

    return 0;
}


/* trapv */
static uint32_t m68k_4e76_trapv (M68000_Context *context, uint16_t instruction)
{
    if (context->state.ccr_overflow)
    {
        m68k_exception (context, 0x1c);
    }

    return 0;
}


/* jsr (An) */
static uint32_t m68k_4e90_jsr_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    /* Note: Take the new PC early in case it is affected by the stack push. */
    uint32_t new_pc = context->state.a [reg];

    /* Push the current PC to the stack */
    context->state.a [7] -= 4;
    write_long (context, context->state.a [7], context->state.pc);

    /* Update the PC */
    context->state.pc = new_pc;

    return 0;
}


/* jsr (xxx.l) */
static uint32_t m68k_4eb9_jsr_al (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = read_extension_long (context);

    /* Push the current PC to the stack */
    context->state.a [7] -= 4;
    write_long (context, context->state.a [7], context->state.pc);

    /* Update the PC */
    context->state.pc = address;

    return 0;
}


/* jsr d(PC) */
static uint32_t m68k_4eba_jsr_dpc (M68000_Context *context, uint16_t instruction)
{
    uint32_t new_pc = address_with_displacement (context, context->state.pc);

    /* Push the address of the next instruction to the stack */
    context->state.a [7] -= 4;
    write_long (context, context->state.a [7], context->state.pc);

    context->state.pc = new_pc;

    return 0;
}


/* jsr d(PC+Xi) */
static uint32_t m68k_4ebb_jsr_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint32_t new_pc = address_with_index (context, context->state.pc);

    /* Push the address of the next instruction to the stack */
    context->state.a [7] -= 4;
    write_long (context, context->state.a [7], context->state.pc);

    context->state.pc = new_pc;

    return 0;
}


/* jmp (An) */
static uint32_t m68k_4ed0_jmp_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    context->state.pc = context->state.a [reg];

    return 0;
}


/* jmp (xxx.l) */
static uint32_t m68k_4ef9_jmp_al (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = read_extension_long (context);

    context->state.pc = address;

    return 0;
}


/* jmp d(PC+Xi) */
static uint32_t m68k_4efb_jmp_dpcxi (M68000_Context *context, uint16_t instruction)
{
    /* Note: The jump is from the location of the extension. */
    context->state.pc = address_with_index (context, context->state.pc);

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

    return 0;
}


/* addq.b (An+), #xx */
static uint32_t m68k_5018_addq_b_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint8_t a = read_byte (context, context->state.a [reg]);

    uint8_t result = a + b;
    write_byte (context, context->state.a [reg], result);
    context->state.a [reg] += (reg == 7) ? 2 : 1;
    m68k_add_b_flags (context, a, b, result);

    return 0;
}


/* addq.b (-An), #xx */
static uint32_t m68k_5020_addq_b_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint8_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    context->state.a [reg] -= (reg == 7) ? 2 : 1;
    uint8_t a = read_byte (context, context->state.a [reg]);

    uint8_t result = a + b;
    write_byte (context, context->state.a [reg], result);
    m68k_add_b_flags (context, a, b, result);

    return 0;
}


/* addq.b d(An), #xx */
static uint32_t m68k_5028_addq_b_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t address = address_with_displacement (context, context->state.a [reg]);

    uint8_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint8_t a = read_byte (context, address);

    uint8_t result = a + b;
    write_byte (context, address, result);
    m68k_add_b_flags (context, a, b, result);

    return 0;
}


/* addq.b d(An+Xi), #xx */
static uint32_t m68k_5030_addq_b_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t address = address_with_index (context, context->state.a [reg]);

    uint8_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint8_t a = read_byte (context, address);

    uint8_t result = a + b;
    write_byte (context, address, result);
    m68k_add_b_flags (context, a, b, result);

    return 0;
}


/* addq.b (xxx.w), #xx */
static uint32_t m68k_5038_addq_b_aw (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = (int16_t) read_extension (context);

    uint8_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint8_t a = read_byte (context, address);

    uint8_t result = a + b;
    write_byte (context, address, result);
    m68k_add_b_flags (context, a, b, result);

    return 0;
}


/* addq.b (xxx.l), #xx */
static uint32_t m68k_5039_addq_b_al (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = read_extension_long (context);

    uint8_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint8_t a = read_byte (context, address);

    uint8_t result = a + b;
    write_byte (context, address, result);
    m68k_add_b_flags (context, a, b, result);

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

    return 0;
}


/* addq.w (An+), #xx */
static uint32_t m68k_5058_addq_w_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t a = read_word (context, context->state.a [reg]);

    uint16_t result = a + b;
    write_word (context, context->state.a [reg], result);
    context->state.a [reg] += 2;
    m68k_add_w_flags (context, a, b, result);

    return 0;
}


/* addq.w d(An), #xx */
static uint32_t m68k_5068_addq_w_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t address = address_with_displacement (context, context->state.a [reg]);

    uint16_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t a = read_word (context, address);

    uint16_t result = a + b;
    write_word (context, address, result);
    m68k_add_w_flags (context, a, b, result);

    return 0;
}


/* addq.w (xxx.w), #xx */
static uint32_t m68k_5078_addq_w_aw (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = (int16_t) read_extension (context);

    uint16_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t a = read_word (context, address);

    uint16_t result = a + b;
    write_word (context, address, result);
    m68k_add_w_flags (context, a, b, result);

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

    return 0;
}


/* addq.l (xxx.w), #xx */
static uint32_t m68k_50b8_addq_l_aw (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = (int16_t) read_extension (context);

    uint32_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint32_t a = read_long (context, address);

    uint32_t result = a + b;
    write_long (context, address, result);
    m68k_add_l_flags (context, a, b, result);

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

    return 0;
}


/* subq.b (An), #xx */
static uint32_t m68k_5110_subq_b_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t address = context->state.a [reg];

    uint8_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint8_t a = read_byte (context, address);

    uint8_t result = a - b;
    write_byte (context, address, result);
    m68k_sub_b_flags (context, a, b, result);

    return 0;
}


/* subq.b d(An), #xx */
static uint32_t m68k_5128_subq_b_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t address = address_with_displacement (context, context->state.a [reg]);

    uint8_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint8_t a = read_byte (context, address);

    uint8_t result = a - b;
    write_byte (context, address, result);
    m68k_sub_b_flags (context, a, b, result);

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

    return 0;
}


/* subq.b (xxx.w), #xx */
static uint32_t m68k_5138_subq_b_aw (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = (int16_t) read_extension (context);

    uint8_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint8_t a = read_byte (context, address);

    uint8_t result = a - b;
    write_byte (context, address, result);
    m68k_sub_b_flags (context, a, b, result);

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

    return 0;
}


/* subq.w (An+), #xx */
static uint32_t m68k_5158_subq_w_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint16_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t a = read_word (context, context->state.a [reg]);

    uint16_t result = a - b;
    write_word (context, context->state.a [reg], result);
    context->state.a [reg] += 2;
    m68k_sub_w_flags (context, a, b, result);

    return 0;
}


/* subq.w d(An), #xx */
static uint32_t m68k_5168_subq_w_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t address = address_with_displacement (context, context->state.a [reg]);

    uint16_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t a = read_word (context, address);

    uint16_t result = a - b;
    write_word (context, address, result);
    m68k_sub_w_flags (context, a, b, result);

    return 0;
}


/* subq.w (xxx.w), #xx */
static uint32_t m68k_5178_subq_w_aw (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = (int16_t) read_extension (context);

    uint16_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t a = read_word (context, address);

    uint16_t result = a - b;
    write_word (context, address, result);
    m68k_sub_w_flags (context, a, b, result);

    return 0;
}


/* subq.l Dn, #xx */
static uint32_t m68k_5180_subq_l_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint32_t a = context->state.d [reg].l;

    uint32_t result = a - b;
    context->state.d [reg].l = result;
    m68k_sub_l_flags (context, a, b, result);

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

    return 0;
}


/* subq.l (An), #xx */
static uint32_t m68k_5190_subq_l_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint32_t a = read_long (context, context->state.a [reg]);

    uint32_t result = a - b;
    write_long (context, context->state.a [reg], result);
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* subq.l (An+), #xx */
static uint32_t m68k_5198_subq_l_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    uint32_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint32_t a = read_long (context, context->state.a [reg]);

    uint32_t result = a - b;
    write_long (context, context->state.a [reg], result);
    context->state.a [reg] += 4;
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* subq.l (-An), #xx */
static uint32_t m68k_51a0_subq_l_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    context->state.a [reg] -= 4;

    uint32_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint32_t a = read_long (context, context->state.a [reg]);

    uint32_t result = a - b;
    write_long (context, context->state.a [reg], result);
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* subq.l d(An), #xx */
static uint32_t m68k_51a8_subq_l_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t address = address_with_displacement (context, context->state.a [reg]);

    uint32_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint32_t a = read_long (context, address);

    uint32_t result = a - b;
    write_long (context, address, result);
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* subq.l d(An+Xi), #xx */
static uint32_t m68k_51b0_subq_l_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t address = address_with_index (context, context->state.a [reg]);

    uint32_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint32_t a = read_long (context, address);

    uint32_t result = a - b;
    write_long (context, address, result);
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* subq.l (xxx.w), #xx */
static uint32_t m68k_51b8_subq_l_aw (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = (int16_t) read_extension (context);

    uint32_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint32_t a = read_long (context, address);

    uint32_t result = a - b;
    write_long (context, address, result);
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* subq.l (xxx.l), #xx */
static uint32_t m68k_51b9_subq_l_al (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = read_extension_long (context);

    uint32_t b = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint32_t a = read_long (context, address);

    uint32_t result = a - b;
    write_long (context, address, result);
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* dbf Dn, #xxxx */
static uint32_t m68k_51c8_dbf (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t address = address_with_displacement (context, context->state.pc);

    context->state.d [reg].w--;
    if (context->state.d [reg].w != 0xffff)
    {
        context->state.pc = address;
    }

    return 0;
}


/* st.b Dn */
static uint32_t m68k_50c0_st_b_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    context->state.d [reg].b = 0xff;

    return 0;
}


/* st.b (An) */
static uint32_t m68k_50d0_st_b_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    write_byte (context, context->state.a [reg], 0xff);

    return 0;
}


/* st.b (An+) */
static uint32_t m68k_50d8_st_b_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    write_byte (context, context->state.a [reg], 0xff);
    context->state.a [reg] += (reg == 7) ? 2 : 1;

    return 0;
}


/* st.b (-An) */
static uint32_t m68k_50e0_st_b_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    context->state.a [reg] -= (reg == 7) ? 2 : 1;
    write_byte (context, context->state.a [reg], 0xff);

    return 0;
}


/* st.b d(An) */
static uint32_t m68k_50e8_st_b_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    write_byte_with_displacement (context, context->state.a [reg], 0xff);

    return 0;
}


/* st.b d(An+Xi) */
static uint32_t m68k_50f0_st_b_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;

    write_byte_with_index (context, context->state.a [reg], 0xff);

    return 0;
}


/* st.b (xxx.w) */
static uint32_t m68k_50f8_st_b_aw (M68000_Context *context, uint16_t instruction)
{
    write_byte_aw (context, 0xff);

    return 0;
}


/* st.b (xxx.l) */
static uint32_t m68k_50f9_st_b_al (M68000_Context *context, uint16_t instruction)
{
    write_byte_al (context, 0xff);

    return 0;
}


/* sne.b Dn */
static uint32_t m68k_56c0_sne_b_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint8_t value = (!context->state.ccr_zero) ? 0xff : 0x00;
    context->state.d [reg].b = value;

    return 0;
}


/* sne.b (An) */
static uint32_t m68k_56d0_sne_b_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint8_t value = (!context->state.ccr_zero) ? 0xff : 0x00;
    write_byte (context, context->state.a [reg], value);

    return 0;
}


/* sne.b (An+) */
static uint32_t m68k_56d8_sne_b_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint8_t value = (!context->state.ccr_zero) ? 0xff : 0x00;
    write_byte (context, context->state.a [reg], value);
    context->state.a [reg] += (reg == 7) ? 2 : 1;

    return 0;
}


/* sne.b (-An) */
static uint32_t m68k_56e0_sne_b_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint8_t value = (!context->state.ccr_zero) ? 0xff : 0x00;
    context->state.a [reg] -= (reg == 7) ? 2 : 1;
    write_byte (context, context->state.a [reg], value);

    return 0;
}


/* sne.b d(An) */
static uint32_t m68k_56e8_sne_b_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint8_t value = (!context->state.ccr_zero) ? 0xff : 0x00;
    write_byte_with_displacement (context, context->state.a [reg], value);

    return 0;
}


/* sne.b d(An+Xi) */
static uint32_t m68k_56f0_sne_b_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint8_t value = (!context->state.ccr_zero) ? 0xff : 0x00;
    write_byte_with_index (context, context->state.a [reg], value);

    return 0;
}


/* sne.b (xxx.w) */
static uint32_t m68k_56f8_sne_b_aw (M68000_Context *context, uint16_t instruction)
{
    uint8_t value = (!context->state.ccr_zero) ? 0xff : 0x00;
    write_byte_aw (context, value);

    return 0;
}


/* sne.b (xxx.l) */
static uint32_t m68k_56f9_sne_b_al (M68000_Context *context, uint16_t instruction)
{
    uint8_t value = (!context->state.ccr_zero) ? 0xff : 0x00;
    write_byte_al (context, value);

    return 0;
}


/* dbeq Dn, #xxxx */
static uint32_t m68k_57c8_dbeq (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t address = address_with_displacement (context, context->state.pc);

    if (!context->state.ccr_zero)
    {
        context->state.d [reg].w--;
        if (context->state.d [reg].w != 0xffff)
        {
            context->state.pc = address;
        }
    }

    return 0;
}


/* bra.w */
static uint32_t m68k_6000_bra_w (M68000_Context *context, uint16_t instruction)
{
    context->state.pc = address_with_displacement (context, context->state.pc);

    return 0;
}


/* bra.s */
static uint32_t m68k_6001_bra_s (M68000_Context *context, uint16_t instruction)
{
    int8_t displacement = instruction & 0xff;
    context->state.pc += displacement;

    return 0;
}


/* bsr.w */
static uint32_t m68k_6100_bsr_w (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = address_with_displacement (context, context->state.pc);

    context->state.a[7] -= 4;
    write_long (context, context->state.a[7], context->state.pc);

    context->state.pc = address;

    return 0;
}


/* bsr.s */
static uint32_t m68k_6101_bsr_s (M68000_Context *context, uint16_t instruction)
{
    int8_t displacement = instruction & 0xff;

    context->state.a[7] -= 4;
    write_long (context, context->state.a[7], context->state.pc);

    context->state.pc += displacement;

    return 0;
}


/* bhi.w */
static uint32_t m68k_6200_bhi_w (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = address_with_displacement (context, context->state.pc);

    if (!context->state.ccr_carry && !context->state.ccr_zero)
    {
        context->state.pc = address;
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
    }

    return 0;
}


/* bls.w */
static uint32_t m68k_6300_bls_w (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = address_with_displacement (context, context->state.pc);

    if (context->state.ccr_carry || context->state.ccr_zero)
    {
        context->state.pc = address;
    }

    return 0;
}


/* bls.s */
static uint32_t m68k_6301_bls_s (M68000_Context *context, uint16_t instruction)
{
    if (context->state.ccr_carry || context->state.ccr_zero)
    {
        int8_t displacement = instruction & 0xff;
        context->state.pc += displacement;
    }

    return 0;
}


/* bcc.w / bhs.w */
static uint32_t m68k_6400_bcc_w (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = address_with_displacement (context, context->state.pc);

    if (!context->state.ccr_carry)
    {
        context->state.pc = address;
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
    }

    return 0;
}


/* bcs.w / blo.w */
static uint32_t m68k_6500_bcs_w (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = address_with_displacement (context, context->state.pc);

    if (context->state.ccr_carry)
    {
        context->state.pc = address;
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
    }

    return 0;
}


/* bne.w */
static uint32_t m68k_6600_bne_w (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = address_with_displacement (context, context->state.pc);

    if (!context->state.ccr_zero)
    {
        context->state.pc = address;
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
    }

    return 0;
}


/* beq.w */
static uint32_t m68k_6700_beq_w (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = address_with_displacement (context, context->state.pc);

    if (context->state.ccr_zero)
    {
        context->state.pc = address;
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
    }

    return 0;
}


/* bpl.w */
static uint32_t m68k_6a00_bpl_w (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = address_with_displacement (context, context->state.pc);

    if (!context->state.ccr_negative)
    {
        context->state.pc = address;
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
    }

    return 0;
}


/* bmi.w */
static uint32_t m68k_6b00_bmi_w (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = address_with_displacement (context, context->state.pc);

    if (context->state.ccr_negative)
    {
        context->state.pc = address;
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
    }

    return 0;
}


/* bge.w */
static uint32_t m68k_6c00_bge_w (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = address_with_displacement (context, context->state.pc);

    if ((context->state.ccr_negative && context->state.ccr_overflow) ||
        (!context->state.ccr_negative && !context->state.ccr_overflow))
    {
        context->state.pc = address;
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
    }

    return 0;
}


/* blt.w */
static uint32_t m68k_6d00_blt_w (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = address_with_displacement (context, context->state.pc);

    if ((context->state.ccr_negative && !context->state.ccr_overflow) ||
        (!context->state.ccr_negative && context->state.ccr_overflow))
    {
        context->state.pc = address;
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
    }

    return 0;
}


/* bgt.w */
static uint32_t m68k_6e00_bgt_w (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = address_with_displacement (context, context->state.pc);

    if ((context->state.ccr_negative && context->state.ccr_overflow && !context->state.ccr_zero) ||
        (!context->state.ccr_negative && !context->state.ccr_overflow && !context->state.ccr_zero))
    {
        context->state.pc = address;
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
    }

    return 0;
}


/* ble.w */
static uint32_t m68k_6f00_ble_w (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = address_with_displacement (context, context->state.pc);

    if (context->state.ccr_zero ||
        (context->state.ccr_negative && !context->state.ccr_overflow) ||
        (!context->state.ccr_negative && context->state.ccr_overflow))
    {
        context->state.pc = address;
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

    return 0;
}


/* or.b Dn ← Dn | d(An) */
static uint32_t m68k_8028_or_b_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t b = read_byte_with_displacement (context, context->state.a [source_reg]);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a | b;

    context->state.d [dest_reg].b = result;
    m68k_move_b_flags (context, result);

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

    return 0;
}


/* divu.w Dn ← Dn ÷ Dn */
static uint32_t m68k_80c0_divu_w_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t dividend = context->state.d [dest_reg].l;
    uint32_t divisor = context->state.d [source_reg].w;

    if (divisor == 0)
    {
        m68k_exception (context, 0x14);
        return 0;
    }

    uint32_t quotient = dividend / divisor;
    uint16_t remainder = dividend % divisor;

    context->state.ccr_overflow = quotient > 0xffff;
    context->state.ccr_carry = 0;

    if (context->state.ccr_overflow)
    {
        context->state.ccr_negative = true;
        context->state.ccr_zero = false;
    }
    else
    {
        context->state.ccr_zero = (quotient == 0);
        context->state.ccr_negative = (quotient >= 0x8000);
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

    return 0;
}


/* divu.w Dn ← Dn ÷ (An) */
static uint32_t m68k_80d0_divu_w_dn_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t dividend = context->state.d [dest_reg].l;
    uint32_t divisor = read_word (context, context->state.a [source_reg]);

    if (divisor == 0)
    {
        m68k_exception (context, 0x14);
        return 0;
    }

    uint32_t quotient = dividend / divisor;
    uint16_t remainder = dividend % divisor;

    context->state.ccr_overflow = quotient > 0xffff;
    context->state.ccr_carry = 0;

    if (context->state.ccr_overflow)
    {
        context->state.ccr_negative = true;
        context->state.ccr_zero = false;
    }
    else
    {
        context->state.ccr_zero = (quotient == 0);
        context->state.ccr_negative = (quotient >= 0x8000);
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

    return 0;
}


/* divu.w Dn ← Dn ÷ (An+) */
static uint32_t m68k_80d8_divu_w_dn_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t dividend = context->state.d [dest_reg].l;
    uint32_t divisor = read_word (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 2;

    if (divisor == 0)
    {
        m68k_exception (context, 0x14);
        return 0;
    }

    uint32_t quotient = dividend / divisor;
    uint16_t remainder = dividend % divisor;

    context->state.ccr_overflow = quotient > 0xffff;
    context->state.ccr_carry = 0;

    if (context->state.ccr_overflow)
    {
        context->state.ccr_negative = true;
        context->state.ccr_zero = false;
    }
    else
    {
        context->state.ccr_zero = (quotient == 0);
        context->state.ccr_negative = (quotient >= 0x8000);
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

    return 0;
}


/* divu.w Dn ← Dn ÷ (-An) */
static uint32_t m68k_80e0_divu_w_dn_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    context->state.a [source_reg] -= 2;

    uint32_t dividend = context->state.d [dest_reg].l;
    uint32_t divisor = read_word (context, context->state.a [source_reg]);

    if (divisor == 0)
    {
        m68k_exception (context, 0x14);
        return 0;
    }

    uint32_t quotient = dividend / divisor;
    uint16_t remainder = dividend % divisor;

    context->state.ccr_overflow = quotient > 0xffff;
    context->state.ccr_carry = 0;

    if (context->state.ccr_overflow)
    {
        context->state.ccr_negative = true;
        context->state.ccr_zero = false;
    }
    else
    {
        context->state.ccr_zero = (quotient == 0);
        context->state.ccr_negative = (quotient >= 0x8000);
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

    return 0;
}


/* divu.w Dn ← Dn ÷ d(An) */
static uint32_t m68k_80e8_divu_w_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t dividend = context->state.d [dest_reg].l;
    uint32_t divisor = read_word_with_displacement (context, context->state.a [source_reg]);

    if (divisor == 0)
    {
        m68k_exception (context, 0x14);
        return 0;
    }

    uint32_t quotient = dividend / divisor;
    uint16_t remainder = dividend % divisor;

    context->state.ccr_overflow = quotient > 0xffff;
    context->state.ccr_carry = 0;

    if (context->state.ccr_overflow)
    {
        context->state.ccr_negative = true;
        context->state.ccr_zero = false;
    }
    else
    {
        context->state.ccr_zero = (quotient == 0);
        context->state.ccr_negative = (quotient >= 0x8000);
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

    return 0;
}


/* divu.w Dn ← Dn ÷ d(An+Xi) */
static uint32_t m68k_80f0_divu_w_dn_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t dividend = context->state.d [dest_reg].l;
    uint32_t divisor = read_word_with_index (context, context->state.a [source_reg]);

    if (divisor == 0)
    {
        m68k_exception (context, 0x14);
        return 0;
    }

    uint32_t quotient = dividend / divisor;
    uint16_t remainder = dividend % divisor;

    context->state.ccr_overflow = quotient > 0xffff;
    context->state.ccr_carry = 0;

    if (context->state.ccr_overflow)
    {
        context->state.ccr_negative = true;
        context->state.ccr_zero = false;
    }
    else
    {
        context->state.ccr_zero = (quotient == 0);
        context->state.ccr_negative = (quotient >= 0x8000);
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

    return 0;
}


/* divu.w Dn ← Dn ÷ (xxx.w) */
static uint32_t m68k_80f8_divu_w_dn_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t dividend = context->state.d [dest_reg].l;
    uint32_t divisor = read_word_aw (context);

    if (divisor == 0)
    {
        m68k_exception (context, 0x14);
        return 0;
    }

    uint32_t quotient = dividend / divisor;
    uint16_t remainder = dividend % divisor;

    context->state.ccr_overflow = quotient > 0xffff;
    context->state.ccr_carry = 0;

    if (context->state.ccr_overflow)
    {
        context->state.ccr_negative = true;
        context->state.ccr_zero = false;
    }
    else
    {
        context->state.ccr_zero = (quotient == 0);
        context->state.ccr_negative = (quotient >= 0x8000);
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

    return 0;
}


/* divu.w Dn ← Dn ÷ (xxx.l) */
static uint32_t m68k_80f9_divu_w_dn_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t dividend = context->state.d [dest_reg].l;
    uint32_t divisor = read_word_al (context);

    if (divisor == 0)
    {
        m68k_exception (context, 0x14);
        return 0;
    }

    uint32_t quotient = dividend / divisor;
    uint16_t remainder = dividend % divisor;

    context->state.ccr_overflow = quotient > 0xffff;
    context->state.ccr_carry = 0;

    if (context->state.ccr_overflow)
    {
        context->state.ccr_negative = true;
        context->state.ccr_zero = false;
    }
    else
    {
        context->state.ccr_zero = (quotient == 0);
        context->state.ccr_negative = (quotient >= 0x8000);
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

    return 0;
}


/* divu.w Dn ← Dn ÷ d(PC) */
static uint32_t m68k_80fa_divu_w_dn_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t dividend = context->state.d [dest_reg].l;
    uint32_t divisor = read_word_with_displacement (context, context->state.pc);

    if (divisor == 0)
    {
        m68k_exception (context, 0x14);
        return 0;
    }

    uint32_t quotient = dividend / divisor;
    uint16_t remainder = dividend % divisor;

    context->state.ccr_overflow = quotient > 0xffff;
    context->state.ccr_carry = 0;

    if (context->state.ccr_overflow)
    {
        context->state.ccr_negative = true;
        context->state.ccr_zero = false;
    }
    else
    {
        context->state.ccr_zero = (quotient == 0);
        context->state.ccr_negative = (quotient >= 0x8000);
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

    return 0;
}


/* divu.w Dn ← Dn ÷ d(PC+Xi) */
static uint32_t m68k_80fb_divu_w_dn_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t dividend = context->state.d [dest_reg].l;
    uint32_t divisor = read_word_with_index (context, context->state.pc);

    if (divisor == 0)
    {
        m68k_exception (context, 0x14);
        return 0;
    }

    uint32_t quotient = dividend / divisor;
    uint16_t remainder = dividend % divisor;

    context->state.ccr_overflow = quotient > 0xffff;
    context->state.ccr_carry = 0;

    if (context->state.ccr_overflow)
    {
        context->state.ccr_negative = true;
        context->state.ccr_zero = false;
    }
    else
    {
        context->state.ccr_zero = (quotient == 0);
        context->state.ccr_negative = (quotient >= 0x8000);
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

    return 0;
}


/* divu.w Dn ← Dn ÷ #xxxx */
static uint32_t m68k_80fc_divu_w_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t dividend = context->state.d [dest_reg].l;
    uint32_t divisor = read_extension (context);

    if (divisor == 0)
    {
        m68k_exception (context, 0x14);
        return 0;
    }

    uint32_t quotient = dividend / divisor;
    uint16_t remainder = dividend % divisor;

    context->state.ccr_overflow = quotient > 0xffff;
    context->state.ccr_carry = 0;

    if (context->state.ccr_overflow)
    {
        context->state.ccr_negative = true;
        context->state.ccr_zero = false;
    }
    else
    {
        context->state.ccr_zero = (quotient == 0);
        context->state.ccr_negative = (quotient >= 0x8000);
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

    return 0;
}


/* or.b d(An) ← d(An) | Dn */
static uint32_t m68k_8128_or_b_dan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint16_t dest_reg = instruction & 0x07;
    uint32_t address = address_with_displacement (context, context->state.a [dest_reg]);

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = read_byte (context, address);
    uint8_t result = a | b;

    write_byte (context, address, result);
    m68k_move_b_flags (context, result);

    return 0;
}


/* divs.w Dn ← Dn ÷ Dn */
static uint32_t m68k_81c0_divs_w_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    int32_t dividend = context->state.d [dest_reg].l;
    int32_t divisor = (int16_t) context->state.d [source_reg].w;

    if (divisor == 0)
    {
        m68k_exception (context, 0x14);
        return 0;
    }

    int32_t quotient = dividend / divisor;
    int16_t remainder = dividend % divisor;

    context->state.ccr_overflow = quotient > 32767 || quotient < -32768;
    context->state.ccr_carry = 0;

    if (context->state.ccr_overflow)
    {
        context->state.ccr_negative = true;
        context->state.ccr_zero = false;
    }
    else
    {
        context->state.ccr_negative = (quotient < 0);
        context->state.ccr_zero = (quotient == 0);
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

    return 0;
}


/* divs.w Dn ← Dn ÷ (An) */
static uint32_t m68k_81d0_divs_w_dn_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    int32_t dividend = context->state.d [dest_reg].l;
    int32_t divisor = (int16_t) read_word (context, context->state.a [source_reg]);

    if (divisor == 0)
    {
        m68k_exception (context, 0x14);
        return 0;
    }

    int32_t quotient = dividend / divisor;
    int16_t remainder = dividend % divisor;

    context->state.ccr_overflow = quotient > 32767 || quotient < -32768;
    context->state.ccr_carry = 0;

    if (context->state.ccr_overflow)
    {
        context->state.ccr_negative = true;
        context->state.ccr_zero = false;
    }
    else
    {
        context->state.ccr_negative = (quotient < 0);
        context->state.ccr_zero = (quotient == 0);
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

    return 0;
}


/* divs.w Dn ← Dn ÷ (An+) */
static uint32_t m68k_81d8_divs_w_dn_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    int32_t dividend = context->state.d [dest_reg].l;
    int32_t divisor = (int16_t) read_word (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 2;

    if (divisor == 0)
    {
        m68k_exception (context, 0x14);
        return 0;
    }

    int32_t quotient = dividend / divisor;
    int16_t remainder = dividend % divisor;

    context->state.ccr_overflow = quotient > 32767 || quotient < -32768;
    context->state.ccr_carry = 0;

    if (context->state.ccr_overflow)
    {
        context->state.ccr_negative = true;
        context->state.ccr_zero = false;
    }
    else
    {
        context->state.ccr_negative = (quotient < 0);
        context->state.ccr_zero = (quotient == 0);
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

    return 0;
}


/* divs.w Dn ← Dn ÷ (-An) */
static uint32_t m68k_81e0_divs_w_dn_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    context->state.a [source_reg] -= 2;

    int32_t dividend = context->state.d [dest_reg].l;
    int32_t divisor = (int16_t) read_word (context, context->state.a [source_reg]);

    if (divisor == 0)
    {
        m68k_exception (context, 0x14);
        return 0;
    }

    int32_t quotient = dividend / divisor;
    int16_t remainder = dividend % divisor;

    context->state.ccr_overflow = quotient > 32767 || quotient < -32768;
    context->state.ccr_carry = 0;

    if (context->state.ccr_overflow)
    {
        context->state.ccr_negative = true;
        context->state.ccr_zero = false;
    }
    else
    {
        context->state.ccr_negative = (quotient < 0);
        context->state.ccr_zero = (quotient == 0);
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

    return 0;
}


/* divs.w Dn ← Dn ÷ d(An) */
static uint32_t m68k_81e8_divs_w_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    int32_t dividend = context->state.d [dest_reg].l;
    int32_t divisor = (int16_t) read_word_with_displacement (context, context->state.a [source_reg]);

    if (divisor == 0)
    {
        m68k_exception (context, 0x14);
        return 0;
    }

    int32_t quotient = dividend / divisor;
    int16_t remainder = dividend % divisor;

    context->state.ccr_overflow = quotient > 32767 || quotient < -32768;
    context->state.ccr_carry = 0;

    if (context->state.ccr_overflow)
    {
        context->state.ccr_negative = true;
        context->state.ccr_zero = false;
    }
    else
    {
        context->state.ccr_negative = (quotient < 0);
        context->state.ccr_zero = (quotient == 0);
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

    return 0;
}


/* divs.w Dn ← Dn ÷ d(An+Xi) */
static uint32_t m68k_81f0_divs_w_dn_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    int32_t dividend = context->state.d [dest_reg].l;
    int32_t divisor = (int16_t) read_word_with_index (context, context->state.a [source_reg]);

    if (divisor == 0)
    {
        m68k_exception (context, 0x14);
        return 0;
    }

    int32_t quotient = dividend / divisor;
    int16_t remainder = dividend % divisor;

    context->state.ccr_overflow = quotient > 32767 || quotient < -32768;
    context->state.ccr_carry = 0;

    if (context->state.ccr_overflow)
    {
        context->state.ccr_negative = true;
        context->state.ccr_zero = false;
    }
    else
    {
        context->state.ccr_negative = (quotient < 0);
        context->state.ccr_zero = (quotient == 0);
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

    return 0;
}


/* divs.w Dn ← Dn ÷ (xxx.w) */
static uint32_t m68k_81f8_divs_w_dn_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    int32_t dividend = context->state.d [dest_reg].l;
    int32_t divisor = (int16_t) read_word_aw (context);

    if (divisor == 0)
    {
        m68k_exception (context, 0x14);
        return 0;
    }

    int32_t quotient = dividend / divisor;
    int16_t remainder = dividend % divisor;

    context->state.ccr_overflow = quotient > 32767 || quotient < -32768;
    context->state.ccr_carry = 0;

    if (context->state.ccr_overflow)
    {
        context->state.ccr_negative = true;
        context->state.ccr_zero = false;
    }
    else
    {
        context->state.ccr_negative = (quotient < 0);
        context->state.ccr_zero = (quotient == 0);
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

    return 0;
}


/* divs.w Dn ← Dn ÷ (xxx.l) */
static uint32_t m68k_81f9_divs_w_dn_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    int32_t dividend = context->state.d [dest_reg].l;
    int32_t divisor = (int16_t) read_word_al (context);

    if (divisor == 0)
    {
        m68k_exception (context, 0x14);
        return 0;
    }

    int32_t quotient = dividend / divisor;
    int16_t remainder = dividend % divisor;

    context->state.ccr_overflow = quotient > 32767 || quotient < -32768;
    context->state.ccr_carry = 0;

    if (context->state.ccr_overflow)
    {
        context->state.ccr_negative = true;
        context->state.ccr_zero = false;
    }
    else
    {
        context->state.ccr_negative = (quotient < 0);
        context->state.ccr_zero = (quotient == 0);
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

    return 0;
}


/* divs.w Dn ← Dn ÷ d(PC) */
static uint32_t m68k_81fa_divs_w_dn_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    int32_t dividend = context->state.d [dest_reg].l;
    int32_t divisor = (int16_t) read_word_with_displacement (context, context->state.pc);

    if (divisor == 0)
    {
        m68k_exception (context, 0x14);
        return 0;
    }

    int32_t quotient = dividend / divisor;
    int16_t remainder = dividend % divisor;

    context->state.ccr_overflow = quotient > 32767 || quotient < -32768;
    context->state.ccr_carry = 0;

    if (context->state.ccr_overflow)
    {
        context->state.ccr_negative = true;
        context->state.ccr_zero = false;
    }
    else
    {
        context->state.ccr_negative = (quotient < 0);
        context->state.ccr_zero = (quotient == 0);
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

    return 0;
}


/* divs.w Dn ← Dn ÷ d(PC+Xi) */
static uint32_t m68k_81fb_divs_w_dn_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    int32_t dividend = context->state.d [dest_reg].l;
    int32_t divisor = (int16_t) read_word_with_index (context, context->state.pc);

    if (divisor == 0)
    {
        m68k_exception (context, 0x14);
        return 0;
    }

    int32_t quotient = dividend / divisor;
    int16_t remainder = dividend % divisor;

    context->state.ccr_overflow = quotient > 32767 || quotient < -32768;
    context->state.ccr_carry = 0;

    if (context->state.ccr_overflow)
    {
        context->state.ccr_negative = true;
        context->state.ccr_zero = false;
    }
    else
    {
        context->state.ccr_negative = (quotient < 0);
        context->state.ccr_zero = (quotient == 0);
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

    return 0;
}


/* divs.w Dn ← Dn ÷ #xxxx */
static uint32_t m68k_81fc_divs_w_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    int32_t dividend = context->state.d [dest_reg].l;
    int32_t divisor = (int16_t) read_extension (context);

    if (divisor == 0)
    {
        m68k_exception (context, 0x14);
        return 0;
    }

    int32_t quotient = dividend / divisor;
    int16_t remainder = dividend % divisor;

    context->state.ccr_overflow = quotient > 32767 || quotient < -32768;
    context->state.ccr_carry = 0;

    if (context->state.ccr_overflow)
    {
        context->state.ccr_negative = true;
        context->state.ccr_zero = false;
    }
    else
    {
        context->state.ccr_negative = (quotient < 0);
        context->state.ccr_zero = (quotient == 0);
        context->state.d [dest_reg].w_low = quotient;
        context->state.d [dest_reg].w_high = remainder;
    }

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

    return 0;
}


/* sub.b Dn ← Dn - (An) */
static uint32_t m68k_9010_sub_b_dn_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t b = read_byte (context, context->state.a [source_reg]);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    context->state.d [dest_reg].b = result;
    m68k_sub_b_flags (context, a, b, result);

    return 0;
}


/* sub.b Dn ← Dn - (An+) */
static uint32_t m68k_9018_sub_b_dn_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t b = read_byte (context, context->state.a [source_reg]);
    context->state.a [source_reg] += (source_reg == 7) ? 2 : 1;
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    context->state.d [dest_reg].b = result;
    m68k_sub_b_flags (context, a, b, result);

    return 0;
}


/* sub.b Dn ← Dn - (-An) */
static uint32_t m68k_9020_sub_b_dn_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [source_reg] -= (source_reg == 7) ? 2 : 1;
    uint8_t b = read_byte (context, context->state.a [source_reg]);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    context->state.d [dest_reg].b = result;
    m68k_sub_b_flags (context, a, b, result);

    return 0;
}


/* sub.b Dn ← Dn - d(An) */
static uint32_t m68k_9028_sub_b_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t b = read_byte_with_displacement (context, context->state.a [source_reg]);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    context->state.d [dest_reg].b = result;
    m68k_sub_b_flags (context, a, b, result);

    return 0;
}


/* sub.b Dn ← Dn - d(An+Xi) */
static uint32_t m68k_9030_sub_b_dn_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t b = read_byte_with_index (context, context->state.a [source_reg]);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    context->state.d [dest_reg].b = result;
    m68k_sub_b_flags (context, a, b, result);

    return 0;
}


/* sub.b Dn ← Dn - (xxx.w) */
static uint32_t m68k_9038_sub_b_dn_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t b = read_byte_aw (context);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    context->state.d [dest_reg].b = result;
    m68k_sub_b_flags (context, a, b, result);

    return 0;
}


/* sub.b Dn ← Dn - (xxx.l) */
static uint32_t m68k_9039_sub_b_dn_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t b = read_byte_al (context);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    context->state.d [dest_reg].b = result;
    m68k_sub_b_flags (context, a, b, result);

    return 0;
}


/* sub.b Dn ← Dn - d(PC) */
static uint32_t m68k_903a_sub_b_dn_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t b = read_byte_with_displacement (context, context->state.pc);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    context->state.d [dest_reg].b = result;
    m68k_sub_b_flags (context, a, b, result);

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

    return 0;
}


/* sub.b Dn ← Dn - #xx */
static uint32_t m68k_903c_sub_b_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t b = read_extension (context);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    context->state.d [dest_reg].b = result;
    m68k_sub_b_flags (context, a, b, result);

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

    return 0;
}


/* sub.w Dn ← Dn - (An+) */
static uint32_t m68k_9058_sub_w_dn_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t b = read_word (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 2;
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a - b;

    context->state.d [dest_reg].w = result;
    m68k_sub_w_flags (context, a, b, result);

    return 0;
}


/* sub.w Dn ← Dn - (-An) */
static uint32_t m68k_9060_sub_w_dn_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [source_reg] -= 2;
    uint16_t b = read_word (context, context->state.a [source_reg]);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a - b;

    context->state.d [dest_reg].w = result;
    m68k_sub_w_flags (context, a, b, result);

    return 0;
}


/* sub.w Dn ← Dn - d(An) */
static uint32_t m68k_9068_sub_w_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t b = read_word_with_displacement (context, context->state.a [source_reg]);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a - b;

    context->state.d [dest_reg].w = result;
    m68k_sub_w_flags (context, a, b, result);

    return 0;
}


/* sub.w Dn ← Dn - d(An+Xi) */
static uint32_t m68k_9070_sub_w_dn_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t b = read_word_with_index (context, context->state.a [source_reg]);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a - b;

    context->state.d [dest_reg].w = result;
    m68k_sub_w_flags (context, a, b, result);

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

    return 0;
}


/* sub.w Dn ← Dn - (xxx.l) */
static uint32_t m68k_9079_sub_w_dn_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t b = read_word_al (context);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a - b;

    context->state.d [dest_reg].w = result;
    m68k_sub_w_flags (context, a, b, result);

    return 0;
}


/* sub.w Dn ← Dn - d(PC) */
static uint32_t m68k_907a_sub_w_dn_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t b = read_word_with_displacement (context, context->state.pc);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a - b;

    context->state.d [dest_reg].w = result;
    m68k_sub_w_flags (context, a, b, result);

    return 0;
}


/* sub.w Dn ← Dn - d(PC+Xi) */
static uint32_t m68k_907b_sub_w_dn_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t b = read_word_with_index (context, context->state.pc);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a - b;

    context->state.d [dest_reg].w = result;
    m68k_sub_w_flags (context, a, b, result);

    return 0;
}


/* sub.w Dn ← Dn - #xxxx */
static uint32_t m68k_907c_sub_w_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t b = read_extension (context);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a - b;

    context->state.d [dest_reg].w = result;
    m68k_sub_w_flags (context, a, b, result);

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

    return 0;
}


/* sub.l Dn ← Dn - An */
static uint32_t m68k_9088_sub_l_dn_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t b = context->state.a [source_reg];
    uint32_t a = context->state.d [dest_reg].l;
    uint32_t result = a - b;

    context->state.d [dest_reg].l = result;
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* sub.l Dn ← Dn - (An) */
static uint32_t m68k_9090_sub_l_dn_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t b = read_long (context, context->state.a [source_reg]);
    uint32_t a = context->state.d [dest_reg].l;
    uint32_t result = a - b;

    context->state.d [dest_reg].l = result;
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* sub.l Dn ← Dn - (An+) */
static uint32_t m68k_9098_sub_l_dn_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t b = read_long (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 4;
    uint32_t a = context->state.d [dest_reg].l;
    uint32_t result = a - b;

    context->state.d [dest_reg].l = result;
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* sub.l Dn ← Dn - (-An) */
static uint32_t m68k_90a0_sub_l_dn_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    context->state.a [source_reg] -= 4;

    uint32_t b = read_long (context, context->state.a [source_reg]);
    uint32_t a = context->state.d [dest_reg].l;
    uint32_t result = a - b;

    context->state.d [dest_reg].l = result;
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* sub.l Dn ← Dn - d(An) */
static uint32_t m68k_90a8_sub_l_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t b = read_long_with_displacement (context, context->state.a [source_reg]);
    uint32_t a = context->state.d [dest_reg].l;
    uint32_t result = a - b;

    context->state.d [dest_reg].l = result;
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* sub.l Dn ← Dn - d(An+Xi) */
static uint32_t m68k_90b0_sub_l_dn_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    uint32_t address = address_with_index (context, context->state.a [source_reg]);

    uint32_t b = read_long (context, address);
    uint32_t a = context->state.d [dest_reg].l;
    uint32_t result = a - b;

    context->state.d [dest_reg].l = result;
    m68k_sub_l_flags (context, a, b, result);

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

    return 0;
}


/* sub.l Dn ← Dn - (xxx.l) */
static uint32_t m68k_90b9_sub_l_dn_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_long_al (context);
    uint32_t a = context->state.d [dest_reg].l;
    uint32_t result = a - b;

    context->state.d [dest_reg].l = result;
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* sub.l Dn ← Dn - d(PC) */
static uint32_t m68k_90ba_sub_l_dn_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_long_with_displacement (context, context->state.pc);
    uint32_t a = context->state.d [dest_reg].l;
    uint32_t result = a - b;

    context->state.d [dest_reg].l = result;
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* sub.l Dn ← Dn - d(PC+Xi) */
static uint32_t m68k_90bb_sub_l_dn_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint32_t address = address_with_index (context, context->state.pc);

    uint32_t b = read_long (context, address);
    uint32_t a = context->state.d [dest_reg].l;
    uint32_t result = a - b;

    context->state.d [dest_reg].l = result;
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* sub.l Dn ← Dn - #xxxxxxxx */
static uint32_t m68k_90bc_sub_l_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_extension_long (context);
    uint32_t a = context->state.d [dest_reg].l;
    uint32_t result = a - b;

    context->state.d [dest_reg].l = result;
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* suba.w An ← An - Dn */
static uint32_t m68k_90c0_suba_w_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = (int16_t) context->state.d [source_reg].w;
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* suba.w An ← An - An */
static uint32_t m68k_90c8_suba_w_an_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = (int16_t) (context->state.a [source_reg] & 0xffff);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* suba.w An ← An - (An) */
static uint32_t m68k_90d0_suba_w_an_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = (int16_t) read_word (context, context->state.a [source_reg]);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* suba.w An ← An - (An+) */
static uint32_t m68k_90d8_suba_w_an_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = (int16_t) read_word (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 2;
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* suba.w An ← An - (-An) */
static uint32_t m68k_90e0_suba_w_an_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    context->state.a [source_reg] -= 2;
    uint32_t b = (int16_t) read_word (context, context->state.a [source_reg]);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* suba.w An ← An - d(An) */
static uint32_t m68k_90e8_suba_w_an_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = (int16_t) read_word_with_displacement (context, context->state.a [source_reg]);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* suba.w An ← An - d(An+Xi) */
static uint32_t m68k_90f0_suba_w_an_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = (int16_t) read_word_with_index (context, context->state.a [source_reg]);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* suba.w An ← An - (xxx.w) */
static uint32_t m68k_90f8_suba_w_an_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = (int16_t) read_word_aw (context);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* suba.w An ← An - (xxx.l) */
static uint32_t m68k_90f9_suba_w_an_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = (int16_t) read_word_al (context);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* suba.w An ← An - d(PC) */
static uint32_t m68k_90fa_suba_w_an_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = (int16_t) read_word_with_displacement (context, context->state.pc);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* suba.w An ← An - d(PC+Xi) */
static uint32_t m68k_90fb_suba_w_an_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = (int16_t) read_word_with_index (context, context->state.pc);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* suba.w An ← An - #xxxx */
static uint32_t m68k_90fc_suba_w_an_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = (int16_t) read_extension (context);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* sub.b (An) ← (An) - Dn */
static uint32_t m68k_9110_sub_b_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint16_t dest_reg = instruction & 0x07;

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = read_byte (context, context->state.a [dest_reg]);
    uint8_t result = a - b;

    write_byte (context, context->state.a [dest_reg], result);
    m68k_sub_b_flags (context, a, b, result);

    return 0;
}


/* sub.b (An+) ← (An+) - Dn */
static uint32_t m68k_9118_sub_b_anp_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint16_t dest_reg = instruction & 0x07;

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = read_byte (context, context->state.a [dest_reg]);
    uint8_t result = a - b;

    write_byte (context, context->state.a [dest_reg], result);
    context->state.a [dest_reg] += (dest_reg == 7) ? 2 : 1;
    m68k_sub_b_flags (context, a, b, result);

    return 0;
}


/* sub.b (-An) ← (-An) - Dn */
static uint32_t m68k_9120_sub_b_pan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint16_t dest_reg = instruction & 0x07;

    uint8_t b = context->state.d [source_reg].b;
    context->state.a [dest_reg] -= (dest_reg == 7) ? 2 : 1;
    uint8_t a = read_byte (context, context->state.a [dest_reg]);
    uint8_t result = a - b;

    write_byte (context, context->state.a [dest_reg], result);
    m68k_sub_b_flags (context, a, b, result);

    return 0;
}


/* sub.b d(An) ← d(An) - Dn */
static uint32_t m68k_9128_sub_b_dan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = instruction & 0x07;
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint32_t address = address_with_displacement (context, context->state.a [dest_reg]);

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = read_byte (context, address);
    uint8_t result = a - b;

    write_byte (context, address, result);
    m68k_sub_b_flags (context, a, b, result);

    return 0;
}


/* sub.b d(An+Xi) ← d(An+Xi) - Dn */
static uint32_t m68k_9130_sub_b_danxi_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = instruction & 0x07;
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint32_t address = address_with_index (context, context->state.a [dest_reg]);

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = read_byte (context, address);
    uint8_t result = a - b;

    write_byte (context, address, result);
    m68k_sub_b_flags (context, a, b, result);

    return 0;
}


/* sub.b (xxx.w) ← (xxx.w) - Dn */
static uint32_t m68k_9138_sub_b_aw_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint32_t address = (int16_t) read_extension (context);

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = read_byte (context, address);
    uint8_t result = a - b;

    write_byte (context, address, result);
    m68k_sub_b_flags (context, a, b, result);

    return 0;
}


/* sub.b (xxx.l) ← (xxx.l) - Dn */
static uint32_t m68k_9139_sub_b_al_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint32_t address = read_extension_long (context);

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = read_byte (context, address);
    uint8_t result = a - b;

    write_byte (context, address, result);
    m68k_sub_b_flags (context, a, b, result);

    return 0;
}


/* sub.w (An) ← (An) - Dn */
static uint32_t m68k_9150_sub_w_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint16_t dest_reg = instruction & 0x07;
    uint32_t address = context->state.a [dest_reg];

    uint16_t b = context->state.d [source_reg].w;
    uint16_t a = read_word (context, address);
    uint16_t result = a - b;

    write_word (context, address, result);
    m68k_sub_w_flags (context, a, b, result);

    return 0;
}


/* sub.w (An+) ← (An+) - Dn */
static uint32_t m68k_9158_sub_w_anp_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint16_t dest_reg = instruction & 0x07;
    uint32_t address = context->state.a [dest_reg];
    context->state.a [dest_reg] += 2;

    uint16_t b = context->state.d [source_reg].w;
    uint16_t a = read_word (context, address);
    uint16_t result = a - b;

    write_word (context, address, result);
    m68k_sub_w_flags (context, a, b, result);

    return 0;
}


/* sub.w (-An) ← (-An) - Dn */
static uint32_t m68k_9160_sub_w_pan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint16_t dest_reg = instruction & 0x07;
    context->state.a [dest_reg] -= 2;
    uint32_t address = context->state.a [dest_reg];

    uint16_t b = context->state.d [source_reg].w;
    uint16_t a = read_word (context, address);
    uint16_t result = a - b;

    write_word (context, address, result);
    m68k_sub_w_flags (context, a, b, result);

    return 0;
}


/* sub.w d(An) ← d(An) - Dn */
static uint32_t m68k_9168_sub_w_dan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint16_t dest_reg = instruction & 0x07;
    uint32_t address = address_with_displacement (context, context->state.a [dest_reg]);

    uint16_t b = context->state.d [source_reg].w;
    uint16_t a = read_word (context, address);
    uint16_t result = a - b;

    write_word (context, address, result);
    m68k_sub_w_flags (context, a, b, result);

    return 0;
}


/* sub.w d(An+Xi) ← d(An+Xi) - Dn */
static uint32_t m68k_9170_sub_w_danxi_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint16_t dest_reg = instruction & 0x07;
    uint32_t address = address_with_index (context, context->state.a [dest_reg]);

    uint16_t b = context->state.d [source_reg].w;
    uint16_t a = read_word (context, address);
    uint16_t result = a - b;

    write_word (context, address, result);
    m68k_sub_w_flags (context, a, b, result);

    return 0;
}


/* sub.w (xxx.w) ← (xxx.w) - Dn */
static uint32_t m68k_9178_sub_w_aw_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint32_t address = (int16_t) read_extension (context);

    uint16_t b = context->state.d [source_reg].w;
    uint16_t a = read_word (context, address);
    uint16_t result = a - b;

    write_word (context, address, result);
    m68k_sub_w_flags (context, a, b, result);

    return 0;
}


/* sub.w (xxx.l) ← (xxx.l) - Dn */
static uint32_t m68k_9179_sub_w_al_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint32_t address = read_extension_long (context);

    uint16_t b = context->state.d [source_reg].w;
    uint16_t a = read_word (context, address);
    uint16_t result = a - b;

    write_word (context, address, result);
    m68k_sub_w_flags (context, a, b, result);

    return 0;
}


/* sub.l (An) ← (An) - Dn */
static uint32_t m68k_9190_sub_l_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint16_t dest_reg = instruction & 0x07;

    uint32_t b = context->state.d [source_reg].l;
    uint32_t a = read_long (context, context->state.a [dest_reg]);
    uint32_t result = a - b;

    write_long (context, context->state.a [dest_reg], result);
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* sub.l (An+) ← (An+) - Dn */
static uint32_t m68k_9198_sub_l_anp_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint16_t dest_reg = instruction & 0x07;

    uint32_t b = context->state.d [source_reg].l;
    uint32_t a = read_long (context, context->state.a [dest_reg]);
    uint32_t result = a - b;

    write_long (context, context->state.a [dest_reg], result);
    context->state.a [dest_reg] += 4;
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* sub.l (-An) ← (-An) - Dn */
static uint32_t m68k_91a0_sub_l_pan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint16_t dest_reg = instruction & 0x07;
    context->state.a [dest_reg] -= 4;

    uint32_t b = context->state.d [source_reg].l;
    uint32_t a = read_long (context, context->state.a [dest_reg]);
    uint32_t result = a - b;

    write_long (context, context->state.a [dest_reg], result);
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* sub.l d(An) ← d(An) - Dn */
static uint32_t m68k_91a8_sub_l_dan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint16_t dest_reg = instruction & 0x07;
    uint32_t address = address_with_displacement (context, context->state.a [dest_reg]);

    uint32_t b = context->state.d [source_reg].l;
    uint32_t a = read_long (context, address);
    uint32_t result = a - b;

    write_long (context, address, result);
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* sub.l d(An+Xi) ← d(An+Xi) - Dn */
static uint32_t m68k_91b0_sub_l_danxi_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint16_t dest_reg = instruction & 0x07;
    uint32_t address = address_with_index (context, context->state.a [dest_reg]);

    uint32_t b = context->state.d [source_reg].l;
    uint32_t a = read_long (context, address);
    uint32_t result = a - b;

    write_long (context, address, result);
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* sub.l (xxx.w) ← (xxx.w) - Dn */
static uint32_t m68k_91b8_sub_l_aw_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint32_t address = (int16_t) read_extension (context);

    uint32_t b = context->state.d [source_reg].l;
    uint32_t a = read_long (context, address);
    uint32_t result = a - b;

    write_long (context, address, result);
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* sub.l (xxx.l) ← (xxx.l) - Dn */
static uint32_t m68k_91b9_sub_l_al_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint32_t address = read_extension_long (context);

    uint32_t b = context->state.d [source_reg].l;
    uint32_t a = read_long (context, address);
    uint32_t result = a - b;

    write_long (context, address, result);
    m68k_sub_l_flags (context, a, b, result);

    return 0;
}


/* suba.l An ← An - Dn */
static uint32_t m68k_91c0_suba_l_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = context->state.d [source_reg].l;
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* suba.l An ← An - An */
static uint32_t m68k_91c8_suba_l_an_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = context->state.a [source_reg];
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* suba.l An ← An - (An) */
static uint32_t m68k_91d0_suba_l_an_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_long (context, context->state.a [source_reg]);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* suba.l An ← An - (An+) */
static uint32_t m68k_91d8_suba_l_an_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_long (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 4;
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* suba.l An ← An - (-An) */
static uint32_t m68k_91e0_suba_l_an_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    context->state.a [source_reg] -= 4;
    uint32_t b = read_long (context, context->state.a [source_reg]);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* suba.l An ← An - d(An) */
static uint32_t m68k_91e8_suba_l_an_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_long_with_displacement (context, context->state.a [source_reg]);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* suba.l An ← An - d(An+Xi) */
static uint32_t m68k_91f0_suba_l_an_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_long_with_index (context, context->state.a [source_reg]);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* suba.l An ← An - (xxx.w) */
static uint32_t m68k_91f8_suba_l_an_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_long_aw (context);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* suba.l An ← An - (xxx.l) */
static uint32_t m68k_91f9_suba_l_an_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_long_al (context);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* suba.l An ← An - d(PC) */
static uint32_t m68k_91fa_suba_l_an_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_long_with_displacement (context, context->state.pc);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* suba.l An ← An - d(PC+Xi) */
static uint32_t m68k_91fb_suba_l_an_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_long_with_index (context, context->state.pc);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* suba.l An ← An - #xxxxxxxx */
static uint32_t m68k_91fc_suba_l_an_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_extension_long (context);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    context->state.a [dest_reg] = result;

    return 0;
}


/* Unimplemented - Line-A */
static uint32_t m68k_a000_line_a (M68000_Context *context, uint16_t instruction)
{
    context->state.pc -= 2;
    m68k_exception (context, 0x28);

    return 0;
}


/* cmp.b Dn - Dn */
static uint32_t m68k_b000_cmp_b_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

    return 0;
}


/* cmp.b Dn - (An) */
static uint32_t m68k_b010_cmp_b_dn_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t b = read_byte (context, context->state.a [source_reg]);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

    return 0;
}


/* cmp.b Dn - (An+) */
static uint32_t m68k_b018_cmp_b_dn_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t b = read_byte (context, context->state.a [source_reg]);
    context->state.a [source_reg] += (source_reg == 7) ? 2 : 1;
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

    return 0;
}


/* cmp.b Dn - (-An) */
static uint32_t m68k_b020_cmp_b_dn_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    context->state.a [source_reg] -= (source_reg == 7) ? 2 : 1;
    uint8_t b = read_byte (context, context->state.a [source_reg]);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

    return 0;
}


/* cmp.b Dn - d(An) */
static uint32_t m68k_b028_cmp_b_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t b = read_byte_with_displacement (context, context->state.a [source_reg]);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

    return 0;
}


/* cmp.b Dn - d(An+Xi) */
static uint32_t m68k_b030_cmp_b_dn_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t b = read_byte_with_index (context, context->state.a [source_reg]);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

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

    return 0;
}


/* cmp.b Dn - (xxx.l) */
static uint32_t m68k_b039_cmp_b_dn_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t b = read_byte_al (context);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

    return 0;
}


/* cmp.b Dn - d(PC) */
static uint32_t m68k_b03a_cmp_b_dn_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t b = read_byte_with_displacement (context, context->state.pc);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

    return 0;
}


/* cmp.b Dn - d(PC+Xi) */
static uint32_t m68k_b03b_cmp_b_dn_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t b = read_byte_with_index (context, context->state.pc);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

    return 0;
}


/* cmp.b Dn - #xx */
static uint32_t m68k_b03c_cmp_b_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t b = read_extension (context);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a - b;

    m68k_cmp_b_flags (context, a, b, result);

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

    return 0;
}


/* cmp.w Dn - An */
static uint32_t m68k_b048_cmp_w_dn_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t b = context->state.a [source_reg];
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a - b;

    m68k_cmp_w_flags (context, a, b, result);

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

    return 0;
}


/* cmp.w Dn - d(An) */
static uint32_t m68k_b068_cmp_w_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t b = read_word_with_displacement (context, context->state.a [source_reg]);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a - b;

    m68k_cmp_w_flags (context, a, b, result);

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

    return 0;
}


/* cmp.l Dn - (An) */
static uint32_t m68k_b090_cmp_l_dn_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint32_t b = read_long (context, context->state.a [source_reg]);
    uint32_t a = context->state.d [dest_reg].l;
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmp.l Dn - (An) */
static uint32_t m68k_b0b8_cmp_l_dn_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_long_aw (context);
    uint32_t a = context->state.d [dest_reg].l;
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmpa.w An - Dn */
static uint32_t m68k_b0c0_cmpa_w_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = (int16_t) context->state.d [source_reg].w;
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmpa.w An - An */
static uint32_t m68k_b0c8_cmpa_w_an_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = (int16_t) (context->state.a [source_reg] & 0xffff);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmpa.w An - (An) */
static uint32_t m68k_b0d0_cmpa_w_an_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = (int16_t) read_word (context, context->state.a [source_reg]);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmpa.l An - (An+) */
static uint32_t m68k_b0d8_cmpa_w_an_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = (int16_t) read_word (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 2;
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmpa.w An - (-An) */
static uint32_t m68k_b0e0_cmpa_w_an_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    context->state.a [source_reg] -= 2;
    uint32_t b = (int16_t) read_word (context, context->state.a [source_reg]);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmpa.w An - d(An) */
static uint32_t m68k_b0e8_cmpa_w_an_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = (int16_t) read_word_with_displacement (context, context->state.a [source_reg]);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmpa.w An - d(An+Xi) */
static uint32_t m68k_b0f0_cmpa_w_an_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = (int16_t) read_word_with_index (context, context->state.a [source_reg]);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmpa.w An - (xxx.w) */
static uint32_t m68k_b0f8_cmpa_w_an_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = (int16_t) read_word_aw (context);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmpa.w An - (xxx.l) */
static uint32_t m68k_b0f9_cmpa_w_an_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = (int16_t) read_word_al (context);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmpa.w An - d(PC) */
static uint32_t m68k_b0fa_cmpa_w_an_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = (int16_t) read_word_with_displacement (context, context->state.pc);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmpa.w An - d(PC+Xi) */
static uint32_t m68k_b0fb_cmpa_w_an_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = (int16_t) read_word_with_index (context, context->state.pc);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmpa.w An - #xxxxxxxx */
static uint32_t m68k_b0fc_cmpa_w_an_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = (int16_t) read_extension (context);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

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

    return 0;
}


/* cmpa.l An - Dn */
static uint32_t m68k_b1c0_cmpa_l_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = context->state.d [source_reg].l;
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmpa.l An - An */
static uint32_t m68k_b1c8_cmpa_l_an_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = context->state.a [source_reg];
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmpa.l An - (An) */
static uint32_t m68k_b1d0_cmpa_l_an_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_long (context, context->state.a [source_reg]);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmpa.l An - (An+) */
static uint32_t m68k_b1d8_cmpa_l_an_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_long (context, context->state.a [source_reg]);
    context->state.a [source_reg] += 4;
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmpa.l An - (-An) */
static uint32_t m68k_b1e0_cmpa_l_an_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    context->state.a [source_reg] -= 4;
    uint32_t b = read_long (context, context->state.a [source_reg]);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmpa.l An - d(An) */
static uint32_t m68k_b1e8_cmpa_l_an_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_long_with_displacement (context, context->state.a [source_reg]);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmpa.l An - d(An+Xi) */
static uint32_t m68k_b1f0_cmpa_l_an_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_long_with_index (context, context->state.a [source_reg]);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmpa.l An - (xxx.w) */
static uint32_t m68k_b1f8_cmpa_l_an_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_long_aw (context);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmpa.l An - (xxx.l) */
static uint32_t m68k_b1f9_cmpa_l_an_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_long_al (context);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmpa.l An - d(PC) */
static uint32_t m68k_b1fa_cmpa_l_an_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_long_with_displacement (context, context->state.pc);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmpa.l An - d(PC+Xi) */
static uint32_t m68k_b1fb_cmpa_l_an_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_long_with_index (context, context->state.pc);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

    return 0;
}


/* cmpa.l An - #xxxxxxxx */
static uint32_t m68k_b1fc_cmpa_l_an_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint32_t b = read_extension_long (context);
    uint32_t a = context->state.a [dest_reg];
    uint32_t result = a - b;

    m68k_cmp_l_flags (context, a, b, result);

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

    return 0;
}


/* and.b Dn ← Dn & (An) */
static uint32_t m68k_c010_and_b_dn_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t b = read_byte (context, context->state.a [source_reg]);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a & b;

    context->state.d [dest_reg].b = result;
    m68k_move_b_flags (context, result);

    return 0;
}


/* and.b Dn ← Dn & (An+) */
static uint32_t m68k_c018_and_b_dn_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t b = read_byte (context, context->state.a [source_reg]);
    context->state.a [source_reg] += (source_reg == 7) ? 2 : 1;
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a & b;

    context->state.d [dest_reg].b = result;
    m68k_move_b_flags (context, result);

    return 0;
}


/* and.b Dn ← Dn & (-An) */
static uint32_t m68k_c020_and_b_dn_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [source_reg] -= (source_reg == 7) ? 2 : 1;
    uint8_t b = read_byte (context, context->state.a [source_reg]);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a & b;

    context->state.d [dest_reg].b = result;
    m68k_move_b_flags (context, result);

    return 0;
}


/* and.b Dn ← Dn & d(An) */
static uint32_t m68k_c028_and_b_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t b = read_byte_with_displacement (context, context->state.a [source_reg]);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a & b;

    context->state.d [dest_reg].b = result;
    m68k_move_b_flags (context, result);

    return 0;
}


/* and.b Dn ← Dn & d(An+Xi) */
static uint32_t m68k_c030_and_b_dn_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t b = read_byte_with_index (context, context->state.a [source_reg]);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a & b;

    context->state.d [dest_reg].b = result;
    m68k_move_b_flags (context, result);

    return 0;
}


/* and.b Dn ← Dn & (xxx.w) */
static uint32_t m68k_c038_and_b_dn_aw (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t b = read_byte_aw (context);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a & b;

    context->state.d [dest_reg].b = result;
    m68k_move_b_flags (context, result);

    return 0;
}


/* and.b Dn ← Dn & (xxx.l) */
static uint32_t m68k_c039_and_b_dn_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t b = read_byte_al (context);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a & b;

    context->state.d [dest_reg].b = result;
    m68k_move_b_flags (context, result);

    return 0;
}


/* and.b Dn ← Dn & d(PC) */
static uint32_t m68k_c03a_and_b_dn_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t b = read_byte_with_displacement (context, context->state.pc);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a & b;

    context->state.d [dest_reg].b = result;
    m68k_move_b_flags (context, result);

    return 0;
}


/* and.b Dn ← Dn & d(PC+Xi) */
static uint32_t m68k_c03b_and_b_dn_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t b = read_byte_with_index (context, context->state.pc);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a & b;

    context->state.d [dest_reg].b = result;
    m68k_move_b_flags (context, result);

    return 0;
}


/* and.b Dn ← Dn & #xx */
static uint32_t m68k_c03c_and_b_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint8_t b = read_extension (context);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a & b;

    context->state.d [dest_reg].b = result;
    m68k_move_b_flags (context, result);

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

    return 0;
}


/* mulu.w Dn ← Dn × Dn */
static uint32_t m68k_c0c0_mulu_w_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t b = context->state.d [source_reg].w;
    uint16_t a = context->state.d [dest_reg].w;
    uint32_t result = (uint32_t) a * (uint32_t) b;

    context->state.d [dest_reg].l = result;

    context->state.ccr_negative = ((int32_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;

    return 0;
}


/* mulu.w Dn ← Dn × #xxxx */
static uint32_t m68k_c0fc_mulu_w_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t b = read_extension (context);
    uint16_t a = context->state.d [dest_reg].w;
    uint32_t result = (uint32_t) a * (uint32_t) b;

    context->state.d [dest_reg].l = result;

    context->state.ccr_negative = ((int32_t) result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;

    return 0;
}


/* and.b (An) ← (An) & Dn */
static uint32_t m68k_c110_and_b_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint16_t dest_reg = instruction & 0x07;

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = read_byte (context, context->state.a [dest_reg]);
    uint8_t result = a & b;

    write_byte (context, context->state.a [dest_reg], result);
    m68k_move_b_flags (context, result);

    return 0;
}


/* and.b (An+) ← (An+) & Dn */
static uint32_t m68k_c118_and_b_anp_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint16_t dest_reg = instruction & 0x07;

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = read_byte (context, context->state.a [dest_reg]);
    uint8_t result = a & b;

    write_byte (context, context->state.a [dest_reg], result);
    context->state.a [dest_reg] += (dest_reg == 7) ? 2 : 1;
    m68k_move_b_flags (context, result);

    return 0;
}


/* and.b (-An) ← (-An) & Dn */
static uint32_t m68k_c120_and_b_pan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint16_t dest_reg = instruction & 0x07;

    uint8_t b = context->state.d [source_reg].b;
    context->state.a [dest_reg] -= (dest_reg == 7) ? 2 : 1;
    uint8_t a = read_byte (context, context->state.a [dest_reg]);
    uint8_t result = a & b;

    write_byte (context, context->state.a [dest_reg], result);
    m68k_move_b_flags (context, result);

    return 0;
}


/* and.b d(An) ← d(An) & Dn */
static uint32_t m68k_c128_and_b_dan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint16_t dest_reg = instruction & 0x07;
    uint32_t address = address_with_displacement (context, context->state.a [dest_reg]);

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = read_byte (context, address);
    uint8_t result = a & b;

    write_byte (context, address, result);
    m68k_move_b_flags (context, result);

    return 0;
}


/* and.b d(An+Xi) ← d(An+Xi) & Dn */
static uint32_t m68k_c130_and_b_danxi_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint16_t dest_reg = instruction & 0x07;
    uint32_t address = address_with_index (context, context->state.a [dest_reg]);

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = read_byte (context, address);
    uint8_t result = a & b;

    write_byte (context, address, result);
    m68k_move_b_flags (context, result);

    return 0;
}


/* and.b (xxx.w) ← (xxx.w) & Dn */
static uint32_t m68k_c138_and_b_aw_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint32_t address = (int16_t) read_extension (context);

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = read_byte (context, address);
    uint8_t result = a & b;

    write_byte (context, address, result);
    m68k_move_b_flags (context, result);

    return 0;
}


/* and.b (xxx.l) ← (xxx.l) & Dn */
static uint32_t m68k_c139_and_b_al_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint32_t address = read_extension_long (context);

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = read_byte (context, address);
    uint8_t result = a & b;

    write_byte (context, address, result);
    m68k_move_b_flags (context, result);

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

    return 0;
}


/* muls.w Dn ← Dn × d(An) */
static uint32_t m68k_c1e8_muls_w_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = instruction & 0x07;
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    int16_t b = read_word_with_displacement (context, context->state.a [source_reg]);
    int16_t a = context->state.d [dest_reg].w;
    int32_t result = a * b;

    context->state.d [dest_reg].l = result;

    context->state.ccr_negative = (result < 0);
    context->state.ccr_zero = (result == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;

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

    return 0;
}


/* add.b Dn ← Dn + d(An) */
static uint32_t m68k_d028_add_b_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint8_t b = read_byte_with_displacement (context, context->state.a [source_reg]);
    uint8_t a = context->state.d [dest_reg].b;
    uint8_t result = a + b;

    context->state.d [dest_reg].b = result;
    m68k_add_b_flags (context, a, b, result);

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

    return 0;
}


/* add.w Dn ← Dn + (An+) */
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

    return 0;
}


/* add.w Dn ← Dn + (-An) */
static uint32_t m68k_d060_add_w_dn_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    context->state.a [source_reg] -= 2;

    uint16_t b = read_word (context, context->state.a [source_reg]);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a + b;

    context->state.d [dest_reg].w = result;
    m68k_add_w_flags (context, a, b, result);

    return 0;
}


/* add.w Dn ← Dn + d(An) */
static uint32_t m68k_d068_add_w_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t b = read_word_with_displacement (context, context->state.a [source_reg]);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a + b;

    context->state.d [dest_reg].w = result;
    m68k_add_w_flags (context, a, b, result);

    return 0;
}


/* add.w Dn ← Dn + d(An+Xi) */
static uint32_t m68k_d070_add_w_dn_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    uint16_t b = read_word_with_index (context, context->state.a [source_reg]);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a + b;

    context->state.d [dest_reg].w = result;
    m68k_add_w_flags (context, a, b, result);

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

    return 0;
}


/* add.w Dn ← Dn + (xxx.l) */
static uint32_t m68k_d079_add_w_dn_al (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t b = read_word_al (context);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a + b;

    context->state.d [dest_reg].w = result;
    m68k_add_w_flags (context, a, b, result);

    return 0;
}


/* add.w Dn ← Dn + d(PC) */
static uint32_t m68k_d07a_add_w_dn_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t b = read_word_with_displacement (context, context->state.pc);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a + b;

    context->state.d [dest_reg].w = result;
    m68k_add_w_flags (context, a, b, result);

    return 0;
}


/* add.w Dn ← Dn + d(PC+Xi) */
static uint32_t m68k_d07b_add_w_dn_dpcxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;

    uint16_t b = read_word_with_index (context, context->state.pc);
    uint16_t a = context->state.d [dest_reg].w;
    uint16_t result = a + b;

    context->state.d [dest_reg].w = result;
    m68k_add_w_flags (context, a, b, result);

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

    return 0;
}


/* adda.w An ← An + Dn */
static uint32_t m68k_d0c0_adda_w_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [dest_reg] += (int16_t) context->state.d [source_reg].w;

    return 0;
}


/* adda.w An ← An + An */
static uint32_t m68k_d0c8_adda_w_an_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [dest_reg] += (int16_t) (uint16_t) context->state.a [source_reg];

    return 0;
}


/* adda.w An ← An + (An) */
static uint32_t m68k_d0d0_adda_w_an_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [dest_reg] += (int16_t) read_word (context, context->state.a [source_reg]);

    return 0;
}


/* adda.w An ← An + #xxxx */
static uint32_t m68k_d0fc_adda_w_an_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t imm = read_extension (context);

    context->state.a [dest_reg] += (int16_t) imm;

    return 0;
}


/* adda.w An ← An + d(An+Xi) */
static uint32_t m68k_d0f0_adda_w_an_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [dest_reg] += (int16_t) read_word_with_index (context, context->state.a [source_reg]);

    return 0;
}


/* add.b d(An) ← d(An) + Dn */
static uint32_t m68k_d128_add_b_dan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = instruction & 0x07;
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint32_t address = address_with_displacement (context, context->state.a [dest_reg]);

    uint8_t b = context->state.d [source_reg].b;
    uint8_t a = read_byte (context, address);
    uint8_t result = a + b;

    write_byte (context, address, result);
    m68k_add_b_flags (context, a, b, result);

    return 0;
}


/* add.w (An) ← (An) + Dn */
static uint32_t m68k_d150_add_w_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = instruction & 0x07;
    uint16_t source_reg = (instruction >> 9) & 0x07;

    uint16_t b = context->state.d [source_reg].w;
    uint16_t a = read_word (context, context->state.a [dest_reg]);
    uint16_t result = a + b;

    write_word (context, context->state.a [dest_reg], result);
    m68k_add_w_flags (context, a, b, result);

    return 0;
}


/* add.w (An+) ← (An+) + Dn */
static uint32_t m68k_d158_add_w_anp_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = instruction & 0x07;
    uint16_t source_reg = (instruction >> 9) & 0x07;

    uint16_t b = context->state.d [source_reg].w;
    uint16_t a = read_word (context, context->state.a [dest_reg]);
    uint16_t result = a + b;

    write_word (context, context->state.a [dest_reg], result);
    context->state.a [dest_reg] += 2;
    m68k_add_w_flags (context, a, b, result);

    return 0;
}


/* add.w (-An) ← (-An) + Dn */
static uint32_t m68k_d160_add_w_pan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = instruction & 0x07;
    uint16_t source_reg = (instruction >> 9) & 0x07;
    context->state.a [dest_reg] -= 2;

    uint16_t b = context->state.d [source_reg].w;
    uint16_t a = read_word (context, context->state.a [dest_reg]);
    uint16_t result = a + b;

    write_word (context, context->state.a [dest_reg], result);
    m68k_add_w_flags (context, a, b, result);

    return 0;
}


/* add.w d(An) ← d(An) + Dn */
static uint32_t m68k_d168_add_w_dan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = instruction & 0x07;
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint32_t address = address_with_displacement (context, context->state.a [dest_reg]);

    uint16_t b = context->state.d [source_reg].w;
    uint16_t a = read_word (context, address);
    uint16_t result = a + b;

    write_word (context, address, result);
    m68k_add_w_flags (context, a, b, result);

    return 0;
}


/* add.w d(An+Xi) ← d(An+Xi) + Dn */
static uint32_t m68k_d170_add_w_danxi_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = instruction & 0x07;
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint32_t address = address_with_index (context, context->state.a [dest_reg]);

    uint16_t b = context->state.d [source_reg].w;
    uint16_t a = read_word (context, address);
    uint16_t result = a + b;

    write_word (context, address, result);
    m68k_add_w_flags (context, a, b, result);

    return 0;
}


/* add.w (xxx.w) ← (xxx.w) + Dn */
static uint32_t m68k_d178_add_w_aw_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint32_t address = (int16_t) read_extension (context);

    uint16_t b = context->state.d [source_reg].w;
    uint16_t a = read_word (context, address);
    uint16_t result = a + b;

    write_word (context, address, result);
    m68k_add_w_flags (context, a, b, result);

    return 0;
}


/* add.w (xxx.l) ← (xxx.l) + Dn */
static uint32_t m68k_d179_add_w_al_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    int32_t address = read_extension_long (context);

    uint16_t b = context->state.d [source_reg].w;
    uint16_t a = read_word (context, address);
    uint16_t result = a + b;

    write_word (context, address, result);
    m68k_add_w_flags (context, a, b, result);

    return 0;
}


/* add.l (An) ← (An) + Dn */
static uint32_t m68k_d190_add_l_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = instruction & 0x07;
    uint16_t source_reg = (instruction >> 9) & 0x07;

    uint32_t b = context->state.d [source_reg].l;
    uint32_t a = read_long (context, context->state.a [dest_reg]);
    uint32_t result = a + b;

    write_long (context, context->state.a [dest_reg], result);
    m68k_add_l_flags (context, a, b, result);

    return 0;
}


/* add.l d(An) ← d(An) + Dn */
static uint32_t m68k_d1a8_add_l_dan_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = instruction & 0x07;
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint32_t address = address_with_displacement (context, context->state.a [dest_reg]);

    uint32_t b = context->state.d [source_reg].l;
    uint32_t a = read_long (context, address);
    uint32_t result = a + b;

    write_long (context, address, result);
    m68k_add_l_flags (context, a, b, result);

    return 0;
}


/* add.l (xxx.w) ← (xxx.w) + Dn */
static uint32_t m68k_d1b8_add_l_aw_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t source_reg = (instruction >> 9) & 0x07;
    uint32_t address = (int16_t) read_extension (context);

    uint32_t b = context->state.d [source_reg].l;
    uint32_t a = read_long (context, address);
    uint32_t result = a + b;

    write_long (context, address, result);
    m68k_add_l_flags (context, a, b, result);

    return 0;
}


/* adda.l An ← An + Dn */
static uint32_t m68k_d1c0_adda_l_an_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;

    context->state.a [dest_reg] += context->state.d [source_reg].l;

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

    return 0;
}


/* ror.b Dn ← Dn >> #xx */
static uint32_t m68k_e018_ror_b_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t count = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t reg = instruction & 0x07;
    uint8_t value = context->state.d [reg].b;

    for (uint32_t i = 0; i < count; i++)
    {
        context->state.ccr_carry = value & 0x01;
        value = (value >> 1) | (value << 7);
    }

    context->state.ccr_negative = ((int8_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    context->state.d [reg].b = value;

    return 0;
}


/* ror.b Dn ← Dn >> Dn */
static uint32_t m68k_e038_ror_b_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t count_reg = (instruction >> 9) & 0x07;
    uint16_t reg = instruction & 0x07;
    uint8_t value = context->state.d [reg].b;

    uint16_t count = context->state.d [count_reg].b & 0x3f;

    context->state.ccr_carry = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        context->state.ccr_carry = value & 0x01;
        value = (value >> 1) | (value << 7);
    }

    context->state.ccr_negative = ((int8_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    context->state.d [reg].b = value;

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

    return 0;
}


/* ror.w Dn ← Dn >> Dn */
static uint32_t m68k_e078_ror_w_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t count_reg = (instruction >> 9) & 0x07;
    uint16_t reg = instruction & 0x07;
    uint16_t value = context->state.d [reg].w;

    uint8_t count = context->state.d [count_reg].b & 0x3f;

    context->state.ccr_carry = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        context->state.ccr_carry = value & 0x0001;
        value = (value >> 1) | (value << 15);
    }

    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    context->state.d [reg].w = value;

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

    return 0;
}


/* ror.l Dn ← Dn >> Dn */
static uint32_t m68k_e0b8_ror_l_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t count_reg = (instruction >> 9) & 0x07;
    uint16_t reg = instruction & 0x07;
    uint32_t value = context->state.d [reg].l;

    uint8_t count = context->state.d [count_reg].b & 0x3f;

    context->state.ccr_carry = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        context->state.ccr_carry = value & 0x00000001;
        value = (value >> 1) | (value << 31);
    }

    context->state.ccr_negative = ((int32_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    context->state.d [reg].l = value;

    return 0;
}


/* asr.w d(An) ← d(An) >> 1 */
static uint32_t m68k_e0e8_asr_w_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t address = address_with_displacement (context, context->state.a [reg]);
    uint16_t value = read_word (context, address);

    context->state.ccr_carry = value & 0x0001;
    context->state.ccr_extend = value & 0x0001;
    value = (value >> 1) | (value & 0x8000);

    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    write_word (context, address, value);

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

    return 0;
}


/* asl.w Dn ← Dn << Dn */
static uint32_t m68k_e160_asl_w_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t count_reg = (instruction >> 9) & 0x07;
    uint16_t reg = instruction & 0x07;
    uint16_t value = context->state.d [reg].w;

    uint8_t count = context->state.d [count_reg].b & 0x3f;

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

    return 0;
}


/* rol.w Dn ← Dn << Dn */
static uint32_t m68k_e178_rol_w_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t count_reg = (instruction >> 9) & 0x07;
    uint16_t reg = instruction & 0x07;
    uint16_t value = context->state.d [reg].w;

    uint16_t count = context->state.d [count_reg].b & 0x3f;

    context->state.ccr_carry = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        value = (value << 1) | (value >> 15);
        context->state.ccr_carry = value & 0x01;
    }

    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    context->state.d [reg].w = value;

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

    return 0;
}


/* lsl.l Dn ← Dn << #xx */
static uint32_t m68k_e188_lsl_l_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t count = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t reg = instruction & 0x07;
    uint32_t value = context->state.d [reg].l;

    context->state.ccr_carry = 0;

    for (uint32_t i = 0; i < count; i++)
    {
        context->state.ccr_carry = value >> 31;
        context->state.ccr_extend = value >> 31;
        value = value << 1;
    }

    context->state.ccr_negative = ((int32_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    context->state.d [reg].l = value;

    return 0;
}


/* rol.l Dn ← Dn << #xx */
static uint32_t m68k_e198_rol_l_dn_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t count = (instruction & 0x0e00) ? ((instruction >> 9) & 0x07) : 8;
    uint16_t reg = instruction & 0x07;
    uint32_t value = context->state.d [reg].l;

    for (uint32_t i = 0; i < count; i++)
    {
        value = (value << 1) | (value >> 31);
        context->state.ccr_carry = value & 0x00000001;
    }

    context->state.ccr_negative = ((int32_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    context->state.d [reg].l = value;

    return 0;
}


/* rol.l Dn ← Dn << Dn */
static uint32_t m68k_e1b8_rol_l_dn_dn (M68000_Context *context, uint16_t instruction)
{
    uint16_t count_reg = (instruction >> 9) & 0x07;
    uint16_t reg = instruction & 0x07;
    uint32_t value = context->state.d [reg].l;

    uint16_t count = context->state.d [count_reg].b & 0x3f;

    context->state.ccr_carry = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        value = (value << 1) | (value >> 31);
        context->state.ccr_carry = value & 0x00000001;
    }

    context->state.ccr_negative = ((int32_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    context->state.d [reg].l = value;

    return 0;
}


/* asl.w (An) ← (An) << 1 */
static uint32_t m68k_e1d0_asl_w_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t address = context->state.a [reg];
    uint16_t value = read_word (context, address);

    context->state.ccr_carry = value >> 15;
    context->state.ccr_extend = value >> 15;
    context->state.ccr_overflow = (value >> 14) ^ (value >> 15);
    value = value << 1;

    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);

    write_word (context, address, value);

    return 0;
}


/* asl.w (An+) ← (An+) << 1 */
static uint32_t m68k_e1d8_asl_w_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t address = context->state.a [reg];
    context->state.a [reg] += 2;
    uint16_t value = read_word (context, address);

    context->state.ccr_carry = value >> 15;
    context->state.ccr_extend = value >> 15;
    context->state.ccr_overflow = (value >> 14) ^ (value >> 15);
    value = value << 1;

    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);

    write_word (context, address, value);

    return 0;
}


/* asl.w (-An) ← (-An) << 1 */
static uint32_t m68k_e1e0_asl_w_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    context->state.a [reg] -= 2;
    uint32_t address = context->state.a [reg];
    uint16_t value = read_word (context, address);

    context->state.ccr_carry = value >> 15;
    context->state.ccr_extend = value >> 15;
    context->state.ccr_overflow = (value >> 14) ^ (value >> 15);
    value = value << 1;

    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);

    write_word (context, address, value);

    return 0;
}


/* asl.w d(An) ← d(An) << 1 */
static uint32_t m68k_e1e8_asl_w_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t address = address_with_displacement (context, context->state.a [reg]);
    uint16_t value = read_word (context, address);

    context->state.ccr_carry = value >> 15;
    context->state.ccr_extend = value >> 15;
    context->state.ccr_overflow = (value >> 14) ^ (value >> 15);
    value = value << 1;

    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);

    write_word (context, address, value);

    return 0;
}


/* asl.w d(An+Xi) ← d(An+Xi) << 1 */
static uint32_t m68k_e1f0_asl_w_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t address = address_with_index (context, context->state.a [reg]);
    uint16_t value = read_word (context, address);

    context->state.ccr_carry = value >> 15;
    context->state.ccr_extend = value >> 15;
    context->state.ccr_overflow = (value >> 14) ^ (value >> 15);
    value = value << 1;

    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);

    write_word (context, address, value);

    return 0;
}


/* asl.w (xxx.w) ← (xxx.w) << 1 */
static uint32_t m68k_e1f8_asl_w_aw (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = (int16_t) read_extension (context);
    uint16_t value = read_word (context, address);

    context->state.ccr_carry = value >> 15;
    context->state.ccr_extend = value >> 15;
    context->state.ccr_overflow = (value >> 14) ^ (value >> 15);
    value = value << 1;

    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);

    write_word (context, address, value);

    return 0;
}


/* asl.w (xxx.l) ← (xxx.l) << 1 */
static uint32_t m68k_e1f9_asl_w_al (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = read_extension_long (context);
    uint16_t value = read_word (context, address);

    context->state.ccr_carry = value >> 15;
    context->state.ccr_extend = value >> 15;
    context->state.ccr_overflow = (value >> 14) ^ (value >> 15);
    value = value << 1;

    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);

    write_word (context, address, value);

    return 0;
}


/* ror.w (An) ← (An) >> 1 */
static uint32_t m68k_e6d0_ror_w_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t value = read_word (context, context->state.a [reg]);

    context->state.ccr_carry = value & 0x0001;
    value = (value >> 1) | (value << 15);
    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    write_word (context, context->state.a [reg], value);

    return 0;
}


/* ror.w (An+) ← (An+) >> 1 */
static uint32_t m68k_e6d8_ror_w_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t value = read_word (context, context->state.a [reg]);

    context->state.ccr_carry = value & 0x0001;
    value = (value >> 1) | (value << 15);
    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    write_word (context, context->state.a [reg], value);
    context->state.a [reg] += 2;

    return 0;
}


/* ror.w (-An) ← (-An) >> 1 */
static uint32_t m68k_e6e0_ror_w_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    context->state.a [reg] -= 2;
    uint16_t value = read_word (context, context->state.a [reg]);

    context->state.ccr_carry = value & 0x0001;
    value = (value >> 1) | (value << 15);
    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    write_word (context, context->state.a [reg], value);

    return 0;
}


/* ror.w d(An) ← d(An) >> 1 */
static uint32_t m68k_e6e8_ror_w_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t address = address_with_displacement (context, context->state.a [reg]);
    uint16_t value = read_word (context, address);

    context->state.ccr_carry = value & 0x0001;
    value = (value >> 1) | (value << 15);
    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    write_word (context, address, value);

    return 0;
}


/* ror.w d(An+Xi) ← d(An+Xi) >> 1 */
static uint32_t m68k_e6f0_ror_w_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t address = address_with_index (context, context->state.a [reg]);
    uint16_t value = read_word (context, address);

    context->state.ccr_carry = value & 0x0001;
    value = (value >> 1) | (value << 15);
    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    write_word (context, address, value);

    return 0;
}


/* ror.w (xxx.w) ← d(xxx.w) >> 1 */
static uint32_t m68k_e6f8_ror_w_aw (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = (int16_t) read_extension (context);
    uint16_t value = read_word (context, address);

    context->state.ccr_carry = value & 0x0001;
    value = (value >> 1) | (value << 15);
    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    write_word (context, address, value);

    return 0;
}


/* ror.w (xxx.l) ← d(xxx.l) >> 1 */
static uint32_t m68k_e6f9_ror_w_al (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = read_extension_long (context);
    uint16_t value = read_word (context, address);

    context->state.ccr_carry = value & 0x0001;
    value = (value >> 1) | (value << 15);
    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    write_word (context, address, value);

    return 0;
}


/* rol.w (An) ← (An) << 1 */
static uint32_t m68k_e7d0_rol_w_an (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t value = read_word (context, context->state.a [reg]);

    value = (value << 1) | (value >> 15);
    context->state.ccr_carry = value & 0x0001;
    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    write_word (context, context->state.a [reg], value);

    return 0;
}


/* rol.w (An+) ← (An+) << 1 */
static uint32_t m68k_e7d8_rol_w_anp (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint16_t value = read_word (context, context->state.a [reg]);

    value = (value << 1) | (value >> 15);
    context->state.ccr_carry = value & 0x0001;
    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    write_word (context, context->state.a [reg], value);
    context->state.a [reg] += 2;

    return 0;
}


/* rol.w (-An) ← (-An) << 1 */
static uint32_t m68k_e7e0_rol_w_pan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    context->state.a [reg] -= 2;
    uint16_t value = read_word (context, context->state.a [reg]);

    value = (value << 1) | (value >> 15);
    context->state.ccr_carry = value & 0x0001;
    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    write_word (context, context->state.a [reg], value);

    return 0;
}


/* rol.w d(An) ← d(An) << 1 */
static uint32_t m68k_e7e8_rol_w_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t address = address_with_displacement (context, context->state.a [reg]);
    uint16_t value = read_word (context, address);

    value = (value << 1) | (value >> 15);
    context->state.ccr_carry = value & 0x0001;
    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    write_word (context, address, value);

    return 0;
}


/* rol.w d(An+Xi) ← d(An+Xi) << 1 */
static uint32_t m68k_e7f0_rol_w_danxi (M68000_Context *context, uint16_t instruction)
{
    uint16_t reg = instruction & 0x07;
    uint32_t address = address_with_index (context, context->state.a [reg]);
    uint16_t value = read_word (context, address);

    value = (value << 1) | (value >> 15);
    context->state.ccr_carry = value & 0x0001;
    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    write_word (context, address, value);

    return 0;
}


/* rol.w (xxx.w) ← (xxx.w) << 1 */
static uint32_t m68k_e7f8_rol_w_aw (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = (int16_t) read_extension (context);
    uint16_t value = read_word (context, address);

    value = (value << 1) | (value >> 15);
    context->state.ccr_carry = value & 0x0001;
    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    write_word (context, address, value);

    return 0;
}


/* rol.w (xxx.l) ← (xxx.l) << 1 */
static uint32_t m68k_e7f9_rol_w_al (M68000_Context *context, uint16_t instruction)
{
    uint32_t address = read_extension_long (context);
    uint16_t value = read_word (context, address);

    value = (value << 1) | (value >> 15);
    context->state.ccr_carry = value & 0x0001;
    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;

    write_word (context, address, value);

    return 0;
}


/* Unimplemented - Line-F */
static uint32_t m68k_f000_line_f (M68000_Context *context, uint16_t instruction)
{
    context->state.pc -= 2;
    m68k_exception (context, 0x2c);

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
            m68k_instruction [0x0190 | (bit_reg << 9) | data_reg] = m68k_0190_bclr_b_an_dn;
            m68k_instruction [0x0198 | (bit_reg << 9) | data_reg] = m68k_0198_bclr_b_anp_dn;
            m68k_instruction [0x01a0 | (bit_reg << 9) | data_reg] = m68k_01a0_bclr_b_pan_dn;
            m68k_instruction [0x01a8 | (bit_reg << 9) | data_reg] = m68k_01a8_bclr_b_dan_dn;
            m68k_instruction [0x01b0 | (bit_reg << 9) | data_reg] = m68k_01b0_bclr_b_danxi_dn;
            m68k_instruction [0x01c0 | (bit_reg << 9) | data_reg] = m68k_01c0_bset_l_dn_dn;
            m68k_instruction [0x01d0 | (bit_reg << 9) | data_reg] = m68k_01d0_bset_b_an_dn;
            m68k_instruction [0x01d8 | (bit_reg << 9) | data_reg] = m68k_01d8_bset_b_anp_dn;
            m68k_instruction [0x01e0 | (bit_reg << 9) | data_reg] = m68k_01e0_bset_b_pan_dn;
            m68k_instruction [0x01e8 | (bit_reg << 9) | data_reg] = m68k_01e8_bset_b_dan_dn;
            m68k_instruction [0x01f0 | (bit_reg << 9) | data_reg] = m68k_01f0_bset_b_danxi_dn;
        }
        m68k_instruction [0x01b8 | (data_reg << 9)] = m68k_01b8_bclr_b_aw_dn; /* actually the bit-register. */
        m68k_instruction [0x01b9 | (data_reg << 9)] = m68k_01b9_bclr_b_al_dn; /* actually the bit-register. */
        m68k_instruction [0x01f8 | (data_reg << 9)] = m68k_01f8_bset_b_aw_dn; /* actually the bit-register. */
        m68k_instruction [0x01f9 | (data_reg << 9)] = m68k_01f9_bset_b_al_dn; /* actually the bit-register. */
        m68k_instruction [0x0800 | data_reg] = m68k_0800_btst_l_dn_imm;
        m68k_instruction [0x0810 | data_reg] = m68k_0810_btst_b_an_imm;
        m68k_instruction [0x0828 | data_reg] = m68k_0828_btst_b_dan_imm;
        m68k_instruction [0x0830 | data_reg] = m68k_0830_btst_b_danxi_imm;
        m68k_instruction [0x0840 | data_reg] = m68k_0840_bchg_l_dn_imm;
        m68k_instruction [0x0868 | data_reg] = m68k_0868_bchg_b_dan_imm;
        m68k_instruction [0x0880 | data_reg] = m68k_0880_bclr_l_dn_imm;
        m68k_instruction [0x0890 | data_reg] = m68k_0890_bclr_b_an_imm;
        m68k_instruction [0x0898 | data_reg] = m68k_0898_bclr_b_anp_imm;
        m68k_instruction [0x08a0 | data_reg] = m68k_08a0_bclr_b_pan_imm;
        m68k_instruction [0x08a8 | data_reg] = m68k_08a8_bclr_b_dan_imm;
        m68k_instruction [0x08b0 | data_reg] = m68k_08b0_bclr_b_danxi_imm;
        m68k_instruction [0x08c0 | data_reg] = m68k_08c0_bset_l_dn_imm;
        m68k_instruction [0x08d0 | data_reg] = m68k_08d0_bset_b_an_imm;
        m68k_instruction [0x08d8 | data_reg] = m68k_08d8_bset_b_anp_imm;
        m68k_instruction [0x08e0 | data_reg] = m68k_08e0_bset_b_pan_imm;
        m68k_instruction [0x08e8 | data_reg] = m68k_08e8_bset_b_dan_imm;
        m68k_instruction [0x08f0 | data_reg] = m68k_08f0_bset_b_danxi_imm;
    }

    m68k_instruction [0x0838] = m68k_0838_btst_b_aw_imm;
    m68k_instruction [0x0839] = m68k_0839_btst_b_al_imm;
    m68k_instruction [0x08b8] = m68k_08b8_bclr_b_aw_imm;
    m68k_instruction [0x08b9] = m68k_08b9_bclr_b_al_imm;
    m68k_instruction [0x08f8] = m68k_08f8_bset_b_aw_imm;
    m68k_instruction [0x08f9] = m68k_08f9_bset_b_al_imm;

    /* immediate */
    for (uint16_t reg = 0; reg < 8; reg++)
    {
        m68k_instruction [0x0000 | reg] = m68k_0000_ori_b_dn;
        m68k_instruction [0x0028 | reg] = m68k_0028_ori_b_dan;
        m68k_instruction [0x0040 | reg] = m68k_0040_ori_w_dn;
        m68k_instruction [0x0200 | reg] = m68k_0200_andi_b_dn;
        m68k_instruction [0x0228 | reg] = m68k_0228_andi_b_dan;
        m68k_instruction [0x0240 | reg] = m68k_0240_andi_w_dn;
        m68k_instruction [0x0250 | reg] = m68k_0250_andi_w_an;
        m68k_instruction [0x0258 | reg] = m68k_0258_andi_w_anp;
        m68k_instruction [0x0260 | reg] = m68k_0260_andi_w_pan;
        m68k_instruction [0x0268 | reg] = m68k_0268_andi_w_dan;
        m68k_instruction [0x0270 | reg] = m68k_0270_andi_w_danxi;
        m68k_instruction [0x0280 | reg] = m68k_0280_andi_l_dn;
        m68k_instruction [0x0290 | reg] = m68k_0290_andi_l_an;
        m68k_instruction [0x0298 | reg] = m68k_0298_andi_l_anp;
        m68k_instruction [0x02a0 | reg] = m68k_02a0_andi_l_pan;
        m68k_instruction [0x02a8 | reg] = m68k_02a8_andi_l_dan;
        m68k_instruction [0x02b0 | reg] = m68k_02b0_andi_l_danxi;
        m68k_instruction [0x0400 | reg] = m68k_0400_subi_b_dn;
        m68k_instruction [0x0410 | reg] = m68k_0410_subi_b_an;
        m68k_instruction [0x0418 | reg] = m68k_0418_subi_b_anp;
        m68k_instruction [0x0420 | reg] = m68k_0420_subi_b_pan;
        m68k_instruction [0x0428 | reg] = m68k_0428_subi_b_dan;
        m68k_instruction [0x0430 | reg] = m68k_0430_subi_b_danxi;
        m68k_instruction [0x0440 | reg] = m68k_0440_subi_w_dn;
        m68k_instruction [0x0450 | reg] = m68k_0450_subi_w_an;
        m68k_instruction [0x0458 | reg] = m68k_0458_subi_w_anp;
        m68k_instruction [0x0460 | reg] = m68k_0460_subi_w_pan;
        m68k_instruction [0x0468 | reg] = m68k_0468_subi_w_dan;
        m68k_instruction [0x0470 | reg] = m68k_0470_subi_w_danxi;
        m68k_instruction [0x0480 | reg] = m68k_0480_subi_l_dn;
        m68k_instruction [0x0490 | reg] = m68k_0490_subi_l_an;
        m68k_instruction [0x0498 | reg] = m68k_0498_subi_l_anp;
        m68k_instruction [0x04a0 | reg] = m68k_04a0_subi_l_pan;
        m68k_instruction [0x04a8 | reg] = m68k_04a8_subi_l_dan;
        m68k_instruction [0x04b0 | reg] = m68k_04b0_subi_l_danxi;
        m68k_instruction [0x0600 | reg] = m68k_0600_addi_b_dn;
        m68k_instruction [0x0610 | reg] = m68k_0610_addi_b_an;
        m68k_instruction [0x0618 | reg] = m68k_0618_addi_b_anp;
        m68k_instruction [0x0620 | reg] = m68k_0620_addi_b_pan;
        m68k_instruction [0x0628 | reg] = m68k_0628_addi_b_dan;
        m68k_instruction [0x0630 | reg] = m68k_0630_addi_b_danxi;
        m68k_instruction [0x0640 | reg] = m68k_0640_addi_w_dn;
        m68k_instruction [0x0650 | reg] = m68k_0650_addi_w_an;
        m68k_instruction [0x0658 | reg] = m68k_0658_addi_w_anp;
        m68k_instruction [0x0660 | reg] = m68k_0660_addi_w_pan;
        m68k_instruction [0x0668 | reg] = m68k_0668_addi_w_dan;
        m68k_instruction [0x0670 | reg] = m68k_0670_addi_w_danxi;
        m68k_instruction [0x0680 | reg] = m68k_0680_addi_l_dn;
        m68k_instruction [0x0690 | reg] = m68k_0690_addi_l_an;
        m68k_instruction [0x0698 | reg] = m68k_0698_addi_l_anp;
        m68k_instruction [0x06a0 | reg] = m68k_06a0_addi_l_pan;
        m68k_instruction [0x06a8 | reg] = m68k_06a8_addi_l_dan;
        m68k_instruction [0x06b0 | reg] = m68k_06b0_addi_l_danxi;
        m68k_instruction [0x0a00 | reg] = m68k_0a00_eori_b_dn;
        m68k_instruction [0x0a10 | reg] = m68k_0a10_eori_b_an;
        m68k_instruction [0x0a18 | reg] = m68k_0a18_eori_b_anp;
        m68k_instruction [0x0a20 | reg] = m68k_0a20_eori_b_pan;
        m68k_instruction [0x0a28 | reg] = m68k_0a28_eori_b_dan;
        m68k_instruction [0x0a30 | reg] = m68k_0a30_eori_b_danxi;
        m68k_instruction [0x0a40 | reg] = m68k_0a40_eori_w_dn;
        m68k_instruction [0x0a80 | reg] = m68k_0a80_eori_l_dn;
        m68k_instruction [0x0c00 | reg] = m68k_0c00_cmpi_b_dn;
        m68k_instruction [0x0c10 | reg] = m68k_0c10_cmpi_b_an;
        m68k_instruction [0x0c18 | reg] = m68k_0c18_cmpi_b_anp;
        m68k_instruction [0x0c20 | reg] = m68k_0c20_cmpi_b_pan;
        m68k_instruction [0x0c28 | reg] = m68k_0c28_cmpi_b_dan;
        m68k_instruction [0x0c30 | reg] = m68k_0c30_cmpi_b_danxi;
        m68k_instruction [0x0c40 | reg] = m68k_0c40_cmpi_w_dn;
        m68k_instruction [0x0c50 | reg] = m68k_0c50_cmpi_w_an;
        m68k_instruction [0x0c68 | reg] = m68k_0c68_cmpi_w_dan;
        m68k_instruction [0x0c98 | reg] = m68k_0c98_cmpi_l_anp;
    }
    m68k_instruction [0x0038] = m68k_0038_ori_b_aw;
    m68k_instruction [0x0238] = m68k_0238_andi_b_aw;
    m68k_instruction [0x0278] = m68k_0278_andi_w_aw;
    m68k_instruction [0x0279] = m68k_0279_andi_w_al;
    m68k_instruction [0x02b8] = m68k_02b8_andi_l_aw;
    m68k_instruction [0x02b9] = m68k_02b9_andi_l_al;
    m68k_instruction [0x0438] = m68k_0438_subi_b_aw;
    m68k_instruction [0x0439] = m68k_0439_subi_b_al;
    m68k_instruction [0x0478] = m68k_0478_subi_w_aw;
    m68k_instruction [0x0479] = m68k_0479_subi_w_al;
    m68k_instruction [0x04b8] = m68k_04b8_subi_l_aw;
    m68k_instruction [0x04b9] = m68k_04b9_subi_l_al;
    m68k_instruction [0x0638] = m68k_0638_addi_b_aw;
    m68k_instruction [0x0639] = m68k_0639_addi_b_al;
    m68k_instruction [0x0678] = m68k_0678_addi_w_aw;
    m68k_instruction [0x0679] = m68k_0679_addi_w_al;
    m68k_instruction [0x06b8] = m68k_06b8_addi_l_aw;
    m68k_instruction [0x06b9] = m68k_06b9_addi_l_al;
    m68k_instruction [0x0a38] = m68k_0a38_eori_b_aw;
    m68k_instruction [0x0a39] = m68k_0a39_eori_b_al;
    m68k_instruction [0x0c38] = m68k_0c38_cmpi_b_aw;
    m68k_instruction [0x0c39] = m68k_0c39_cmpi_b_al;
    m68k_instruction [0x0c78] = m68k_0c78_cmpi_w_aw;

    /* move */
    for (uint16_t reg_a = 0; reg_a < 8; reg_a++)
    {
        for (uint16_t reg_b = 0; reg_b < 8; reg_b++)
        {
            m68k_instruction [0x1000 | (reg_a << 9) | reg_b] = m68k_1000_move_b_dn_dn;
            m68k_instruction [0x1010 | (reg_a << 9) | reg_b] = m68k_1010_move_b_dn_an;
            m68k_instruction [0x1018 | (reg_a << 9) | reg_b] = m68k_1018_move_b_dn_anp;
            m68k_instruction [0x1020 | (reg_a << 9) | reg_b] = m68k_1020_move_b_dn_pan;
            m68k_instruction [0x1028 | (reg_a << 9) | reg_b] = m68k_1028_move_b_dn_dan;
            m68k_instruction [0x1030 | (reg_a << 9) | reg_b] = m68k_1030_move_b_dn_danxi;
            m68k_instruction [0x1080 | (reg_a << 9) | reg_b] = m68k_1080_move_b_an_dn;
            m68k_instruction [0x1090 | (reg_a << 9) | reg_b] = m68k_1090_move_b_an_an;
            m68k_instruction [0x1098 | (reg_a << 9) | reg_b] = m68k_1098_move_b_an_anp;
            m68k_instruction [0x10a0 | (reg_a << 9) | reg_b] = m68k_10a0_move_b_an_pan;
            m68k_instruction [0x10a8 | (reg_a << 9) | reg_b] = m68k_10a8_move_b_an_dan;
            m68k_instruction [0x10b0 | (reg_a << 9) | reg_b] = m68k_10b0_move_b_an_danxi;
            m68k_instruction [0x10c0 | (reg_a << 9) | reg_b] = m68k_10c0_move_b_anp_dn;
            m68k_instruction [0x10d0 | (reg_a << 9) | reg_b] = m68k_10d0_move_b_anp_an;
            m68k_instruction [0x10d8 | (reg_a << 9) | reg_b] = m68k_10d8_move_b_anp_anp;
            m68k_instruction [0x10e0 | (reg_a << 9) | reg_b] = m68k_10e0_move_b_anp_pan;
            m68k_instruction [0x10e8 | (reg_a << 9) | reg_b] = m68k_10e8_move_b_anp_dan;
            m68k_instruction [0x10f0 | (reg_a << 9) | reg_b] = m68k_10f0_move_b_anp_danxi;
            m68k_instruction [0x1100 | (reg_a << 9) | reg_b] = m68k_1100_move_b_pan_dn;
            m68k_instruction [0x1110 | (reg_a << 9) | reg_b] = m68k_1110_move_b_pan_an;
            m68k_instruction [0x1118 | (reg_a << 9) | reg_b] = m68k_1118_move_b_pan_anp;
            m68k_instruction [0x1120 | (reg_a << 9) | reg_b] = m68k_1120_move_b_pan_pan;
            m68k_instruction [0x1128 | (reg_a << 9) | reg_b] = m68k_1128_move_b_pan_dan;
            m68k_instruction [0x1130 | (reg_a << 9) | reg_b] = m68k_1130_move_b_pan_danxi;
            m68k_instruction [0x1140 | (reg_a << 9) | reg_b] = m68k_1140_move_b_dan_dn;
            m68k_instruction [0x1150 | (reg_a << 9) | reg_b] = m68k_1150_move_b_dan_an;
            m68k_instruction [0x1158 | (reg_a << 9) | reg_b] = m68k_1158_move_b_dan_anp;
            m68k_instruction [0x1160 | (reg_a << 9) | reg_b] = m68k_1160_move_b_dan_pan;
            m68k_instruction [0x1168 | (reg_a << 9) | reg_b] = m68k_1168_move_b_dan_dan;
            m68k_instruction [0x1170 | (reg_a << 9) | reg_b] = m68k_1170_move_b_dan_danxi;
            m68k_instruction [0x1180 | (reg_a << 9) | reg_b] = m68k_1180_move_b_danxi_dn;
            m68k_instruction [0x1190 | (reg_a << 9) | reg_b] = m68k_1190_move_b_danxi_an;
            m68k_instruction [0x1198 | (reg_a << 9) | reg_b] = m68k_1198_move_b_danxi_anp;
            m68k_instruction [0x11a0 | (reg_a << 9) | reg_b] = m68k_11a0_move_b_danxi_pan;
            m68k_instruction [0x11a8 | (reg_a << 9) | reg_b] = m68k_11a8_move_b_danxi_dan;
            m68k_instruction [0x11b0 | (reg_a << 9) | reg_b] = m68k_11b0_move_b_danxi_danxi;
            m68k_instruction [0x2000 | (reg_a << 9) | reg_b] = m68k_2000_move_l_dn_dn;
            m68k_instruction [0x2008 | (reg_a << 9) | reg_b] = m68k_2008_move_l_dn_an;
            m68k_instruction [0x2010 | (reg_a << 9) | reg_b] = m68k_2010_move_l_dn_an;
            m68k_instruction [0x2018 | (reg_a << 9) | reg_b] = m68k_2018_move_l_dn_anp;
            m68k_instruction [0x2020 | (reg_a << 9) | reg_b] = m68k_2020_move_l_dn_pan;
            m68k_instruction [0x2028 | (reg_a << 9) | reg_b] = m68k_2028_move_l_dn_dan;
            m68k_instruction [0x2030 | (reg_a << 9) | reg_b] = m68k_2030_move_l_dn_danxi;
            m68k_instruction [0x2040 | (reg_a << 9) | reg_b] = m68k_2040_movea_l_an_dn;
            m68k_instruction [0x2048 | (reg_a << 9) | reg_b] = m68k_2048_movea_l_an_an;
            m68k_instruction [0x2050 | (reg_a << 9) | reg_b] = m68k_2050_movea_l_an_an;
            m68k_instruction [0x2058 | (reg_a << 9) | reg_b] = m68k_2058_movea_l_an_anp;
            m68k_instruction [0x2060 | (reg_a << 9) | reg_b] = m68k_2060_movea_l_an_pan;
            m68k_instruction [0x2068 | (reg_a << 9) | reg_b] = m68k_2068_movea_l_an_dan;
            m68k_instruction [0x2070 | (reg_a << 9) | reg_b] = m68k_2070_movea_l_an_danxi;
            m68k_instruction [0x2080 | (reg_a << 9) | reg_b] = m68k_2080_move_l_an_dn;
            m68k_instruction [0x2090 | (reg_a << 9) | reg_b] = m68k_2090_move_l_an_an;
            m68k_instruction [0x2098 | (reg_a << 9) | reg_b] = m68k_2098_move_l_an_anp;
            m68k_instruction [0x20a0 | (reg_a << 9) | reg_b] = m68k_20a0_move_l_an_pan;
            m68k_instruction [0x20a8 | (reg_a << 9) | reg_b] = m68k_20a8_move_l_an_dan;
            m68k_instruction [0x20b0 | (reg_a << 9) | reg_b] = m68k_20b0_move_l_an_danxi;
            m68k_instruction [0x20c0 | (reg_a << 9) | reg_b] = m68k_20c0_move_l_anp_dn;
            m68k_instruction [0x20c8 | (reg_a << 9) | reg_b] = m68k_20c8_move_l_anp_an;
            m68k_instruction [0x20d0 | (reg_a << 9) | reg_b] = m68k_20d0_move_l_anp_an;
            m68k_instruction [0x20d8 | (reg_a << 9) | reg_b] = m68k_20d8_move_l_anp_anp;
            m68k_instruction [0x20e0 | (reg_a << 9) | reg_b] = m68k_20e0_move_l_anp_pan;
            m68k_instruction [0x20e8 | (reg_a << 9) | reg_b] = m68k_20e8_move_l_anp_dan;
            m68k_instruction [0x20f0 | (reg_a << 9) | reg_b] = m68k_20f0_move_l_anp_danxi;
            m68k_instruction [0x2100 | (reg_a << 9) | reg_b] = m68k_2100_move_l_pan_dn;
            m68k_instruction [0x2108 | (reg_a << 9) | reg_b] = m68k_2108_move_l_pan_an;
            m68k_instruction [0x2110 | (reg_a << 9) | reg_b] = m68k_2110_move_l_pan_an;
            m68k_instruction [0x2118 | (reg_a << 9) | reg_b] = m68k_2118_move_l_pan_anp;
            m68k_instruction [0x2120 | (reg_a << 9) | reg_b] = m68k_2120_move_l_pan_pan;
            m68k_instruction [0x2128 | (reg_a << 9) | reg_b] = m68k_2128_move_l_pan_dan;
            m68k_instruction [0x2130 | (reg_a << 9) | reg_b] = m68k_2130_move_l_pan_danxi;
            m68k_instruction [0x2140 | (reg_a << 9) | reg_b] = m68k_2140_move_l_dan_dn;
            m68k_instruction [0x2148 | (reg_a << 9) | reg_b] = m68k_2148_move_l_dan_an;
            m68k_instruction [0x2150 | (reg_a << 9) | reg_b] = m68k_2150_move_l_dan_an;
            m68k_instruction [0x2158 | (reg_a << 9) | reg_b] = m68k_2158_move_l_dan_anp;
            m68k_instruction [0x2160 | (reg_a << 9) | reg_b] = m68k_2160_move_l_dan_pan;
            m68k_instruction [0x2168 | (reg_a << 9) | reg_b] = m68k_2168_move_l_dan_dan;
            m68k_instruction [0x2170 | (reg_a << 9) | reg_b] = m68k_2170_move_l_dan_danxi;
            m68k_instruction [0x2180 | (reg_a << 9) | reg_b] = m68k_2180_move_l_danxi_dn;
            m68k_instruction [0x2188 | (reg_a << 9) | reg_b] = m68k_2188_move_l_danxi_an;
            m68k_instruction [0x2190 | (reg_a << 9) | reg_b] = m68k_2190_move_l_danxi_an;
            m68k_instruction [0x2198 | (reg_a << 9) | reg_b] = m68k_2198_move_l_danxi_anp;
            m68k_instruction [0x21a0 | (reg_a << 9) | reg_b] = m68k_21a0_move_l_danxi_pan;
            m68k_instruction [0x21a8 | (reg_a << 9) | reg_b] = m68k_21a8_move_l_danxi_dan;
            m68k_instruction [0x21b0 | (reg_a << 9) | reg_b] = m68k_21b0_move_l_danxi_danxi;
            m68k_instruction [0x3000 | (reg_a << 9) | reg_b] = m68k_3000_move_w_dn_dn;
            m68k_instruction [0x3008 | (reg_a << 9) | reg_b] = m68k_3008_move_w_dn_an;
            m68k_instruction [0x3010 | (reg_a << 9) | reg_b] = m68k_3010_move_w_dn_an;
            m68k_instruction [0x3018 | (reg_a << 9) | reg_b] = m68k_3018_move_w_dn_anp;
            m68k_instruction [0x3020 | (reg_a << 9) | reg_b] = m68k_3020_move_w_dn_pan;
            m68k_instruction [0x3028 | (reg_a << 9) | reg_b] = m68k_3028_move_w_dn_dan;
            m68k_instruction [0x3030 | (reg_a << 9) | reg_b] = m68k_3030_move_w_dn_danxi;
            m68k_instruction [0x3040 | (reg_a << 9) | reg_b] = m68k_3040_movea_w_an_dn;
            m68k_instruction [0x3048 | (reg_a << 9) | reg_b] = m68k_3048_movea_w_an_an;
            m68k_instruction [0x3050 | (reg_a << 9) | reg_b] = m68k_3050_movea_w_an_an;
            m68k_instruction [0x3058 | (reg_a << 9) | reg_b] = m68k_3058_movea_w_an_anp;
            m68k_instruction [0x3060 | (reg_a << 9) | reg_b] = m68k_3060_movea_w_an_pan;
            m68k_instruction [0x3068 | (reg_a << 9) | reg_b] = m68k_3068_movea_w_an_dan;
            m68k_instruction [0x3070 | (reg_a << 9) | reg_b] = m68k_3070_movea_w_an_danxi;
            m68k_instruction [0x3080 | (reg_a << 9) | reg_b] = m68k_3080_move_w_an_dn;
            m68k_instruction [0x3088 | (reg_a << 9) | reg_b] = m68k_3088_move_w_an_an;
            m68k_instruction [0x3090 | (reg_a << 9) | reg_b] = m68k_3090_move_w_an_an;
            m68k_instruction [0x3098 | (reg_a << 9) | reg_b] = m68k_3098_move_w_an_anp;
            m68k_instruction [0x30a0 | (reg_a << 9) | reg_b] = m68k_30a0_move_w_an_pan;
            m68k_instruction [0x30a8 | (reg_a << 9) | reg_b] = m68k_30a8_move_w_an_dan;
            m68k_instruction [0x30b0 | (reg_a << 9) | reg_b] = m68k_30b0_move_w_an_danxi;
            m68k_instruction [0x30c0 | (reg_a << 9) | reg_b] = m68k_30c0_move_w_anp_dn;
            m68k_instruction [0x30c8 | (reg_a << 9) | reg_b] = m68k_30c8_move_w_anp_an;
            m68k_instruction [0x30d0 | (reg_a << 9) | reg_b] = m68k_30d0_move_w_anp_an;
            m68k_instruction [0x30d8 | (reg_a << 9) | reg_b] = m68k_30d8_move_w_anp_anp;
            m68k_instruction [0x30e0 | (reg_a << 9) | reg_b] = m68k_30e0_move_w_anp_pan;
            m68k_instruction [0x30e8 | (reg_a << 9) | reg_b] = m68k_30e8_move_w_anp_dan;
            m68k_instruction [0x30f0 | (reg_a << 9) | reg_b] = m68k_30f0_move_w_anp_danxi;
            m68k_instruction [0x3100 | (reg_a << 9) | reg_b] = m68k_3100_move_w_pan_dn;
            m68k_instruction [0x3108 | (reg_a << 9) | reg_b] = m68k_3108_move_w_pan_an;
            m68k_instruction [0x3110 | (reg_a << 9) | reg_b] = m68k_3110_move_w_pan_an;
            m68k_instruction [0x3118 | (reg_a << 9) | reg_b] = m68k_3118_move_w_pan_anp;
            m68k_instruction [0x3120 | (reg_a << 9) | reg_b] = m68k_3120_move_w_pan_pan;
            m68k_instruction [0x3128 | (reg_a << 9) | reg_b] = m68k_3128_move_w_pan_dan;
            m68k_instruction [0x3130 | (reg_a << 9) | reg_b] = m68k_3130_move_w_pan_danxi;
            m68k_instruction [0x3140 | (reg_a << 9) | reg_b] = m68k_3140_move_w_dan_dn;
            m68k_instruction [0x3148 | (reg_a << 9) | reg_b] = m68k_3148_move_w_dan_an;
            m68k_instruction [0x3150 | (reg_a << 9) | reg_b] = m68k_3150_move_w_dan_an;
            m68k_instruction [0x3158 | (reg_a << 9) | reg_b] = m68k_3158_move_w_dan_anp;
            m68k_instruction [0x3160 | (reg_a << 9) | reg_b] = m68k_3160_move_w_dan_pan;
            m68k_instruction [0x3168 | (reg_a << 9) | reg_b] = m68k_3168_move_w_dan_dan;
            m68k_instruction [0x3170 | (reg_a << 9) | reg_b] = m68k_3170_move_w_dan_danxi;
            m68k_instruction [0x3180 | (reg_a << 9) | reg_b] = m68k_3180_move_w_danxi_dn;
            m68k_instruction [0x3188 | (reg_a << 9) | reg_b] = m68k_3188_move_w_danxi_an;
            m68k_instruction [0x3190 | (reg_a << 9) | reg_b] = m68k_3190_move_w_danxi_an;
            m68k_instruction [0x3198 | (reg_a << 9) | reg_b] = m68k_3198_move_w_danxi_anp;
            m68k_instruction [0x31a0 | (reg_a << 9) | reg_b] = m68k_31a0_move_w_danxi_pan;
            m68k_instruction [0x31a8 | (reg_a << 9) | reg_b] = m68k_31a8_move_w_danxi_dan;
            m68k_instruction [0x31b0 | (reg_a << 9) | reg_b] = m68k_31b0_move_w_danxi_danxi;
        }
        m68k_instruction [0x1038 | (reg_a << 9)] = m68k_1038_move_b_dn_aw;
        m68k_instruction [0x1039 | (reg_a << 9)] = m68k_1039_move_b_dn_al;
        m68k_instruction [0x103a | (reg_a << 9)] = m68k_103a_move_b_dn_dpc;
        m68k_instruction [0x103b | (reg_a << 9)] = m68k_103b_move_b_dn_dpcxi;
        m68k_instruction [0x103c | (reg_a << 9)] = m68k_103c_move_b_dn_imm;
        m68k_instruction [0x10b8 | (reg_a << 9)] = m68k_10b8_move_b_an_aw;
        m68k_instruction [0x10b9 | (reg_a << 9)] = m68k_10b9_move_b_an_al;
        m68k_instruction [0x10ba | (reg_a << 9)] = m68k_10ba_move_b_an_dpc;
        m68k_instruction [0x10bb | (reg_a << 9)] = m68k_10bb_move_b_an_dpcxi;
        m68k_instruction [0x10bc | (reg_a << 9)] = m68k_10bc_move_b_an_imm;
        m68k_instruction [0x10f8 | (reg_a << 9)] = m68k_10f8_move_b_anp_aw;
        m68k_instruction [0x10f9 | (reg_a << 9)] = m68k_10f9_move_b_anp_al;
        m68k_instruction [0x10fa | (reg_a << 9)] = m68k_10fa_move_b_anp_dpc;
        m68k_instruction [0x10fb | (reg_a << 9)] = m68k_10fb_move_b_anp_dpcxi;
        m68k_instruction [0x10fc | (reg_a << 9)] = m68k_10fc_move_b_anp_imm;
        m68k_instruction [0x1138 | (reg_a << 9)] = m68k_1138_move_b_pan_aw;
        m68k_instruction [0x1139 | (reg_a << 9)] = m68k_1139_move_b_pan_al;
        m68k_instruction [0x113a | (reg_a << 9)] = m68k_113a_move_b_pan_dpc;
        m68k_instruction [0x113b | (reg_a << 9)] = m68k_113b_move_b_pan_dpcxi;
        m68k_instruction [0x113c | (reg_a << 9)] = m68k_113c_move_b_pan_imm;
        m68k_instruction [0x1178 | (reg_a << 9)] = m68k_1178_move_b_dan_aw;
        m68k_instruction [0x1179 | (reg_a << 9)] = m68k_1179_move_b_dan_al;
        m68k_instruction [0x117a | (reg_a << 9)] = m68k_117a_move_b_dan_dpc;
        m68k_instruction [0x117b | (reg_a << 9)] = m68k_117b_move_b_dan_dpcxi;
        m68k_instruction [0x117c | (reg_a << 9)] = m68k_117c_move_b_dan_imm;
        m68k_instruction [0x11b8 | (reg_a << 9)] = m68k_11b8_move_b_danxi_aw;
        m68k_instruction [0x11b9 | (reg_a << 9)] = m68k_11b9_move_b_danxi_al;
        m68k_instruction [0x11ba | (reg_a << 9)] = m68k_11ba_move_b_danxi_dpc;
        m68k_instruction [0x11bb | (reg_a << 9)] = m68k_11bb_move_b_danxi_dpcxi;
        m68k_instruction [0x11bc | (reg_a << 9)] = m68k_11bc_move_b_danxi_imm;
        m68k_instruction [0x11c0 | reg_a       ] = m68k_11c0_move_b_aw_dn;
        m68k_instruction [0x11d0 | reg_a       ] = m68k_11d0_move_b_aw_an;
        m68k_instruction [0x11d8 | reg_a       ] = m68k_11d8_move_b_aw_anp;
        m68k_instruction [0x11e0 | reg_a       ] = m68k_11e0_move_b_aw_pan;
        m68k_instruction [0x11e8 | reg_a       ] = m68k_11e8_move_b_aw_dan;
        m68k_instruction [0x11f0 | reg_a       ] = m68k_11f0_move_b_aw_danxi;
        m68k_instruction [0x13c0 | reg_a       ] = m68k_13c0_move_b_al_dn;
        m68k_instruction [0x13d0 | reg_a       ] = m68k_13d0_move_b_al_an;
        m68k_instruction [0x13d8 | reg_a       ] = m68k_13d8_move_b_al_anp;
        m68k_instruction [0x13e0 | reg_a       ] = m68k_13e0_move_b_al_pan;
        m68k_instruction [0x13e8 | reg_a       ] = m68k_13e8_move_b_al_dan;
        m68k_instruction [0x13f0 | reg_a       ] = m68k_13f0_move_b_al_danxi;
        m68k_instruction [0x2038 | (reg_a << 9)] = m68k_2038_move_l_dn_aw;
        m68k_instruction [0x2039 | (reg_a << 9)] = m68k_2039_move_l_dn_al;
        m68k_instruction [0x203a | (reg_a << 9)] = m68k_203a_move_l_dn_dpc;
        m68k_instruction [0x203b | (reg_a << 9)] = m68k_203b_move_l_dn_dpcxi;
        m68k_instruction [0x203c | (reg_a << 9)] = m68k_203c_move_l_dn_imm;
        m68k_instruction [0x2078 | (reg_a << 9)] = m68k_2078_movea_l_an_aw;
        m68k_instruction [0x2079 | (reg_a << 9)] = m68k_2079_movea_l_an_al;
        m68k_instruction [0x207a | (reg_a << 9)] = m68k_207a_movea_l_an_dpc;
        m68k_instruction [0x207b | (reg_a << 9)] = m68k_207b_movea_l_an_dpcxi;
        m68k_instruction [0x207c | (reg_a << 9)] = m68k_207c_movea_l_an_imm;
        m68k_instruction [0x20b8 | (reg_a << 9)] = m68k_20b8_move_l_an_aw;
        m68k_instruction [0x20b9 | (reg_a << 9)] = m68k_20b9_move_l_an_al;
        m68k_instruction [0x20ba | (reg_a << 9)] = m68k_20ba_move_l_an_dpc;
        m68k_instruction [0x20bb | (reg_a << 9)] = m68k_20bb_move_l_an_dpcxi;
        m68k_instruction [0x20bc | (reg_a << 9)] = m68k_20bc_move_l_an_imm;
        m68k_instruction [0x20f8 | (reg_a << 9)] = m68k_20f8_move_l_anp_aw;
        m68k_instruction [0x20f9 | (reg_a << 9)] = m68k_20f9_move_l_anp_al;
        m68k_instruction [0x20fa | (reg_a << 9)] = m68k_20fa_move_l_anp_dpc;
        m68k_instruction [0x20fb | (reg_a << 9)] = m68k_20fb_move_l_anp_dpcxi;
        m68k_instruction [0x20fc | (reg_a << 9)] = m68k_20fc_move_l_anp_imm;
        m68k_instruction [0x2138 | (reg_a << 9)] = m68k_2138_move_l_pan_aw;
        m68k_instruction [0x2139 | (reg_a << 9)] = m68k_2139_move_l_pan_al;
        m68k_instruction [0x213a | (reg_a << 9)] = m68k_213a_move_l_pan_dpc;
        m68k_instruction [0x213b | (reg_a << 9)] = m68k_213b_move_l_pan_dpcxi;
        m68k_instruction [0x213c | (reg_a << 9)] = m68k_213c_move_l_pan_imm;
        m68k_instruction [0x2178 | (reg_a << 9)] = m68k_2178_move_l_dan_aw;
        m68k_instruction [0x2179 | (reg_a << 9)] = m68k_2179_move_l_dan_al;
        m68k_instruction [0x217a | (reg_a << 9)] = m68k_217a_move_l_dan_dpc;
        m68k_instruction [0x217b | (reg_a << 9)] = m68k_217b_move_l_dan_dpcxi;
        m68k_instruction [0x217c | (reg_a << 9)] = m68k_217c_move_l_dan_imm;
        m68k_instruction [0x21b8 | (reg_a << 9)] = m68k_21b8_move_l_danxi_aw;
        m68k_instruction [0x21b9 | (reg_a << 9)] = m68k_21b9_move_l_danxi_al;
        m68k_instruction [0x21ba | (reg_a << 9)] = m68k_21ba_move_l_danxi_dpc;
        m68k_instruction [0x21bb | (reg_a << 9)] = m68k_21bb_move_l_danxi_dpcxi;
        m68k_instruction [0x21bc | (reg_a << 9)] = m68k_21bc_move_l_danxi_imm;
        m68k_instruction [0x21c0 | reg_a       ] = m68k_21c0_move_l_aw_dn;
        m68k_instruction [0x21c8 | reg_a       ] = m68k_21c8_move_l_aw_an;
        m68k_instruction [0x21d0 | reg_a       ] = m68k_21d0_move_l_aw_an;
        m68k_instruction [0x21d8 | reg_a       ] = m68k_21d8_move_l_aw_anp;
        m68k_instruction [0x21e0 | reg_a       ] = m68k_21e0_move_l_aw_pan;
        m68k_instruction [0x21e8 | reg_a       ] = m68k_21e8_move_l_aw_dan;
        m68k_instruction [0x21f0 | reg_a       ] = m68k_21f0_move_l_aw_danxi;
        m68k_instruction [0x23c0 | reg_a       ] = m68k_23c0_move_l_al_dn;
        m68k_instruction [0x23c8 | reg_a       ] = m68k_23c8_move_l_al_an;
        m68k_instruction [0x23d0 | reg_a       ] = m68k_23d0_move_l_al_an;
        m68k_instruction [0x23d8 | reg_a       ] = m68k_23d8_move_l_al_anp;
        m68k_instruction [0x23e0 | reg_a       ] = m68k_23e0_move_l_al_pan;
        m68k_instruction [0x23e8 | reg_a       ] = m68k_23e8_move_l_al_dan;
        m68k_instruction [0x23f0 | reg_a       ] = m68k_23f0_move_l_al_danxi;
        m68k_instruction [0x3038 | (reg_a << 9)] = m68k_3038_move_w_dn_aw;
        m68k_instruction [0x3039 | (reg_a << 9)] = m68k_3039_move_w_dn_al;
        m68k_instruction [0x303a | (reg_a << 9)] = m68k_303a_move_w_dn_dpc;
        m68k_instruction [0x303b | (reg_a << 9)] = m68k_303b_move_w_dn_dpcxi;
        m68k_instruction [0x303c | (reg_a << 9)] = m68k_303c_move_w_dn_imm;
        m68k_instruction [0x3078 | (reg_a << 9)] = m68k_3078_movea_w_an_aw;
        m68k_instruction [0x3079 | (reg_a << 9)] = m68k_3079_movea_w_an_al;
        m68k_instruction [0x307a | (reg_a << 9)] = m68k_307a_movea_w_an_dpc;
        m68k_instruction [0x307b | (reg_a << 9)] = m68k_307b_movea_w_an_dpcxi;
        m68k_instruction [0x307c | (reg_a << 9)] = m68k_307c_movea_w_an_imm;
        m68k_instruction [0x30b8 | (reg_a << 9)] = m68k_30b8_move_w_an_aw;
        m68k_instruction [0x30b9 | (reg_a << 9)] = m68k_30b9_move_w_an_al;
        m68k_instruction [0x30ba | (reg_a << 9)] = m68k_30ba_move_w_an_dpc;
        m68k_instruction [0x30bb | (reg_a << 9)] = m68k_30bb_move_w_an_dpcxi;
        m68k_instruction [0x30bc | (reg_a << 9)] = m68k_30bc_move_w_an_imm;
        m68k_instruction [0x30f8 | (reg_a << 9)] = m68k_30f8_move_w_anp_aw;
        m68k_instruction [0x30f9 | (reg_a << 9)] = m68k_30f9_move_w_anp_al;
        m68k_instruction [0x30fa | (reg_a << 9)] = m68k_30fa_move_w_anp_dpc;
        m68k_instruction [0x30fb | (reg_a << 9)] = m68k_30fb_move_w_anp_dpcxi;
        m68k_instruction [0x30fc | (reg_a << 9)] = m68k_30fc_move_w_anp_imm;
        m68k_instruction [0x3138 | (reg_a << 9)] = m68k_3138_move_w_pan_aw;
        m68k_instruction [0x3139 | (reg_a << 9)] = m68k_3139_move_w_pan_al;
        m68k_instruction [0x313a | (reg_a << 9)] = m68k_313a_move_w_pan_dpc;
        m68k_instruction [0x313b | (reg_a << 9)] = m68k_313b_move_w_pan_dpcxi;
        m68k_instruction [0x313c | (reg_a << 9)] = m68k_313c_move_w_pan_imm;
        m68k_instruction [0x3178 | (reg_a << 9)] = m68k_3178_move_w_dan_aw;
        m68k_instruction [0x3179 | (reg_a << 9)] = m68k_3179_move_w_dan_al;
        m68k_instruction [0x317a | (reg_a << 9)] = m68k_317a_move_w_dan_dpc;
        m68k_instruction [0x317b | (reg_a << 9)] = m68k_317b_move_w_dan_dpcxi;
        m68k_instruction [0x317c | (reg_a << 9)] = m68k_317c_move_w_dan_imm;
        m68k_instruction [0x31b8 | (reg_a << 9)] = m68k_31b8_move_w_danxi_aw;
        m68k_instruction [0x31b9 | (reg_a << 9)] = m68k_31b9_move_w_danxi_al;
        m68k_instruction [0x31ba | (reg_a << 9)] = m68k_31ba_move_w_danxi_dpc;
        m68k_instruction [0x31bb | (reg_a << 9)] = m68k_31bb_move_w_danxi_dpcxi;
        m68k_instruction [0x31bc | (reg_a << 9)] = m68k_31bc_move_w_danxi_imm;
        m68k_instruction [0x31c0 | reg_a       ] = m68k_31c0_move_w_aw_dn;
        m68k_instruction [0x31c8 | reg_a       ] = m68k_31c8_move_w_aw_an;
        m68k_instruction [0x31d0 | reg_a       ] = m68k_31d0_move_w_aw_an;
        m68k_instruction [0x31d8 | reg_a       ] = m68k_31d8_move_w_aw_anp;
        m68k_instruction [0x31e0 | reg_a       ] = m68k_31e0_move_w_aw_pan;
        m68k_instruction [0x31e8 | reg_a       ] = m68k_31e8_move_w_aw_dan;
        m68k_instruction [0x31f0 | reg_a       ] = m68k_31f0_move_w_aw_danxi;
        m68k_instruction [0x33c0 | reg_a       ] = m68k_33c0_move_w_al_dn;
        m68k_instruction [0x33c8 | reg_a       ] = m68k_33c8_move_w_al_an;
        m68k_instruction [0x33d0 | reg_a       ] = m68k_33d0_move_w_al_an;
        m68k_instruction [0x33d8 | reg_a       ] = m68k_33d8_move_w_al_anp;
        m68k_instruction [0x33e0 | reg_a       ] = m68k_33e0_move_w_al_pan;
        m68k_instruction [0x33e8 | reg_a       ] = m68k_33e8_move_w_al_dan;
        m68k_instruction [0x33f0 | reg_a       ] = m68k_33f0_move_w_al_danxi;
        m68k_instruction [0x40c0 | reg_a       ] = m68k_40c0_move_dn_sr;
        m68k_instruction [0x40d0 | reg_a       ] = m68k_40d0_move_an_sr;
        m68k_instruction [0x40d8 | reg_a       ] = m68k_40d8_move_anp_sr;
        m68k_instruction [0x40e0 | reg_a       ] = m68k_40e0_move_pan_sr;
        m68k_instruction [0x40e8 | reg_a       ] = m68k_40e8_move_dan_sr;
        m68k_instruction [0x40f0 | reg_a       ] = m68k_40f0_move_danxi_sr;
        m68k_instruction [0x44c0 | reg_a       ] = m68k_44c0_move_ccr_dn;
        m68k_instruction [0x46c0 | reg_a       ] = m68k_46c0_move_sr_dn;
        m68k_instruction [0x46d0 | reg_a       ] = m68k_46d0_move_sr_an;
        m68k_instruction [0x46d8 | reg_a       ] = m68k_46d8_move_sr_anp;
        m68k_instruction [0x46e0 | reg_a       ] = m68k_46e0_move_sr_pan;
        m68k_instruction [0x46e8 | reg_a       ] = m68k_46e8_move_sr_dan;
        m68k_instruction [0x46f0 | reg_a       ] = m68k_46f0_move_sr_danxi;
        m68k_instruction [0x4e60 | reg_a       ] = m68k_4e60_move_an_usp;
        m68k_instruction [0x4e68 | reg_a       ] = m68k_4e68_move_usp_an;
    }
    m68k_instruction [0x11f8] = m68k_11f8_move_b_aw_aw;
    m68k_instruction [0x11f9] = m68k_11f9_move_b_aw_al;
    m68k_instruction [0x11fa] = m68k_11fa_move_b_aw_dpc;
    m68k_instruction [0x11fb] = m68k_11fb_move_b_aw_dpcxi;
    m68k_instruction [0x11fc] = m68k_11fc_move_b_aw_imm;
    m68k_instruction [0x13f8] = m68k_13f8_move_b_al_aw;
    m68k_instruction [0x13f9] = m68k_13f9_move_b_al_al;
    m68k_instruction [0x13fa] = m68k_13fa_move_b_al_dpc;
    m68k_instruction [0x13fb] = m68k_13fb_move_b_al_dpcxi;
    m68k_instruction [0x13fc] = m68k_13fc_move_b_al_imm;
    m68k_instruction [0x21f8] = m68k_21f8_move_l_aw_aw;
    m68k_instruction [0x21f9] = m68k_21f9_move_l_aw_al;
    m68k_instruction [0x21fa] = m68k_21fa_move_l_aw_dpc;
    m68k_instruction [0x21fb] = m68k_21fb_move_l_aw_dpcxi;
    m68k_instruction [0x21fc] = m68k_21fc_move_l_aw_imm;
    m68k_instruction [0x23f8] = m68k_23f8_move_l_al_aw;
    m68k_instruction [0x23f9] = m68k_23f9_move_l_al_al;
    m68k_instruction [0x23fa] = m68k_23fa_move_l_al_dpc;
    m68k_instruction [0x23fb] = m68k_23fb_move_l_al_dpcxi;
    m68k_instruction [0x23fc] = m68k_23fc_move_l_al_imm;
    m68k_instruction [0x31f8] = m68k_31f8_move_w_aw_aw;
    m68k_instruction [0x31f9] = m68k_31f9_move_w_aw_al;
    m68k_instruction [0x31fa] = m68k_31fa_move_w_aw_dpc;
    m68k_instruction [0x31fb] = m68k_31fb_move_w_aw_dpcxi;
    m68k_instruction [0x31fc] = m68k_31fc_move_w_aw_imm;
    m68k_instruction [0x33f8] = m68k_33f8_move_w_al_aw;
    m68k_instruction [0x33f9] = m68k_33f9_move_w_al_al;
    m68k_instruction [0x33fa] = m68k_33fa_move_w_al_dpc;
    m68k_instruction [0x33fb] = m68k_33fb_move_w_al_dpcxi;
    m68k_instruction [0x33fc] = m68k_33fc_move_w_al_imm;
    m68k_instruction [0x40f8] = m68k_40f8_move_aw_sr;
    m68k_instruction [0x40f9] = m68k_40f9_move_al_sr;
    m68k_instruction [0x46f8] = m68k_46f8_move_sr_aw;
    m68k_instruction [0x46f9] = m68k_46f9_move_sr_al;
    m68k_instruction [0x46fa] = m68k_46fa_move_sr_dpc;
    m68k_instruction [0x46fb] = m68k_46fb_move_sr_dpcxi;
    m68k_instruction [0x46fc] = m68k_46fc_move_sr_imm;

    /* not */
    for (uint16_t reg = 0; reg < 8; reg++)
    {
        m68k_instruction [0x4600 | reg] = m68k_4600_not_b_dn;
        m68k_instruction [0x4640 | reg] = m68k_4640_not_w_dn;
        m68k_instruction [0x4680 | reg] = m68k_4680_not_l_dn;
    }

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
        m68k_instruction [0x4a80 | reg] = m68k_4a80_tst_l_dn;
        m68k_instruction [0x4a90 | reg] = m68k_4a90_tst_l_an;
        m68k_instruction [0x4a98 | reg] = m68k_4a98_tst_l_anp;
        m68k_instruction [0x4aa0 | reg] = m68k_4aa0_tst_l_pan;
        m68k_instruction [0x4aa8 | reg] = m68k_4aa8_tst_l_dan;
        m68k_instruction [0x4ab0 | reg] = m68k_4ab0_tst_l_danxi;
    }
    m68k_instruction [0x4a38] = m68k_4a38_tst_b_aw;
    m68k_instruction [0x4a78] = m68k_4a78_tst_w_aw;
    m68k_instruction [0x4a79] = m68k_4a79_tst_w_al;
    m68k_instruction [0x4ab8] = m68k_4ab8_tst_l_aw;
    m68k_instruction [0x4ab9] = m68k_4ab9_tst_l_al;


    m68k_instruction [0x4e71] = m68k_4e71_nop;
    m68k_instruction [0x4e73] = m68k_4e73_rte;
    m68k_instruction [0x4e75] = m68k_4e75_rts;
    m68k_instruction [0x4e76] = m68k_4e76_trapv;

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
        m68k_instruction [0x48a0 | an] = m68k_48a0_movem_w_pan_regs;
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
            m68k_instruction [0x41d0 | (an << 9) | reg] = m68k_41d0_lea_an;
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
        m68k_instruction [0x4200 | reg] = m68k_4200_clr_d_an;
        m68k_instruction [0x4210 | reg] = m68k_4210_clr_b_an;
        m68k_instruction [0x4218 | reg] = m68k_4218_clr_b_anp;
        m68k_instruction [0x4220 | reg] = m68k_4220_clr_b_pan;
        m68k_instruction [0x4228 | reg] = m68k_4228_clr_b_dan;
        m68k_instruction [0x4230 | reg] = m68k_4230_clr_b_danxi;
        m68k_instruction [0x4268 | reg] = m68k_4268_clr_w_dan;
        m68k_instruction [0x4280 | reg] = m68k_4280_clr_l_dn;
        m68k_instruction [0x4290 | reg] = m68k_4290_clr_l_an;
        m68k_instruction [0x4298 | reg] = m68k_4298_clr_l_anp;
        m68k_instruction [0x42a0 | reg] = m68k_42a0_clr_l_pan;
        m68k_instruction [0x42a8 | reg] = m68k_42a8_clr_l_dan;
        m68k_instruction [0x42b0 | reg] = m68k_42b0_clr_l_danxi;
    }
    m68k_instruction [0x4238] = m68k_4238_clr_b_aw;
    m68k_instruction [0x4239] = m68k_4239_clr_b_al;
    m68k_instruction [0x4278] = m68k_4278_clr_w_aw;
    m68k_instruction [0x42b8] = m68k_42b8_clr_l_aw;
    m68k_instruction [0x42b9] = m68k_42b9_clr_l_al;

    /* neg / swap / ext */
    for (uint16_t reg = 0; reg < 8; reg++)
    {
        m68k_instruction [0x4400 | reg] = m68k_4400_neg_b_dn;
        m68k_instruction [0x4410 | reg] = m68k_4410_neg_b_an;
        m68k_instruction [0x4428 | reg] = m68k_4428_neg_b_dan;
        m68k_instruction [0x4440 | reg] = m68k_4440_neg_w_dn;
        m68k_instruction [0x4450 | reg] = m68k_4450_neg_w_an;
        m68k_instruction [0x4458 | reg] = m68k_4458_neg_w_anp;
        m68k_instruction [0x4460 | reg] = m68k_4460_neg_w_pan;
        m68k_instruction [0x4468 | reg] = m68k_4468_neg_w_dan;
        m68k_instruction [0x4470 | reg] = m68k_4470_neg_w_danxi;
        m68k_instruction [0x4840 | reg] = m68k_4840_swap_dn;
        m68k_instruction [0x4880 | reg] = m68k_4880_ext_w_dn;
        m68k_instruction [0x48c0 | reg] = m68k_48c0_ext_l_dn;
    }
    m68k_instruction [0x4478] = m68k_4478_neg_w_aw;
    m68k_instruction [0x4479] = m68k_4479_neg_w_al;

    /* trap */
    for (uint32_t vector = 0; vector < 16; vector++)
    {
        m68k_instruction [0x4e40 | vector] = m68k_4e40_trap;
    }

    /* addq / subq */
    for (uint16_t data = 0; data < 8; data++)
    {
        for (uint16_t reg = 0; reg < 8; reg++)
        {
            m68k_instruction [0x5000 | (data << 9) | reg] = m68k_5000_addq_b_dn;
            m68k_instruction [0x5010 | (data << 9) | reg] = m68k_5010_addq_b_an;
            m68k_instruction [0x5018 | (data << 9) | reg] = m68k_5018_addq_b_anp;
            m68k_instruction [0x5020 | (data << 9) | reg] = m68k_5020_addq_b_pan;
            m68k_instruction [0x5028 | (data << 9) | reg] = m68k_5028_addq_b_dan;
            m68k_instruction [0x5030 | (data << 9) | reg] = m68k_5030_addq_b_danxi;
            m68k_instruction [0x5040 | (data << 9) | reg] = m68k_5040_addq_w_dn;
            m68k_instruction [0x5048 | (data << 9) | reg] = m68k_5048_addq_w_an;
            m68k_instruction [0x5050 | (data << 9) | reg] = m68k_5050_addq_w_an;
            m68k_instruction [0x5058 | (data << 9) | reg] = m68k_5058_addq_w_anp;
            m68k_instruction [0x5068 | (data << 9) | reg] = m68k_5068_addq_w_dan;
            m68k_instruction [0x5088 | (data << 9) | reg] = m68k_5088_addq_l_an;
            m68k_instruction [0x5100 | (data << 9) | reg] = m68k_5100_subq_b_dn;
            m68k_instruction [0x5110 | (data << 9) | reg] = m68k_5110_subq_b_an;
            m68k_instruction [0x5128 | (data << 9) | reg] = m68k_5128_subq_b_dan;
            m68k_instruction [0x5130 | (data << 9) | reg] = m68k_5130_subq_b_danxi;
            m68k_instruction [0x5140 | (data << 9) | reg] = m68k_5140_subq_w_dn;
            m68k_instruction [0x5148 | (data << 9) | reg] = m68k_5148_subq_w_an;
            m68k_instruction [0x5150 | (data << 9) | reg] = m68k_5150_subq_w_an;
            m68k_instruction [0x5158 | (data << 9) | reg] = m68k_5158_subq_w_anp;
            m68k_instruction [0x5168 | (data << 9) | reg] = m68k_5168_subq_w_dan;
            m68k_instruction [0x5180 | (data << 9) | reg] = m68k_5180_subq_l_dn;
            m68k_instruction [0x5188 | (data << 9) | reg] = m68k_5188_subq_l_an;
            m68k_instruction [0x5190 | (data << 9) | reg] = m68k_5190_subq_l_an;
            m68k_instruction [0x5198 | (data << 9) | reg] = m68k_5198_subq_l_anp;
            m68k_instruction [0x51a0 | (data << 9) | reg] = m68k_51a0_subq_l_pan;
            m68k_instruction [0x51a8 | (data << 9) | reg] = m68k_51a8_subq_l_dan;
            m68k_instruction [0x51b0 | (data << 9) | reg] = m68k_51b0_subq_l_danxi;
        }
        m68k_instruction [0x5038 | (data << 9)] = m68k_5038_addq_b_aw;
        m68k_instruction [0x5039 | (data << 9)] = m68k_5039_addq_b_al;
        m68k_instruction [0x5078 | (data << 9)] = m68k_5078_addq_w_aw;
        m68k_instruction [0x50b8 | (data << 9)] = m68k_50b8_addq_l_aw;
        m68k_instruction [0x5138 | (data << 9)] = m68k_5138_subq_b_aw;
        m68k_instruction [0x5178 | (data << 9)] = m68k_5178_subq_w_aw;
        m68k_instruction [0x51b8 | (data << 9)] = m68k_51b8_subq_l_aw;
        m68k_instruction [0x51b9 | (data << 9)] = m68k_51b9_subq_l_al;
    }

    /* Scc */
    for (uint16_t dn = 0; dn < 8; dn++)
    {
        m68k_instruction [0x50c0 | dn] = m68k_50c0_st_b_dn;
        m68k_instruction [0x50d0 | dn] = m68k_50d0_st_b_an;
        m68k_instruction [0x50d8 | dn] = m68k_50d8_st_b_anp;
        m68k_instruction [0x50e0 | dn] = m68k_50e0_st_b_pan;
        m68k_instruction [0x50e8 | dn] = m68k_50e8_st_b_dan;
        m68k_instruction [0x50f0 | dn] = m68k_50f0_st_b_danxi;
        m68k_instruction [0x56c0 | dn] = m68k_56c0_sne_b_dn;
        m68k_instruction [0x56d0 | dn] = m68k_56d0_sne_b_an;
        m68k_instruction [0x56d8 | dn] = m68k_56d8_sne_b_anp;
        m68k_instruction [0x56e0 | dn] = m68k_56e0_sne_b_pan;
        m68k_instruction [0x56e8 | dn] = m68k_56e8_sne_b_dan;
        m68k_instruction [0x56f0 | dn] = m68k_56f0_sne_b_danxi;
    }
    m68k_instruction [0x50f8] = m68k_50f8_st_b_aw;
    m68k_instruction [0x50f9] = m68k_50f9_st_b_al;
    m68k_instruction [0x56f8] = m68k_56f8_sne_b_aw;
    m68k_instruction [0x56f9] = m68k_56f9_sne_b_al;

    /* dbcc */
    for (uint16_t dn = 0; dn < 8; dn++)
    {
        m68k_instruction [0x51c8 | dn] = m68k_51c8_dbf;
        m68k_instruction [0x57c8 | dn] = m68k_57c8_dbeq;
    }

    /* Bcc/BSR/BRA with 16-bit displacement */
    m68k_instruction [0x6000] = m68k_6000_bra_w;
    m68k_instruction [0x6100] = m68k_6100_bsr_w;
    m68k_instruction [0x6200] = m68k_6200_bhi_w;
    m68k_instruction [0x6300] = m68k_6300_bls_w;
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
        m68k_instruction [0x6300 | d] = m68k_6301_bls_s;
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
            m68k_instruction [0x80d0 | (reg << 9) | ea] = m68k_80d0_divu_w_dn_an;
            m68k_instruction [0x80d8 | (reg << 9) | ea] = m68k_80d8_divu_w_dn_anp;
            m68k_instruction [0x80e0 | (reg << 9) | ea] = m68k_80e0_divu_w_dn_pan;
            m68k_instruction [0x80e8 | (reg << 9) | ea] = m68k_80e8_divu_w_dn_dan;
            m68k_instruction [0x80f0 | (reg << 9) | ea] = m68k_80f0_divu_w_dn_danxi;
            m68k_instruction [0x81c0 | (reg << 9) | ea] = m68k_81c0_divs_w_dn_dn;
            m68k_instruction [0x81d0 | (reg << 9) | ea] = m68k_81d0_divs_w_dn_an;
            m68k_instruction [0x81d8 | (reg << 9) | ea] = m68k_81d8_divs_w_dn_anp;
            m68k_instruction [0x81e0 | (reg << 9) | ea] = m68k_81e0_divs_w_dn_pan;
            m68k_instruction [0x81e8 | (reg << 9) | ea] = m68k_81e8_divs_w_dn_dan;
            m68k_instruction [0x81f0 | (reg << 9) | ea] = m68k_81f0_divs_w_dn_danxi;
        }
        m68k_instruction [0x80f8 | (reg << 9)] = m68k_80f8_divu_w_dn_aw;
        m68k_instruction [0x80f9 | (reg << 9)] = m68k_80f9_divu_w_dn_al;
        m68k_instruction [0x80fa | (reg << 9)] = m68k_80fa_divu_w_dn_dpc;
        m68k_instruction [0x80fb | (reg << 9)] = m68k_80fb_divu_w_dn_dpcxi;
        m68k_instruction [0x80fc | (reg << 9)] = m68k_80fc_divu_w_dn_imm;
        m68k_instruction [0x81f8 | (reg << 9)] = m68k_81f8_divs_w_dn_aw;
        m68k_instruction [0x81f9 | (reg << 9)] = m68k_81f9_divs_w_dn_al;
        m68k_instruction [0x81fa | (reg << 9)] = m68k_81fa_divs_w_dn_dpc;
        m68k_instruction [0x81fb | (reg << 9)] = m68k_81fb_divs_w_dn_dpcxi;
        m68k_instruction [0x81fc | (reg << 9)] = m68k_81fc_divs_w_dn_imm;
    }

    /* sub */
    for (uint16_t reg_a = 0; reg_a < 8; reg_a++)
    {
        for (uint16_t reg_b = 0; reg_b < 8; reg_b++)
        {
            m68k_instruction [0x9000 | (reg_a << 9) | reg_b] = m68k_9000_sub_b_dn_dn;
            m68k_instruction [0x9010 | (reg_a << 9) | reg_b] = m68k_9010_sub_b_dn_an;
            m68k_instruction [0x9018 | (reg_a << 9) | reg_b] = m68k_9018_sub_b_dn_anp;
            m68k_instruction [0x9020 | (reg_a << 9) | reg_b] = m68k_9020_sub_b_dn_pan;
            m68k_instruction [0x9028 | (reg_a << 9) | reg_b] = m68k_9028_sub_b_dn_dan;
            m68k_instruction [0x9030 | (reg_a << 9) | reg_b] = m68k_9030_sub_b_dn_danxi;
            m68k_instruction [0x9040 | (reg_a << 9) | reg_b] = m68k_9040_sub_w_dn_dn;
            m68k_instruction [0x9048 | (reg_a << 9) | reg_b] = m68k_9048_sub_w_dn_an;
            m68k_instruction [0x9050 | (reg_a << 9) | reg_b] = m68k_9050_sub_w_dn_an;
            m68k_instruction [0x9058 | (reg_a << 9) | reg_b] = m68k_9058_sub_w_dn_anp;
            m68k_instruction [0x9060 | (reg_a << 9) | reg_b] = m68k_9060_sub_w_dn_pan;
            m68k_instruction [0x9068 | (reg_a << 9) | reg_b] = m68k_9068_sub_w_dn_dan;
            m68k_instruction [0x9070 | (reg_a << 9) | reg_b] = m68k_9070_sub_w_dn_danxi;
            m68k_instruction [0x9080 | (reg_a << 9) | reg_b] = m68k_9080_sub_l_dn_dn;
            m68k_instruction [0x9088 | (reg_a << 9) | reg_b] = m68k_9088_sub_l_dn_an;
            m68k_instruction [0x9090 | (reg_a << 9) | reg_b] = m68k_9090_sub_l_dn_an;
            m68k_instruction [0x9098 | (reg_a << 9) | reg_b] = m68k_9098_sub_l_dn_anp;
            m68k_instruction [0x90a0 | (reg_a << 9) | reg_b] = m68k_90a0_sub_l_dn_pan;
            m68k_instruction [0x90a8 | (reg_a << 9) | reg_b] = m68k_90a8_sub_l_dn_dan;
            m68k_instruction [0x90b0 | (reg_a << 9) | reg_b] = m68k_90b0_sub_l_dn_danxi;
            m68k_instruction [0x90c0 | (reg_a << 9) | reg_b] = m68k_90c0_suba_w_an_dn;
            m68k_instruction [0x90c8 | (reg_a << 9) | reg_b] = m68k_90c8_suba_w_an_an;
            m68k_instruction [0x90d0 | (reg_a << 9) | reg_b] = m68k_90d0_suba_w_an_an;
            m68k_instruction [0x90d8 | (reg_a << 9) | reg_b] = m68k_90d8_suba_w_an_anp;
            m68k_instruction [0x90e0 | (reg_a << 9) | reg_b] = m68k_90e0_suba_w_an_pan;
            m68k_instruction [0x90e8 | (reg_a << 9) | reg_b] = m68k_90e8_suba_w_an_dan;
            m68k_instruction [0x90f0 | (reg_a << 9) | reg_b] = m68k_90f0_suba_w_an_danxi;
            m68k_instruction [0x9110 | (reg_a << 9) | reg_b] = m68k_9110_sub_b_an_dn;
            m68k_instruction [0x9118 | (reg_a << 9) | reg_b] = m68k_9118_sub_b_anp_dn;
            m68k_instruction [0x9120 | (reg_a << 9) | reg_b] = m68k_9120_sub_b_pan_dn;
            m68k_instruction [0x9128 | (reg_a << 9) | reg_b] = m68k_9128_sub_b_dan_dn;
            m68k_instruction [0x9130 | (reg_a << 9) | reg_b] = m68k_9130_sub_b_danxi_dn;
            m68k_instruction [0x9150 | (reg_a << 9) | reg_b] = m68k_9150_sub_w_an_dn;
            m68k_instruction [0x9158 | (reg_a << 9) | reg_b] = m68k_9158_sub_w_anp_dn;
            m68k_instruction [0x9160 | (reg_a << 9) | reg_b] = m68k_9160_sub_w_pan_dn;
            m68k_instruction [0x9168 | (reg_a << 9) | reg_b] = m68k_9168_sub_w_dan_dn;
            m68k_instruction [0x9170 | (reg_a << 9) | reg_b] = m68k_9170_sub_w_danxi_dn;
            m68k_instruction [0x9190 | (reg_a << 9) | reg_b] = m68k_9190_sub_l_an_dn;
            m68k_instruction [0x9198 | (reg_a << 9) | reg_b] = m68k_9198_sub_l_anp_dn;
            m68k_instruction [0x91a0 | (reg_a << 9) | reg_b] = m68k_91a0_sub_l_pan_dn;
            m68k_instruction [0x91a8 | (reg_a << 9) | reg_b] = m68k_91a8_sub_l_dan_dn;
            m68k_instruction [0x91b0 | (reg_a << 9) | reg_b] = m68k_91b0_sub_l_danxi_dn;
            m68k_instruction [0x91c0 | (reg_a << 9) | reg_b] = m68k_91c0_suba_l_an_dn;
            m68k_instruction [0x91c8 | (reg_a << 9) | reg_b] = m68k_91c8_suba_l_an_an;
            m68k_instruction [0x91d0 | (reg_a << 9) | reg_b] = m68k_91d0_suba_l_an_an;
            m68k_instruction [0x91d8 | (reg_a << 9) | reg_b] = m68k_91d8_suba_l_an_anp;
            m68k_instruction [0x91e0 | (reg_a << 9) | reg_b] = m68k_91e0_suba_l_an_pan;
            m68k_instruction [0x91e8 | (reg_a << 9) | reg_b] = m68k_91e8_suba_l_an_dan;
            m68k_instruction [0x91f0 | (reg_a << 9) | reg_b] = m68k_91f0_suba_l_an_danxi;
        }
        m68k_instruction [0x9038 | (reg_a << 9)] = m68k_9038_sub_b_dn_aw;
        m68k_instruction [0x9039 | (reg_a << 9)] = m68k_9039_sub_b_dn_al;
        m68k_instruction [0x903a | (reg_a << 9)] = m68k_903a_sub_b_dn_dpc;
        m68k_instruction [0x903b | (reg_a << 9)] = m68k_903b_sub_b_dn_dpcxi;
        m68k_instruction [0x903c | (reg_a << 9)] = m68k_903c_sub_b_dn_imm;
        m68k_instruction [0x9078 | (reg_a << 9)] = m68k_9078_sub_w_dn_aw;
        m68k_instruction [0x9079 | (reg_a << 9)] = m68k_9079_sub_w_dn_al;
        m68k_instruction [0x907a | (reg_a << 9)] = m68k_907a_sub_w_dn_dpc;
        m68k_instruction [0x907b | (reg_a << 9)] = m68k_907b_sub_w_dn_dpcxi;
        m68k_instruction [0x907c | (reg_a << 9)] = m68k_907c_sub_w_dn_imm;
        m68k_instruction [0x90b8 | (reg_a << 9)] = m68k_90b8_sub_l_dn_aw;
        m68k_instruction [0x90b9 | (reg_a << 9)] = m68k_90b9_sub_l_dn_al;
        m68k_instruction [0x90ba | (reg_a << 9)] = m68k_90ba_sub_l_dn_dpc;
        m68k_instruction [0x90bb | (reg_a << 9)] = m68k_90bb_sub_l_dn_dpcxi;
        m68k_instruction [0x90bc | (reg_a << 9)] = m68k_90bc_sub_l_dn_imm;
        m68k_instruction [0x90f8 | (reg_a << 9)] = m68k_90f8_suba_w_an_aw;
        m68k_instruction [0x90f9 | (reg_a << 9)] = m68k_90f9_suba_w_an_al;
        m68k_instruction [0x90fa | (reg_a << 9)] = m68k_90fa_suba_w_an_dpc;
        m68k_instruction [0x90fb | (reg_a << 9)] = m68k_90fb_suba_w_an_dpcxi;
        m68k_instruction [0x90fc | (reg_a << 9)] = m68k_90fc_suba_w_an_imm;
        m68k_instruction [0x9138 | (reg_a << 9)] = m68k_9138_sub_b_aw_dn;
        m68k_instruction [0x9139 | (reg_a << 9)] = m68k_9139_sub_b_al_dn;
        m68k_instruction [0x9178 | (reg_a << 9)] = m68k_9178_sub_w_aw_dn;
        m68k_instruction [0x9179 | (reg_a << 9)] = m68k_9179_sub_w_al_dn;
        m68k_instruction [0x91b8 | (reg_a << 9)] = m68k_91b8_sub_l_aw_dn;
        m68k_instruction [0x91b9 | (reg_a << 9)] = m68k_91b9_sub_l_al_dn;
        m68k_instruction [0x91f8 | (reg_a << 9)] = m68k_91f8_suba_l_an_aw;
        m68k_instruction [0x91f9 | (reg_a << 9)] = m68k_91f9_suba_l_an_al;
        m68k_instruction [0x91fa | (reg_a << 9)] = m68k_91fa_suba_l_an_dpc;
        m68k_instruction [0x91fb | (reg_a << 9)] = m68k_91fb_suba_l_an_dpcxi;
        m68k_instruction [0x91fc | (reg_a << 9)] = m68k_91fc_suba_l_an_imm;
    }

    /* Line-A exception */
    for (uint32_t i = 0x0000; i <= 0x0fff; i++)
    {
        m68k_instruction [0xa000 | i] = m68k_a000_line_a;
    }

    /* cmp / cmpa */
    for (uint16_t reg = 0; reg < 8; reg++)
    {
        for (uint16_t ea = 0; ea < 8; ea++)
        {
            m68k_instruction [0xb000 | (reg << 9) | ea] = m68k_b000_cmp_b_dn_dn;
            m68k_instruction [0xb010 | (reg << 9) | ea] = m68k_b010_cmp_b_dn_an;
            m68k_instruction [0xb018 | (reg << 9) | ea] = m68k_b018_cmp_b_dn_anp;
            m68k_instruction [0xb020 | (reg << 9) | ea] = m68k_b020_cmp_b_dn_pan;
            m68k_instruction [0xb028 | (reg << 9) | ea] = m68k_b028_cmp_b_dn_dan;
            m68k_instruction [0xb030 | (reg << 9) | ea] = m68k_b030_cmp_b_dn_danxi;
            m68k_instruction [0xb040 | (reg << 9) | ea] = m68k_b040_cmp_w_dn_dn;
            m68k_instruction [0xb048 | (reg << 9) | ea] = m68k_b048_cmp_w_dn_an;
            m68k_instruction [0xb050 | (reg << 9) | ea] = m68k_b050_cmp_w_dn_an;
            m68k_instruction [0xb068 | (reg << 9) | ea] = m68k_b068_cmp_w_dn_dan;
            m68k_instruction [0xb088 | (reg << 9) | ea] = m68k_b088_cmp_l_dn_an;
            m68k_instruction [0xb090 | (reg << 9) | ea] = m68k_b090_cmp_l_dn_an;
            m68k_instruction [0xb0c0 | (reg << 9) | ea] = m68k_b0c0_cmpa_w_an_dn;
            m68k_instruction [0xb0c8 | (reg << 9) | ea] = m68k_b0c8_cmpa_w_an_an;
            m68k_instruction [0xb0d0 | (reg << 9) | ea] = m68k_b0d0_cmpa_w_an_an;
            m68k_instruction [0xb0d8 | (reg << 9) | ea] = m68k_b0d8_cmpa_w_an_anp;
            m68k_instruction [0xb0e0 | (reg << 9) | ea] = m68k_b0e0_cmpa_w_an_pan;
            m68k_instruction [0xb0e8 | (reg << 9) | ea] = m68k_b0e8_cmpa_w_an_dan;
            m68k_instruction [0xb0f0 | (reg << 9) | ea] = m68k_b0f0_cmpa_w_an_danxi;
            m68k_instruction [0xb1c0 | (reg << 9) | ea] = m68k_b1c0_cmpa_l_an_dn;
            m68k_instruction [0xb1c8 | (reg << 9) | ea] = m68k_b1c8_cmpa_l_an_an;
            m68k_instruction [0xb1d0 | (reg << 9) | ea] = m68k_b1d0_cmpa_l_an_an;
            m68k_instruction [0xb1d8 | (reg << 9) | ea] = m68k_b1d8_cmpa_l_an_anp;
            m68k_instruction [0xb1e0 | (reg << 9) | ea] = m68k_b1e0_cmpa_l_an_pan;
            m68k_instruction [0xb1e8 | (reg << 9) | ea] = m68k_b1e8_cmpa_l_an_dan;
            m68k_instruction [0xb1f0 | (reg << 9) | ea] = m68k_b1f0_cmpa_l_an_danxi;
        }
        m68k_instruction [0xb038 | (reg << 9)] = m68k_b038_cmp_b_dn_aw;
        m68k_instruction [0xb039 | (reg << 9)] = m68k_b039_cmp_b_dn_al;
        m68k_instruction [0xb03a | (reg << 9)] = m68k_b03a_cmp_b_dn_dpc;
        m68k_instruction [0xb03b | (reg << 9)] = m68k_b03b_cmp_b_dn_dpcxi;
        m68k_instruction [0xb03c | (reg << 9)] = m68k_b03c_cmp_b_dn_imm;
        m68k_instruction [0xb078 | (reg << 9)] = m68k_b078_cmp_w_dn_aw;
        m68k_instruction [0xb0b8 | (reg << 9)] = m68k_b0b8_cmp_l_dn_aw;
        m68k_instruction [0xb0f8 | (reg << 9)] = m68k_b0f8_cmpa_w_an_aw;
        m68k_instruction [0xb0f9 | (reg << 9)] = m68k_b0f9_cmpa_w_an_al;
        m68k_instruction [0xb0fa | (reg << 9)] = m68k_b0fa_cmpa_w_an_dpc;
        m68k_instruction [0xb0fb | (reg << 9)] = m68k_b0fb_cmpa_w_an_dpcxi;
        m68k_instruction [0xb0fc | (reg << 9)] = m68k_b0fc_cmpa_w_an_imm;
        m68k_instruction [0xb1f8 | (reg << 9)] = m68k_b1f8_cmpa_l_an_aw;
        m68k_instruction [0xb1f9 | (reg << 9)] = m68k_b1f9_cmpa_l_an_al;
        m68k_instruction [0xb1fa | (reg << 9)] = m68k_b1fa_cmpa_l_an_dpc;
        m68k_instruction [0xb1fb | (reg << 9)] = m68k_b1fb_cmpa_l_an_dpcxi;
        m68k_instruction [0xb1fc | (reg << 9)] = m68k_b1fc_cmpa_l_an_imm;
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
            m68k_instruction [0xc010 | (reg_a << 9) | reg_b] = m68k_c010_and_b_dn_an;
            m68k_instruction [0xc018 | (reg_a << 9) | reg_b] = m68k_c018_and_b_dn_anp;
            m68k_instruction [0xc020 | (reg_a << 9) | reg_b] = m68k_c020_and_b_dn_pan;
            m68k_instruction [0xc028 | (reg_a << 9) | reg_b] = m68k_c028_and_b_dn_dan;
            m68k_instruction [0xc030 | (reg_a << 9) | reg_b] = m68k_c030_and_b_dn_danxi;
            m68k_instruction [0xc040 | (reg_a << 9) | reg_b] = m68k_c040_and_w_dn_dn;
            m68k_instruction [0xc080 | (reg_a << 9) | reg_b] = m68k_c080_and_l_dn_dn;
            m68k_instruction [0xc110 | (reg_a << 9) | reg_b] = m68k_c110_and_b_an_dn;
            m68k_instruction [0xc118 | (reg_a << 9) | reg_b] = m68k_c118_and_b_anp_dn;
            m68k_instruction [0xc120 | (reg_a << 9) | reg_b] = m68k_c120_and_b_pan_dn;
            m68k_instruction [0xc128 | (reg_a << 9) | reg_b] = m68k_c128_and_b_dan_dn;
            m68k_instruction [0xc130 | (reg_a << 9) | reg_b] = m68k_c130_and_b_danxi_dn;
        }
        m68k_instruction [0xc038 | (reg_a << 9)] = m68k_c038_and_b_dn_aw;
        m68k_instruction [0xc039 | (reg_a << 9)] = m68k_c039_and_b_dn_al;
        m68k_instruction [0xc03a | (reg_a << 9)] = m68k_c03a_and_b_dn_dpc;
        m68k_instruction [0xc03b | (reg_a << 9)] = m68k_c03b_and_b_dn_dpcxi;
        m68k_instruction [0xc03c | (reg_a << 9)] = m68k_c03c_and_b_dn_imm;
        m68k_instruction [0xc07b | (reg_a << 9)] = m68k_c07b_and_w_dn_dpcxi;
        m68k_instruction [0xc138 | (reg_a << 9)] = m68k_c138_and_b_aw_dn;
        m68k_instruction [0xc139 | (reg_a << 9)] = m68k_c139_and_b_al_dn;
    }

    /* exg */
    for (uint16_t reg_x = 0; reg_x < 8; reg_x++)
    {
        for (uint16_t reg_y = 0; reg_y < 8; reg_y++)
        {
            m68k_instruction [0xc140 | (reg_x << 9) | reg_y] = m68k_c140_exg_l_dn_dn;
        }
    }

    /* muls / mulu */
    for (uint16_t reg = 0; reg < 8; reg++)
    {
        for (uint16_t ea = 0; ea < 8; ea++)
        {
            m68k_instruction [0xc0c0 | (reg << 9) | ea] = m68k_c0c0_mulu_w_dn_dn;
            m68k_instruction [0xc1c0 | (reg << 9) | ea] = m68k_c1c0_muls_w_dn_dn;
            m68k_instruction [0xc1e8 | (reg << 9) | ea] = m68k_c1e8_muls_w_dn_dan;
        }
        m68k_instruction [0xc0fc | (reg << 9)] = m68k_c0fc_mulu_w_dn_imm;
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
            m68k_instruction [0xd060 | (reg << 9) | ea] = m68k_d060_add_w_dn_pan;
            m68k_instruction [0xd068 | (reg << 9) | ea] = m68k_d068_add_w_dn_dan;
            m68k_instruction [0xd070 | (reg << 9) | ea] = m68k_d070_add_w_dn_danxi;
            m68k_instruction [0xd080 | (reg << 9) | ea] = m68k_d080_add_l_dn_dn;
            m68k_instruction [0xd088 | (reg << 9) | ea] = m68k_d088_add_l_dn_an;
            m68k_instruction [0xd0c0 | (reg << 9) | ea] = m68k_d0c0_adda_w_an_dn;
            m68k_instruction [0xd0c8 | (reg << 9) | ea] = m68k_d0c8_adda_w_an_an;
            m68k_instruction [0xd0d0 | (reg << 9) | ea] = m68k_d0d0_adda_w_an_an;
            m68k_instruction [0xd0f0 | (reg << 9) | ea] = m68k_d0f0_adda_w_an_danxi;
            m68k_instruction [0xd128 | (reg << 9) | ea] = m68k_d128_add_b_dan_dn;
            m68k_instruction [0xd150 | (reg << 9) | ea] = m68k_d150_add_w_an_dn;
            m68k_instruction [0xd158 | (reg << 9) | ea] = m68k_d158_add_w_anp_dn;
            m68k_instruction [0xd160 | (reg << 9) | ea] = m68k_d160_add_w_pan_dn;
            m68k_instruction [0xd168 | (reg << 9) | ea] = m68k_d168_add_w_dan_dn;
            m68k_instruction [0xd170 | (reg << 9) | ea] = m68k_d170_add_w_danxi_dn;
            m68k_instruction [0xd190 | (reg << 9) | ea] = m68k_d190_add_l_an_dn;
            m68k_instruction [0xd1a8 | (reg << 9) | ea] = m68k_d1a8_add_l_dan_dn;
            m68k_instruction [0xd1c0 | (reg << 9) | ea] = m68k_d1c0_adda_l_an_dn;
        }
        m68k_instruction [0xd038 | (reg << 9)] = m68k_d038_add_b_dn_aw;
        m68k_instruction [0xd078 | (reg << 9)] = m68k_d078_add_w_dn_aw;
        m68k_instruction [0xd079 | (reg << 9)] = m68k_d079_add_w_dn_al;
        m68k_instruction [0xd07a | (reg << 9)] = m68k_d07a_add_w_dn_dpc;
        m68k_instruction [0xd07b | (reg << 9)] = m68k_d07b_add_w_dn_dpcxi;
        m68k_instruction [0xd0b8 | (reg << 9)] = m68k_d0b8_add_l_dn_aw;
        m68k_instruction [0xd0fc | (reg << 9)] = m68k_d0fc_adda_w_an_imm;
        m68k_instruction [0xd178 | (reg << 9)] = m68k_d178_add_w_aw_dn;
        m68k_instruction [0xd179 | (reg << 9)] = m68k_d179_add_w_al_dn;
        m68k_instruction [0xd1b8 | (reg << 9)] = m68k_d1b8_add_l_aw_dn;
    }

    /* shift / rotate */
    for (uint16_t reg = 0; reg < 8; reg++)
    {
        for (uint16_t count = 0; count < 8; count++)
        {
            m68k_instruction [0xe008 | (count << 9) | reg] = m68k_e008_lsr_b_dn_imm;
            m68k_instruction [0xe018 | (count << 9) | reg] = m68k_e018_ror_b_dn_imm;
            m68k_instruction [0xe038 | (count << 9) | reg] = m68k_e038_ror_b_dn_dn;
            m68k_instruction [0xe040 | (count << 9) | reg] = m68k_e040_asr_w_dn_imm;
            m68k_instruction [0xe048 | (count << 9) | reg] = m68k_e048_lsr_w_dn_imm;
            m68k_instruction [0xe058 | (count << 9) | reg] = m68k_e058_ror_w_dn_imm;
            m68k_instruction [0xe068 | (count << 9) | reg] = m68k_e068_lsr_w_dn_dn;
            m68k_instruction [0xe078 | (count << 9) | reg] = m68k_e078_ror_w_dn_dn;
            m68k_instruction [0xe080 | (count << 9) | reg] = m68k_e080_asr_l_dn_imm;
            m68k_instruction [0xe098 | (count << 9) | reg] = m68k_e098_ror_l_dn_imm;
            m68k_instruction [0xe0b8 | (count << 9) | reg] = m68k_e0b8_ror_l_dn_dn;
            m68k_instruction [0xe108 | (count << 9) | reg] = m68k_e108_lsl_b_dn_imm;
            m68k_instruction [0xe118 | (count << 9) | reg] = m68k_e118_rol_b_dn_imm;
            m68k_instruction [0xe138 | (count << 9) | reg] = m68k_e138_rol_b_dn_dn;
            m68k_instruction [0xe140 | (count << 9) | reg] = m68k_e140_asl_w_dn_imm;
            m68k_instruction [0xe148 | (count << 9) | reg] = m68k_e148_lsl_w_dn_imm;
            m68k_instruction [0xe150 | (count << 9) | reg] = m68k_e150_roxl_w_dn_imm;
            m68k_instruction [0xe158 | (count << 9) | reg] = m68k_e158_rol_w_dn_imm;
            m68k_instruction [0xe160 | (count << 9) | reg] = m68k_e160_asl_w_dn_dn;
            m68k_instruction [0xe168 | (count << 9) | reg] = m68k_e168_lsl_w_dn_dn;
            m68k_instruction [0xe178 | (count << 9) | reg] = m68k_e178_rol_w_dn_dn;
            m68k_instruction [0xe180 | (count << 9) | reg] = m68k_e180_asl_l_dn_imm;
            m68k_instruction [0xe188 | (count << 9) | reg] = m68k_e188_lsl_l_dn_imm;
            m68k_instruction [0xe198 | (count << 9) | reg] = m68k_e198_rol_l_dn_imm;
            m68k_instruction [0xe1b8 | (count << 9) | reg] = m68k_e1b8_rol_l_dn_dn;
        }
        m68k_instruction [0xe0e8 | reg] = m68k_e0e8_asr_w_dan;
        m68k_instruction [0xe1d0 | reg] = m68k_e1d0_asl_w_an;
        m68k_instruction [0xe1d8 | reg] = m68k_e1d8_asl_w_anp;
        m68k_instruction [0xe1e0 | reg] = m68k_e1e0_asl_w_pan;
        m68k_instruction [0xe1e8 | reg] = m68k_e1e8_asl_w_dan;
        m68k_instruction [0xe1f0 | reg] = m68k_e1f0_asl_w_danxi;
        m68k_instruction [0xe6d0 | reg] = m68k_e6d0_ror_w_an;
        m68k_instruction [0xe6d8 | reg] = m68k_e6d8_ror_w_anp;
        m68k_instruction [0xe6e0 | reg] = m68k_e6e0_ror_w_pan;
        m68k_instruction [0xe6e8 | reg] = m68k_e6e8_ror_w_dan;
        m68k_instruction [0xe6f0 | reg] = m68k_e6f0_ror_w_danxi;
        m68k_instruction [0xe7d0 | reg] = m68k_e7d0_rol_w_an;
        m68k_instruction [0xe7d8 | reg] = m68k_e7d8_rol_w_anp;
        m68k_instruction [0xe7e0 | reg] = m68k_e7e0_rol_w_pan;
        m68k_instruction [0xe7e8 | reg] = m68k_e7e8_rol_w_dan;
        m68k_instruction [0xe7f0 | reg] = m68k_e7f0_rol_w_danxi;
    }
    m68k_instruction [0xe1f8] = m68k_e1f8_asl_w_aw;
    m68k_instruction [0xe1f9] = m68k_e1f9_asl_w_al;
    m68k_instruction [0xe6f8] = m68k_e6f8_ror_w_aw;
    m68k_instruction [0xe6f9] = m68k_e6f9_ror_w_al;
    m68k_instruction [0xe7f8] = m68k_e7f8_rol_w_aw;
    m68k_instruction [0xe7f9] = m68k_e7f9_rol_w_al;

    /* Line-F exception */
    for (uint32_t i = 0x0000; i <= 0x0fff; i++)
    {
        m68k_instruction [0xf000 | i] = m68k_f000_line_f;
    }

    /* An estimate of progress */
    uint32_t populated = 0;
    for (uint32_t i = 0; i < SIZE_64K; i++)
    {
        if (m68k_instruction [i] != NULL)
        {
            populated++;
        }
    }
    printf ("[%s] %d of %d opcodes populated. (%2.1f%%)\n", __func__,
            populated, SIZE_64K, 100.0 * (populated / (float) SIZE_64K));
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
        snepulator_error ("M68000 Error", "Unknown %s instruction: %04x. (PC=%06x)",
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
                 instruction, context->state.pc - 2);
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
            m68k_exception (context, 0x60 + (interrupt << 2));
            context->state.sr_interrupt_priority = interrupt;

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
