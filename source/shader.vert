R"(
#version 330 core

layout (location = 0) in vec3 position_in;

out vec2 texture_coordinates;

void main ()
{
    gl_Position = vec4 (position_in.x, position_in.y, position_in.z, 1.0);
}
)"
