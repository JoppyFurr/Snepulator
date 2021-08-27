R"(
#version 330 core

#define VIDEO_SIDE_BORDER       8
#define VIDEO_BUFFER_WIDTH      (256 + (2 * VIDEO_SIDE_BORDER))
#define VIDEO_BUFFER_LINES      240

#define VIDEO_FILTER_NEAREST    0
#define VIDEO_FILTER_LINEAR     1
#define VIDEO_FILTER_SCANLINES  2
#define VIDEO_FILTER_DOT_MATRIX 3

#define OPTION_VIDEO_FILTER     (options.x)
#define OPTION_SHOW_BORDER      (options.y)
#define OPTION_BLANK_LEFT       (options.z)

#define BLACK                   vec4 (0.0, 0.0, 0.0, 1.0)

in vec4 gl_FragCoord;

out vec4 pixel;

uniform sampler2D video_out;
uniform ivec2 video_resolution;
uniform ivec2 video_start;
uniform ivec2 output_resolution;
uniform ivec3 options; /* x = video_filter, y = show_border, z = blank_left */
uniform int scale;


/*
 * Given a pixel coordinate into the video-out, return the colour value.
 * This  function handles the dimming of the border area.
 */
vec4 get_pixel (int x, int y)
{
    bool active_area;

    if (x >= video_start.x + OPTION_BLANK_LEFT && x < (video_start.x + video_resolution.x) &&
        y >= video_start.y                     && y < (video_start.y + video_resolution.y))
    {
        active_area = true;
    }
    else
    {
        active_area = false;
    }

    if (active_area)
    {
        return texelFetch (video_out, ivec2 (x, y), 0);
    }
    else if (bool (OPTION_SHOW_BORDER))
    {
        return mix (BLACK, texelFetch (video_out, ivec2 (x, y), 0), 0.5);
    }

    return BLACK;
}


/*
 * Given a line number, return the portion of light that is allowed
 * to pass through when the scanline filter is enabled.
 */
float scanline_mix (int line)
{
    int t; /* Thickness of the shading at the top and bottom of each scanline. */

    if (scale > 4)
    {
        t = scale / 4;
    }
    else
    {
        t = 1;
    }

    switch (scale)
    {
        /* Hard-coded values for lower resolutions. */
        case 1:
            return 0.75;
        case 2:
            return 1.0 - float (line) * 0.5;

        default:
            /* Ramp up, average brightness = 0.625 */
            if (line < t)
            {
                float position = (float (line) + 0.5) / float (t);
                float mix = 0.25 + position * 0.75;

                return smoothstep (0.0, 1.0, mix);
            }
            /* Constant, average brightness = 1.0 */
            if (line < (t + (scale - 2 * t)))
            {
                return 1.0;
            }
            /* Ramp down, average brightness = 0.625 */
            else
            {
                float position = (float (line - (scale - t)) + 0.5) / float (t);
                float mix = 1.0 - position * 0.75;

                return smoothstep (0.0, 1.0, mix);
            }
    }
}


/*
 * Entry point for fragment shader.
 */
void main()
{
    /* Screen location of this fragment. */
    int x = int (gl_FragCoord.x);
    int y = int (gl_FragCoord.y);

    /* First screen-pixel of the video_out texture area. */
    int start_x = output_resolution.x / 2 - (VIDEO_BUFFER_WIDTH * scale) / 2;
    int start_y = output_resolution.y / 2 - (VIDEO_BUFFER_LINES * scale) / 2;

    /* Index into the source texture. */
    /* TODO: Consider division behaviour when crossing the negative boundary */
    int video_x = (x - start_x) / scale;
    int video_y = (y - start_y) / scale;

    /* Counts how many screen-pixels we are into this video-pixel */
    int mod_x = ((output_resolution.x * scale) + (x - start_x)) % scale;
    int mod_y = ((output_resolution.y * scale) + (y - start_y)) % scale;

    /* For areas outside the video_out texture area, select the pixel a column
       inward from the edge of the texture. This makes it safe to sample the
       surrounding pixels. */
    if (video_x < 1)
    {
        video_x = 1;
    }
    else if (video_x > VIDEO_BUFFER_WIDTH - 2)
    {
        video_x = VIDEO_BUFFER_WIDTH - 2;
    }

    /* Top border. */
    if (video_y < 1)
    {
        video_y = 1;
    }
    /* Active area. */
    else if (video_y > VIDEO_BUFFER_LINES - 2)
    {
        video_y = VIDEO_BUFFER_LINES - 2;
    }

    float x_mix;
    float y_mix;

    switch (OPTION_VIDEO_FILTER)
    {
        case VIDEO_FILTER_LINEAR:
            x_mix = float (mod_x) / float (scale);
            y_mix = float (mod_y) / float (scale);

            /* Fetch the four pixels to interpolate between */
            vec4 pixel_tl = get_pixel (video_x,     video_y    );
            vec4 pixel_tr = get_pixel (video_x + 1, video_y    );
            vec4 pixel_bl = get_pixel (video_x,     video_y + 1);
            vec4 pixel_br = get_pixel (video_x + 1, video_y + 1);

            /* Interpolate */
            vec4 pixel_t = mix (pixel_tl, pixel_tr, x_mix);
            vec4 pixel_b = mix (pixel_bl, pixel_br, x_mix);
            pixel        = mix (pixel_t,  pixel_b,  y_mix);
            break;

        case VIDEO_FILTER_SCANLINES:

            /* Interpolate in the X axis */
            x_mix = smoothstep (0.0, 1.0, float (mod_x) / float (scale));
            vec4 pixel_l = get_pixel (video_x,     video_y);
            vec4 pixel_r = get_pixel (video_x + 1, video_y);
            pixel = mix (pixel_l, pixel_r, x_mix);

            /* Apply scanlines */
            pixel = mix (BLACK, pixel, scanline_mix (mod_y));

            break;

        case VIDEO_FILTER_DOT_MATRIX:
            if ((mod_x == 0) || (mod_y == 0))
            {
                pixel = BLACK;
            }
            else
            {
                pixel = get_pixel (video_x, video_y);
            }
            break;

        case VIDEO_FILTER_NEAREST:
        default:
            pixel = get_pixel (video_x, video_y);
            break;
    }
}
)"
