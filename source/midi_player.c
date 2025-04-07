/*
 * Snepulator
 * MIDI Player implementation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snepulator.h"
#include "util.h"

#include "sound/band_limit.h"
#include "sound/sn76489.h"
#include "sound/ym2413.h"
#include "vgm_player.h"
#include "midi_player.h"

extern Snepulator_State state;


/*
 * Callback to supply audio frames.
 */
static void midi_player_audio_callback (void *context_ptr, int16_t *stream, uint32_t count)
{
    MIDI_Player_Context *context = (MIDI_Player_Context *) context_ptr;

    if (context->ym2413_clock)
    {
        ym2413_get_samples (context->ym2413_context, stream, count);
    }
}


/*
 * Clean up structures and free memory.
 */
static void midi_player_cleanup (void *context_ptr)
{
    MIDI_Player_Context *context = (MIDI_Player_Context *) context_ptr;

    if (context->ym2413_context != NULL)
    {
        free (context->ym2413_context);
        context->ym2413_context = NULL;
    }

    if (context->midi != NULL)
    {
        free (context->midi);
        context->midi = NULL;
    }
}


/*
 * Read a 32-bit big-endian value from the MIDI file.
 */
static uint32_t midi_read_32 (MIDI_Player_Context *context)
{
    uint32_t value = util_ntoh32 (* (uint32_t *) &context->midi [context->index]);
    context->index += 4;
    return value;
}


/*
 * Read a 16-bit big-endian value from the MIDI file.
 */
static uint16_t midi_read_16 (MIDI_Player_Context *context)
{
    uint16_t value = util_ntoh16 (* (uint16_t *) &context->midi [context->index]);
    context->index += 2;
    return value;
}


/*
 * Initialize the MIDI Player
 */
MIDI_Player_Context *midi_player_init (void)
{
    MIDI_Player_Context *context;
    YM2413_Context *ym2413_context;

    /* Check we've been passed a filename */
    if (state.cart_filename == NULL)
    {
        snepulator_error ("Error", "No file");
        return NULL;
    }

    context = calloc (1, sizeof (MIDI_Player_Context));
    if (context == NULL)
    {
        snepulator_error ("Error", "Unable to allocate memory for MIDI_Player_Context");
        return NULL;
    }

    /* Load MIDI file */
    if (util_load_file (&context->midi, &context->midi_size, state.cart_filename) == -1)
    {
        snepulator_error ("Error", "Unable to load MIDI file");
        midi_player_cleanup (context);
        free (context);
        return NULL;
    }


    /* Read header */
    if (memcmp (context->midi, "MThd", 4) != 0)
    {
        snepulator_error ("MIDI Error", "Not a MIDI file");
        midi_player_cleanup (context);
        free (context);
        return NULL;
    }
    context->index += 4;

    uint32_t header_length = midi_read_32 (context);
    if (header_length < 6)
    {
        snepulator_error ("MIDI Error", "Invalid HThd length %d.", header_length);
        midi_player_cleanup (context);
        free (context);
        return NULL;
    }

    context->format = midi_read_16 (context);
    context->n_tracks = midi_read_16 (context);
    context->tick_div = midi_read_16 (context);

    /* Skip any additional header fields */
    if (header_length > 6)
    {
        context->index += header_length - 6;
    }

    fprintf (stdout, "%d KiB MIDI %s loaded.\n", context->midi_size >> 10, state.cart_filename);
    /* DEBUG */
    fprintf (stdout, "format = %d, n_tracks = %d, tick_div=0x%04x\n", context->format, context->n_tracks, context->tick_div);

    /* Initialise sound chip */
    ym2413_context = ym2413_init ();
    context->ym2413_context = ym2413_context;

    /* Initial video parameters */
    state.video_start_x = VIDEO_SIDE_BORDER;
    state.video_start_y = (VIDEO_BUFFER_LINES - 192) / 2;
    state.video_width   = 256;
    state.video_height  = 192;

    /* Hook up callbacks */
    state.audio_callback = midi_player_audio_callback;
    state.cleanup = midi_player_cleanup;
    state.run_callback = NULL; /* TODO */

#if 0
    /* Start playing */
    state.clock_rate = /* TODO */;
    state.run = RUN_STATE_RUNNING;

    return context;
#else
    /* Not ready yet */
    free (context);
    return NULL;
#endif
}
