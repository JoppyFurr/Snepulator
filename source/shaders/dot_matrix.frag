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
uniform vec2 scale;

layout (origin_upper_left, pixel_center_integer) in vec4 gl_FragCoord;


/*
 * Given a pixel coordinate into the video-out, return the colour value.
 * This function handles the dimming of the border area.
 *
 * We fake a resolution that is int(scale) times bigger than the true
 * resolution, with the grid lines applied.
 */
vec4 get_pixel (int x, int y)
{
    if (mod (x, int(scale.x)) == 0 ||
        mod (y, int(scale.y)) == 0)
    {
        return BLACK;
    }

    x /= int(scale.x);
    y /= int(scale.y);

    x = clamp (x, 0, VIDEO_BUFFER_WIDTH - 1);
    y = clamp (y, 0, VIDEO_BUFFER_LINES - 1);

    /* Active area */
    if (x >= video_start.x + OPTION_BLANK_LEFT && x < (video_start.x + video_resolution.x) &&
        y >= video_start.y                     && y < (video_start.y + video_resolution.y))
    {
        return texelFetch (video_out, ivec2 (x, y), 0);
    }

    return BLACK;
}


/*
 * Convert from sRGB to Linear
 */
vec4 to_linear (vec4 colour)
{
    bvec3 cutoff = lessThan (colour.rgb, vec3 (0.04045));

    /* Conversion calculation for sRGB values greater than 0.04045 */
    vec3 greater = pow ((colour.rgb + vec3 (0.055)) / vec3 (1.055), vec3(2.4));

    /* Conversion calculation for sRGB values less that 0.04045 */
    vec3 less = colour.rgb / vec3 (12.92);

    /* Use mix to select between the two */
    return vec4 (mix (greater, less, cutoff), colour.a);
}


/*
 * Convert from Linear to sRGB
 */
vec4 from_linear (vec4 colour)
{
    bvec3 cutoff = lessThan(colour.rgb, vec3(0.0031308));

    /* Conversion calculation for linear values greater than 0.0031308 */
    vec3 greater = vec3 (1.055) * pow (colour.rgb, vec3 (1.0/2.4)) - vec3 (0.055);

    /* Conversion calculation for linear values less than than 0.0031308 */
    vec3 less = colour.rgb * vec3 (12.92);

    /* Use mix to select between the two */
    return vec4 (mix (greater, less, cutoff), colour.a);
}


/*
 * Entry point for fragment shader.
 */
void main()
{
    /* Goal: Look like the following : 1. Integer scale
     *                                 2. Draw lines (configurable?)
     *                                 3, linear to finish
     *                                  , prefer none instead of black for 1x? */

    /* Calculote the top-left screen-pixel that lands on the video_out texture area. */
    float start_x = floor ((float (output_resolution.x) / 2.0) - ((VIDEO_BUFFER_WIDTH / 2.0) * scale.x));
    float start_y = floor ((float (output_resolution.y) / 2.0) - ((VIDEO_BUFFER_LINES / 2.0) * scale.y));

    /* Calculate the location of the current pixel in video_out texture coordinates.
     * This location is calculated as if video_out were already integer scaled.
     * For non-integer scales, linear interpolation is used to cover the final bit of stretching. */
    float video_x = (gl_FragCoord.x - start_x) * floor (scale.x) / scale.x;
    float video_y = (gl_FragCoord.y - start_y) * floor (scale.y) / scale.y;

    /* Fetch the four pixels to interpolate between */
    vec4 pixel_tl = get_pixel (int (video_x    ), int (video_y    ));
    vec4 pixel_tr = get_pixel (int (video_x + 1), int (video_y    ));
    vec4 pixel_bl = get_pixel (int (video_x    ), int (video_y + 1));
    vec4 pixel_br = get_pixel (int (video_x + 1), int (video_y + 1));

    /* Interpolate.
     * Conversion to linear colour is needed to keep the darkness of the grid lines consistent.
     * If scale is an integer, then fract() should be zero for hard pixel edges. */
    vec4 pixel_t = mix (to_linear (pixel_tl), to_linear (pixel_tr), fract (video_x));
    vec4 pixel_b = mix (to_linear (pixel_bl), to_linear (pixel_br), fract (video_x));
    pixel        = from_linear (mix (pixel_t,  pixel_b,  fract (video_y)));

    /* TODO: Consider something closer to the scanlines shader. While the single-pixel grid separation
             looks decent on phones, it's never looked quite right on computer monitors.
             Perhaps a thicker but lighter grid would give a better effect. */
}
)"
