R"(#version 330 core

layout (origin_upper_left, pixel_center_integer) in vec4 gl_FragCoord;

const vec2 buffer_size = vec2 (256, 240);
const vec4 black = vec4 (0.0, 0.0, 0.0, 1.0);

uniform sampler2D active_area;
uniform sampler2D backdrop;
uniform ivec2 video_resolution;
uniform ivec2 video_start;
uniform vec2 output_resolution;
uniform vec2 scale;

out vec4 pixel;


/*
 * Given a pixel coordinate into the video-out, return the colour value.
 * This function handles the dimming of the border area.
 *
 * We fake a resolution that is int(scale) times bigger than the true
 * resolution, with the grid lines applied.
 */
vec4 get_pixel (vec2 position)
{
    if (mod (floor (position.x), floor(scale.x)) == 0 ||
        mod (floor (position.y), floor(scale.y)) == 0)
    {
        return black;
    }

    position /= floor (scale);

    /* Active area */
    if (position.x >= video_start.x && position.x < (video_start.x + video_resolution.x) &&
        position.y >= video_start.y && position.y < (video_start.y + video_resolution.y))
    {
        return texelFetch (active_area, ivec2 (position), 0);
    }

    return black;
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

    /* Calculate the top-left screen-pixel that lands on the video_out texture area. */
    vec2 start = floor ((output_resolution / 2.0) - (video_resolution * scale / 2.0) - video_start * scale);

    /* Calculate the location of the current pixel in video_out texture coordinates.
     * This location is calculated as if video_out were already integer scaled.
     * For non-integer scales, linear interpolation is used to cover the final bit of stretching. */
    vec2 texture_position = (gl_FragCoord.xy - start) * floor (scale) / scale;

    /* Fetch the four pixels to interpolate between */
    vec4 pixel_tl = get_pixel (texture_position              );
    vec4 pixel_tr = get_pixel (texture_position + vec2 (1, 0));
    vec4 pixel_bl = get_pixel (texture_position + vec2 (0, 1));
    vec4 pixel_br = get_pixel (texture_position + vec2 (1, 1));

    /* Interpolate.
     * Conversion to linear colour is needed to keep the darkness of the grid lines consistent.
     * If scale is an integer, then fract() should be zero for hard pixel edges. */
    vec4 pixel_t = mix (to_linear (pixel_tl), to_linear (pixel_tr), fract (texture_position.x));
    vec4 pixel_b = mix (to_linear (pixel_bl), to_linear (pixel_br), fract (texture_position.x));
    pixel        = from_linear (mix (pixel_t,  pixel_b,  fract (texture_position.y)));

    /* TODO: Consider something closer to the scanlines shader. While the single-pixel grid separation
             looks decent on phones, it's never looked quite right on computer monitors.
             Perhaps a thicker but lighter grid would give a better effect. */
}
)"
