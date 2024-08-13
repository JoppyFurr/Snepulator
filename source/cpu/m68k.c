/*
 * Snepulator
 * Motorola 68000 implementation
 */

#include <stdlib.h>

#include "../snepulator.h"
#include "m68k.h"


/*
 * Run the 68000 for the specified number of clock cycles.
 */
void m68k_run_cycles (M68000_Context *context, int64_t cycles)
{
    return;
}


/*
 * Create the 68000 context with power-on defaults.
 */
M68000_Context *m68k_init (void *parent,
                           uint16_t (* memory_read) (void *, uint16_t),
                           void     (* memory_write)(void *, uint16_t, uint16_t),
                           uint8_t  (* get_int)     (void *))
{
    M68000_Context *context;

    context = calloc (1, sizeof (M68000_Context));
    if (context == NULL)
    {
        snepulator_error ("Error", "Unable to allocate memory for M68000_Context");
        return NULL;
    }

    /* TODO: power-on defaults */

    context->parent       = parent;
    context->memory_read  = memory_read;
    context->memory_write = memory_write;
    context->get_int      = get_int;

    return context;
}
