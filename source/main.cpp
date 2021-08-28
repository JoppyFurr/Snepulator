#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <pthread.h>

#include <GL/gl3w.h>
#include <SDL2/SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_opengl3.h"

extern "C" {

#include "util.h"
#include "snepulator.h"
#include "config.h"
#include "database/sms_db.h"

#include "gamepad.h"
#include "sg-1000.h"
#include "sms.h"
#include "colecovision.h"

}

#include "gui/input.h"
#include "gui/menubar.h"
#include "gui/open.h"

/* Images */
#include "../images/snepulator_icon.c"
#include "../images/snepulator_logo.c"
#include "../images/snepulator_paused.c"

/* Shaders */
const char *vertex_shader_source =
    #include "shader.vert"
;
const char *fragment_shader_source =
    #include "shader.frag"
;

/* Constants */
const float Float4_Black[4] = { 0.0, 0.0, 0.0, 0.0 };

/* Global state */
Snepulator_State state;
pthread_mutex_t video_mutex = PTHREAD_MUTEX_INITIALIZER;
bool config_capture_events = false;
SDL_Window *window = NULL;
SDL_GLContext glcontext = NULL;
ImFont *font;

/* Implementation in input.cpp */
bool input_modal_consume_event (SDL_Event event);
void snepulator_render_input_modal (void);


/*
 * Display an error message.
 */
void snepulator_error (const char *title, const char *message)
{
    state.running = false;
    state.ready = false;
    state.show_gui = true;

    /* All errors get printed to console */
    fprintf (stderr, "%s: %s\n", title, message);

    /* The first reported error gets shown as a pop-up */
    if (state.error_title == NULL)
    {
        state.error_title = strdup (title);
        state.error_message = strdup (message);
    }
}


/*
 * Draw the logo to the output texture.
 */
void snepulator_draw_logo (void)
{
    memset (state.video_out_data, 0, sizeof (state.video_out_data));

    for (uint32_t y = 0; y < snepulator_logo.height; y++)
    {
        uint32_t x_offset = VIDEO_BUFFER_WIDTH / 2 - snepulator_logo.width / 2;
        uint32_t y_offset = VIDEO_BUFFER_LINES / 2 - snepulator_logo.height / 2;

        for (uint32_t x = 0; x < snepulator_logo.width; x++)
        {
            state.video_out_data [(x + x_offset) + (y + y_offset) * VIDEO_BUFFER_WIDTH].r =
                snepulator_logo.pixel_data [(x + y * snepulator_logo.width) * 3 + 0] / 255.0;
            state.video_out_data [(x + x_offset) + (y + y_offset) * VIDEO_BUFFER_WIDTH].g =
                snepulator_logo.pixel_data [(x + y * snepulator_logo.width) * 3 + 1] / 255.0;
            state.video_out_data [(x + x_offset) + (y + y_offset) * VIDEO_BUFFER_WIDTH].b =
                snepulator_logo.pixel_data [(x + y * snepulator_logo.width) * 3 + 2] / 255.0;
        }
    }
}


void snepulator_render_error ()
{
    static bool first = true;

    if (first)
    {
        first = false;
        ImGui::OpenPopup (state.error_title);
    }

    if (ImGui::BeginPopupModal (state.error_title, NULL, ImGuiWindowFlags_AlwaysAutoResize |
                                                        ImGuiWindowFlags_NoMove |
                                                        ImGuiWindowFlags_NoScrollbar))
    {
        ImGui::Text ("%s", state.error_message);

        ImGui::Spacing ();
        ImGui::SameLine (ImGui::GetContentRegionAvail().x + 16 - 128);
        if (ImGui::Button ("Close", ImVec2 (120,0))) {
            snepulator_draw_logo ();
            ImGui::CloseCurrentPopup ();

            /* Reset the error popup */
            free (state.error_title);
            state.error_title = NULL;
            free (state.error_message);
            state.error_message = NULL;
            first = true;
        }
        ImGui::EndPopup ();
    }
}


/*
 * Callback for "Load ROM..."
 */
void snepulator_load_rom (char *path)
{
    if (state.cart_filename != NULL)
    {
        free (state.cart_filename);
    }

    state.cart_filename = strdup (path);

    snepulator_system_init ();
}


/*
 * Callback for "Load Master System BIOS..."
 * Removes the cartridge and runs a BIOS.
 */
void snepulator_load_sms_bios (char *path)
{
    snepulator_reset ();

    if (state.cart_filename != NULL)
    {
        free (state.cart_filename);
        state.cart_filename = NULL;
    }

    if (state.sms_bios_filename != NULL)
    {
        free (state.sms_bios_filename);
    }

    /* Store and use the BIOS */
    config_string_set ("sms", "bios", path);
    config_write ();

    state.sms_bios_filename = strdup (path);
    sms_init ();
}


/*
 * Callback for "Load ColecoVision BIOS..."
 * Removes the cartridge and runs a BIOS.
 */
void snepulator_load_colecovision_bios (char *path)
{
    snepulator_reset ();

    if (state.cart_filename != NULL)
    {
        free (state.cart_filename);
        state.cart_filename = NULL;
    }

    if (state.colecovision_bios_filename != NULL)
    {
        free (state.colecovision_bios_filename);
    }

    /* Store and use the BIOS */
    config_string_set ("colecovision", "bios", path);
    config_write ();

    state.colecovision_bios_filename = strdup (path);
    colecovision_init ();
}


/*
 * Import configuration from file and apply where needed.
 */
int config_import (void)
{
    char *string = NULL;
    uint32_t uint = 0;

    config_read ();

    /* SMS Region - Defaults to World */
    state.region = REGION_WORLD;
    if (config_string_get ("sms", "region", &string) == 0)
    {
        if (strcmp (string, "Japan") == 0)
        {
            state.region = REGION_JAPAN;
        }
    }

    /* Video Format - Defaults to Auto */
    state.format = VIDEO_FORMAT_NTSC;
    state.format_auto = true;
    if (config_string_get ("sms", "format", &string) == 0)
    {
        if (strcmp (string, "PAL") == 0)
        {
            state.format = VIDEO_FORMAT_PAL;
            state.format_auto = false;
        }
        else if (strcmp (string, "NTSC") == 0)
        {
            state.format = VIDEO_FORMAT_NTSC;
            state.format_auto = false;
        }
    }

    /* Video Filter - Defaults to Scanlines */
    state.video_filter = VIDEO_FILTER_SCANLINES;
    if (config_string_get ("video", "filter", &string) == 0)
    {
        if (strcmp (string, "Dot Matrix") == 0)
        {
            state.video_filter = VIDEO_FILTER_DOT_MATRIX;
        }
        else if (strcmp (string, "Linear") == 0)
        {
            state.video_filter = VIDEO_FILTER_LINEAR;
        }
        else if (strcmp (string, "Nearest") == 0)
        {
            state.video_filter = VIDEO_FILTER_NEAREST;
        }
    }

    /* 3D Video Mode - Defaults to Red-Cyan */
    state.video_3d_mode = VIDEO_3D_RED_CYAN;
    if (config_string_get ("video", "3d-mode", &string) == 0)
    {
        if (strcmp (string, "Red-Green") == 0)
        {
            state.video_3d_mode = VIDEO_3D_RED_GREEN;
        }
        else if (strcmp (string, "Magenta-Green") == 0)
        {
            state.video_3d_mode = VIDEO_3D_MAGENTA_GREEN;
        }
        else if (strcmp (string, "Left-Only") == 0)
        {
            state.video_3d_mode = VIDEO_3D_LEFT_ONLY;
        }
        else if (strcmp (string, "Right-Only") == 0)
        {
            state.video_3d_mode = VIDEO_3D_RIGHT_ONLY;
        }
    }

    /* 3D Video Colour Saturation - Defaults to 25% */
    state.video_3d_saturation = 0.25;
    if (config_uint_get ("video", "3d-saturation", &uint) == 0)
    {
        state.video_3d_saturation = uint / 100.0;
    }

    /* BIOS Paths */
    if (config_string_get ("sms", "bios", &string) == 0)
    {
        state.sms_bios_filename = strdup (string);
    }
    if (config_string_get ("colecovision", "bios", &string) == 0)
    {
        state.colecovision_bios_filename = strdup (string);
    }

    return 0;
}


/*
 * Audio callback wrapper.
 */
void snepulator_audio_callback (void *userdata, uint8_t *stream, int len)
{
    if (state.audio_callback != NULL)
        state.audio_callback (userdata, stream, len);
    else
        memset (stream, 0, len);
}


/*
 * Clear the state of the currently running system.
 */
void snepulator_reset (void)
{
    /* Stop emulation */
    state.running = false;

    /* Save any battery-backed memory. */
    if (state.sync != NULL)
    {
        state.sync ();
    }

    /* Mark the system as not-ready. */
    state.ready = false;

    /* Clear callback functions */
    state.run = NULL;
    state.audio_callback = NULL;
    state.get_clock_rate = NULL;
    state.get_int = NULL;
    state.get_nmi = NULL;
    state.sync = NULL;
    state.state_save = NULL;
    state.state_load = NULL;

    /* Clear additional video parameters */
    state.video_has_border = false;
    state.video_blank_left = 0;

    /* Clear hash and hints */
    memset (state.rom_hash, 0, sizeof (state.rom_hash));
    state.rom_hints = 0x00;

    /* Free memory */
    if (state.ram != NULL)
    {
        free (state.ram);
        state.ram = NULL;
    }
    if (state.sram != NULL)
    {
        free (state.sram);
        state.sram = NULL;
    }
    if (state.vram != NULL)
    {
        free (state.vram);
        state.vram = NULL;
    }
    if (state.bios != NULL)
    {
        free (state.bios);
        state.bios = NULL;
        state.bios_size = 0;
    }
    if (state.rom != NULL)
    {
        free (state.rom);
        state.rom = NULL;
        state.rom_size = 0;
        state.rom_mask = 0;
    }

    /* Auto format default to NTSC */
    if (state.format_auto)
    {
        state.format = VIDEO_FORMAT_NTSC;
    }
}


/*
 * Call the appropriate initialisation for the chosen ROM
 */
void snepulator_system_init (void)
{
    char extension[16] = { '\0' };
    char *extension_ptr = NULL;

    snepulator_reset ();

    if (state.cart_filename != NULL)
    {
        extension_ptr = strrchr (state.cart_filename, '.');

        if (extension_ptr != NULL)
        {
            for (int i = 0; i < 15 && extension_ptr[i] != '\0'; i++)
            {
                extension [i] = tolower (extension_ptr [i]);
            }
        }
    }

    if (strcmp (extension, ".col") == 0)
    {
        state.console = CONSOLE_COLECOVISION;
        colecovision_init ();
    }
    else if (strcmp (extension, ".gg") == 0)
    {
        state.console = CONSOLE_GAME_GEAR;
        sms_init ();
    }
    else if (strcmp (extension, ".sg") == 0)
    {
        state.console = CONSOLE_SG_1000;
        sg_1000_init ();
    }
    else
    {
        /* Default to Master System */
        state.console = CONSOLE_MASTER_SYSTEM;
        sms_init ();
    }
}


/*
 * Pause emulation and show the pause screen.
 */
void snepulator_pause (void)
{

    state.running = false;

    /* Convert the screen to black and white */
    for (int x = 0; x < (VIDEO_BUFFER_WIDTH * VIDEO_BUFFER_LINES); x++)
    {
        state.video_out_data [x] = to_greyscale (state.video_out_data [x]);
    }

    /* Draw the "Pause" splash over the screen */
    for (uint32_t y = 0; y < snepulator_paused.height; y++)
    {
        uint32_t x_offset = VIDEO_BUFFER_WIDTH / 2 - snepulator_paused.width / 2;
        uint32_t y_offset = VIDEO_BUFFER_LINES / 2 - snepulator_paused.height / 2;

        for (uint32_t x = 0; x < snepulator_paused.width; x++)
        {
            /* Treat black as transparent */
            if (snepulator_paused.pixel_data [(x + y * snepulator_paused.width) * 3 + 0] == 0)
            {
                continue;
            }

            state.video_out_data [(x + x_offset) + (y + y_offset) * VIDEO_BUFFER_WIDTH].r =
                snepulator_paused.pixel_data [(x + y * snepulator_paused.width) * 3 + 0] / 255.0;
            state.video_out_data [(x + x_offset) + (y + y_offset) * VIDEO_BUFFER_WIDTH].g =
                snepulator_paused.pixel_data [(x + y * snepulator_paused.width) * 3 + 1] / 255.0;
            state.video_out_data [(x + x_offset) + (y + y_offset) * VIDEO_BUFFER_WIDTH].b =
                snepulator_paused.pixel_data [(x + y * snepulator_paused.width) * 3 + 2] / 255.0;
        }
    }
}


/*
 * Thread to run the actual console emulation.
 *
 * Completely separated from the GUI, time is kept using snepulator_get_ticks ().
 */
void *main_emulation_loop (void *data)
{
    static uint32_t ticks_previous = snepulator_get_ticks ();
    uint32_t ticks_current;

    while (!state.abort)
    {
        ticks_current = snepulator_get_ticks ();
        if (state.running)
        {
            state.run (ticks_current - ticks_previous);
        }

        ticks_previous = ticks_current;

        /* Sleep */
        snepulator_delay (1);
    }

    pthread_exit (NULL);
}


/*
 * Toggle between full-screen and windowed.
 */
void toggle_fullscreen (void)
{
    static bool fullscreen = false;

    fullscreen = !fullscreen;

    SDL_SetWindowFullscreen (window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}


/*
 * Main GUI loop.
 *
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


/*
 * Render with GLSL Shader.
 */
GLuint shader_program = 0;
GLuint video_out_texture = 0;
GLuint vertex_array = 0;
void shader_callback (const ImDrawList *parent_list, const ImDrawCmd *cmd)
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
                  0, GL_RGB, GL_FLOAT, state.video_out_data);
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


int main_gui_loop (void)
{

    int gl_success = 0;

    /* Create the video-out texture, initialise with an empty image */
    glGenTextures (1, &video_out_texture);
    glBindTexture (GL_TEXTURE_2D, video_out_texture);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB,
                  VIDEO_BUFFER_WIDTH, VIDEO_BUFFER_LINES,
                  0, GL_RGB, GL_FLOAT, NULL);

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
        snepulator_error ("GLSL", info_log);
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
        snepulator_error ("GLSL", info_log);
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

    /* TODO: To avoid a flicker - only draw this if no game is loaded. */
    snepulator_draw_logo ();

    /* Main loop */
    while (!state.abort)
    {
        /* Process user-input */
        SDL_GetWindowSize (window, &state.host_width, &state.host_height);
        SDL_Event event;

        if (state.host_width < 768)
        {
            /* Standard font */
            ImGui::GetIO ().FontGlobalScale = 0.5;
        }
        else
        {
            /* Pixel-doubled font */
            ImGui::GetIO ().FontGlobalScale = 1.0;
        }

        /* Issue: SDL_PollEvent can take over 400 ms when attaching a
         * gamepad, but also needs to be called from the main thread. */
        while (SDL_PollEvent (&event))
        {

            /* Alt+Enter or F11 shortcuts for full-screen */
            if ((event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F11) ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_RETURN && (SDL_GetModState () & KMOD_ALT)))
            {
                toggle_fullscreen ();
                continue;
            }

            ImGui_ImplSDL2_ProcessEvent (&event);

            if (event.type == SDL_QUIT)
            {
                state.abort = true;
            }

            /* Allow ROM files to be dropped onto the Snepulator window */
            if (event.type == SDL_DROPFILE && event.drop.file != NULL)
            {
                snepulator_load_rom (event.drop.file);
                SDL_free (event.drop.file);
            }

            gamepad_process_event (&event);

            /* Allow the input configuration dialogue to sample input */
            if (config_capture_events)
            {
                if (input_modal_consume_event (event))
                {
                    continue;
                }
            }

            /* Use mouse motion to show / hide the menubar */
            if (event.type == SDL_MOUSEMOTION)
            {
                state.mouse_time = snepulator_get_ticks ();
                state.show_gui = true;
                SDL_ShowCursor (SDL_ENABLE);
            }

            /* Use the mouse coordinates for the light phaser target */
            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
            {
                int32_t phaser_x = (event.button.x - (state.host_width  / 2 - (VIDEO_BUFFER_WIDTH * state.video_scale) / 2))
                                   / state.video_scale - VIDEO_SIDE_BORDER;
                int32_t phaser_y = (event.button.y - (state.host_height / 2 - (VIDEO_BUFFER_LINES * state.video_scale) / 2))
                                   / state.video_scale - state.video_start_y;

                state.phaser_x = phaser_x;
                state.phaser_y = phaser_y;
            }

            /* Device change */
            else if (event.type == SDL_JOYDEVICEADDED || event.type == SDL_JOYDEVICEREMOVED)
            {
                /* If a player's joystick has been disconnected, pause the game */
                if (event.type == SDL_JOYDEVICEREMOVED && gamepad_joystick_user_count (event.jdevice.which) != 0)
                {
                    snepulator_pause ();
                }

                gamepad_list_update ();
            }
        }

        /* Scale the image to a multiple of SMS resolution */
        state.video_scale = (state.host_width / state.video_width) > (state.host_height / state.video_height) ?
                            (state.host_height / state.video_height) : (state.host_width  / state.video_width);

        if (state.video_scale < 1)
        {
            state.video_scale = 1;
        }

        /* TODO: Remove state.video_out_texture_width/height */
        switch (state.video_filter)
        {
            case VIDEO_FILTER_NEAREST:
            case VIDEO_FILTER_LINEAR:
            case VIDEO_FILTER_SCANLINES:
                if (state.video_has_border)
                {
                    state.video_show_border = true;
                }
                else
                {
                    state.video_show_border = false;
                }
                break;

            case VIDEO_FILTER_DOT_MATRIX:
                state.video_show_border = false;
                break;
        }

        /* Render ImGui */
        ImGui_ImplOpenGL3_NewFrame ();
        ImGui_ImplSDL2_NewFrame (window);
        ImGui::NewFrame ();
        if (state.show_gui)
        {
            snepulator_render_menubar ();
            snepulator_render_open_modal ();
            snepulator_render_input_modal ();
            if (state.error_title != NULL)
            {
                snepulator_render_error ();
            }
        }

        /* Hide the GUI and cursor if the mouse is not being used */
        if (snepulator_get_ticks() > (state.mouse_time + 3000))
        {
            if (state.running)
            {
                state.show_gui = false;
                SDL_ShowCursor (SDL_DISABLE);
            }
        }

        /* Video-out display area. */
        ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (0.0, 0.0));
        ImGui::PushStyleColor (ImGuiCol_WindowBg, ImVec4 (0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::SetNextWindowPos (ImVec2 (0, 0));
        ImGui::SetNextWindowSize (ImVec2 (state.host_width, state.host_height));
        ImGui::Begin ("VDP Output", NULL, ImGuiWindowFlags_NoTitleBar |
                                          ImGuiWindowFlags_NoResize |
                                          ImGuiWindowFlags_NoScrollbar |
                                          ImGuiWindowFlags_NoInputs |
                                          ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoFocusOnAppearing |
                                          ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImGui::Text ("This text should not be visible.");
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddCallback (shader_callback, NULL);

        ImGui::End ();
        ImGui::PopStyleColor (1);
        ImGui::PopStyleVar ();


        /* Draw to HW */
        ImGui::Render ();
        SDL_GL_MakeCurrent (window, glcontext);
        glViewport (0, 0, (int)ImGui::GetIO ().DisplaySize.x, (int)ImGui::GetIO ().DisplaySize.y);
        glClearColor (0.0, 0.0, 0.0, 0.0);
        glClear (GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData (ImGui::GetDrawData ());
        SDL_GL_SwapWindow (window);

        /* Update statistics (rolling average) */
        static int host_previous_completion_time = 0;
        static int host_current_time = 0;
        host_current_time = snepulator_get_ticks ();
        if (host_previous_completion_time)
        {
            state.host_framerate *= 0.95;
            state.host_framerate += 0.05 * (1000.0 / (host_current_time - host_previous_completion_time));
        }
        host_previous_completion_time = host_current_time;
    }

    glDeleteTextures (1, &video_out_texture);

    return 0;
}


/*
 * Set the console region.
 */
void snepulator_region_set (Console_Region region)
{
    if (region == REGION_WORLD)
    {
        state.region = REGION_WORLD;
        config_string_set ("sms", "region", "World");
    }
    else if (region == REGION_JAPAN)
    {
        state.region = REGION_JAPAN;
        config_string_set ("sms", "region", "Japan");
    }

    config_write ();
}


/*
 * Set the console video filter.
 */
void snepulator_video_filter_set (Video_Filter filter)
{
    if (filter == VIDEO_FILTER_NEAREST)
    {
        state.video_filter = VIDEO_FILTER_NEAREST;
        config_string_set ("video", "filter", "Nearest");
    }
    else if (filter == VIDEO_FILTER_LINEAR)
    {
        state.video_filter = VIDEO_FILTER_LINEAR;
        config_string_set ("video", "filter", "Linear");
    }
    else if (filter == VIDEO_FILTER_SCANLINES)
    {
        state.video_filter = VIDEO_FILTER_SCANLINES;
        config_string_set ("video", "filter", "Scanlines");
    }
    else if (filter == VIDEO_FILTER_DOT_MATRIX)
    {
        state.video_filter = VIDEO_FILTER_DOT_MATRIX;
        config_string_set ("video", "filter", "Dot Matrix");
    }

    config_write ();
}


/*
 * Set the video format.
 */
void snepulator_video_format_set (Video_Format format)
{
    if (format == VIDEO_FORMAT_NTSC)
    {
        state.format_auto = false;
        state.format = VIDEO_FORMAT_NTSC;
        config_string_set ("sms", "format", "NTSC");
    }
    else if (format == VIDEO_FORMAT_PAL)
    {
        state.format_auto = false;
        state.format = VIDEO_FORMAT_PAL;
        config_string_set ("sms", "format", "PAL");
    }
    else if (format == VIDEO_FORMAT_AUTO)
    {
        state.format_auto = true;
        config_string_set ("sms", "format", "Auto");

        if (state.ready == true)
        {
            if (state.rom_hints & SMS_HINT_PAL_ONLY)
            {
                state.format = VIDEO_FORMAT_PAL;
            }
            else
            {
                state.format = VIDEO_FORMAT_NTSC;
            }
        }
    }

    config_write ();
}


/*
 * Set the video 3D mode.
 */
void snepulator_video_3d_mode_set (Video_3D_Mode mode)
{
    if (mode == VIDEO_3D_RED_CYAN)
    {
        state.video_3d_mode = VIDEO_3D_RED_CYAN;
        config_string_set ("video", "3d-mode", "Red-Cyan");
    }
    else if (mode == VIDEO_3D_RED_GREEN)
    {
        state.video_3d_mode = VIDEO_3D_RED_GREEN;
        config_string_set ("video", "3d-mode", "Red-Green");
    }
    else if (mode == VIDEO_3D_MAGENTA_GREEN)
    {
        state.video_3d_mode = VIDEO_3D_MAGENTA_GREEN;
        config_string_set ("video", "3d-mode", "Magenta-Green");
    }
    else if (mode == VIDEO_3D_LEFT_ONLY)
    {
        state.video_3d_mode = VIDEO_3D_LEFT_ONLY;
        config_string_set ("video", "3d-mode", "Left-Only");
    }
    else if (mode == VIDEO_3D_RIGHT_ONLY)
    {
        state.video_3d_mode = VIDEO_3D_RIGHT_ONLY;
        config_string_set ("video", "3d-mode", "Right-Only");
    }

    config_write ();
}


/*
 * Set the video 3D colour saturation.
 */
void snepulator_video_3d_saturation_set (double saturation)
{
    if (saturation >= 0.0 && saturation <= 1.0)
    {
        state.video_3d_saturation = saturation;
        config_uint_set ("video", "3d-saturation", saturation * 100);
    }

    config_write ();
}


/*
 * Initial text sent to stdout when starting.
 */
static void about (void)
{
    char sdl_version [16] = { };
    SDL_version sdl_v;

    SDL_GetVersion (&sdl_v);
    snprintf (sdl_version, 15, "%d.%d.%d", sdl_v.major, sdl_v.minor, sdl_v.patch);

    printf ("────────────────────────────────────╮\n");
    printf (" Snepulator, the emulator with mow! │\n");
    printf (" Built on %-10s                │\n", BUILD_DATE);
    printf (" ImGui version: %-9s           │\n", IMGUI_VERSION);
    printf (" SDL Version %-10s             │\n", sdl_version);
    printf ("────────────────────────────────────╯\n\n");
}


/*
 * Entry point.
 */
int main (int argc, char **argv)
{
    /* Audio */
    SDL_AudioDeviceID audio_device_id;
    SDL_AudioSpec desired_audiospec;
    SDL_AudioSpec obtained_audiospec;
    memset (&desired_audiospec, 0, sizeof (desired_audiospec));
    desired_audiospec.freq = 48000;
    desired_audiospec.format = AUDIO_S16LSB;
    desired_audiospec.channels = 2;
    desired_audiospec.samples = 1024;
    desired_audiospec.callback = snepulator_audio_callback;

    about ();

    /* Initialise Snepulator state */
    memset (&state, 0, sizeof (state));
    state.show_gui = true;
    state.video_width = 256;
    state.video_height = 192;
    state.video_3d_mode = VIDEO_3D_RED_CYAN;
    state.video_3d_saturation = 0.25;

    /* Parse all CLI arguments */
    while (*(++argv))
    {
        if (!state.cart_filename)
        {
            /* ROM to load */
            state.cart_filename = strdup (*(argv));
        }
        else
        {
            /* Display usage */
            fprintf (stdout, "Usage: Snepulator [rom.sms]\n");
            return EXIT_FAILURE;
        }
    }

    /* Import configuration */
    if (config_import () == -1)
    {
        return EXIT_FAILURE;
    }

    /* Initialise SDL */
    if (SDL_Init (SDL_INIT_EVERYTHING) == -1)
    {
        snepulator_error ("SDL Error", SDL_GetError ());
        return EXIT_FAILURE;
    }

    SDL_SetHint (SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    /* Create a window */
    /* For now, lets assume Master System resolution.
     * 256 × 192, with 16 pixels for left/right border, and 32 pixels for top/bottom border */
    SDL_GL_SetAttribute (SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute (SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute (SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute (SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute (SDL_GL_STENCIL_SIZE, 8); /* Do we need this? */
    SDL_GL_SetAttribute (SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute (SDL_GL_CONTEXT_MINOR_VERSION, 2);

    /* Twice the Master System resolution, plus enough extra for 16 px boarders */
    /* TODO: Make dialogues fit (full-screen?) */
    window = SDL_CreateWindow ("Snepulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                      VIDEO_BUFFER_WIDTH * 2, VIDEO_BUFFER_LINES * 2, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    if (window == NULL)
    {
        snepulator_error ("SDL Error", SDL_GetError ());
        SDL_Quit ();
        return EXIT_FAILURE;
    }

    glcontext = SDL_GL_CreateContext (window);
    if (glcontext == NULL)
    {
        snepulator_error ("SDL Error", SDL_GetError ());
        SDL_Quit ();
        return EXIT_FAILURE;
    }
    SDL_GL_SetSwapInterval (1);

    /* GL Loader */
    if (gl3wInit () != 0)
    {
        snepulator_error ("GL Error", "gl3wInit () fails");
        SDL_Quit ();
        return EXIT_FAILURE;
    }

    /* Set icon */
    SDL_Surface *icon = SDL_CreateRGBSurfaceFrom ((void *) snepulator_icon.pixel_data, snepulator_icon.width, snepulator_icon.height,
                                                  snepulator_icon.bytes_per_pixel * 8, snepulator_icon.bytes_per_pixel * snepulator_icon.width,
                                                  0xff << 0, 0xff << 8, 0xff << 16, 0xff << 24);
    if (icon == NULL)
    {
        snepulator_error ("SDL Error", SDL_GetError ());
        SDL_Quit ();
        return EXIT_FAILURE;
    }
    SDL_SetWindowIcon (window, icon);
    SDL_FreeSurface (icon);

    /* Setup ImGui binding */
    ImGui::CreateContext ();
    ImGui::GetIO ().IniFilename = NULL;
    ImGui_ImplSDL2_InitForOpenGL (window, glcontext);
    ImGui_ImplOpenGL3_Init ();

    /* Double font size to allow both 1× and 2× size to appear crisp. */
    ImFontConfig font_config = { };
    font_config.SizePixels = 13 * 2;
    font = ImGui::GetIO ().Fonts->AddFontDefault (&font_config);
    ImGui::GetIO ().FontGlobalScale = 0.5;

    /* Style */
    ImGui::PushStyleColor (ImGuiCol_MenuBarBg,      ImVec4 (0.4, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_TitleBgActive,  ImVec4 (0.5, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_PopupBg,        ImVec4 (0.2, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_Header,         ImVec4 (0.6, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_HeaderHovered,  ImVec4 (0.8, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_ScrollbarBg,    ImVec4 (0.1, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_ScrollbarGrab,  ImVec4 (0.5, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_ScrollbarGrabHovered, ImVec4 (0.8, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_ScrollbarGrabActive,  ImVec4 (0.9, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_Button,         ImVec4 (0.5, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_ButtonHovered,  ImVec4 (0.8, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_ButtonActive,   ImVec4 (0.9, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_FrameBg,        ImVec4 (0.3, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_FrameBgHovered, ImVec4 (0.4, 0.0, 0.0, 1.0));
    ImGui::PushStyleVar (ImGuiStyleVar_WindowRounding, 4.0);
    ImGui::PushStyleVar (ImGuiStyleVar_WindowBorderSize, 0.0);

    /* Open the default audio device */
    audio_device_id = SDL_OpenAudioDevice (NULL, 0, &desired_audiospec, &obtained_audiospec,
                                           SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_FORMAT_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    SDL_PauseAudioDevice (audio_device_id, 0);

    /* Initialise gamepad support */
    gamepad_init ();

    /* If we have a valid ROM to run, start emulation */
    if (state.cart_filename)
    {
        snepulator_system_init ();
    }

    /* Create the emulation thread */
    pthread_t emulation_thread;
    if (pthread_create (&emulation_thread, NULL, main_emulation_loop, NULL) != 0)
    {
        snepulator_error ("pThread Error", "Unable to create emulation_thread.");
    }

    /* Run the main GUI loop until the user quits. */
    main_gui_loop ();

    snepulator_reset ();
    pthread_join (emulation_thread, NULL);

    if (state.error_title)
    {
        free (state.error_title);
        free (state.error_message);
    }

    ImGui_ImplOpenGL3_Shutdown ();
    ImGui_ImplSDL2_Shutdown ();
    ImGui::DestroyContext ();

    SDL_CloseAudioDevice (audio_device_id);
    SDL_GL_DeleteContext (glcontext);
    SDL_DestroyWindow (window);
    SDL_Quit ();

    return EXIT_SUCCESS;
}
