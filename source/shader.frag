R"(
#version 330 core

#define VIDEO_SIDE_BORDER       8u
#define VIDEO_BUFFER_WIDTH      (256u + (2u * VIDEO_SIDE_BORDER))
#define VIDEO_BUFFER_LINES      240u

#define VIDEO_FILTER_NEAREST    0u
#define VIDEO_FILTER_LINEAR     1u
#define VIDEO_FILTER_SCANLINES  2u
#define VIDEO_FILTER_DOT_MATRIX 3u

in vec4 gl_FragCoord;

out vec4 pixel;

uniform sampler2D video_out;
uniform uvec2 input_resolution;
uniform uvec2 input_start;
uniform uvec2 output_resolution;
uniform uvec3 options; /* x = video_filter, y = has_border, z = blank_left */
uniform uint scale;

/* TODO: Confirm layout: Is y=0 the top or the bottom? */

void main()
{
    /* Index into the source texture. */
    uint video_x;
    uint video_y;

    /* Location on screen of this fragment. */
    uint x = uint (gl_FragCoord.x);
    uint y = uint (gl_FragCoord.y);

    /* "start" - First pixel off the active area. */
    uint start_x = output_resolution.x / 2u - (input_resolution.x * scale) / 2u;
    uint start_y = output_resolution.y / 2u - (input_resolution.y * scale) / 2u;

    /* "end" - First pixel of the right/bottom border. */
    uint end_x = start_x + input_resolution.x * scale;
    uint end_y = start_y + input_resolution.y * scale;

    bool border = false;

    /* Left border. */
    if (x < start_x)
    {
        video_x = 0u;
        border = true;
    }
    /* Active area. */
    else if (x < end_x)
    {
        video_x = input_start.x + (x - start_x) / scale;
    }
    else
    /* Right border. */
    {
        video_x = VIDEO_BUFFER_WIDTH - 1u;
        border = true;
    }

    /* Top border. */
    if (y < start_y)
    {
        video_y = 0u;
        video_x = 0u;
        border = true;
    }
    /* Active area. */
    else if (y < end_y)
    {
        video_y = input_start.y + (y - start_y) / scale;
    }
    else
    /* Bottom border. */
    {
        video_y = VIDEO_BUFFER_LINES - 1u;
        video_x = 0u;
        border = true;
    }

    /* Handle SMS left-blank. */
    if (video_x < (VIDEO_SIDE_BORDER + options.z))
    {
        border = true;
    }

    if (border)
    {
        pixel = mix (vec4 (0.0, 0.0, 0.0, 1.0),
                     texture (video_out, vec2 (float(video_x) / (VIDEO_BUFFER_WIDTH - 1u), float(video_y) / (VIDEO_BUFFER_LINES - 1u))),
                     0.5);
    }
    else
    {
        pixel = texture (video_out, vec2 (float(video_x) / (VIDEO_BUFFER_WIDTH - 1u), float(video_y) / (VIDEO_BUFFER_LINES - 1u)));
    }
}
)"
