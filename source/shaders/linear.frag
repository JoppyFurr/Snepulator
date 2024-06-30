R"(#version 330 core

#define OPTION_UNUSED_X         (options.x)
#define OPTION_UNUSED_Y         (options.y)
#define OPTION_BLANK_LEFT       (options.z)

layout (origin_upper_left, pixel_center_integer) in vec4 gl_FragCoord;

const vec2 buffer_size = vec2 (272, 240);
const vec4 black = vec4 (0.0, 0.0, 0.0, 1.0);

uniform sampler2D video_out;
uniform ivec2 video_resolution;
uniform ivec2 video_start;
uniform vec2 output_resolution;
uniform ivec3 options; /* x = unused, y = unused, z = blank_left */
uniform vec2 scale;

out vec4 pixel;


/*
 * Given a pixel coordinate into the video-out, return the colour value.
 * This function handles the dimming of the border area.
 */
vec4 get_pixel (vec2 position)
{
    position = clamp (position, ivec2 (0, 0), buffer_size - 1);

    /* Active area */
    if (position.x >= video_start.x + OPTION_BLANK_LEFT && position.x < (video_start.x + video_resolution.x) &&
        position.y >= video_start.y                     && position.y < (video_start.y + video_resolution.y))
    {
        return texelFetch (video_out, ivec2 (position), 0);
    }

    /* Backdrop is dimmed to 50% */
    else
    {
        return mix (black, texelFetch (video_out, ivec2 (position), 0), 0.5);
    }
}


/*
 * Entry point for fragment shader.
 */
void main()
{
    /* Calculate the top-left screen-pixel that lands on the video_out texture area. */
    vec2 start = floor ((output_resolution / 2.0) - (buffer_size * scale / 2.0));

    /* Calculate the location of the current pixel in video_out texture coordinates. */
    vec2 texture_position = (gl_FragCoord.xy - start) / scale;

    /* Fetch the four pixels to interpolate between */
    texture_position -= vec2 (0.5, 0.5);
    vec4 pixel_tl = get_pixel (texture_position              );
    vec4 pixel_tr = get_pixel (texture_position + vec2 (1, 0));
    vec4 pixel_bl = get_pixel (texture_position + vec2 (0, 1));
    vec4 pixel_br = get_pixel (texture_position + vec2 (1, 1));

    /* Interpolate. Note that we do not convert to linear colour for the
       conversion, as that causes the image to appear more blurry than expected. */
    vec4 pixel_t = mix (pixel_tl, pixel_tr, fract (texture_position.x));
    vec4 pixel_b = mix (pixel_bl, pixel_br, fract (texture_position.x));
    pixel        = mix (pixel_t,  pixel_b,  fract (texture_position.y));

}
)"
