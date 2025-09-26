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
 */
vec4 get_pixel (vec2 position)
{
    /* Active area */
    if (position.x >= video_start.x && position.x < (video_start.x + video_resolution.x) &&
        position.y >= video_start.y && position.y < (video_start.y + video_resolution.y))
    {
        return texelFetch (active_area, ivec2 (position), 0);
    }

    /* Backdrop is dimmed to 50% */
    else
    {
        int line = clamp (int (position.y), video_start.y, video_start.y + video_resolution.y - 1);
        return mix (black, texelFetch (backdrop, ivec2 (line, 0), 0), 0.5);
    }
}


/*
 * Entry point for fragment shader.
 */
void main()
{
    /* Calculate the top-left screen-pixel that lands on the video_out texture area. */
    vec2 start = floor ((output_resolution / 2.0) - (video_resolution * scale / 2.0) - video_start * scale);

    /* Calculate the location of the current pixel in video_out texture coordinates. */
    vec2 texture_position = (gl_FragCoord.xy - start) / scale;

    /* Interpolate the X axis with smoothstep. */
    texture_position.x -= 0.5;
    vec4 pixel_l = get_pixel (texture_position              );
    vec4 pixel_r = get_pixel (texture_position + vec2 (1, 0));
    pixel = mix (pixel_l, pixel_r, smoothstep (0.0, 1.0, fract (texture_position.x)));

    /* Transform the video_y coordinate into a distance from the beam-centre:
     * 0 is the centre of the beam, where we have full brightness.
     * 0.5 is the space between lines, where the image is darkest. */
    float dist = abs (fract (texture_position.y) - 0.5);

    if (dist > 0.25)
    {
        float brightness = 1.0 - (dist - 0.25) * 4 * 0.45; /* Darkest point is 0.55 */
        pixel = mix (black, pixel, brightness);
    }
}
)"
