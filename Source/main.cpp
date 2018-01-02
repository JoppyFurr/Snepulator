#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include "SDL2/SDL.h"
#include "SDL2/SDL_joystick.h"
#include <GL/gl3w.h>
#include "../Libraries/imgui-1.49/imgui.h"
#include "../Libraries/imgui-1.49/examples/sdl_opengl3_example/imgui_impl_sdl_gl3.h"

extern "C" {
#include "snepulator.h"
#include "gpu/sega_vdp.h"
#include "sms.h"
    /* TODO: Move this into a struct */
    extern SMS_Gamepad gamepad_1;
    extern SMS_Region region;
    extern SMS_Framerate framerate;
    extern bool pause_button;
}

/* Global state */
Snepulator snepulator;

/* Gamepads */
SDL_Joystick *player_1_joystick;
SDL_Joystick *player_2_joystick;
int player_1_joystick_id;
int player_2_joystick_id;

void snepulator_render_menubar (void)
{
    bool open_modal = false;

    /* What colour should this be? A "Snepulator" theme, or should it blend in with the overscan colour? */
    /* TODO: Some measure should be taken to prevent the menu from obscuring the gameplay */
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open", NULL)) { snepulator.running = false; open_modal = true; }
            if (ImGui::MenuItem("Pause", NULL, !snepulator.running)) { snepulator.running = !snepulator.running; }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", NULL)) { snepulator.abort = true; }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Audio"))
        {
            if (ImGui::BeginMenu("Device"))
            {
                int count = SDL_GetNumAudioDevices (0);
                for (int i = 0; i < count; i++)
                {
                    const char *audio_device_name = SDL_GetAudioDeviceName (i, 0);
                    if (audio_device_name == NULL)
                        audio_device_name = "Unknown Audio Device";

                    if (ImGui::MenuItem(audio_device_name, NULL)) { }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Video"))
        {
            if (ImGui::BeginMenu("Filter"))
            {
                if (ImGui::MenuItem("GL_NEAREST", NULL, snepulator.video_filter == VIDEO_FILTER_NEAREST))
                {
                    snepulator.video_filter = VIDEO_FILTER_NEAREST;
                }
                if (ImGui::MenuItem("GL_LINEAR",  NULL, snepulator.video_filter == VIDEO_FILTER_LINEAR))
                {
                    snepulator.video_filter = VIDEO_FILTER_LINEAR;
                }
                if (ImGui::MenuItem("Scanlines",  NULL, snepulator.video_filter == VIDEO_FILTER_SCANLINES))
                {
                    snepulator.video_filter = VIDEO_FILTER_SCANLINES;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Console"))
        {
            if (ImGui::MenuItem("Hard Reset"))
            {
                sms_init (snepulator.bios_filename, snepulator.cart_filename);
            }
            ImGui::Separator();

            if (ImGui::MenuItem("World", NULL, region == REGION_WORLD)) { region = REGION_WORLD; }
            if (ImGui::MenuItem("Japan", NULL, region == REGION_JAPAN)) { region = REGION_JAPAN; }
            ImGui::Separator();

            if (ImGui::MenuItem("NTSC", NULL, framerate == FRAMERATE_NTSC)) { framerate = FRAMERATE_NTSC; }
            if (ImGui::MenuItem("PAL",  NULL, framerate == FRAMERATE_PAL))  { framerate = FRAMERATE_PAL; }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Input"))
        {
            if (ImGui::BeginMenu("Player 1"))
            {
                for (int i = 0; i < SDL_NumJoysticks (); i++)
                {
                    const char *joystick_name = SDL_JoystickNameForIndex (i);
                    if (joystick_name == NULL)
                        joystick_name = "Unknown Joystick";

                    /* TODO: Deal with joysticks being removed from the system mid-game. Maybe auto-pause? */
                    if (ImGui::MenuItem(joystick_name, NULL, player_1_joystick_id == i) && player_1_joystick_id != i)
                    {
                        if (player_1_joystick)
                            SDL_JoystickClose (player_1_joystick);

                        player_1_joystick = SDL_JoystickOpen (i);
                        if (player_1_joystick)
                            player_1_joystick_id = i;
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Player 2"))
            {
                if (ImGui::MenuItem("Not Implemented", NULL)) { };
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Statistics"))
        {
            ImGui::Text ("Host: %.2f fps", snepulator.host_framerate);
            ImGui::Text ("VDP:  %.2f fps", snepulator.vdp_framerate);
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    /* Open any popups requested */
    if (open_modal)
        ImGui::OpenPopup("Open ROM...");

}

void snepulator_render_open_modal (void)
{
    static float f;
    if (ImGui::BeginPopupModal ("Open ROM...", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        /* TODO: Something other than hard coded values */

        ImGui::Text("/home/joppy/ROMs/Master System/");

        /* ROM Directories */
        ImGui::BeginChild ("Directories", ImVec2 (150, 400), true);
            ImGui::Button ("Master System", ImVec2 (ImGui::GetContentRegionAvailWidth (), 24));
            ImGui::Button ("SG-1000",       ImVec2 (ImGui::GetContentRegionAvailWidth (), 24));
        ImGui::EndChild ();

        ImGui::SameLine ();

        /* Directory Contents */
        ImGui::BeginChild ("Files", ImVec2 (350, 400), true);
        ImGui::EndChild ();

        /* Buttons */
        if (ImGui::Button ("Cancel", ImVec2 (120,0))) {
            snepulator.running = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine ();
        if (ImGui::Button ("Open", ImVec2(120,0))) {
            snepulator.running = true;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

typedef struct Float3_t {
    float data [3];
} Float3;

#define VDP_STRIDE_Y (256 * 3)
int main (int argc, char **argv)
{
    /* Video */
    SDL_Window *window = NULL;
    SDL_GLContext glcontext = NULL;
    GLuint sms_vdp_texture = 0;
    int window_width;
    int window_height;

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
    desired_audiospec.callback = sms_audio_callback;

    printf ("Snepulator.\n");
    printf ("Built on " BUILD_DATE ".\n");

    /* Initialize Snepulator state */
    memset (&snepulator, 0, sizeof (snepulator));
    snepulator.video_filter = VIDEO_FILTER_SCANLINES;
    player_1_joystick_id = -1;
    player_2_joystick_id = -1;

    /* Parse all CLI arguments */
    while (*(++argv))
    {
        if (!strcmp ("-b", *argv))
        {
            /* BIOS to load */
            snepulator.bios_filename = *(++argv);
        }
        else if (!snepulator.cart_filename)
        {
            /* ROM to load */
            snepulator.cart_filename = *(argv);
        }
        else
        {
            /* Display usage */
            fprintf (stdout, "Usage: Snepulator [-b bios.sms] rom.sms\n");
            return EXIT_FAILURE;
        }
    }

    /* Initialize SDL */
    if (SDL_Init (SDL_INIT_EVERYTHING) == -1)
    {
        fprintf (stderr, "Error: SDL_Init failed.\n");
        return EXIT_FAILURE;
    }

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
    /* TODO: Consider a slightly larger window to expose overscan / the menu bar */
    window = SDL_CreateWindow ("Snepulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                      256, 192, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    if (window == NULL)
    {
        fprintf (stderr, "Error: SDL_CreateWindowfailed.\n");
        SDL_Quit ();
        return EXIT_FAILURE;
    }

    glcontext = SDL_GL_CreateContext (window);
    if (glcontext == NULL)
    {
        fprintf (stderr, "Error: SDL_GL_CreateContext failed.\n");
        SDL_Quit ();
        return EXIT_FAILURE;
    }
    SDL_GL_SetSwapInterval (1);

    /* Setup ImGui binding */
    gl3wInit();
    ImGui_ImplSdlGL3_Init (window);

    /* Create texture for VDP output */
    glGenTextures (1, &sms_vdp_texture);
    glBindTexture (GL_TEXTURE_2D, sms_vdp_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, snepulator.video_background);

    /* Open the default audio device */
    audio_device_id = SDL_OpenAudioDevice (NULL, 0, &desired_audiospec, &obtained_audiospec,
            SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_FORMAT_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE );

    SDL_PauseAudioDevice (audio_device_id, 0);

    /* Initialise SMS */
    sms_init (snepulator.bios_filename, snepulator.cart_filename);

    snepulator.running = true;

    /* Main loop */
    while (!snepulator.abort)
    {
        /* INPUT */
        SDL_GetWindowSize (window, &window_width, &window_height);
        SDL_Event event;

        /* TODO: For now, we hard-code the USB Saturn gamepad on my desk. A "Configure gamepad" option needs to be created. */
        /* TODO: For any saved configuration, use UUID instead of "id". */
        while (SDL_PollEvent (&event))
        {
            ImGui_ImplSdlGL3_ProcessEvent (&event);

            if (event.type == SDL_QUIT)
            {
                snepulator.abort = true;
            }

            /* Player 1 Gamepad input */
            else if (event.type == SDL_JOYAXISMOTION && event.jaxis.which == player_1_joystick_id)
            {
                switch (event.jaxis.axis)
                {
                    case 0: /* Left-right */
                        gamepad_1.left = event.jaxis.value < -1000;
                        gamepad_1.right = event.jaxis.value > 1000;
                        break;
                    case 1: /* Up-down */
                        gamepad_1.up = event.jaxis.value < -1000;
                        gamepad_1.down = event.jaxis.value > 1000;
                        break;
                    default:
                        break;
                }
            }

            else if ((event.type == SDL_JOYBUTTONUP || SDL_JOYBUTTONDOWN) &&
                      event.jbutton.which == player_1_joystick_id)
            {
                switch (event.jbutton.button)
                {
                    case 2: /* "B" on my USB Saturn gamepad */
                        gamepad_1.button_1 = (event.type == SDL_JOYBUTTONDOWN) ? true : false;
                        break;
                    case 1: /* "C" on my USB Saturn gamepad */
                        gamepad_1.button_2 = (event.type == SDL_JOYBUTTONDOWN) ? true : false;
                        break;
                    case 9:
                        pause_button = (event.type == SDL_JOYBUTTONDOWN) ? true : false;
                    default: /* Don't care */
                        break;
                }
            }

            else if (event.type == SDL_JOYBUTTONUP && event.jbutton.which == player_1_joystick_id)
            {
            }

            else if (event.type == SDL_JOYDEVICEREMOVED && event.jdevice.which == player_1_joystick_id)
                printf ("TODO: Player 1 joystick removed. Do something nice like pause the game until it's re-attached.\n");
        }

        /* EMULATE */
        if (snepulator.running)
            sms_run (1000.0 / 60.0); /* Simulate 1/60 of a second */

        /* RENDER VDP */
        vdp_render (); /* TODO: Perhaps rename to vdp_get_latest_frame */
        glBindTexture (GL_TEXTURE_2D, sms_vdp_texture);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, snepulator.video_filter);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, snepulator.video_filter);
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, snepulator.video_background);

        switch (snepulator.video_filter)
        {
            case VIDEO_FILTER_NEAREST:
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexImage2D    (GL_TEXTURE_2D, 0, GL_RGB, 256, 192, 0, GL_RGB, GL_FLOAT,
                                 snepulator.sms_vdp_texture_data);
                break;
            case VIDEO_FILTER_LINEAR:
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexImage2D    (GL_TEXTURE_2D, 0, GL_RGB, 256, 192, 0, GL_RGB, GL_FLOAT,
                                 snepulator.sms_vdp_texture_data);
                break;
            case VIDEO_FILTER_SCANLINES:
                /* TODO: Scanlines should also cover the overscan area */
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                Float3 *source = (Float3 *) snepulator.sms_vdp_texture_data;
                Float3 *dest   = (Float3 *) snepulator.sms_vdp_texture_data_output;

                for (int y = 0; y < 192; y++)
                {
                    for (int x = 0; x < 256; x++)
                    {
                        /* Prescale 2×3 */
                        dest [x * 2 + 0 + (3 * y + 0) * 512] = source [x + y * 256];
                        dest [x * 2 + 1 + (3 * y + 0) * 512] = source [x + y * 256];
                        dest [x * 2 + 0 + (3 * y + 1) * 512] = source [x + y * 256];
                        dest [x * 2 + 1 + (3 * y + 1) * 512] = source [x + y * 256];
                        dest [x * 2 + 0 + (3 * y + 2) * 512] = source [x + y * 256];
                        dest [x * 2 + 1 + (3 * y + 2) * 512] = source [x + y * 256];

                        /* Scanlines (40%) */
                        dest [x * 2 + 0 + (3 * y + 2) * 512].data[0] *= (1.0 - 0.4);
                        dest [x * 2 + 1 + (3 * y + 2) * 512].data[0] *= (1.0 - 0.4);
                        dest [x * 2 + 0 + (3 * y + 2) * 512].data[1] *= (1.0 - 0.4);
                        dest [x * 2 + 1 + (3 * y + 2) * 512].data[1] *= (1.0 - 0.4);
                        dest [x * 2 + 0 + (3 * y + 2) * 512].data[2] *= (1.0 - 0.4);
                        dest [x * 2 + 1 + (3 * y + 2) * 512].data[2] *= (1.0 - 0.4);
                    }
                }
                glTexImage2D    (GL_TEXTURE_2D, 0, GL_RGB, 256 * 2, 192 * 3, 0, GL_RGB, GL_FLOAT,
                                 snepulator.sms_vdp_texture_data_output);
                break;
        }

        /* RENDER GUI */
        ImGui_ImplSdlGL3_NewFrame (window);
        snepulator_render_menubar ();
        snepulator_render_open_modal ();

        /* Window Contents */
        {
            /* Scale the image to a multiple of SMS resolution */
            uint8_t scale = (window_width / 256) > (window_height / 192) ? (window_height / 192) : (window_width / 256);
            if (scale < 1)
                scale = 1;
            ImGui::PushStyleColor (ImGuiCol_WindowBg, ImColor (0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::SetNextWindowSize (ImVec2 (window_width, window_height));
            ImGui::Begin ("VDP Output", NULL, ImGuiWindowFlags_NoTitleBar |
                                              ImGuiWindowFlags_NoResize |
                                              ImGuiWindowFlags_NoScrollbar |
                                              ImGuiWindowFlags_NoInputs |
                                              ImGuiWindowFlags_NoSavedSettings |
                                              ImGuiWindowFlags_NoFocusOnAppearing |
                                              ImGuiWindowFlags_NoBringToFrontOnFocus);

            /* Centre VDP output */
            ImGui::SetCursorPosX (window_width / 2 - (256 * scale) / 2);
            ImGui::SetCursorPosY (window_height / 2 - (192 * scale) / 2);
            ImGui::Image ((void *) (uintptr_t) sms_vdp_texture, ImVec2 (256 * scale, 192 * scale),
                          /* uv0 */  ImVec2 (0, 0),
                          /* uv1 */  ImVec2 (1, 1),
                          /* tint */ ImColor (255, 255, 255, 255),
                          /* border */ ImColor (0, 0, 0, 0));
            ImGui::End();
            ImGui::PopStyleColor (1);
        }

        /* Draw to HW */
        glViewport(0, 0, (int)ImGui::GetIO().DisplaySize.x, (int)ImGui::GetIO().DisplaySize.y);
        /* A thought: What about the option to dim the background? */
        glClearColor(snepulator.video_background[0] * 0.80, snepulator.video_background[1] * 0.80, snepulator.video_background[2] * 0.80, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui::Render();

        SDL_GL_SwapWindow (window);

        /* Update statistics (rolling average) */
        static int host_previous_completion_time = 0;
        static int host_current_time = 0;
        host_current_time = SDL_GetTicks();
        if (host_previous_completion_time)
        {
            snepulator.host_framerate *= 0.95;
            snepulator.host_framerate += 0.05 * (1000.0 / (host_current_time - host_previous_completion_time));
        }
        host_previous_completion_time = host_current_time;
    }

    fprintf (stdout, "EMULATION ENDED.\n");

    SDL_CloseAudioDevice (audio_device_id);
    glDeleteTextures (1, &sms_vdp_texture);
    SDL_GL_DeleteContext (glcontext);
    SDL_DestroyWindow (window);
    SDL_Quit ();

    return EXIT_SUCCESS;
}
