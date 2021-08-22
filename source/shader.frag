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

    /* TODO: Increased buffer size to handle higher-resolution games. */

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

    switch (OPTION_VIDEO_FILTER)
    {
        case VIDEO_FILTER_LINEAR:

            float x_mix = float (mod_x) / float (scale);
            float y_mix = float (mod_y) / float (scale);

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
            if (mod_y == 0)
            {
                pixel = vec4 (0.0, 0.0, 0.0, 1.0);
            }
            else
            {
                pixel = get_pixel (video_x, video_y);
            }
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
