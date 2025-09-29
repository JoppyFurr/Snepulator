R"(#version 330 core

layout (origin_upper_left, pixel_center_integer) in vec4 gl_FragCoord;

const vec2 buffer_size = vec2 (256, 240);
const vec4 black = vec4 (0.0, 0.0, 0.0, 1.0);

uniform sampler2D active_area;
uniform sampler2D backdrop;
uniform ivec2 frame_resolution;
uniform vec2 host_resolution;
uniform vec2 scale;

out vec4 pixel;


/*
 * Given a pixel coordinate into the video-out, return the colour value.
 * This function handles the dimming of the border area.
 */
vec4 get_pixel (vec2 position)
{
    /* Active area */
    if (position.x >= 0 && position.x < frame_resolution.x &&
        position.y >= 0 && position.y < frame_resolution.y)
    {
        return texelFetch (active_area, ivec2 (position), 0);
    }

    /* Backdrop is dimmed to 50% */
    else
    {
        int line = clamp (int (position.y), 0, frame_resolution.y - 1);
        return mix (black, texelFetch (backdrop, ivec2 (line, 0), 0), 0.5);
    }
}


/*
 * Entry point for fragment shader.
 */
void main()
{
    /* Calculate the top-left screen-pixel that lands on the video_out texture area. */
    vec2 start = floor ((host_resolution / 2.0) - (frame_resolution * scale / 2.0));

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
