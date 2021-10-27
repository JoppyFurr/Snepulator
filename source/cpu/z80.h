/*
 * Z80 API
 */

/* Structs */
typedef struct Z80_State_s {

    /* Main Register Set */
    union {
        struct {
            uint16_t af;
            uint16_t bc;
            uint16_t de;
            uint16_t hl;
        };
        struct {
            uint8_t f;
            uint8_t a;
            uint8_t c;
            uint8_t b;
            uint8_t e;
            uint8_t d;
            uint8_t l;
            uint8_t h;
        };
        struct {
            uint8_t flag_carry:1;
            uint8_t flag_sub:1;
            uint8_t flag_parity_overflow:1;
            uint8_t flag_x:1;
            uint8_t flag_half:1;
            uint8_t flag_y:1;
            uint8_t flag_zero:1;
            uint8_t flag_sign:1;
        };
    };

    /* Alternate Register Set */
    union {
        struct {
            uint16_t af_alt;
            uint16_t bc_alt;
            uint16_t de_alt;
            uint16_t hl_alt;
        };
        struct {
            uint8_t f_alt;
            uint8_t a_alt;
            uint8_t c_alt;
            uint8_t b_alt;
            uint8_t e_alt;
            uint8_t d_alt;
            uint8_t l_alt;
            uint8_t h_alt;
        };
    };

    /* Special Purpose Registers */
    union {
        struct {
            uint16_t ir;
            uint16_t ix;
            uint16_t iy;
            uint16_t sp;
            uint16_t pc;
        };
        struct {
            uint8_t i;
            uint8_t r;
            uint8_t ix_l;
            uint8_t ix_h;
            uint8_t iy_l;
            uint8_t iy_h;
            uint8_t sp_l;
            uint8_t sp_h;
            uint8_t pc_l;
            uint8_t pc_h;
        };
    };

    /* Interrupts */
    uint8_t im;
    uint8_t iff1;
    uint8_t iff2;
    uint8_t wait_after_ei;
    uint8_t halt;

    /* Left-over cycles */
    uint32_t excess_cycles;

} Z80_State;

typedef struct Z80_Context_s {

    void *parent;
    Z80_State state;
    uint64_t cycle_count; /* Cycle counter since power-on */
    uint64_t used_cycles; /* Cycles used by the current instruction */

    /* Connections to the rest of the system */
    uint8_t (* memory_read)  (void *, uint16_t);
    void    (* memory_write) (void *, uint16_t, uint8_t);
    uint8_t (* io_read)      (void *, uint8_t);
    void    (* io_write)     (void *, uint8_t, uint8_t);
    bool    (* get_int)      (void *);
    bool    (* get_nmi)      (void *);

} Z80_Context;

/* Z80 FLAGS */
#define Z80_FLAG_CARRY      BIT_0
#define Z80_FLAG_SUB        BIT_1
#define Z80_FLAG_PARITY     BIT_2
#define Z80_FLAG_OVERFLOW   BIT_2
#define Z80_FLAG_X          BIT_3
#define Z80_FLAG_HALF       BIT_4
#define Z80_FLAG_Y          BIT_5
#define Z80_FLAG_ZERO       BIT_6
#define Z80_FLAG_SIGN       BIT_7
#define Z80_FLAG_NONE       0x00

/* Create the Z80 context with power-on defaults. */
Z80_Context *z80_init (void *parent,
                       uint8_t (* memory_read) (void *, uint16_t),
                       void    (* memory_write)(void *, uint16_t, uint8_t),
                       uint8_t (* io_read)     (void *, uint8_t),
                       void    (* io_write)    (void *, uint8_t, uint8_t),
                       bool    (* get_int)     (void *),
                       bool    (* get_nmi)     (void *));

/* Simulate the Z80 for the specified number of clock cycles. */
void z80_run_cycles (Z80_Context *context, uint64_t cycles);

/* Export Z80 state. */
void z80_state_save (Z80_Context *context);

/* Import Z80 state. */
void z80_state_load (Z80_Context *context, uint32_t version, uint32_t size, void *data);
