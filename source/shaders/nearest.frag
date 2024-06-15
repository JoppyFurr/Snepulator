R"(#version 330 core

#define VIDEO_SIDE_BORDER       8
#define VIDEO_BUFFER_WIDTH      (256 + (2 * VIDEO_SIDE_BORDER))
#define VIDEO_BUFFER_LINES      240

#define OPTION_UNUSED_X         (options.x)
#define OPTION_UNUSED_Y         (options.y)
#define OPTION_BLANK_LEFT       (options.z)

#define BLACK                   vec4 (0.0, 0.0, 0.0, 1.0)

out vec4 pixel;

uniform sampler2D video_out;
uniform ivec2 video_resolution;
uniform ivec2 video_start;
uniform ivec2 output_resolution;
uniform ivec3 options; /* x = unused, y = unused, z = blank_left */
uniform int scale;


/*
 * Given a pixel coordinate into the video-out, return the colour value.
 * This function handles the dimming of the border area.
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

    /* Dim the border area */
    return mix (BLACK, texelFetch (video_out, ivec2 (x, y), 0), 0.5);
}


/*
 * Entry point for fragment shader.
 */
void main()
{
    /* Screen location of this fragment. */
    int x = int (gl_FragCoord.x);
    int y = (output_resolution.y - 1) - int (gl_FragCoord.y);

    /* First screen-pixel of the video_out texture area. */
    int start_x = output_resolution.x / 2 - (VIDEO_BUFFER_WIDTH * scale) / 2;
    int start_y = output_resolution.y / 2 - (VIDEO_BUFFER_LINES * scale) / 2;

    /* Index into the source texture. */
    /* TODO: Consider division behaviour when crossing the negative boundary */
    int video_x = (x - start_x) / scale;
    int video_y = (y - start_y) / scale;

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

    /* Extend top border. */
    if (video_y < 1)
    {
        video_y = 1;
    }

    /* Extend bottom border. */
    else if (video_y > VIDEO_BUFFER_LINES - 2)
    {
        video_y = VIDEO_BUFFER_LINES - 2;
    }

    pixel = get_pixel (video_x, video_y);
}
)"
