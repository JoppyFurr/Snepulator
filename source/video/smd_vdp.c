/*
 * Snepulator
 * Sega Mega Drive VDP implementation.
 */

#include <stdlib.h>

#include "../snepulator.h"
#include "smd_vdp.h"

/*
 * Read the VDP status register.
 */
uint16_t smd_vdp_status_read (SMD_VDP_Context *context)
{
    /* TODO: Bit 0 for PAL */
    return 0x0000;
}


/*
 * Run one scanline on the VDP.
 */
void smd_vdp_run_one_scanline (SMD_VDP_Context *context)
{
    return;
}


/*
 * Create an SMD VDP context with power-on defaults.
 */
SMD_VDP_Context *smd_vdp_init (void *parent, void (* frame_done) (void *))
{
    SMD_VDP_Context *context;

    context = calloc (1, sizeof (SMD_VDP_Context));
    if (context == NULL)
    {
        snepulator_error ("Error", "Unable to allocate memory for SMD_VDP_Context");
        return NULL;
    }

    context->parent = parent;
    context->video_width = 256;
    context->video_height = 224;

    context->frame_done = frame_done;

    return context;
}
