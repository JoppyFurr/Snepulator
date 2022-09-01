/*
 * Video path:
 *
 *   ╭────────────────────────╮
 *   │    VDP frame-buffer    │
 *   ╰───────────┬────────────╯
 *               │ memcpy
 *   ╭───────────┴────────────╮
 *   │  state.video_out_data  │
 *   ╰───────────┬────────────╯
 *               │ glTexImage2D
 *   ╭───────────┴────────────╮
 *   │      GLSL Shader       │
 *   ╰────────────────────────╯
 */
#include <pthread.h>

#include <GL/gl3w.h>

#include "imgui.h"

extern "C" {
#include "snepulator_types.h"
#include "snepulator.h"
}

/* Shader source code */
const char *vertex_shader_source =
    #include "shader.vert"
;
const char *fragment_shader_source =
    #include "shader.frag"
;

extern Snepulator_State state;
extern GLuint video_out_texture;
extern pthread_mutex_t video_mutex;
GLuint shader_program = 0;
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

    /*********************
     **  Vertex Shader  **
     *********************/
    GLuint vertex_shader = 0;
    vertex_shader = glCreateShader (GL_VERTEX_SHADER);
    glShaderSource (vertex_shader, 1, &vertex_shader_source, NULL);
    glCompileShader (vertex_shader);

    glGetShaderiv (vertex_shader, GL_COMPILE_STATUS, &gl_success);
    if (!gl_success)
    {
        char info_log [512] = { '\0' };
        glGetShaderInfoLog (vertex_shader, 512, NULL, info_log);
        snepulator_error ("Vertex Shader", info_log);
    }

    /***********************
     **  Fragment Shader  **
     ***********************/
    GLuint fragment_shader = 0;
    fragment_shader = glCreateShader (GL_FRAGMENT_SHADER);
    glShaderSource (fragment_shader, 1, &fragment_shader_source, NULL);
    glCompileShader (fragment_shader);

    glGetShaderiv (fragment_shader, GL_COMPILE_STATUS, &gl_success);
    if (!gl_success)
    {
        char info_log [512] = { '\0' };
        glGetShaderInfoLog (fragment_shader, 512, NULL, info_log);
        snepulator_error ("Fragment Shader:", info_log);
    }

    /**********************
     **  Shader Program  **
     **********************/
    shader_program = glCreateProgram ();
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

    glDeleteShader (vertex_shader);
    glDeleteShader (fragment_shader);
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

    glUseProgram (shader_program);

    /* Copy the most recent frame into video_out_texture */
    /* TODO: Should this happen when the frame is complete instead of here? */

    glBindTexture (GL_TEXTURE_2D, video_out_texture);
    pthread_mutex_lock (&video_mutex);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB,
                  VIDEO_BUFFER_WIDTH, VIDEO_BUFFER_LINES,
                  0, GL_RGB, GL_UNSIGNED_BYTE, state.video_out_data);
    pthread_mutex_unlock (&video_mutex);

    /* Set the uniforms */
    location = glGetUniformLocation (shader_program, "video_resolution");
    if (location != -1)
    {
        glUniform2i (location, state.video_width, state.video_height);
    }

    location = glGetUniformLocation (shader_program, "video_start");
    if (location != -1)
    {
        glUniform2i (location, state.video_start_x, state.video_start_y);
    }

    location = glGetUniformLocation (shader_program, "output_resolution");
    if (location != -1)
    {
        glUniform2i (location, state.host_width, state.host_height);
    }

    location = glGetUniformLocation (shader_program, "options");
    if (location != -1)
    {
        glUniform3i (location, state.video_filter, state.video_show_border, state.video_blank_left);
    }

    location = glGetUniformLocation (shader_program, "scale");
    if (location != -1)
    {
        glUniform1i (location, state.video_scale);
    }

    glBindVertexArray (vertex_array);
    glDrawArrays (GL_TRIANGLES, 0, 6);

    /* Restore state */
    glUseProgram (last_program);
    glBindVertexArray (last_vertex_array);
}
