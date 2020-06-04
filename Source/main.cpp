#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include <GL/gl3w.h>
#include <SDL2/SDL.h>

#include "imgui.h"
#include "examples/imgui_impl_sdl.h"
#include "examples/imgui_impl_opengl3.h"

#include <vector>

extern "C" {

#include "util.h"
#include "snepulator.h"
#include "config.h"

#include "video/tms9918a.h"
#include "video/sms_vdp.h"
#include "cpu/z80.h"

#include "gamepad.h"
#include "sg-1000.h"
#include "sms.h"
#include "colecovision.h"

}

#include "gui/input.h"
#include "gui/menubar.h"
#include "gui/open.h"

/* Images */
#include "../Images/snepulator_icon.c"
#include "../Images/snepulator_logo.c"

/* Global state */
Snepulator_State state;
bool config_capture_events = false;

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
        if (ImGui::Button ("Exit", ImVec2 (120,0))) {
            state.abort = true;
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

    system_init ();
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

    /* TODO: Separate BIOS file names for different consoles. */
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

    /* TODO: Separate BIOS file names for different consoles. */
    state.colecovision_bios_filename = strdup (path);

    colecovision_init ();
}




/*
 * Import configuration from file and apply where needed.
 */
int config_import (void)
{
    char *string = NULL;

    config_read ();

    /* Video filter - Defaults to Scanlines */
    state.video_filter = VIDEO_FILTER_SCANLINES;
    if (config_string_get ("video", "filter", &string) == 0)
    {
        if (strcmp (string, "GL_NEAREST") == 0)
        {
            state.video_filter = VIDEO_FILTER_NEAREST;
        }
        else if (strcmp (string, "GL_LINEAR") == 0)
        {
            state.video_filter = VIDEO_FILTER_LINEAR;
        }
    }

    /* SMS Format - Defaults to NTSC */
    state.system = VIDEO_SYSTEM_NTSC;
    if (config_string_get ("sms", "format", &string) == 0)
    {
        if (strcmp (string, "PAL") == 0)
        {
            state.system = VIDEO_SYSTEM_PAL;
        }
    }

    /* SMS Region - Defaults to World */
    state.region = REGION_WORLD;
    if (config_string_get ("sms", "region", &string) == 0)
    {
        if (strcmp (string, "Japan") == 0)
        {
            state.region = REGION_JAPAN;
        }
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
    state.ready = false;
    state.running = false;

    /* Clear callback functions */
    state.run = NULL;
    state.audio_callback = NULL;
    state.get_clock_rate = NULL;

    /* Free memory */
    if (state.ram != NULL)
    {
        free (state.ram);
        state.ram = NULL;
    }
    if (state.bios != NULL)
    {
        free (state.bios);
        state.bios = NULL;
    }
    if (state.rom != NULL)
    {
        free (state.rom);
        state.rom = NULL;
    }
}


void system_init ()
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

    if (strcmp (extension, ".sg") == 0)
    {
        sg_1000_init ();
    }
    else if (strcmp (extension, ".col") == 0)
    {
        colecovision_init ();
    }
    else
    {
        /* Default to Master System */
        sms_init ();
    }
}


/*
 * Entry point.
 */
int main (int argc, char **argv)
{
    /* Video */
    SDL_Window *window = NULL;
    SDL_GLContext glcontext = NULL;
    GLuint video_out_texture = 0;

    /* Audio */
    /* TODO: Will we ever want to change the SDL audio driver? */
    SDL_AudioDeviceID audio_device_id;
    SDL_AudioSpec desired_audiospec;
    SDL_AudioSpec obtained_audiospec;
    memset (&desired_audiospec, 0, sizeof (desired_audiospec));
    desired_audiospec.freq = 48000;
    desired_audiospec.format = AUDIO_S16LSB;
    desired_audiospec.channels = 1;
    desired_audiospec.samples = 2048;
    desired_audiospec.callback = snepulator_audio_callback;

    printf ("Snepulator.\n");
    printf ("Built on " BUILD_DATE ".\n");

    /* Initialize Snepulator state */
    memset (&state, 0, sizeof (state));
    state.show_gui = true;
    state.video_width = 256;
    state.video_height = 192;

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
        return -1;
    }

    /* Initialize SDL */
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
                      VIDEO_BUFFER_WIDTH * 2, VIDEO_BUFFER_LINES * 2 + 16, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
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
    gl3wInit ();
    ImGui::CreateContext ();
    ImGui::GetIO ().IniFilename = NULL;
    ImGui_ImplSDL2_InitForOpenGL (window, glcontext);
    ImGui_ImplOpenGL3_Init ();

    /* Style */
    ImGui::PushStyleColor (ImGuiCol_MenuBarBg,     ImVec4 (0.5, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_TitleBgActive, ImVec4 (0.5, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_PopupBg,       ImVec4 (0.2, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_Header,        ImVec4 (0.8, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_HeaderHovered, ImVec4 (0.8, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_ScrollbarBg,   ImVec4 (0.1, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_ScrollbarGrab, ImVec4 (0.5, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_ScrollbarGrabHovered, ImVec4 (0.8, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_ScrollbarGrabActive,  ImVec4 (0.9, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_Button,        ImVec4 (0.5, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_ButtonHovered, ImVec4 (0.8, 0.0, 0.0, 1.0));
    ImGui::PushStyleColor (ImGuiCol_ButtonActive,  ImVec4 (0.9, 0.0, 0.0, 1.0));
    ImGui::PushStyleVar (ImGuiStyleVar_WindowRounding, 4.0);
    ImGui::PushStyleVar (ImGuiStyleVar_WindowBorderSize, 0.0);

    /* Create texture for video output */
    glGenTextures (1, &video_out_texture);
    glBindTexture (GL_TEXTURE_2D, video_out_texture);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    float Float4_Black[4] = { 0.0, 0.0, 0.0, 0.0 };
    glTexParameterfv (GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, Float4_Black);

    /* Load the logo to display before a rom is loaded */
    for (int y = 0; y < snepulator_logo.height; y++)
    {
        uint32_t x_offset = VIDEO_BUFFER_WIDTH / 2 - snepulator_logo.width / 2;
        uint32_t y_offset = VIDEO_BUFFER_LINES / 2 - snepulator_logo.height / 2;

        for (int x = 0; x < snepulator_logo.width; x++)
        {
            state.video_out_texture_data [(x + x_offset) + (y + y_offset) * VIDEO_BUFFER_WIDTH].r =
                snepulator_logo.pixel_data [(x + y * snepulator_logo.width) * 3 + 0] / 255.0;
            state.video_out_texture_data [(x + x_offset) + (y + y_offset) * VIDEO_BUFFER_WIDTH].g =
                snepulator_logo.pixel_data [(x + y * snepulator_logo.width) * 3 + 1] / 255.0;
            state.video_out_texture_data [(x + x_offset) + (y + y_offset) * VIDEO_BUFFER_WIDTH].b =
                snepulator_logo.pixel_data [(x + y * snepulator_logo.width) * 3 + 2] / 255.0;
        }
    }

    /* Open the default audio device */
    audio_device_id = SDL_OpenAudioDevice (NULL, 0, &desired_audiospec, &obtained_audiospec,
            SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_FORMAT_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE );

    SDL_PauseAudioDevice (audio_device_id, 0);

    /* Initialise gamepad support */
    gamepad_init ();

    /* If we have a valid ROM to run, start emulation */
    if (state.cart_filename)
    {
        system_init ();
    }

    /* Main loop */
    while (!state.abort)
    {
        /* INPUT */
        SDL_GetWindowSize (window, &state.host_width, &state.host_height);
        SDL_Event event;

        while (SDL_PollEvent (&event))
        {
            ImGui_ImplSDL2_ProcessEvent (&event);

            if (event.type == SDL_QUIT)
            {
                state.abort = true;
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

            /* Mouse */
            if (event.type == SDL_MOUSEMOTION)
            {
                state.mouse_time = SDL_GetTicks ();
                state.show_gui = true;
                SDL_ShowCursor (SDL_ENABLE);
            }

            /* Device change */
            else if (event.type == SDL_JOYDEVICEADDED | event.type == SDL_JOYDEVICEREMOVED)
            {
                gamepad_list_update ();
            }
        }

        /* EMULATE */
        if (state.running)
            state.run (1000.0 / 60.0); /* Simulate 1/60 of a second */

        /* RENDER VDP */
        glBindTexture (GL_TEXTURE_2D, video_out_texture);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, state.video_filter);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, state.video_filter);
        glTexParameterfv (GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, Float4_Black);

        switch (state.video_filter)
        {
            case VIDEO_FILTER_NEAREST:
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexImage2D    (GL_TEXTURE_2D, 0, GL_RGB, VIDEO_BUFFER_WIDTH, VIDEO_BUFFER_LINES, 0, GL_RGB, GL_FLOAT,
                                 state.video_out_texture_data);
                break;
            case VIDEO_FILTER_LINEAR:
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexImage2D    (GL_TEXTURE_2D, 0, GL_RGB, VIDEO_BUFFER_WIDTH, VIDEO_BUFFER_LINES, 0, GL_RGB, GL_FLOAT,
                                 state.video_out_texture_data);
                break;
            case VIDEO_FILTER_SCANLINES:
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                float_Colour *source = state.video_out_texture_data;
                float_Colour *dest   = state.video_out_texture_data_scanlines;

                /* Prescale by 2x3, then add scanlines */
                uint32_t output_width  = VIDEO_BUFFER_WIDTH * 2;
                uint32_t output_height = VIDEO_BUFFER_LINES * 3;
                for (int y = 0; y < output_height; y++)
                {
                    for (int x = 0; x < output_width; x++)
                    {
                        /* Prescale 2×3 */
                        dest [x + y * output_width] = source [x / 2 + y / 3 * VIDEO_BUFFER_WIDTH];

                        /* Two solid lines are followed by one darkened line */
                        if (y % 3 == 2)
                        {
                            /* The darkened line between scanlines takes its colour as the average of the two scanlines */
                            /* TODO: Better math for blending colours? This can make things darker than they should be. */
                            /* TODO: Operations to work with float_Colour? */
                            if (y != output_height - 1)
                            {
                                dest [x + y * output_width].r = dest [x + (y + 0) * output_width].r * 0.5 +
                                                                dest [x + (y + 1) * output_width].r * 0.5;
                                dest [x + y * output_width].g = dest [x + (y + 0) * output_width].g * 0.5 +
                                                                dest [x + (y + 1) * output_width].g * 0.5;
                                dest [x + y * output_width].b = dest [x + (y + 0) * output_width].b * 0.5 +
                                                                dest [x + (y + 1) * output_width].b * 0.5;
                            }

                            /* To darken the lines, remove 40% of their value */
                            dest [x + y * VIDEO_BUFFER_WIDTH * 2].r *= (1.0 - 0.4);
                            dest [x + y * VIDEO_BUFFER_WIDTH * 2].g *= (1.0 - 0.4);
                            dest [x + y * VIDEO_BUFFER_WIDTH * 2].b *= (1.0 - 0.4);
                        }
                    }
                }
                glTexImage2D    (GL_TEXTURE_2D, 0, GL_RGB, VIDEO_BUFFER_WIDTH * 2, VIDEO_BUFFER_LINES * 3, 0, GL_RGB, GL_FLOAT,
                                 state.video_out_texture_data_scanlines);
                break;
        }

        /* RENDER GUI */
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

        if (SDL_GetTicks() > (state.mouse_time + 3000))
        {
            /* If the emulator is running, we can hide the GUI */
            if (state.running)
            {
                state.show_gui = false;
                SDL_ShowCursor (SDL_DISABLE);
            }
        }

        /* Window Contents */
        {
            /* Scale the image to a multiple of SMS resolution */
            uint8_t scale = (state.host_width / state.video_width) > (state.host_height / state.video_height) ? (state.host_height / state.video_height)
                                                                                                              : (state.host_width  / state.video_width);
            if (scale < 1)
                scale = 1;
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

            /* First, draw the background, taken from the leftmost slice of the actual image */
            ImGui::SetCursorPosX (0);
            ImGui::SetCursorPosY (state.host_height / 2 - (VIDEO_BUFFER_LINES * scale) / 2);

            ImGui::Image ((void *) (uintptr_t) video_out_texture, ImVec2 (state.host_width, VIDEO_BUFFER_LINES * scale),
                          /* uv0 */  ImVec2 (0.00, 0.0),
                          /* uv1 */  ImVec2 (0.01, 1.0),
                          /* tint */ ImColor (255, 255, 255, 255),
                          /* border */ ImColor (0, 0, 0, 0));

            /* Now, draw the actual image */
            ImGui::SetCursorPosX (state.host_width  / 2 - (VIDEO_BUFFER_WIDTH * scale) / 2);
            ImGui::SetCursorPosY (state.host_height / 2 - (VIDEO_BUFFER_LINES * scale) / 2);

            ImGui::Image ((void *) (uintptr_t) video_out_texture, ImVec2 (VIDEO_BUFFER_WIDTH * scale, VIDEO_BUFFER_LINES * scale),
                          /* uv0 */  ImVec2 (0.0, 0.0),
                          /* uv1 */  ImVec2 (1.0, 1.0),
                          /* tint */ ImColor (255, 255, 255, 255),
                          /* border */ ImColor (0, 0, 0, 0));
            ImGui::End ();
            ImGui::PopStyleColor (1);
        }

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
        host_current_time = SDL_GetTicks ();
        if (host_previous_completion_time)
        {
            state.host_framerate *= 0.95;
            state.host_framerate += 0.05 * (1000.0 / (host_current_time - host_previous_completion_time));
        }
        host_previous_completion_time = host_current_time;
    }

    fprintf (stdout, "EMULATION ENDED.\n");

    if (state.error_title)
    {
        free (state.error_title);
        free (state.error_message);
    }

    ImGui_ImplOpenGL3_Shutdown ();
    ImGui_ImplSDL2_Shutdown ();
    ImGui::DestroyContext ();

    SDL_CloseAudioDevice (audio_device_id);
    glDeleteTextures (1, &video_out_texture);
    SDL_GL_DeleteContext (glcontext);
    SDL_DestroyWindow (window);
    SDL_Quit ();

    return EXIT_SUCCESS;
}
