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
#include "examples/imgui_impl_sdl.h"
#include "examples/imgui_impl_opengl3.h"

#include <vector>

extern "C" {

#include "util.h"
#include "snepulator.h"
#include "config.h"

#include "video/tms9928a.h"
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
#include "../Images/snepulator_paused.c"

/* Constants */
const float Float4_Black[4] = { 0.0, 0.0, 0.0, 0.0 };

/* Global state */
Snepulator_State state;
pthread_mutex_t video_mutex = PTHREAD_MUTEX_INITIALIZER;
bool config_capture_events = false;
SDL_Window *window = NULL;
SDL_GLContext glcontext = NULL;

/* Implementation in input.cpp */
bool input_modal_consume_event (SDL_Event event);
void snepulator_render_input_modal (void);


/*
 * Draw the logo to the output texture.
 */
void draw_logo (void)
{
    memset (state.video_out_data, 0, sizeof (state.video_out_data));

    for (int y = 0; y < snepulator_logo.height; y++)
    {
        uint32_t x_offset = VIDEO_BUFFER_WIDTH / 2 - snepulator_logo.width / 2;
        uint32_t y_offset = VIDEO_BUFFER_LINES / 2 - snepulator_logo.height / 2;

        for (int x = 0; x < snepulator_logo.width; x++)
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
        if (ImGui::Button ("Close", ImVec2 (120,0))) {
            draw_logo ();
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
    state.format = VIDEO_FORMAT_NTSC;
    if (config_string_get ("sms", "format", &string) == 0)
    {
        if (strcmp (string, "PAL") == 0)
        {
            state.format = VIDEO_FORMAT_PAL;
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

    /* Clear additonal video parameters */
    state.video_extra_left_border = 0;

    /* Clear hash */
    memset (state.rom_hash, 0, sizeof (state.rom_hash));

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


/*
 * Call the appropriate initialisation for the chosen ROM
 */
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
    for (int y = 0; y < snepulator_paused.height; y++)
    {
        uint32_t x_offset = VIDEO_BUFFER_WIDTH / 2 - snepulator_paused.width / 2;
        uint32_t y_offset = VIDEO_BUFFER_LINES / 2 - snepulator_paused.height / 2;

        for (int x = 0; x < snepulator_paused.width; x++)
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
 * Completely separated from the GUI, time is kept using SDL_GetTicks ().
 */
void *main_emulation_loop (void *data)
{
    static uint32_t ticks_previous = SDL_GetTicks ();
    uint32_t ticks_current;

    while (!state.abort)
    {
        ticks_current = SDL_GetTicks ();
        if (state.running)
        {
            state.run (ticks_current - ticks_previous);
        }

        ticks_previous = ticks_current;

        /* Sleep */
        SDL_Delay (1);
    }

    pthread_exit (NULL);
}

/*
 * Main GUI loop.
 */
int main_gui_loop (void)
{
    GLuint video_out_texture = 0;

    /* Create texture for video output */
    glGenTextures (1, &video_out_texture);
    glBindTexture (GL_TEXTURE_2D, video_out_texture);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameterfv (GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, Float4_Black);
    draw_logo ();

    /* Main loop */
    while (!state.abort)
    {
        /* Process user-input */
        SDL_GetWindowSize (window, &state.host_width, &state.host_height);
        SDL_Event event;

        /* Issue: SDL_PollEvent can take over 400 ms when attaching a
         * gamepad, but also needs to be called from the main thread. */
        while (SDL_PollEvent (&event))
        {
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
                /* If a player's joystick has been disconnected, pause the game */
                if (event.type == SDL_JOYDEVICEREMOVED && gamepad_joystick_user_count (event.jdevice.which) != 0)
                {
                    snepulator_pause ();
                }

                gamepad_list_update ();
            }
        }

        /* Copy and filter the video-out texture data into the GUI */
        glBindTexture (GL_TEXTURE_2D, video_out_texture);
        glTexParameterfv (GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, Float4_Black);

        pthread_mutex_lock (&video_mutex);

        switch (state.video_filter)
        {
            case VIDEO_FILTER_NEAREST:
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

                memcpy (state.video_out_texture_data, state.video_out_data, sizeof (state.video_out_data));
                video_dim (1, 1);

                glTexImage2D    (GL_TEXTURE_2D, 0, GL_RGB, VIDEO_BUFFER_WIDTH, VIDEO_BUFFER_LINES, 0, GL_RGB, GL_FLOAT,
                                 state.video_out_texture_data);
                break;
            case VIDEO_FILTER_LINEAR:
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                memcpy (state.video_out_texture_data, state.video_out_data, sizeof (state.video_out_data));
                video_dim (1, 1);

                glTexImage2D    (GL_TEXTURE_2D, 0, GL_RGB, VIDEO_BUFFER_WIDTH, VIDEO_BUFFER_LINES, 0, GL_RGB, GL_FLOAT,
                                 state.video_out_texture_data);
                break;
            case VIDEO_FILTER_SCANLINES:
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                float_Colour *source = state.video_out_data;
                float_Colour *dest   = state.video_out_texture_data;

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
                video_dim (2, 3);
                glTexImage2D    (GL_TEXTURE_2D, 0, GL_RGB, VIDEO_BUFFER_WIDTH * 2, VIDEO_BUFFER_LINES * 3, 0, GL_RGB, GL_FLOAT,
                                 state.video_out_texture_data);
                break;
        }

        pthread_mutex_unlock (&video_mutex);

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
        if (SDL_GetTicks() > (state.mouse_time + 3000))
        {
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
 * Entry point.
 */
int main (int argc, char **argv)
{
    int ret;

    /* Audio */
    SDL_AudioDeviceID audio_device_id;
    SDL_AudioSpec desired_audiospec;
    SDL_AudioSpec obtained_audiospec;
    memset (&desired_audiospec, 0, sizeof (desired_audiospec));
    desired_audiospec.freq = 48000;
    desired_audiospec.format = AUDIO_S16LSB;
    desired_audiospec.channels = 1;
    desired_audiospec.samples = 1024;
    desired_audiospec.callback = snepulator_audio_callback;

    about ();

    /* Initialize Snepulator state */
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
            SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_FORMAT_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE );

    SDL_PauseAudioDevice (audio_device_id, 0);

    /* Initialise gamepad support */
    gamepad_init ();

    /* If we have a valid ROM to run, start emulation */
    if (state.cart_filename)
    {
        system_init ();
    }

    pthread_t emulation_thread;

    /* Create the emulation thread */
    ret = pthread_create (&emulation_thread, NULL, main_emulation_loop, NULL);
    if (ret != 0)
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
