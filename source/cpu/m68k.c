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
    return util_ntoh16 (context->memory_read_16 (context->parent, addr & 0x00ffffff));
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
    context->memory_write_16 (context->parent, addr & 0x00ffffff, util_hton16 (data));
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
static inline uint32_t read_extenison_long (M68000_Context *context)
{
    uint32_reg addr;
    addr.w_high = read_extension (context);
    addr.w_low  = read_extension (context);
    return addr.l;
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


/* move.b Dn ← d(An) */
static uint32_t m68k_1028_move_b_dn_dan (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint16_t source_reg = instruction & 0x07;
    uint16_t displacement = read_extension (context);

    uint8_t value = read_byte (context, context->state.a [source_reg] + (int16_t) displacement);
    context->state.d [dest_reg].b = value;

    context->state.ccr_negative = ((int8_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;

    printf ("move.b d%d ← %04x(a%d)\n", dest_reg, displacement, source_reg);
    return 0;
}


/* move.l d(An) ← #xxxx */
static uint32_t m68k_217c_move_l_dan_imm (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint32_t imm = read_extenison_long (context);
    uint16_t displacement = read_extension (context);

    write_long (context, context->state.a [dest_reg] + (int16_t) displacement, imm);

    context->state.ccr_negative = ((int32_t) imm < 0);
    context->state.ccr_zero = (imm == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;

    printf ("move.l %04x(a%d) ← #%08x\n", displacement, dest_reg, imm);
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

    context->state.ccr_negative = ((int16_t) value < 0);
    context->state.ccr_zero = (value == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;

    printf ("move.w d%d ← (a%d)\n", dest_reg, source_reg);
    return 0;
}


/* tst.w (xxx).l */
static uint32_t m68k_4a79_tst_w_al (M68000_Context *context, uint16_t instruction)
{
    uint32_t addr = read_extenison_long (context);
    uint16_t operand = read_word (context, addr);

    context->state.ccr_negative = ((int16_t) operand < 0);
    context->state.ccr_zero = (operand == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;

    printf ("tst.w (%08x).l\n", addr);
    return 0;
}


/* tst.l (xxx).l */
static uint32_t m68k_4ab9_tst_l_al (M68000_Context *context, uint16_t instruction)
{
    uint32_t addr = read_extenison_long (context);
    uint32_t operand = read_long (context, addr);

    context->state.ccr_negative = ((int32_t) operand < 0);
    context->state.ccr_zero = (operand == 0);
    context->state.ccr_overflow = 0;
    context->state.ccr_carry = 0;

    printf ("tst.l (%08x).l\n", addr);
    return 0;
}


/* lea d(PC) */
/* TODO: Replace 'x' in function names with lowest possible value for same instruction */
static uint32_t m68k_4xfa_lea_dpc (M68000_Context *context, uint16_t instruction)
{
    uint16_t dest_reg = (instruction >> 9) & 0x07;
    uint32_t pc = context->state.pc;
    uint16_t displacement = read_extension (context);

    context->state.a [dest_reg] = pc + (int16_t) displacement;

    printf ("lea a%d ← d(pc)\n", dest_reg);
    return 0;
}


/* movem.w memory-to-register (An)+ */
static uint32_t m68k_4c98_movem_w_anp (M68000_Context *context, uint16_t instruction)
{
    uint32_t reg = instruction & 0x07;
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


/* movem.l memory-to-register (An)+ */
static uint32_t m68k_4cd8_movem_l_anp (M68000_Context *context, uint16_t instruction)
{
    uint32_t reg = instruction & 0x07;
    uint16_t mask = read_extension (context);

    /* Data registers first */
    for (uint32_t i = 0; i < 16; i++)
    {
        if (mask & (1 << i))
        {
            if (i < 8)
            {
                context->state.d [i].l = read_long (context, context->state.a [reg]);
                context->state.a [reg] += 4;
            }
            else
            {
                context->state.a [i - 8] = read_long (context, context->state.a [reg]);
                context->state.a [reg] += 4;
            }
        }
    }

    printf ("movem.l (a%d)+\n", reg);
    return 0;
}


/* bra */
static uint32_t m68k_60_bra (M68000_Context *context, uint16_t instruction)
{
    int8_t offset = instruction & 0xff;
    context->state.pc += offset;
    printf ("bra %+d\n", offset);
    return 0;
}


/* bne */
static uint32_t m68k_66_bne (M68000_Context *context, uint16_t instruction)
{
    if (!context->state.ccr_zero)
    {
        int8_t offset = instruction & 0xff;
        context->state.pc += offset;
        printf ("bne %+d\n", offset);
    }
    else
    {
        printf ("bne (not taken).\n");
    }

    return 0;
}


/* beq */
static uint32_t m68k_67_beq (M68000_Context *context, uint16_t instruction)
{
    if (context->state.ccr_zero)
    {
        int8_t offset = instruction & 0xff;
        context->state.pc += offset;
        printf ("beq %+d\n", offset);
    }
    else
    {
        printf ("beq (not taken).\n");
    }

    return 0;
}


/*
 * Initialise the instruction array.
 * TODO: This only needs to happen once
 */
static void m68k_init_instructions (void)
{

    /* andi */
    for (uint16_t dn = 0; dn < 8; dn++)
    {
        m68k_instruction [0x0200 | dn] = m68k_0200_andi_b_dn;
    }

    /* move */
    for (uint16_t dn = 0; dn < 8; dn++)
    {
        for (uint16_t an = 0; an < 8; an++)
        {
            m68k_instruction [0x1028 | (dn << 9) | an] = m68k_1028_move_b_dn_dan;
            m68k_instruction [0x3010 | (dn << 9) | an] = m68k_3010_move_w_dn_an;
        }
    }
    for (uint16_t an = 0; an < 8; an++)
    {
        m68k_instruction [0x217c | an << 9] = m68k_217c_move_l_dan_imm;
    }

    m68k_instruction [0x4a79] = m68k_4a79_tst_w_al;
    m68k_instruction [0x4ab9] = m68k_4ab9_tst_l_al;

    /* movem */
    for (uint16_t an = 0; an < 8; an++)
    {
        m68k_instruction [0x4c98 | an] = m68k_4c98_movem_w_anp;
        m68k_instruction [0x4cd8 | an] = m68k_4cd8_movem_l_anp;
    }

    /* lea */
    for (uint16_t an = 0; an < 8; an++)
    {
        m68k_instruction [0x41fa | (an << 9)] = m68k_4xfa_lea_dpc;
    }

    /* Bcc/BSR/BRA */
    for (uint16_t d = 0x01; d <= 0xff; d++)
    {
        m68k_instruction [0x6000 | d] = m68k_60_bra;
        m68k_instruction [0x6600 | d] = m68k_66_bne;
        m68k_instruction [0x6700 | d] = m68k_67_beq;
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
    return 5000; /* placeholder */
}


/*
 * Run the 68000 for the specified number of clock cycles.
 */
void m68k_run_cycles (M68000_Context *context, int64_t cycles)
{
    /* Account for cycles used during the last run */
    context->clock_cycles += cycles;

    /* As long as we have a positive number of cycles, run an instruction */
    while (context->clock_cycles > 0)
    {
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
