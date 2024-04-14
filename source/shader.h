/*
 * Snepulator
 * GLSL Shader header
 */

/* Sets up a pair of triangles and compiles the shaders. */
void snepulator_shader_setup (void);

/* Render with GLSL Shader. */
void snepulator_shader_callback (const ImDrawList *parent_list, const ImDrawCmd *cmd);
