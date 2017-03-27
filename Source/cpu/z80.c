#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "z80.h"
#include "z80_names.h"

#define SWAP(TYPE, X, Y) { TYPE tmp = X; X = Y; Y = tmp; }

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

void z80_reset ()
{
    memset (&z80_regs, 0, sizeof (Z80_Regs));
}

#define SET_FLAGS_LOGIC  { z80_regs.f = (uint8_even_parity[z80_regs.a] ? Z80_FLAG_PARITY : 0) | \
                                        (z80_regs.a == 0x00 ? Z80_FLAG_ZERO : 0             ) | \
                                        ((z80_regs.a & 0x80)  ? Z80_FLAG_SIGN : 0           ); }

/* TODO: OVERFLOW, HALF */
#define SET_FLAGS_ADD(X) { z80_regs.f = (((uint16_t)z80_regs.a + (uint16_t)X) & 0x100 ? Z80_FLAG_CARRY : 0) | \
                                        (z80_regs.a + X == 0x00 ? Z80_FLAG_ZERO  : 0                      ) | \
                                        ((z80_regs.a + X & 0x80)  ? Z80_FLAG_SIGN  : 0                    ); }

/* TODO: OVERFLOW, HALF */
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

/* TODO: HALF */
#define SET_FLAGS_ADD_16(Y,X) { z80_regs.f = (z80_regs.f & (Z80_FLAG_OVERFLOW | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) | \
                                             ((((uint32_t)Y + (uint32_t)X) & 0x10000) ? Z80_FLAG_CARRY : 0); }

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
            case 0x01: /* LD BC,**   */ z80_regs.bc = param_hl; break;
            case 0x02: /* LD (BC),A  */ memory_write (z80_regs.bc, z80_regs.a); break;
            case 0x03: /* INC BC     */ z80_regs.bc++; break;
            case 0x04: /* INC B      */ z80_regs.b++; SET_FLAGS_INC (z80_regs.b); break;
            case 0x05: /* DEC B      */ z80_regs.b--; SET_FLAGS_DEC (z80_regs.b); break;
            case 0x06: /* LD B,*     */ z80_regs.b = param_l; break;
            case 0x07: /* RLCA       */ z80_regs.a = (z80_regs.a << 1) & ((z80_regs.a & 0x80) ? 1 : 0);
                                        z80_regs.f = (z80_regs.f & (Z80_FLAG_PARITY | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) |
                                                     ((z80_regs.a & 0x01) ? Z80_FLAG_CARRY : 0);
                                              break;
            case 0x08: /* EX AF AF'  */ SWAP (uint16_t, z80_regs.af, z80_regs.alt_af); break;
            case 0x09: /* ADD HL,BC  */ SET_FLAGS_ADD_16 (z80_regs.hl, z80_regs.bc); z80_regs.hl += z80_regs.bc; break;
            case 0x0a: /* LD A,(BC)  */ z80_regs.a = memory_read (z80_regs.bc); break;
            case 0x0b: /* DEC BC     */ z80_regs.bc--; break;
            case 0x0c: /* INC C      */ z80_regs.c++; SET_FLAGS_INC (z80_regs.c); break;
            case 0x0d: /* DEC D      */ z80_regs.d--; SET_FLAGS_DEC (z80_regs.d); break;
            case 0x0e: /* LD C,*     */ z80_regs.c = param_l; break;

            case 0x10: /* DJNZ       */ z80_regs.b--; z80_regs.pc += z80_regs.b ? (int8_t) param_l : 0; break;
            case 0x11: /* LD DE,**   */ z80_regs.de = param_hl; break;
            case 0x13: /* INC DE     */ z80_regs.de++; break;
            case 0x16: /* LD D,*     */ z80_regs.d = param_l; break;
            case 0x17: /* RLA        */ { uint8_t temp = z80_regs.a;
                                        z80_regs.a = (z80_regs.a << 1) + ((z80_regs.f & Z80_FLAG_CARRY) ? 0x01 : 0);
                                        z80_regs.f = (z80_regs.f & (Z80_FLAG_PARITY | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) |
                                                     ((temp & 0x80) ? Z80_FLAG_CARRY : 0); } break;
            case 0x18: /* JR         */ z80_regs.pc += (int8_t)param_l; break;
            case 0x19: /* ADD HL,DE  */ SET_FLAGS_ADD_16 (z80_regs.hl, z80_regs.de); z80_regs.hl += z80_regs.de; break;
            case 0x1a: /* LD A,(DE)  */ z80_regs.a = memory_read (z80_regs.de); break;
            case 0x1b: /* DEC DE     */ z80_regs.de--; break;
            case 0x1f: /* RRA        */ { uint8_t temp = z80_regs.a;
                                        z80_regs.a = (z80_regs.a >> 1) + ((z80_regs.f & Z80_FLAG_CARRY) ? 0x80 : 0);
                                        z80_regs.f = (z80_regs.f & (Z80_FLAG_PARITY | Z80_FLAG_ZERO | Z80_FLAG_SIGN)) |
                                                     ((temp & 0x01) ? Z80_FLAG_CARRY : 0); } break;

            case 0x20: /* JR NZ      */ z80_regs.pc += (z80_regs.f & Z80_FLAG_ZERO) ? 0 : (int8_t)param_l; break;
            case 0x21: /* LD HL, **  */ z80_regs.hl = param_hl; break;
            case 0x23: /* INC HL     */ z80_regs.hl++; break;
            case 0x25: /* DEC H      */ z80_regs.h--; SET_FLAGS_DEC (z80_regs.h); break;
            case 0x28: /* JR Z       */ z80_regs.pc += (z80_regs.f & Z80_FLAG_ZERO) ? (int8_t)param_l : 0; break;
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
            case 0x38: /* JR C,*     */ z80_regs.pc += (z80_regs.f & Z80_FLAG_CARRY) ? (int8_t) param_l : 0; break;
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

            case 0x80: /* ADD A,B    */ SET_FLAGS_ADD (z80_regs.b); z80_regs.a += z80_regs.b; break;
            case 0x81: /* ADD A,C    */ SET_FLAGS_ADD (z80_regs.c); z80_regs.a += z80_regs.c; break;
            case 0x82: /* ADD A,D    */ SET_FLAGS_ADD (z80_regs.d); z80_regs.a += z80_regs.d; break;
            case 0x83: /* ADD A,E    */ SET_FLAGS_ADD (z80_regs.e); z80_regs.a += z80_regs.e; break;
            case 0x84: /* ADD A,H    */ SET_FLAGS_ADD (z80_regs.h); z80_regs.a += z80_regs.h; break;
            case 0x85: /* ADD A,L    */ SET_FLAGS_ADD (z80_regs.l); z80_regs.a += z80_regs.l; break;
            case 0x86: /* ADD A,(HL) */ { uint8_t temp = memory_read (z80_regs.hl);
                                          SET_FLAGS_ADD (temp); z80_regs.a += temp; } break;
            case 0x87: /* ADD A,A    */ SET_FLAGS_ADD (z80_regs.a); z80_regs.a += z80_regs.a; break;

            case 0x88: /* ADC A,B    */ SET_FLAGS_ADD ((z80_regs.b + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                        z80_regs.a +=   z80_regs.b + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
            case 0x89: /* ADC A,C    */ SET_FLAGS_ADD ((z80_regs.c + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)))
                                        z80_regs.a +=   z80_regs.c + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
            case 0x8a: /* ADC A,D    */ SET_FLAGS_ADD ((z80_regs.d + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                        z80_regs.a +=   z80_regs.d + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
            case 0x8b: /* ADC A,E    */ SET_FLAGS_ADD ((z80_regs.e + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                        z80_regs.a +=   z80_regs.e + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
            case 0x8c: /* ADC A,H    */ SET_FLAGS_ADD ((z80_regs.h + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                        z80_regs.a +=   z80_regs.h + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
            case 0x8d: /* ADC A,L    */ SET_FLAGS_ADD ((z80_regs.l + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                        z80_regs.a +=   z80_regs.l + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
            case 0x8e: /* ADC A,(HL) */ SET_FLAGS_ADD ((memory_read (z80_regs.hl) + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                        z80_regs.a +=   memory_read (z80_regs.hl) + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
            case 0x8f: /* ADC A,A    */ SET_FLAGS_ADD ((z80_regs.a + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                        z80_regs.a   += z80_regs.a + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;

            case 0x90: /* SUB A,B    */ SET_FLAGS_SUB (z80_regs.b); z80_regs.a -= z80_regs.b; break;
            case 0x91: /* SUB A,C    */ SET_FLAGS_SUB (z80_regs.c); z80_regs.a -= z80_regs.c; break;
            case 0x92: /* SUB A,D    */ SET_FLAGS_SUB (z80_regs.d); z80_regs.a -= z80_regs.d; break;
            case 0x93: /* SUB A,E    */ SET_FLAGS_SUB (z80_regs.e); z80_regs.a -= z80_regs.e; break;
            case 0x94: /* SUB A,H    */ SET_FLAGS_SUB (z80_regs.h); z80_regs.a -= z80_regs.h; break;
            case 0x95: /* SUB A,L    */ SET_FLAGS_SUB (z80_regs.l); z80_regs.a -= z80_regs.l; break;

            case 0x98: /* SBC A,B    */ SET_FLAGS_SUB ((z80_regs.b + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                        z80_regs.a -=   z80_regs.b + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
            case 0x99: /* SBC A,C    */ SET_FLAGS_SUB ((z80_regs.c + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                        z80_regs.a -=   z80_regs.c + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
            case 0x9a: /* SBC A,D    */ SET_FLAGS_SUB ((z80_regs.d + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                        z80_regs.a -=   z80_regs.d + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
            case 0x9b: /* SBC A,E    */ SET_FLAGS_SUB ((z80_regs.e + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                        z80_regs.a -=   z80_regs.e + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
            case 0x9c: /* SBC A,H    */ SET_FLAGS_SUB ((z80_regs.h + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                        z80_regs.a -=   z80_regs.h + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
            case 0x9d: /* SBC A,L    */ SET_FLAGS_SUB ((z80_regs.l + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                        z80_regs.a -=   z80_regs.l + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;
            case 0x9f: /* SBC A,A    */ SET_FLAGS_SUB ((z80_regs.a + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0)));
                                        z80_regs.a -=   z80_regs.a + ((z80_regs.f & Z80_FLAG_CARRY) ? 1 : 0); break;

            case 0xa0: /* AND A,B    */ z80_regs.a &= z80_regs.b; SET_FLAGS_LOGIC; break;
            case 0xa1: /* AND A,C    */ z80_regs.a &= z80_regs.c; SET_FLAGS_LOGIC; break;
            case 0xa2: /* AND A,D    */ z80_regs.a &= z80_regs.d; SET_FLAGS_LOGIC; break;
            case 0xa3: /* AND A,E    */ z80_regs.a &= z80_regs.e; SET_FLAGS_LOGIC; break;
            case 0xa4: /* AND A,H    */ z80_regs.a &= z80_regs.h; SET_FLAGS_LOGIC; break;
            case 0xa5: /* AND A,L    */ z80_regs.a &= z80_regs.l; SET_FLAGS_LOGIC; break;

            case 0xaf: /* XOR A,A    */ z80_regs.a ^= z80_regs.a; SET_FLAGS_LOGIC; break;

            case 0xb0: /* OR  A,B    */ z80_regs.a |= z80_regs.b; SET_FLAGS_LOGIC; break;
            case 0xb1: /* OR  A,C    */ z80_regs.a |= z80_regs.c; SET_FLAGS_LOGIC; break;
            case 0xb2: /* OR  A,D    */ z80_regs.a |= z80_regs.d; SET_FLAGS_LOGIC; break;
            case 0xb3: /* OR  A,E    */ z80_regs.a |= z80_regs.e; SET_FLAGS_LOGIC; break;
            case 0xb4: /* OR  A,H    */ z80_regs.a |= z80_regs.h; SET_FLAGS_LOGIC; break;
            case 0xb5: /* OR  A,L    */ z80_regs.a |= z80_regs.l; SET_FLAGS_LOGIC; break;
            case 0xb6: /* OR (HL)    */ z80_regs.a |= memory_read (z80_regs.hl); SET_FLAGS_LOGIC; break;
            case 0xb7: /* OR  A,A    */ z80_regs.a |= z80_regs.a; SET_FLAGS_LOGIC; break;

            case 0xb8: /* CP A,B     */ SET_FLAGS_SUB (z80_regs.b); break;
            case 0xb9: /* CP A,C     */ SET_FLAGS_SUB (z80_regs.c); break;
            case 0xba: /* CP A,D     */ SET_FLAGS_SUB (z80_regs.d); break;
            case 0xbb: /* CP A,E     */ SET_FLAGS_SUB (z80_regs.e); break;
            case 0xbc: /* CP A,H     */ SET_FLAGS_SUB (z80_regs.h); break;
            case 0xbd: /* CP A,L     */ SET_FLAGS_SUB (z80_regs.l); break;
            case 0xbe: /* CP A,(HL)  */ { uint8_t temp = memory_read (z80_regs.hl); SET_FLAGS_SUB (temp); } break;
            case 0xbf: /* CP A,A     */ SET_FLAGS_SUB (z80_regs.a); break;

            case 0xc0: /* RET NZ     */ if (!(z80_regs.f & Z80_FLAG_ZERO))
                                        {
                                            z80_regs.pc_l = memory_read (z80_regs.sp++);
                                            z80_regs.pc_h = memory_read (z80_regs.sp++);
                                        } break;
            case 0xc1: /* POP BC     */ z80_regs.c = memory_read (z80_regs.sp++);
                                        z80_regs.b = memory_read (z80_regs.sp++); break;
            case 0xc2: /* JP NZ,**   */ z80_regs.pc = (z80_regs.f & Z80_FLAG_ZERO) ? z80_regs.pc : param_hl; break;
            case 0xc3: /* JP **      */ z80_regs.pc = param_hl; break;
            case 0xc5: /* PUSH BC    */ memory_write (--z80_regs.sp, z80_regs.b);
                                        memory_write (--z80_regs.sp, z80_regs.c); break;
            case 0xc8: /* RET Z      */ if (z80_regs.f & Z80_FLAG_ZERO)
                                        {
                                            z80_regs.pc_l = memory_read (z80_regs.sp++);
                                            z80_regs.pc_h = memory_read (z80_regs.sp++);
                                        } break;
            case 0xc9: /* RET        */ z80_regs.pc_l = memory_read (z80_regs.sp++);
                                        z80_regs.pc_h = memory_read (z80_regs.sp++); break;
            case 0xca: /* JP Z,**    */ z80_regs.pc = (z80_regs.f & Z80_FLAG_ZERO) ? param_hl : z80_regs.pc; break;

            case 0xcb: /* Bit Instructions */

                instruction = memory_read (z80_regs.pc++);

                /* TODO: Perhaps a 256-entry switch statement is not the correct method for this */
                switch (instruction)
                {
                    case 0x79: /* BIT 7,C    */ z80_regs.f = (z80_regs.f & Z80_FLAG_CARRY) |
                                                          ((z80_regs.c & 0x80) ? 0 : Z80_FLAG_ZERO) |
                                                          (Z80_FLAG_HALF); break;
                    case 0x86: /* RES 0,(HL) */ memory_write (z80_regs.hl, memory_read (z80_regs.hl) & 0xfe); break;
                    case 0xb8: /* RES 7,B    */ z80_regs.b &= 0x7f; break;

                    default:
                    fprintf (stderr, "Unknown bit instruction: \"%s\" (%02x). %u instructions have been run.\n",
                             z80_instruction_name_bits[instruction], instruction, instruction_count);
                    return EXIT_FAILURE;
                }
                break;

            case 0xcd: /* CALL       */ memory_write (--z80_regs.sp, z80_regs.pc_h);
                                        memory_write (--z80_regs.sp, z80_regs.pc_l);
                                        z80_regs.pc = param_hl; break;
            case 0xcf: /* RST 08h    */ memory_write (--z80_regs.sp, z80_regs.pc_h);
                                        memory_write (--z80_regs.sp, z80_regs.pc_l);
                                        z80_regs.pc = 0x08; break;

            case 0xd0: /* RET NC     */ if (!(z80_regs.f & Z80_FLAG_CARRY))
                                        {
                                            z80_regs.pc_l = memory_read (z80_regs.sp++);
                                            z80_regs.pc_h = memory_read (z80_regs.sp++);
                                        } break;
            case 0xd1: /* POP DE     */ z80_regs.e = memory_read (z80_regs.sp++);
                                        z80_regs.d = memory_read (z80_regs.sp++); break;
            case 0xd3: /* OUT (*),A  */ io_write (param_l, z80_regs.a); break;
            case 0xd5: /* PUSH DE    */ memory_write (--z80_regs.sp, z80_regs.d);
                                        memory_write (--z80_regs.sp, z80_regs.e); break;
            case 0xd6: /* SUB A,*    */ SET_FLAGS_SUB (param_l); z80_regs.a -= param_l; break;
            case 0xd9: /* EXX        */ SWAP (uint16_t, z80_regs.bc, z80_regs.alt_bc);
                                        SWAP (uint16_t, z80_regs.de, z80_regs.alt_de);
                                        SWAP (uint16_t, z80_regs.hl, z80_regs.alt_hl); break;
            case 0xdb: /* IN (*),A   */ z80_regs.a = io_read (param_l); break;
            case 0xdd: /* IX         */
                instruction = memory_read (z80_regs.pc++);

                switch (z80_instruction_size_ix[instruction])
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
                    case 0xe5: /* PUSH IX    */ memory_write (--z80_regs.sp, z80_regs.ixh);
                                                memory_write (--z80_regs.sp, z80_regs.ixl); break;
                    default:
                    fprintf (stderr, "Unknown ix instruction: \"%s\" (%02x). %u instructions have been run.\n",
                             z80_instruction_name_ix[instruction], instruction, instruction_count);
                    return EXIT_FAILURE;
                }
                break;

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
            case 0xe5: /* PUSH HL    */ memory_write (--z80_regs.sp, z80_regs.h);
                                        memory_write (--z80_regs.sp, z80_regs.l); break;
            case 0xe6: /* AND A,*    */ z80_regs.a &= param_l; SET_FLAGS_LOGIC; break;
            case 0xe7: /* RST 20h    */ memory_write (--z80_regs.sp, z80_regs.pc_h);
                                        memory_write (--z80_regs.sp, z80_regs.pc_l);
                                        z80_regs.pc = 0x20; break;
            case 0xe9: /* JP (HL)    */ z80_regs.pc = param_hl; break;
            case 0xeb: /* EX DE,HL   */ SWAP (uint16_t, z80_regs.de, z80_regs.hl); break;
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
                    case 0x79: /* OUT (C),A */ io_write (z80_regs.c, z80_regs.a); break;
                    case 0xa3: /* OUTI      */ { io_write (z80_regs.c, memory_read(z80_regs.hl)),
                                                 z80_regs.hl++; z80_regs.b--;
                                                 z80_regs.f = (z80_regs.f & Z80_FLAG_CARRY) |
                                                              (Z80_FLAG_SUB) |
                                                              (z80_regs.b == 0 ? Z80_FLAG_ZERO : 0);
                                               } break;

                    case 0xb0: /* LDIR      */ { memory_write (z80_regs.de, memory_read (z80_regs.hl));
                                                 z80_regs.hl++; z80_regs.de++;
                                                 z80_regs.bc--;
                                                 z80_regs.pc -= z80_regs.bc ? 2 : 0;
                                                 z80_regs.f = (z80_regs.f & (Z80_FLAG_CARRY |
                                                                             Z80_FLAG_ZERO  |
                                                                             Z80_FLAG_SIGN)) |
                                                              (z80_regs.bc ? Z80_FLAG_OVERFLOW : 0);
                                               } break;
                    case 0xb3: /* OUTR      */ { io_write (z80_regs.c, memory_read(z80_regs.hl)),
                                                 z80_regs.hl++; z80_regs.b--;
                                                 z80_regs.pc -= z80_regs.b ? 2 : 0;
                                                 z80_regs.f = (z80_regs.f & Z80_FLAG_CARRY) |
                                                              (Z80_FLAG_SUB | Z80_FLAG_ZERO); } break;

                    default:
                    fprintf (stderr, "Unknown extended instruction: \"%s\" (%02x). %u instructions have been run.\n",
                             z80_instruction_name_extended[instruction], instruction, instruction_count);
                    return EXIT_FAILURE;
                }
                break;

            case 0xf1: /* POP AF     */ z80_regs.f = memory_read (z80_regs.sp++);
                                        z80_regs.a = memory_read (z80_regs.sp++); break;
            case 0xf3: /* DI         */ interrupt_enable = false; break;
            case 0xf5: /* PUSH AF    */ memory_write (--z80_regs.sp, z80_regs.a);
                                        memory_write (--z80_regs.sp, z80_regs.f); break;
            case 0xf6: /* OR  A,*    */ z80_regs.a |= param_l; SET_FLAGS_LOGIC; break;
            case 0xfb: /* EI         */ interrupt_enable = true; break;

            case 0xfd: /* IY         */
                instruction = memory_read (z80_regs.pc++);

                switch (z80_instruction_size_ix[instruction])
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
                    case 0xe5: /* PUSH IY    */ memory_write (--z80_regs.sp, z80_regs.iyh);
                                                memory_write (--z80_regs.sp, z80_regs.iyl); break;
                    default:
                    fprintf (stderr, "Unknown iy instruction: \"%s\" (%02x). %u instructions have been run.\n",
                             z80_instruction_name_iy[instruction], instruction, instruction_count);
                    return EXIT_FAILURE;
                }
                break;

            case 0xfe: /* CP A,*     */ SET_FLAGS_SUB (param_l); break;
            case 0xff: /* RST 38h    */ memory_write (--z80_regs.sp, z80_regs.pc_h);
                                        memory_write (--z80_regs.sp, z80_regs.pc_l);
                                        z80_regs.pc = 0x38; break;

            default:
                fprintf (stderr, "Unknown instruction: \"%s\" (%02x). %u instructions have been run.\n",
                         z80_instruction_name[instruction], instruction, instruction_count);
                return EXIT_FAILURE;
        }


        instruction_count++;

        if (instruction_count % 16000 == 0)
        {
            vdp_render ();
            SDL_RenderPresent (renderer);
            SDL_Delay (10);
        }

        /* DEBUG - Return for some debug */
        if (instruction_count == 8000000)
            return EXIT_FAILURE;
    }
}
