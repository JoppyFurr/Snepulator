R"(#version 330 core

#define VIDEO_SIDE_BORDER       8
#define VIDEO_BUFFER_WIDTH      272
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
uniform float scale;

layout (origin_upper_left, pixel_center_integer) in vec4 gl_FragCoord;

/*
 * Given a pixel coordinate into the video-out, return the colour value.
 * This function handles the dimming of the border area.
 */
vec4 get_pixel (int x, int y)
{
    x = clamp (x, 0, VIDEO_BUFFER_WIDTH - 1);
    y = clamp (y, 0, VIDEO_BUFFER_LINES - 1);

    /* Active area */
    if (x >= video_start.x + OPTION_BLANK_LEFT && x < (video_start.x + video_resolution.x) &&
        y >= video_start.y                     && y < (video_start.y + video_resolution.y))
    {
        return texelFetch (video_out, ivec2 (x, y), 0);
    }

    /* Backdrop is dimmed to 50% */
    else
    {
        return mix (BLACK, texelFetch (video_out, ivec2 (x, y), 0), 0.5);
    }
}


/*
 * Entry point for fragment shader.
 */
void main()
{
    /* Calculote the top-left screen-pixel that lands on the video_out texture area. */
    float start_x = floor ((float (output_resolution.x) / 2.0) - ((VIDEO_BUFFER_WIDTH / 2.0) * scale));
    float start_y = floor ((float (output_resolution.y) / 2.0) - ((VIDEO_BUFFER_LINES / 2.0) * scale));

    /* Calculate the location of the current pixel in video_out texture coordinates. */
    float video_x = (gl_FragCoord.x - start_x) / scale;
    float video_y = (gl_FragCoord.y - start_y) / scale;

    /* Fetch the four pixels to interpolate between */
    video_x -= 0.5;
    video_y -= 0.5;
    vec4 pixel_tl = get_pixel (int (video_x),     int (video_y)    );
    vec4 pixel_tr = get_pixel (int (video_x) + 1, int (video_y)    );
    vec4 pixel_bl = get_pixel (int (video_x),     int (video_y) + 1);
    vec4 pixel_br = get_pixel (int (video_x) + 1, int (video_y) + 1);

    /* Interpolate. Note that we do not convert to linear colour for the
       conversion, as that causes the image to appear more blurry than expected. */
    vec4 pixel_t = mix (pixel_tl, pixel_tr, fract (video_x));
    vec4 pixel_b = mix (pixel_bl, pixel_br, fract (video_x));
    pixel        = mix (pixel_t,  pixel_b,  fract (video_y));

}
)"
