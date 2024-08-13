/*
 * Snepulator
 * Motorola 68000 header
 */

/* Structs */
typedef struct M68000_State_s {

    uint16_t placeholder;

} M68000_State;

typedef struct M68000_Context_s {

    void *parent;
    M68000_State state;

    /* Connections to the rest of the system */
    /* TODO: 8-bit read / write support with UDS/LDS */
    uint16_t (* memory_read)  (void *, uint16_t);
    void     (* memory_write) (void *, uint16_t, uint16_t);
    uint8_t  (* get_int)      (void *);

} M68000_Context;

/* Run the 68000 for the specified number of clock cycles. */
void m68k_run_cycles (M68000_Context *context, int64_t cycles);

/* Create the 68000 context with power-on defaults. */
M68000_Context *m68k_init (void *parent,
                           uint16_t (* memory_read) (void *, uint16_t),
                           void     (* memory_write)(void *, uint16_t, uint16_t),
                           uint8_t  (* get_int)     (void *));
