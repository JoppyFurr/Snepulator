/*
 * Snepulator
 * GLSL Shader setup
 */

/*
 * Video path:
 *
 *   ╭────────────────────────╮
 *   │    VDP frame-buffer    │
 *   ╰───────────┬────────────╯
 *               │ memcpy
 *   ╭───────────┴────────────╮
 *   │    state.video_ring    │
 *   ╰───────────┬────────────╯
 *               │ glTexImage2D
 *   ╭───────────┴────────────╮
 *   │      GLSL Shader       │
 *   ╰────────────────────────╯
 */

#include <stdio.h>
#include <GL/gl3w.h>
#include <SDL2/SDL.h>

#include "imgui.h"

extern "C" {
#include "snepulator.h"
}

/* Shader source code */
static const char *vertex_shader_source =
    #include "shaders/shader.vert"
;

static const char *fragment_shader_source [SHADER_COUNT] =
{
    [SHADER_NEAREST] =
        #include "shaders/nearest.frag"
        ,
    [SHADER_NEAREST_SOFT] =
        #include "shaders/nearest-soft.frag"
        ,
    [SHADER_LINEAR] =
        #include "shaders/linear.frag"
        ,
    [SHADER_SCANLINES] =
        #include "shaders/scanlines.frag"
        ,
    [SHADER_DOT_MATRIX] =
        #include "shaders/dot_matrix.frag"
};

extern Snepulator_State state;
extern GLuint active_area_texture;
extern GLuint backdrop_texture;
GLuint shader_programs [SHADER_COUNT] = { };
GLuint vertex_array = 0;


/*
 * Sets up a pair of triangles and compiles the shaders.
 */
void snepulator_shader_setup (void)
{
    int gl_success = 0;

    /* Create two triangles to cover the screen */
    glGenVertexArrays (1, &vertex_array);
    glBindVertexArray (vertex_array);

    static const GLfloat vertex_data [] =
    {
        /* Position */
        -1.0f, -1.0f, 0.0f,
         1.0f, -1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f,
         1.0f, -1.0f, 0.0f,
         1.0f,  1.0f, 0.0f
    };

    GLuint vertex_buffer = 0;
    glGenBuffers (1, &vertex_buffer);
    glBindBuffer (GL_ARRAY_BUFFER, vertex_buffer);
    glBufferData (GL_ARRAY_BUFFER, sizeof (vertex_data), vertex_data, GL_STATIC_DRAW);

    /* Attibute 0: Position */
    glVertexAttribPointer (0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof (GLfloat), (void *) 0);
    glEnableVertexAttribArray (0);

    /* Compile the common vertex shader */
    GLuint vertex_shader = glCreateShader (GL_VERTEX_SHADER);
    glShaderSource (vertex_shader, 1, &vertex_shader_source, NULL);
    glCompileShader (vertex_shader);

    glGetShaderiv (vertex_shader, GL_COMPILE_STATUS, &gl_success);
    if (!gl_success)
    {
        char info_log [512] = { '\0' };
        glGetShaderInfoLog (vertex_shader, 512, NULL, info_log);
        snepulator_error ("Vertex Shader", info_log);
    }

    for (uint32_t i = 0; i < SHADER_COUNT; i++)
    {
        /* Compile fragment shader */
        GLuint fragment_shader = glCreateShader (GL_FRAGMENT_SHADER);
        glShaderSource (fragment_shader, 1, &fragment_shader_source [i], NULL);
        glCompileShader (fragment_shader);

        glGetShaderiv (fragment_shader, GL_COMPILE_STATUS, &gl_success);
        if (!gl_success)
        {
            char info_log [512] = { '\0' };
            int len = sprintf (info_log, "Shader [%d]:\n", i);
            glGetShaderInfoLog (fragment_shader, 512 - len, NULL, &info_log [len]);
            snepulator_error ("Fragment Shader:", info_log);
        }

        /* Link fragment shader with common vertex shader */
        GLuint shader_program = glCreateProgram ();
        glAttachShader (shader_program, vertex_shader);
        glAttachShader (shader_program, fragment_shader);
        glLinkProgram (shader_program);

        glGetProgramiv (shader_program, GL_LINK_STATUS, &gl_success);
        if (!gl_success)
        {
            char info_log [512] = { '\0' };
            glGetShaderInfoLog (shader_program, 512, NULL, info_log);
            snepulator_error ("GLSL", info_log);
        }

        shader_programs [i] = shader_program;
        glDeleteShader (fragment_shader);
    }

    glDeleteShader (vertex_shader);
}


/*
 * Render with GLSL Shader.
 */
void snepulator_shader_callback (const ImDrawList *parent_list, const ImDrawCmd *cmd)
{
    GLint last_program;
    GLint last_vertex_array;
    GLint location;

    /* Save the state that we're about to modify */
    glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array);

    GLuint shader_program = shader_programs [state.shader];

    glUseProgram (shader_program);

    /* Set texture units. Note that unit 0 is taken by Dear ImGui. */
    location = glGetUniformLocation (shader_program, "active_area");
    if (location != -1)
    {
        glUniform1i (location, 1);
    }

    location = glGetUniformLocation (shader_program, "backdrop");
    if (location != -1)
    {
        glUniform1i (location, 2);
    }

    /* Copy the most recent frame into the textures */
    Video_Frame *frame = snepulator_get_current_frame ();

    glActiveTexture (GL_TEXTURE1);
    glBindTexture (GL_TEXTURE_2D, active_area_texture);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, frame->width, frame->height, 0, GL_RGB, GL_UNSIGNED_BYTE, frame->active_area);

    glActiveTexture (GL_TEXTURE2);
    glBindTexture (GL_TEXTURE_2D, backdrop_texture);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, frame->height, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, frame->backdrop);

    /* Set the uniforms */
    location = glGetUniformLocation (shader_program, "frame_resolution");
    if (location != -1)
    {
        glUniform2i (location, frame->width, frame->height);
    }

    location = glGetUniformLocation (shader_program, "host_resolution");
    if (location != -1)
    {
        glUniform2f (location, state.host_width, state.host_height);
    }

    location = glGetUniformLocation (shader_program, "scale");
    if (location != -1)
    {
        glUniform2f (location, state.video_scale * state.video_par, state.video_scale);
    }

    glBindVertexArray (vertex_array);
    glDrawArrays (GL_TRIANGLES, 0, 6);

    /* Restore state */
    glUseProgram (last_program);
    glBindVertexArray (last_vertex_array);
}
