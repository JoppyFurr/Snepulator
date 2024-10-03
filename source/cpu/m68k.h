/*
 * Snepulator
 * Motorola 68000 header
 */

/* Structs */
typedef struct M68000_State_s {

    /* TODO: The last address register, a[7] is used as the stack pointer.
     *       There are actually two of it, one for the supervisor and one
     *       for the user. If we switch between the two modes, we'll need
     *       to swap the a[7] values. */
    uint32_reg d[8];
    uint32_t a[8];

    /* TODO: For now, we assume supervisor mode, so keep usp separate. */
    uint32_t usp;

    union {
        struct {
            uint32_t pc;
        };
        struct {
            uint16_t pc_l;
            uint16_t pc_h;
        };
    };

    union {
        uint16_t sr;
        struct {
            uint8_t ccr_carry:1;
            uint8_t ccr_overflow:1;
            uint8_t ccr_zero:1;
            uint8_t ccr_negative:1;
            uint8_t ccr_extend:1;
        };
    };


} M68000_State;

typedef struct M68000_Context_s {

    void *parent;
    M68000_State state;
    int32_t clock_cycles; /* Outstanding clock cycles to run. */

    /* Connections to the rest of the system */
    /* TODO: 8-bit read / write support with UDS/LDS */
    uint16_t (* memory_read_16)  (void *, uint32_t);
    void     (* memory_write_16) (void *, uint32_t, uint16_t);
    uint8_t  (* memory_read_8)   (void *, uint32_t);
    void     (* memory_write_8)  (void *, uint32_t, uint8_t);
    uint8_t  (* get_int)         (void *);

} M68000_Context;

/* Run the 68000 for the specified number of clock cycles. */
void m68k_run_cycles (M68000_Context *context, int64_t cycles);

/* Operations performed when taking the chip out of reset. */
void m68k_reset (M68000_Context *context);

/* Create the 68000 context with power-on defaults. */
M68000_Context *m68k_init (void *parent,
                           uint16_t (* memory_read_16)  (void *, uint32_t),
                           void     (* memory_write_16) (void *, uint32_t, uint16_t),
                           uint8_t  (* memory_read_8)   (void *, uint32_t),
                           void     (* memory_write_8)  (void *, uint32_t, uint8_t),
                           uint8_t  (* get_int)         (void *));
