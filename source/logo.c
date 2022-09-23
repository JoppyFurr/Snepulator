
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "snepulator_types.h"
#include "snepulator.h"
#include "util.h"

#include "../images/snepulator_logo.c"
#include "logo.h"
#include <stdio.h> /* debug */

extern Snepulator_State state;
extern pthread_mutex_t video_mutex;


/*
 * Draw a frame of the animated logo.
 * For now we only animate a fade-in, then stop outputting new frames.
 */
static void logo_draw_frame (Logo_Context *context)
{
    uint32_t x_offset = VIDEO_BUFFER_WIDTH / 2 - snepulator_logo.width / 2;
    uint32_t y_offset = VIDEO_BUFFER_LINES / 2 - snepulator_logo.height / 2;

    if (context->frame == 0)
    {
        memset (context->frame_buffer, 0, sizeof (context->frame_buffer));
    }

    /* For the first 100 frames, fade the logo in from black. */
    if (context->frame < 100)
    {
        double brightness = (context->frame + 1.0) / 100.0;

        for (uint32_t y = 0; y < snepulator_logo.height; y++)
        {
            for (uint32_t x = 0; x < snepulator_logo.width; x++)
            {
                context->frame_buffer [(x + x_offset) + (y + y_offset) * VIDEO_BUFFER_WIDTH].r =
                    brightness * snepulator_logo.pixel_data [(x + y * snepulator_logo.width) * 3 + 0];
                context->frame_buffer [(x + x_offset) + (y + y_offset) * VIDEO_BUFFER_WIDTH].g =
                    brightness * snepulator_logo.pixel_data [(x + y * snepulator_logo.width) * 3 + 1];
                context->frame_buffer [(x + x_offset) + (y + y_offset) * VIDEO_BUFFER_WIDTH].b =
                    brightness * snepulator_logo.pixel_data [(x + y * snepulator_logo.width) * 3 + 2];
            }
        }

        pthread_mutex_lock (&video_mutex);
        memcpy (state.video_out_data, context->frame_buffer, sizeof (context->frame_buffer));
        pthread_mutex_unlock (&video_mutex);

        context->frame++;
    }
}


/*
 * Run the logo for the specified length of time.
 */
static void logo_run (void *context_ptr, uint32_t ms)
{
    Logo_Context *context = (Logo_Context *) context_ptr;
    static uint32_t usecs = 0;

    usecs += ms * 1000;

    if (usecs > 16666)
    {
        usecs -= 16666;
        logo_draw_frame (context);
    }
}


/*
 * Initialize the logo animation.
 */
Logo_Context *logo_init (void)
{
    Logo_Context *context;

    context = calloc (1, sizeof (Logo_Context));
    if (context == NULL)
    {
        snepulator_error ("Error", "Unable to allocate memory for Logo_Context");
        return NULL;
    }

    /* Video parameters */
    state.video_start_x = VIDEO_SIDE_BORDER;
    state.video_start_y = VIDEO_TOP_BORDER_192;
    state.video_width   = 256;
    state.video_height  = 192;

    /* Hook up callbacks */
    state.run_callback = logo_run;

    /* Begin animation */
    // state.run = RUN_STATE_RUNNING;

    return context;
}
