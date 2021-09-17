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

#include "gamepad.h"
#include "sms.h"
#include "colecovision.h"

}

#include "gui/input.h"
#include "gui/menubar.h"
#include "gui/open.h"

#include "shader.h"

/* Images */
#include "../images/snepulator_icon.c"

/* Constants */
const float Float4_Black[4] = { 0.0, 0.0, 0.0, 0.0 };

/* Global state */
Snepulator_State state;
pthread_mutex_t video_mutex = PTHREAD_MUTEX_INITIALIZER;
bool config_capture_events = false;
SDL_Window *window = NULL;
SDL_GLContext glcontext = NULL;
GLuint video_out_texture = 0;
ImFont *font;

/* Implementation in input.cpp */
bool input_modal_consume_event (SDL_Event event);
void snepulator_render_input_modal (void);


/*
 * Display an error message.
 */
void snepulator_error (const char *title, const char *message)
{
    state.run = RUN_STATE_INIT;
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
 * Thread to run the actual console emulation.
 *
 * Completely separated from the GUI, time is kept using snepulator_get_ticks ().
 */
void *main_emulation_loop (void *data)
{
    static uint32_t ticks_previous = snepulator_get_ticks ();
    uint32_t ticks_current;

    while (state.run != RUN_STATE_EXIT)
    {
        ticks_current = snepulator_get_ticks ();

        if (state.run == RUN_STATE_RUNNING)
        {
            state.run_callback (ticks_current - ticks_previous);
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
 */
int main_gui_loop (void)
{
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

    /* Set up the GLSL shaders */
    snepulator_shader_setup ();

    /* TODO: To avoid a flicker - only draw this if no game is loaded. */
    snepulator_draw_logo ();

    /* Main loop */
    while (state.run != RUN_STATE_EXIT)
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
                state.run = RUN_STATE_EXIT;
            }

            /* Allow ROM files to be dropped onto the Snepulator window */
            if (event.type == SDL_DROPFILE && event.drop.file != NULL)
            {
                snepulator_rom_set (event.drop.file);
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
                    snepulator_pause_set (true);
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

        /* Start the Dear ImGui frame. */
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

        /* Hide the GUI and cursor if the mouse is not being moved. */
        if (snepulator_get_ticks() > (state.mouse_time + 3000))
        {
            if (state.run == RUN_STATE_RUNNING)
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
        draw_list->AddCallback (snepulator_shader_callback, NULL);

        ImGui::End ();
        ImGui::PopStyleColor (1);
        ImGui::PopStyleVar ();


        /* Rendering */
        ImGui::Render ();
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
 * If an SDL audio device is open, close it.
 */
SDL_AudioDeviceID audio_device_id = 0;
void snepulator_audio_device_close ()
{
    if (audio_device_id > 0)
    {
        SDL_CloseAudioDevice (audio_device_id);
    }
    audio_device_id = 0;
}


/*
 * Open an SDL audio device.
 */
void snepulator_audio_device_open (const char *device)
{
    SDL_AudioSpec desired_audiospec = { };
    SDL_AudioSpec obtained_audiospec;

    /* First, close any previous audio device */
    snepulator_audio_device_close ();

    desired_audiospec.freq = 48000;
    desired_audiospec.format = AUDIO_S16LSB;
    desired_audiospec.channels = 2;
    desired_audiospec.samples = 1024;
    desired_audiospec.callback = snepulator_audio_callback;

    audio_device_id = SDL_OpenAudioDevice (device, 0, &desired_audiospec, &obtained_audiospec,
                                           SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_FORMAT_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    SDL_PauseAudioDevice (audio_device_id, 0);
}


/*
 * Entry point.
 */
int main (int argc, char **argv)
{
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
    if (snepulator_config_import () == -1)
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
    SDL_GL_MakeCurrent (window, glcontext);
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
    ImGui_ImplOpenGL3_Init ("#version 330 core");

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
    snepulator_audio_device_open (NULL);

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

    snepulator_audio_device_close ();

    ImGui_ImplOpenGL3_Shutdown ();
    ImGui_ImplSDL2_Shutdown ();
    ImGui::DestroyContext ();

    SDL_GL_DeleteContext (glcontext);
    SDL_DestroyWindow (window);
    SDL_Quit ();

    return EXIT_SUCCESS;
}
