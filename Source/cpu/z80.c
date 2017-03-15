#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define SWAP(TYPE, X, Y) { TYPE tmp = X; X = Y; Y = tmp; }

/* Structs */
/* TODO: Do we need to do anything extra to tell the compiler to pack the structs? */
/* Note: Assuming little endian for now */
typedef struct Z80_Regs_t {
    /* Special Purpose Registers */
    uint8_t  i;  /* Interrupt Vector */
    uint8_t  r;  /* Memory Refresh */
    uint16_t ix; /* Index register */
    uint16_t iy; /* Index register */
    uint16_t sp; /* Stack Pointer */
    union {
        uint16_t pc; /* Program Counter */
        struct {
            uint8_t pc_l;
            uint8_t pc_h;
        };
    };
    /* Main Register Set */
    union {
        struct {
            uint16_t af;
            uint16_t bc;
            uint16_t de;
            uint16_t hl;
        };
        struct {
            uint8_t a;
            uint8_t f;
            uint8_t c;
            uint8_t b;
            uint8_t e;
            uint8_t d;
            uint8_t l;
            uint8_t h;
        };
    };
    /* Alternate Register Set */
    union {
        struct {
            uint16_t alt_af;
            uint16_t alt_bc;
            uint16_t alt_de;
            uint16_t alt_hl;
        };
        struct {
            uint8_t alt_a;
            uint8_t alt_f;
            uint8_t alt_c;
            uint8_t alt_b;
            uint8_t alt_e;
            uint8_t alt_d;
            uint8_t alt_l;
            uint8_t alt_h;
        };
    };
} Z80_Regs;

#define Z80_FLAG_CARRY           (1 << 0)
#define Z80_FLAG_SUB             (1 << 1)
#define Z80_FLAG_PARITY          (1 << 2)
#define Z80_FLAG_OVERFLOW        (1 << 2)
#define Z80_FLAG_HALF            (1 << 4)
#define Z80_FLAG_ZERO            (1 << 6)
#define Z80_FLAG_SIGN            (1 << 7)


/* The CPU interacts with the rest of the world by reading and writing to addresses,
 * so we need to construct an interface to allow this */

Z80_Regs z80_regs;

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

void z80_reset ()
{
    memset (&z80_regs, 0, sizeof (Z80_Regs));
}

#define SET_FLAGS_LOGIC  { z80_regs.f = (uint8_even_parity[z80_regs.a] ? Z80_FLAG_PARITY : 0) | \
                                        (z80_regs.a == 0x00 ? Z80_FLAG_ZERO : 0             ) | \
                                        ((z80_regs.a & 0x80)  ? Z80_FLAG_SIGN : 0           ); }

/* TODO: Overflow flag */
#define SET_FLAGS_ADD(X) { z80_regs.f = (((uint16_t)z80_regs.a + (uint16_t)X) & 0x100 ? Z80_FLAG_CARRY : 0) | \
                                        (z80_regs.a + X == 0x00 ? Z80_FLAG_ZERO  : 0                      ) | \
                                        ((z80_regs.a + X & 0x80)  ? Z80_FLAG_SIGN  : 0                    ); }

/* TODO: Overflow flag */
#define SET_FLAGS_SUB(X) { z80_regs.f = ((uint16_t)z80_regs.a - (uint16_t)X & 0x100 ? Z80_FLAG_CARRY : 0) | \
                                        (Z80_FLAG_SUB                                                   ) | \
                                        (z80_regs.a == X   ? Z80_FLAG_ZERO  : 0                         ) | \
                                        ((z80_regs.a - X & 0x80) ? Z80_FLAG_SIGN  : 0                   ); }

#define SET_FLAGS_INC(X) { z80_regs.f = (z80_regs.f & Z80_FLAG_CARRY             ) | \
                                        (X == 0x80        ? Z80_FLAG_OVERFLOW : 0) | \
                                        ((X & 0x0f) == 0x00 ? Z80_FLAG_HALF : 0  ) | \
                                        (X == 0x00        ? Z80_FLAG_ZERO : 0    ) | \
                                        ((X  & 0x80)        ? Z80_FLAG_SIGN : 0  ); }

#define SET_FLAGS_DEC(X) { z80_regs.f = (z80_regs.f & Z80_FLAG_CARRY             ) | \
                                        (Z80_FLAG_SUB                            ) | \
                                        (X == 0x7f        ? Z80_FLAG_OVERFLOW : 0) | \
                                        ((X & 0x0f) == 0x0f ? Z80_FLAG_HALF : 0  ) | \
                                        (X == 0x00        ? Z80_FLAG_ZERO : 0    ) | \
                                        ((X  & 0x80)        ? Z80_FLAG_SIGN : 0  ); }

#define SET_FLAGS_CPL { z80_regs.f = z80_regs.f | Z80_FLAG_HALF | Z80_FLAG_SUB; }

/* TEMPORORY */
#include "SDL2/SDL.h"
extern SDL_Renderer *renderer;
void vdp_render (void);

uint32_t z80_run (uint8_t (* memory_read) (uint16_t),
                  void    (* memory_write)(uint16_t, uint8_t),
                  uint8_t (* io_read)     (uint8_t),
                  void    (* io_write)    (uint8_t, uint8_t))
{
    uint8_t instruction;
    uint8_t param_l;
    uint8_t param_h;
    uint16_t param_hl;
    uint8_t interrupt_mode;
    bool    interrupt_enable = true;
    uint32_t instruction_count = 0;

    for (;;)
    {
        /* Fetch */
        instruction = memory_read (z80_regs.pc++);

        switch (z80_instruction_size[instruction])
        {
            case 3:
                param_l = memory_read (z80_regs.pc++);
                param_h = memory_read (z80_regs.pc++);
                param_hl = (uint16_t) param_l + ((uint16_t) param_h << 8);
                break;
            case 2:
                param_l = memory_read (z80_regs.pc++);
                break;
            default:
                break;
        }

        switch (instruction)
        {
            case 0x00: /* NOP        */ break;
            case 0x01: /* LD BC      */ z80_regs.b = param_l; z80_regs.c = param_h; break;
            case 0x02: /* LD (BC) A  */ memory_write (z80_regs.bc, z80_regs.a); break;
            case 0x03: /* INC BC     */ z80_regs.bc++; break;
            case 0x04: /* INC B      */ z80_regs.b++; SET_FLAGS_INC (z80_regs.b); break;
            case 0x05: /* DEC B      */ z80_regs.b--; SET_FLAGS_DEC (z80_regs.b); break;
            case 0x06: /* LD B       */ z80_regs.b = param_l; break;
            case 0x07: /* RLCA       */ z80_regs.a = (z80_regs.a << 1) & ((z80_regs.a & 0x80) ? 1 : 0);
                                        z80_regs.f = (z80_regs.f & (Z80_FLAG_PARITY | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) |
                                                     ((z80_regs.a & 0x01) ? Z80_FLAG_CARRY : 0);
                                              break;
            case 0x08: /* EX AF AF'  */ SWAP (uint16_t, z80_regs.af, z80_regs.alt_af); break;
            case 0x0a: /* LD A,(BC)  */ z80_regs.a = memory_read (z80_regs.bc); break;
            case 0x0b: /* DEC BC     */ z80_regs.bc--; break;
            case 0x0c: /* INC C      */ z80_regs.c++; SET_FLAGS_INC (z80_regs.c); break;
            case 0x0e: /* LD C       */ z80_regs.c = param_l; break;

            case 0x10: /* DJNZ       */ z80_regs.b--; z80_regs.pc += z80_regs.b ? (int8_t) param_l : 0; break;
            case 0x11: /* LD DE,**   */ z80_regs.de = param_hl; break;
            case 0x16: /* LD D,*     */ z80_regs.d = param_l; break;
            case 0x18: /* JR         */ z80_regs.pc += param_l; break;
            case 0x1b: /* DEC BE     */ z80_regs.bc--; break;
            case 0x1f: /* RRA        */ { uint8_t temp = z80_regs.a;
                                        z80_regs.a = (z80_regs.a >> 1) + (z80_regs.f & Z80_FLAG_CARRY) ? 0x80 : 0;
                                        z80_regs.f = (z80_regs.f & (Z80_FLAG_PARITY | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) |
                                                     ((temp & 0x80) ? Z80_FLAG_CARRY : 0); } break;

            case 0x20: /* JR NZ      */ z80_regs.pc += (z80_regs.f & Z80_FLAG_ZERO) ? 0 : (int8_t)param_l; break;
            case 0x21: /* LD HL, **  */ z80_regs.hl = param_hl; break;
            case 0x23: /* INC HL     */ z80_regs.hl++; break;
            case 0x25: /* DEC H      */ z80_regs.h--; SET_FLAGS_DEC (z80_regs.h); break;
            case 0x2a: /* LD HL,(**) */ z80_regs.hl = memory_read (param_hl); break;
            case 0x2c: /* INC L      */ z80_regs.l++; SET_FLAGS_INC (z80_regs.l); break;
            case 0x2e: /* LD L,*     */ z80_regs.l = param_l; break;
            case 0x2f: /* CPL        */ z80_regs.a = ~z80_regs.a; SET_FLAGS_CPL; break;

            case 0x31: /* LD SP      */ z80_regs.sp = param_hl; break;
            case 0x32: /* LD (**),A  */ memory_write (param_hl, z80_regs.a); break;
            case 0x33: /* INC SP     */ z80_regs.sp++; break;
            case 0x34: /* INC (HL)   */ { uint8_t temp = memory_read (z80_regs.hl);
                                          temp++;
                                          memory_write (z80_regs.hl, temp);
                                          SET_FLAGS_INC (temp); } break;
            case 0x36: /* LD (HL),*  */ memory_write (z80_regs.hl, param_l); break;
            case 0x38: /* JR C,*     */ z80_regs.pc += (z80_regs.f & Z80_FLAG_CARRY) ? (int8_t) param_l : 0; break;;
            case 0x3a: /* LD A,(**)  */ z80_regs.a = memory_read (param_hl); break;
            case 0x3e: /* LD A,*     */ z80_regs.a = param_l; break;
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
            case 0x5e: /* LD E,(HL)  */ z80_regs.c = memory_read(z80_regs.hl); break;
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

            case 0x80: /* ADD B      */ SET_FLAGS_ADD (z80_regs.b); z80_regs.a += z80_regs.b; break;
            case 0x81: /* ADD C      */ SET_FLAGS_ADD (z80_regs.c); z80_regs.a += z80_regs.c; break;
            case 0x82: /* ADD D      */ SET_FLAGS_ADD (z80_regs.d); z80_regs.a += z80_regs.d; break;
            case 0x83: /* ADD E      */ SET_FLAGS_ADD (z80_regs.e); z80_regs.a += z80_regs.e; break;
            case 0x84: /* ADD H      */ SET_FLAGS_ADD (z80_regs.h); z80_regs.a += z80_regs.h; break;
            case 0x85: /* ADD L      */ SET_FLAGS_ADD (z80_regs.l); z80_regs.a += z80_regs.l; break;
            case 0x86: /* ADD (HL)   */ { uint8_t temp = memory_read (z80_regs.hl);
                                          SET_FLAGS_ADD (temp); z80_regs.a += temp; } break;
            case 0x87: /* ADD A      */ SET_FLAGS_ADD (z80_regs.a); z80_regs.a += z80_regs.a; break;

            case 0x88: /* ADC B      */ SET_FLAGS_ADD ((z80_regs.b + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                        z80_regs.a +=   z80_regs.b + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
            case 0x89: /* ADC C      */ SET_FLAGS_ADD ((z80_regs.c + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)))
                                        z80_regs.a +=   z80_regs.c + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
            case 0x8a: /* ADC D      */ SET_FLAGS_ADD ((z80_regs.d + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                        z80_regs.a +=   z80_regs.d + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
            case 0x8b: /* ADC E      */ SET_FLAGS_ADD ((z80_regs.e + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                        z80_regs.a +=   z80_regs.e + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
            case 0x8c: /* ADC H      */ SET_FLAGS_ADD ((z80_regs.h + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                        z80_regs.a +=   z80_regs.h + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
            case 0x8d: /* ADC L      */ SET_FLAGS_ADD ((z80_regs.l + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                        z80_regs.a +=   z80_regs.l + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
            case 0x8e: /* ADC (HL)   */ SET_FLAGS_ADD ((memory_read (z80_regs.hl) + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                        z80_regs.a +=   memory_read (z80_regs.hl) + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
            case 0x8f: /* ADC A      */ SET_FLAGS_ADD ((z80_regs.a + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                        z80_regs.a   += z80_regs.a + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;

            case 0x90: /* SUB B      */ SET_FLAGS_SUB (z80_regs.b); z80_regs.a -= z80_regs.b; break;
            case 0x91: /* SUB C      */ SET_FLAGS_SUB (z80_regs.b); z80_regs.a -= z80_regs.c; break;
            case 0x92: /* SUB D      */ SET_FLAGS_SUB (z80_regs.b); z80_regs.a -= z80_regs.d; break;
            case 0x93: /* SUB E      */ SET_FLAGS_SUB (z80_regs.b); z80_regs.a -= z80_regs.e; break;
            case 0x94: /* SUB H      */ SET_FLAGS_SUB (z80_regs.b); z80_regs.a -= z80_regs.h; break;
            case 0x95: /* SUB L      */ SET_FLAGS_SUB (z80_regs.b); z80_regs.a -= z80_regs.l; break;

            case 0x99: /* SBC C      */ SET_FLAGS_SUB ((z80_regs.c + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                        z80_regs.a -=   z80_regs.c + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
            case 0x9b: /* SBC E      */ SET_FLAGS_SUB ((z80_regs.e + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                        z80_regs.a -=   z80_regs.e + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;

            case 0xa0: /* AND B      */ z80_regs.a &= z80_regs.b; SET_FLAGS_LOGIC; break;
            case 0xa1: /* AND C      */ z80_regs.a &= z80_regs.c; SET_FLAGS_LOGIC; break;
            case 0xa2: /* AND D      */ z80_regs.a &= z80_regs.d; SET_FLAGS_LOGIC; break;
            case 0xa3: /* AND E      */ z80_regs.a &= z80_regs.e; SET_FLAGS_LOGIC; break;
            case 0xa4: /* AND H      */ z80_regs.a &= z80_regs.h; SET_FLAGS_LOGIC; break;
            case 0xa5: /* AND L      */ z80_regs.a &= z80_regs.l; SET_FLAGS_LOGIC; break;

            case 0xaf: /* XOR A      */ z80_regs.a ^= z80_regs.a; SET_FLAGS_LOGIC; break;

            case 0xb0: /* OR  B      */ z80_regs.a |= z80_regs.b; SET_FLAGS_LOGIC; break;
            case 0xb1: /* OR  C      */ z80_regs.a |= z80_regs.c; SET_FLAGS_LOGIC; break;
            case 0xb2: /* OR  D      */ z80_regs.a |= z80_regs.d; SET_FLAGS_LOGIC; break;
            case 0xb3: /* OR  E      */ z80_regs.a |= z80_regs.e; SET_FLAGS_LOGIC; break;
            case 0xb4: /* OR  H      */ z80_regs.a |= z80_regs.h; SET_FLAGS_LOGIC; break;
            case 0xb5: /* OR  L      */ z80_regs.a |= z80_regs.l; SET_FLAGS_LOGIC; break;
            case 0xb6: /* OR (HL)    */ z80_regs.a |= memory_read (z80_regs.hl); SET_FLAGS_LOGIC; break;
            case 0xb7: /* OR  A      */ z80_regs.a |= z80_regs.a; SET_FLAGS_LOGIC; break;

            case 0xb8: /* CP B       */ SET_FLAGS_SUB (z80_regs.b); break;
            case 0xb9: /* CP C       */ SET_FLAGS_SUB (z80_regs.c); break;
            case 0xba: /* CP D       */ SET_FLAGS_SUB (z80_regs.d); break;
            case 0xbb: /* CP E       */ SET_FLAGS_SUB (z80_regs.e); break;
            case 0xbc: /* CP H       */ SET_FLAGS_SUB (z80_regs.h); break;
            case 0xbd: /* CP L       */ SET_FLAGS_SUB (z80_regs.l); break;
            case 0xbe: /* CP (HL)    */ { uint8_t temp = memory_read (z80_regs.hl); SET_FLAGS_SUB (temp); } break;
            case 0xbf: /* CP A       */ SET_FLAGS_SUB (z80_regs.a); break;

            case 0xc1: /* POP BC     */ z80_regs.c = memory_read (z80_regs.sp++);
                                        z80_regs.b = memory_read (z80_regs.sp++); break;
            case 0xc2: /* JP NZ,**   */ z80_regs.pc = (z80_regs.a & Z80_FLAG_ZERO) ? z80_regs.pc : param_hl; break;
            case 0xc3: /* JP **      */ z80_regs.pc = param_hl; break;
            case 0xc5: /* PUSH BC    */ memory_write (--z80_regs.sp, z80_regs.b);
                                        memory_write (--z80_regs.sp, z80_regs.c); break;
            case 0xc8: /* RET Z      */ if (z80_regs.f & Z80_FLAG_ZERO)
                                        {
                                            z80_regs.pc_l = memory_read (z80_regs.sp++);
                                            z80_regs.pc_h = memory_read (z80_regs.sp++);
                                        } break;
            case 0xcb: /* Bit Instructions */

                instruction = memory_read (z80_regs.pc++);

                /* TODO: Perhaps a 256-entry switch statement is not the correct method for this */
                switch (instruction)
                {
                    case 0x79: /* BIT 7,C */ z80_regs.f = (z80_regs.f & Z80_FLAG_CARRY) |
                                                          ((z80_regs.c & 0x80) ? 0 : Z80_FLAG_ZERO) |
                                                          (Z80_FLAG_HALF); break;
                    case 0xb8: /* RES 7,B */ z80_regs.b &= 0x7f; break;

                    default:
                    fprintf (stderr, "Unknown bit instruction: %02x. %u instructions have been run.\n",
                             instruction, instruction_count);
                    return EXIT_FAILURE;
                }
                break;

            case 0xcd: /* CALL       */ memory_write (--z80_regs.sp, z80_regs.pc_h);
                                        memory_write (--z80_regs.sp, z80_regs.pc_l);
                                        z80_regs.pc = param_hl; break;
            case 0xc9: /* RET        */ z80_regs.pc_l = memory_read (z80_regs.sp++);
                                        z80_regs.pc_h = memory_read (z80_regs.sp++); break;
            case 0xcf: /* RST 08h    */ memory_write (--z80_regs.sp, z80_regs.pc_h);
                                        memory_write (--z80_regs.sp, z80_regs.pc_l);
                                        z80_regs.pc = 0x08; break;

            case 0xd3: /* OUT (*),A  */ io_write (param_l, z80_regs.a); break;
            case 0xd5: /* PUSH DE    */ memory_write (--z80_regs.sp, z80_regs.d);
                                        memory_write (--z80_regs.sp, z80_regs.e); break;
            case 0xd9: /* EXX        */ SWAP (uint16_t, z80_regs.bc, z80_regs.alt_bc);
                                        SWAP (uint16_t, z80_regs.de, z80_regs.alt_de);
                                        SWAP (uint16_t, z80_regs.hl, z80_regs.alt_hl); break;

            case 0xe2: /* JP PO      */ z80_regs.pc = (z80_regs.f & Z80_FLAG_PARITY) ?
                                        z80_regs.pc : param_hl; break;
            case 0xe3: /* EX (SP),HL */ { uint8_t temp;
                                        temp = z80_regs.l;
                                        z80_regs.l = memory_read (z80_regs.sp);
                                        memory_write (z80_regs.sp, temp);
                                        temp = z80_regs.h;
                                        z80_regs.h = memory_read (z80_regs.sp + 1);
                                        memory_write (z80_regs.sp + 1, temp);
                                        } break;
            case 0xed: /* Extended Instructions */

                instruction = memory_read (z80_regs.pc++);

                switch (z80_instruction_size_extended[instruction])
                {
                    case 3:
                        param_l = memory_read (z80_regs.pc++);
                        param_h = memory_read (z80_regs.pc++);
                        break;
                    case 2:
                        param_l = memory_read (z80_regs.pc++);
                        break;
                    default:
                        break;
                }

                switch (instruction)
                {
                    case 0x51: /* OUT (C),D */ io_write (z80_regs.c, z80_regs.d); break;
                    case 0x56: /* IM 1      */ interrupt_mode = 1; break;

                    case 0xb0: /* LDIR      */ { memory_write (z80_regs.de, memory_read (z80_regs.hl));
                                                 z80_regs.hl++; z80_regs.de++;
                                                 z80_regs.bc--;
                                                 z80_regs.pc -= z80_regs.bc ? 2 : 0; } break; /* TODO: Set flags */
                    case 0xb3: /* OTIR      */ { io_write (z80_regs.c, memory_read(z80_regs.hl)),
                                                 z80_regs.hl++; z80_regs.b--;
                                                 z80_regs.pc -= z80_regs.b ? 2 : 0; } break; /* TODO: Set flags */

                    default:
                    fprintf (stderr, "Unknown extended instruction: %02x. %u instructions have been run.\n",
                             instruction, instruction_count);
                    return EXIT_FAILURE;
                }
                break;

            case 0xf3: /* DI */ interrupt_enable = false; break;

            default:
                fprintf (stderr, "Unknown instruction: %02x. %u instructions have been run.\n",
                         instruction, instruction_count);
                return EXIT_FAILURE;
        }


        instruction_count++;

        if (instruction_count % 16000 == 0)
        {
            vdp_render ();
            SDL_RenderPresent (renderer);
            SDL_Delay (33);
        }
    }
}
