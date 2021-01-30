/*
 * Video filters implementation.
 *
 * Input:  state.video_out_data
 * Output: state.video_out_texture_width
 *         state.video_out_texture_height
 *         state.video_out_texture_data
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "../util.h"
#include "../snepulator.h"

extern Snepulator_State state;


/*
 * Dim the non-active part of the video_out_texture data.
 */
void video_filter_dim_border (void)
{
    uint32_t x_scale = state.video_out_texture_width  / VIDEO_BUFFER_WIDTH;
    uint32_t y_scale = state.video_out_texture_height / VIDEO_BUFFER_LINES;
    uint32_t stride  = state.video_out_texture_width;

    for (uint32_t y = 0; y < state.video_out_texture_height; y++)
    {
        for (uint32_t x = 0; x < state.video_out_texture_width; x++)
        {
            if ( ( (y / y_scale) <  state.video_start_y) ||                                   /* Top border */
                 ( (y / y_scale) >= state.video_start_y + state.video_height) ||              /* Bottom border */
                 ( (x / x_scale) <  state.video_start_x + state.video_border_left_extend ) || /* Left border */
                 ( (x / x_scale) >= state.video_start_x + state.video_width ))                /* Right border */
            {
                state.video_out_texture_data [x + y * stride].r *= 0.5;
                state.video_out_texture_data [x + y * stride].g *= 0.5;
                state.video_out_texture_data [x + y * stride].b *= 0.5;
            }
        }
    }
}


/*
 * Scanline effect.
 *
 * Scales texture by 2×3 and reduces brightness on every third line.
 */
void video_filter_scanlines (void)
{
    float_Colour *source = state.video_out_data;
    float_Colour *dest   = state.video_out_texture_data;

    /* Prescale by 2x3, then add scanlines */
    state.video_out_texture_width = VIDEO_BUFFER_WIDTH * 2;
    state.video_out_texture_height = VIDEO_BUFFER_LINES * 3;
    uint32_t stride = state.video_out_texture_width;

    for (int y = 0; y < state.video_out_texture_height; y++)
    {
        for (int x = 0; x < state.video_out_texture_width; x++)
        {
            /* 1:1 copy of original data */
            dest [x + y * stride] = source [x / 2 + y / 3 * VIDEO_BUFFER_WIDTH];

            /* Every third line is darkened, taking colour from
             * both the line above and the line below. */
            if (y % 3 == 2)
            {
                /* TODO: Improve math to handle non-linear colour blending */
                /* TODO: Operations to work with float_Colour? */
                /* TODO: (y + 1) will be from the previous frame… */
                if (y != state.video_out_texture_height - 1)
                {
                    dest [x + y * stride].r = dest [x + (y + 0) * stride].r * 0.5 +
                                              dest [x + (y + 1) * stride].r * 0.5;
                    dest [x + y * stride].g = dest [x + (y + 0) * stride].g * 0.5 +
                                              dest [x + (y + 1) * stride].g * 0.5;
                    dest [x + y * stride].b = dest [x + (y + 0) * stride].b * 0.5 +
                                              dest [x + (y + 1) * stride].b * 0.5;
                }

                /* To dim the lines, reduce their value by 40% */
                dest [x + y * VIDEO_BUFFER_WIDTH * 2].r *= (1.0 - 0.4);
                dest [x + y * VIDEO_BUFFER_WIDTH * 2].g *= (1.0 - 0.4);
                dest [x + y * VIDEO_BUFFER_WIDTH * 2].b *= (1.0 - 0.4);
            }
        }
    }

    /* Dim the area outside of the active area */
    if (state.video_has_border)
    {
        video_filter_dim_border ();
    }
}
