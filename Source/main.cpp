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

#include <vector>

extern "C" {
#include "snepulator.h"
#include "config.h"
#include "video/sega_vdp.h"
#include "cpu/z80.h"
#include "sms.h"
    /* TODO: Move this into a struct */
    extern SMS_Gamepad gamepad_1;
    extern SMS_Region region;
    extern SMS_Framerate framerate;
    extern bool pause_button;
    extern Z80_Regs z80_regs;
}

/* Global state */
Snepulator snepulator;
bool config_capture_events = false;

/* Gamepads */
static std::vector<Gamepad_Mapping> input_devices;
Gamepad_Mapping player_1_mapping;
Gamepad_Mapping player_2_mapping;
SDL_Joystick *player_1_joystick;
SDL_Joystick *player_2_joystick;

/* Implementation in open.cpp */
void snepulator_render_open_modal (void);

/* Implementation in input.cpp */
extern Gamepad_Mapping map_to_edit;
bool input_modal_consume_event (SDL_Event event);
void snepulator_render_input_modal (void);


/*
 * ImGui menu bar.
 */
void snepulator_render_menubar (void)
{
    bool open_modal = false;
    bool input_modal = false;

    /* What colour should this be? A "Snepulator" theme, or should it blend in with the overscan colour? */
    /* TODO: Some measure should be taken to prevent the menu from obscuring the gameplay */
    if (ImGui::BeginMainMenuBar ())
    {
        if (ImGui::BeginMenu ("File"))
        {
            if (ImGui::MenuItem ("Open...", NULL)) { snepulator.running = false; open_modal = true; }
            if (ImGui::MenuItem ("Pause", NULL, !snepulator.running)) { snepulator.running = !snepulator.running; }
            ImGui::Separator ();
            if (ImGui::MenuItem ("Quit", NULL)) { snepulator.abort = true; }
            ImGui::EndMenu ();
        }

        if (ImGui::BeginMenu ("Audio"))
        {
            if (ImGui::BeginMenu ("Device"))
            {
                int count = SDL_GetNumAudioDevices (0);
                for (int i = 0; i < count; i++)
                {
                    const char *audio_device_name = SDL_GetAudioDeviceName (i, 0);
                    if (audio_device_name == NULL)
                        audio_device_name = "Unknown Audio Device";

                    if (ImGui::MenuItem (audio_device_name, NULL)) { }
                }
                ImGui::EndMenu ();
            }
            ImGui::EndMenu ();
        }

        if (ImGui::BeginMenu ("Video"))
        {
            if (ImGui::BeginMenu ("Filter"))
            {
                if (ImGui::MenuItem ("GL_NEAREST", NULL, snepulator.video_filter == VIDEO_FILTER_NEAREST))
                {
                    snepulator.video_filter = VIDEO_FILTER_NEAREST;
                    config_string_set ("video", "filter", "GL_NEAREST");
                    config_write ();
                }
                if (ImGui::MenuItem ("GL_LINEAR",  NULL, snepulator.video_filter == VIDEO_FILTER_LINEAR))
                {
                    snepulator.video_filter = VIDEO_FILTER_LINEAR;
                    config_string_set ("video", "filter", "GL_LINEAR");
                    config_write ();
                }
                if (ImGui::MenuItem ("Scanlines",  NULL, snepulator.video_filter == VIDEO_FILTER_SCANLINES))
                {
                    snepulator.video_filter = VIDEO_FILTER_SCANLINES;
                    config_string_set ("video", "filter", "Scanlines");
                    config_write ();
                }
                ImGui::EndMenu ();
            }
            ImGui::EndMenu ();
        }

        if (ImGui::BeginMenu ("Console"))
        {
            if (ImGui::MenuItem ("Hard Reset"))
            {
                sms_init (snepulator.bios_filename, snepulator.cart_filename);
            }
            ImGui::Separator ();

            if (ImGui::MenuItem ("World", NULL, region == REGION_WORLD)) {
                region = REGION_WORLD;
                config_string_set ("sms", "region", "World");
                config_write ();
            }
            if (ImGui::MenuItem ("Japan", NULL, region == REGION_JAPAN)) {
                region = REGION_JAPAN;
                config_string_set ("sms", "region", "Japan");
                config_write ();
            }
            ImGui::Separator ();

            if (ImGui::MenuItem ("NTSC", NULL, framerate == FRAMERATE_NTSC)) {
                framerate = FRAMERATE_NTSC;
                config_string_set ("sms", "format", "NTSC");
                config_write ();
            }
            if (ImGui::MenuItem ("PAL",  NULL, framerate == FRAMERATE_PAL))  {
                framerate = FRAMERATE_PAL;
                config_string_set ("sms", "format", "PAL");
                config_write ();
            }
            ImGui::EndMenu ();
        }

        /* TODO: Deal with joysticks being removed from the system mid-game. Maybe auto-pause? */
        if (ImGui::BeginMenu ("Input"))
        {
            if (ImGui::BeginMenu ("Player 1"))
            {
                for (int i = 0; i < input_devices.size (); i++)
                {
                    const char *joystick_name;
                    if (input_devices[i].device_id == ID_KEYBOARD)
                    {
                        joystick_name = "Keyboard";
                    }
                    else
                    {
                        joystick_name = SDL_JoystickNameForIndex (input_devices[i].device_id);
                        if (joystick_name == NULL)
                        {
                            joystick_name = "Unknown Joystick";
                        }
                    }

                    if (ImGui::MenuItem (joystick_name, NULL, player_1_mapping.device_id == input_devices[i].device_id))
                    {
                        /* Check that this is not already the active joystick */
                        if (player_1_mapping.device_id != input_devices[i].device_id)
                        {
                            /* Close the previous device */
                            if (player_1_joystick != NULL)
                            {
                                SDL_JoystickClose (player_1_joystick);
                                player_1_joystick = NULL;
                            }
                            player_1_mapping.device_id = ID_NONE;

                            /* Open the new device */
                            if (input_devices[i].device_id == ID_KEYBOARD)
                            {
                                player_1_mapping = input_devices[i];
                            }
                            else
                            {
                                player_1_joystick = SDL_JoystickOpen (input_devices[i].device_id);
                                if (player_1_joystick)
                                {
                                    player_1_mapping = input_devices[i];
                                }
                            }
                        }

                    }
                }
                ImGui::EndMenu ();
            }
            if (ImGui::BeginMenu ("Player 2"))
            {
                if (ImGui::MenuItem ("Not Implemented", NULL)) { };
                ImGui::EndMenu ();
            }

            if (ImGui::MenuItem ("Configure...", NULL)) { snepulator.running = false; input_modal = true; }
            ImGui::EndMenu ();
        }

        if (ImGui::BeginMenu ("DEBUG"))
        {
            ImGui::Text ("CPU");
            ImGui::Text ("PC : %04x", z80_regs.pc);

            ImGui::Separator ();

            ImGui::Text ("VDP");
            ImGui::Text ("Mode : %s", vdp_get_mode_name ());

            ImGui::Separator ();

            if (ImGui::BeginMenu ("Statistics"))
            {
                ImGui::Text ("Video");
                ImGui::Text ("Host: %.2f fps", snepulator.host_framerate);
                ImGui::Text ("VDP:  %.2f fps", snepulator.vdp_framerate);
                ImGui::Separator ();
                ImGui::Text ("Audio");
                ImGui::Text ("Ring buffer: %.2f%% full", snepulator.audio_ring_utilisation * 100.0);

                ImGui::EndMenu ();
            }

            ImGui::Separator ();

            if (ImGui::MenuItem ("Time One Minute", NULL))
            {
                uint32_t start_time;
                uint32_t end_time;
                start_time = SDL_GetTicks ();
                sms_run (60000.0); /* Simulate 60 seconds */
                end_time = SDL_GetTicks ();

                fprintf (stdout, "[DEBUG] Took %d ms to emulate one minute. (%fx speed-up)\n",
                         end_time - start_time, 60000.0 / (end_time - start_time));
            }

            ImGui::EndMenu ();
        }

        ImGui::EndMainMenuBar ();
    }

    /* Open any popups requested */
    if (open_modal)
    {
        ImGui::OpenPopup ("Open ROM...");
    }
    if (input_modal)
    {
        map_to_edit = player_1_mapping;
        config_capture_events = true;
        ImGui::OpenPopup ("Configure input...");
    }

}


/*
 * Update the in-memory button mapping for an input device.
 */
void snepulator_update_input_device (Gamepad_Mapping device)
{
    for (int i = 0; i < input_devices.size (); i++)
    {
        if (input_devices[i].device_id == device.device_id)
        {
            /* Replace the old entry with the new one */
            input_devices[i] = device;
            return;
        }
    }

    fprintf (stderr, "Error: Unable to find device %d.\n", device.device_id);
}


/*
 * Detect input devices and populate the in-memory mapping list.
 * Note: It'd be nice to automatically add/remove mappings as devices are plugged in and removed.
 */
void snepulator_init_input_devices (void)
{
    Gamepad_Mapping no_gamepad = {
        .device_id       = ID_NONE,
    };

    /* TODO: Detect user's keyboard layout and adjust accordingly */
    Gamepad_Mapping default_keyboard = {
        .device_id       = ID_KEYBOARD,
        .direction_up    = { .type = SDL_KEYDOWN, .value = SDLK_COMMA },
        .direction_down  = { .type = SDL_KEYDOWN, .value = SDLK_o },
        .direction_left  = { .type = SDL_KEYDOWN, .value = SDLK_a },
        .direction_right = { .type = SDL_KEYDOWN, .value = SDLK_e },
        .button_1        = { .type = SDL_KEYDOWN, .value = SDLK_v },
        .button_2        = { .type = SDL_KEYDOWN, .value = SDLK_z },
        .pause           = { .type = SDL_KEYDOWN, .value = SDLK_RETURN }
    };
    input_devices.push_back (default_keyboard);

    /* TODO: Recall saved mappings from a file */
    Gamepad_Mapping default_gamepad = {
        .device_id       = ID_NONE,
        .direction_up    = { .type = SDL_JOYAXISMOTION, .value = 1, .negative = true  },
        .direction_down  = { .type = SDL_JOYAXISMOTION, .value = 1, .negative = false },
        .direction_left  = { .type = SDL_JOYAXISMOTION, .value = 0, .negative = true  },
        .direction_right = { .type = SDL_JOYAXISMOTION, .value = 0, .negative = false },
        .button_1        = { .type = SDL_JOYBUTTONDOWN, .value = 2 },
        .button_2        = { .type = SDL_JOYBUTTONDOWN, .value = 1 },
        .pause           = { .type = SDL_JOYBUTTONDOWN, .value = 9 }
    };
    for (int i = 0; i < SDL_NumJoysticks (); i++)
    {
        default_gamepad.device_id = i;
        input_devices.push_back (default_gamepad);
    }

    /* Set default devices for players */
    player_1_mapping = default_keyboard;
    player_2_mapping = no_gamepad;
}


/*
 * Import configuration from file and apply where needed.
 */
int config_import (void)
{
    char *string = NULL;

    config_read ();

    /* Video filter - Defaults to Scanlines */
    snepulator.video_filter = VIDEO_FILTER_SCANLINES;
    if (config_string_get ("video", "filter", &string) == 0)
    {
        if (strcmp (string, "GL_NEAREST") == 0)
        {
            snepulator.video_filter = VIDEO_FILTER_NEAREST;
        }
        else if (strcmp (string, "GL_LINEAR") == 0)
        {
            snepulator.video_filter = VIDEO_FILTER_LINEAR;
        }
    }

    /* SMS Region - Defaults to World*/
    region = REGION_WORLD;
    if (config_string_get ("sms", "region", &string) == 0)
    {
        if (strcmp (string, "Japan") == 0)
        {
            region = REGION_JAPAN;
        }
    }

    /* SMS Format - Defaults to NTSC */
    framerate = FRAMERATE_NTSC;
    if (config_string_get ("sms", "format", &string) == 0)
    {
        if (strcmp (string, "PAL") == 0)
        {
            framerate = FRAMERATE_PAL;
        }
    }

    return 0;
}


/*
 * Entry point.
 */
int main (int argc, char **argv)
{
    /* Video */
    SDL_Window *window = NULL;
    SDL_GLContext glcontext = NULL;
    GLuint sms_vdp_texture = 0;

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

    /* Parse all CLI arguments */
    while (*(++argv))
    {
        if (!strcmp ("-b", *argv))
        {
            /* BIOS to load */
            snepulator.bios_filename = strdup (*(++argv));
        }
        else if (!snepulator.cart_filename)
        {
            /* ROM to load */
            snepulator.cart_filename = strdup (*(argv));
        }
        else
        {
            /* Display usage */
            fprintf (stdout, "Usage: Snepulator [-b bios.sms] rom.sms\n");
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

    /* Twice the Master System resolution, plus enough extra for 16 px boarders */
    /* TODO: Make dialogues fit (full-screen?) and consider hiding the menubar during gameplay */
    window = SDL_CreateWindow ("Snepulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                      VDP_BUFFER_WIDTH * 2, VDP_BUFFER_LINES * 2 + 16, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
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
    gl3wInit ();
    ImGui_ImplSdlGL3_Init (window);

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

    /* Create texture for VDP output */
    glGenTextures (1, &sms_vdp_texture);
    glBindTexture (GL_TEXTURE_2D, sms_vdp_texture);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    float Float4_Black[4] = { 0.0, 0.0, 0.0, 0.0 };
    glTexParameterfv (GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, Float4_Black);

    /* Open the default audio device */
    audio_device_id = SDL_OpenAudioDevice (NULL, 0, &desired_audiospec, &obtained_audiospec,
            SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_FORMAT_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE );

    SDL_PauseAudioDevice (audio_device_id, 0);

    /* Detect input devices */
    snepulator_init_input_devices ();

    /* If we have a valid ROM to run, start emulation */
    /* TODO: Only allow unpause if we have a ROM to run */
    if (snepulator.bios_filename || snepulator.cart_filename)
    {
        sms_init (snepulator.bios_filename, snepulator.cart_filename);
        snepulator.running = true;
    }

    /* Main loop */
    while (!snepulator.abort)
    {
        /* INPUT */
        SDL_GetWindowSize (window, &snepulator.host_width, &snepulator.host_height);
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

            /* Allow the input configuration dialogue to sample input */
            if (config_capture_events)
            {
                if (input_modal_consume_event (event))
                {
                    continue;
                }
            }

            /* Keyboard */
            if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) && player_1_mapping.device_id == ID_KEYBOARD)
            {
                if (event.key.keysym.sym == player_1_mapping.direction_up.value)    { gamepad_1.up       = (event.type == SDL_KEYDOWN); }
                if (event.key.keysym.sym == player_1_mapping.direction_down.value)  { gamepad_1.down     = (event.type == SDL_KEYDOWN); }
                if (event.key.keysym.sym == player_1_mapping.direction_left.value)  { gamepad_1.left     = (event.type == SDL_KEYDOWN); }
                if (event.key.keysym.sym == player_1_mapping.direction_right.value) { gamepad_1.right    = (event.type == SDL_KEYDOWN); }
                if (event.key.keysym.sym == player_1_mapping.button_1.value)        { gamepad_1.button_1 = (event.type == SDL_KEYDOWN); }
                if (event.key.keysym.sym == player_1_mapping.button_2.value)        { gamepad_1.button_2 = (event.type == SDL_KEYDOWN); }
                if (event.key.keysym.sym == player_1_mapping.pause.value)           { pause_button       = (event.type == SDL_KEYDOWN); }
            }

            /* Joystick */
            else if ((event.type == SDL_JOYAXISMOTION) && event.jaxis.which == player_1_mapping.device_id)
            {
                /* TODO: Make the deadzone configurable */
                /* TODO: Shorten these lines via macros */
                if (player_1_mapping.direction_up.type    == SDL_JOYAXISMOTION && event.jaxis.axis == player_1_mapping.direction_up.value)    { gamepad_1.up       = (player_1_mapping.direction_up.negative    ? -event.jaxis.value : event.jaxis.value) > 1000; }
                if (player_1_mapping.direction_down.type  == SDL_JOYAXISMOTION && event.jaxis.axis == player_1_mapping.direction_down.value)  { gamepad_1.down     = (player_1_mapping.direction_down.negative  ? -event.jaxis.value : event.jaxis.value) > 1000; }
                if (player_1_mapping.direction_left.type  == SDL_JOYAXISMOTION && event.jaxis.axis == player_1_mapping.direction_left.value)  { gamepad_1.left     = (player_1_mapping.direction_left.negative  ? -event.jaxis.value : event.jaxis.value) > 1000; }
                if (player_1_mapping.direction_right.type == SDL_JOYAXISMOTION && event.jaxis.axis == player_1_mapping.direction_right.value) { gamepad_1.right    = (player_1_mapping.direction_right.negative ? -event.jaxis.value : event.jaxis.value) > 1000; }
                if (player_1_mapping.button_1.type        == SDL_JOYAXISMOTION && event.jaxis.axis == player_1_mapping.button_1.value)        { gamepad_1.button_1 = (player_1_mapping.button_1.negative        ? -event.jaxis.value : event.jaxis.value) > 1000; }
                if (player_1_mapping.button_2.type        == SDL_JOYAXISMOTION && event.jaxis.axis == player_1_mapping.button_2.value)        { gamepad_1.button_2 = (player_1_mapping.button_2.negative        ? -event.jaxis.value : event.jaxis.value) > 1000; }
                if (player_1_mapping.pause.type           == SDL_JOYAXISMOTION && event.jaxis.axis == player_1_mapping.pause.value)           { pause_button       = (player_1_mapping.pause.negative           ? -event.jaxis.value : event.jaxis.value) > 1000; }
            }
            else if ((event.type == SDL_JOYBUTTONDOWN || event.type == SDL_JOYBUTTONUP) && event.jbutton.which == player_1_mapping.device_id)
            {
                if (player_1_mapping.direction_up.type    == SDL_JOYBUTTONDOWN && event.jbutton.button == player_1_mapping.direction_up.value)    { gamepad_1.up       = (event.type == SDL_JOYBUTTONDOWN); }
                if (player_1_mapping.direction_down.type  == SDL_JOYBUTTONDOWN && event.jbutton.button == player_1_mapping.direction_down.value)  { gamepad_1.down     = (event.type == SDL_JOYBUTTONDOWN); }
                if (player_1_mapping.direction_left.type  == SDL_JOYBUTTONDOWN && event.jbutton.button == player_1_mapping.direction_left.value)  { gamepad_1.left     = (event.type == SDL_JOYBUTTONDOWN); }
                if (player_1_mapping.direction_right.type == SDL_JOYBUTTONDOWN && event.jbutton.button == player_1_mapping.direction_right.value) { gamepad_1.right    = (event.type == SDL_JOYBUTTONDOWN); }
                if (player_1_mapping.button_1.type        == SDL_JOYBUTTONDOWN && event.jbutton.button == player_1_mapping.button_1.value)        { gamepad_1.button_1 = (event.type == SDL_JOYBUTTONDOWN); }
                if (player_1_mapping.button_2.type        == SDL_JOYBUTTONDOWN && event.jbutton.button == player_1_mapping.button_2.value)        { gamepad_1.button_2 = (event.type == SDL_JOYBUTTONDOWN); }
                if (player_1_mapping.pause.type           == SDL_JOYBUTTONDOWN && event.jbutton.button == player_1_mapping.pause.value)           { pause_button       = (event.type == SDL_JOYBUTTONDOWN); }
            }

            /* Device removal */
            else if (event.type == SDL_JOYDEVICEREMOVED && event.jdevice.which == player_1_mapping.device_id)
                printf ("TODO: Player 1 joystick removed. Do something nice like pause the game until it's re-attached.\n");
        }

        /* EMULATE */
        if (snepulator.running)
            sms_run (1000.0 / 60.0); /* Simulate 1/60 of a second */

        /* RENDER VDP */
        vdp_copy_latest_frame ();
        glBindTexture (GL_TEXTURE_2D, sms_vdp_texture);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, snepulator.video_filter);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, snepulator.video_filter);
        glTexParameterfv (GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, Float4_Black);

        switch (snepulator.video_filter)
        {
            case VIDEO_FILTER_NEAREST:
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexImage2D    (GL_TEXTURE_2D, 0, GL_RGB, VDP_BUFFER_WIDTH, VDP_BUFFER_LINES, 0, GL_RGB, GL_FLOAT,
                                 snepulator.sms_vdp_texture_data);
                break;
            case VIDEO_FILTER_LINEAR:
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexImage2D    (GL_TEXTURE_2D, 0, GL_RGB, VDP_BUFFER_WIDTH, VDP_BUFFER_LINES, 0, GL_RGB, GL_FLOAT,
                                 snepulator.sms_vdp_texture_data);
                break;
            case VIDEO_FILTER_SCANLINES:
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                Float3 *source = (Float3 *) snepulator.sms_vdp_texture_data;
                Float3 *dest   = (Float3 *) snepulator.sms_vdp_texture_data_scanlines;

                /* Prescale by 2x3, then add scanlines */
                uint32_t output_width  = VDP_BUFFER_WIDTH * 2;
                uint32_t output_height = VDP_BUFFER_LINES * 3;
                for (int y = 0; y < output_height; y++)
                {
                    for (int x = 0; x < output_width; x++)
                    {
                        /* Prescale 2×3 */
                        dest [x + y * output_width] = source [x / 2 + y / 3 * VDP_BUFFER_WIDTH];

                        /* Scanlines (40%) */
                        if (y % 3 == 2)
                        {
                            /* The space between scanlines inherits light from the scanline above and the scanline below */
                            /* TODO: Better math for blending colours? This can make things darker than they should be. */
                            /* TODO: Should source [] be used instead of dest [] for the RHS? */
                            if (y != output_height - 1)
                            {
                                dest [x + y * output_width].data[0] = dest [x + (y + 0) * output_width].data[0] * 0.5 +
                                                                      dest [x + (y + 1) * output_width].data[0] * 0.5;
                                dest [x + y * output_width].data[1] = dest [x + (y + 0) * output_width].data[1] * 0.5 +
                                                                      dest [x + (y + 1) * output_width].data[1] * 0.5;
                                dest [x + y * output_width].data[2] = dest [x + (y + 0) * output_width].data[2] * 0.5 +
                                                                      dest [x + (y + 1) * output_width].data[2] * 0.5;
                            }

                            dest [x + y * VDP_BUFFER_WIDTH * 2].data[0] *= (1.0 - 0.4);
                            dest [x + y * VDP_BUFFER_WIDTH * 2].data[1] *= (1.0 - 0.4);
                            dest [x + y * VDP_BUFFER_WIDTH * 2].data[2] *= (1.0 - 0.4);
                        }
                    }
                }
                glTexImage2D    (GL_TEXTURE_2D, 0, GL_RGB, VDP_BUFFER_WIDTH * 2, VDP_BUFFER_LINES * 3, 0, GL_RGB, GL_FLOAT,
                                 snepulator.sms_vdp_texture_data_scanlines);
                break;
        }

        /* RENDER GUI */
        ImGui_ImplSdlGL3_NewFrame (window);
        snepulator_render_menubar ();
        snepulator_render_open_modal ();
        snepulator_render_input_modal ();

        /* Window Contents */
        {
            /* Scale the image to a multiple of SMS resolution */
            /* TODO: Can we get host_height to exclude the menu bar? */
            /* TODO: Scale is based on the SMS resolution rather than the VDP buffer, as we don't mind losing some border.
             *       However, does this mean we get a negative cursor position below, and is that okay? */
            uint8_t scale = (snepulator.host_width / 256) > (snepulator.host_height / 224) ? (snepulator.host_height / 224) : (snepulator.host_width / 256);
            if (scale < 1)
                scale = 1;
            ImGui::PushStyleColor (ImGuiCol_WindowBg, ImColor (0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::SetNextWindowSize (ImVec2 (snepulator.host_width, snepulator.host_height));
            ImGui::Begin ("VDP Output", NULL, ImGuiWindowFlags_NoTitleBar |
                                              ImGuiWindowFlags_NoResize |
                                              ImGuiWindowFlags_NoScrollbar |
                                              ImGuiWindowFlags_NoInputs |
                                              ImGuiWindowFlags_NoSavedSettings |
                                              ImGuiWindowFlags_NoFocusOnAppearing |
                                              ImGuiWindowFlags_NoBringToFrontOnFocus);

            /* First, draw the background, taken from the leftmost slice of the actual image */
            ImGui::SetCursorPosX (0);
            ImGui::SetCursorPosY (snepulator.host_height / 2 - (VDP_BUFFER_LINES * scale) / 2);
            ImGui::Image ((void *) (uintptr_t) sms_vdp_texture, ImVec2 (snepulator.host_width, VDP_BUFFER_LINES * scale),
                          /* uv0 */  ImVec2 (0.00, 0.0),
                          /* uv1 */  ImVec2 (0.01, 1.0),
                          /* tint */ ImColor (255, 255, 255, 255),
                          /* border */ ImColor (0, 0, 0, 0));

            /* Now, draw the actual image */
            ImGui::SetCursorPosX (snepulator.host_width  / 2 - (VDP_BUFFER_WIDTH * scale) / 2);
            ImGui::SetCursorPosY (snepulator.host_height / 2 - (VDP_BUFFER_LINES * scale) / 2);
            ImGui::Image ((void *) (uintptr_t) sms_vdp_texture, ImVec2 (VDP_BUFFER_WIDTH * scale, VDP_BUFFER_LINES * scale),
                          /* uv0 */  ImVec2 (0.0, 0.0),
                          /* uv1 */  ImVec2 (1.0, 1.0),
                          /* tint */ ImColor (255, 255, 255, 255),
                          /* border */ ImColor (0, 0, 0, 0));
            ImGui::End ();
            ImGui::PopStyleColor (1);
        }

        /* Draw to HW */
        glViewport (0, 0, (int)ImGui::GetIO ().DisplaySize.x, (int)ImGui::GetIO ().DisplaySize.y);
        glClearColor (0.0, 0.0, 0.0, 0.0);
        glClear (GL_COLOR_BUFFER_BIT);
        ImGui::Render ();

        SDL_GL_SwapWindow (window);

        /* Update statistics (rolling average) */
        static int host_previous_completion_time = 0;
        static int host_current_time = 0;
        host_current_time = SDL_GetTicks ();
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
