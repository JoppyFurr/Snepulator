#include <ctype.h>
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

#include "util.h"
#include "snepulator.h"
#include "open.h"
#include "config.h"

#include "video/tms9918a.h"
#include "video/sms_vdp.h"
#include "cpu/z80.h"

#include "sg-1000.h"
#include "sms.h"
#include "colecovision.h"

    extern File_Open_State open_state;

    /* TODO: Move this into a struct */
    extern SMS_Region region;
    extern Z80_Regs z80_regs;
    extern TMS9918A_Mode sms_vdp_mode_get (void);
}

#include "../Images/snepulator_icon.c"

/* Global state */
Snepulator_State state;
bool config_capture_events = false;

/* Gamepads */
static std::vector<Gamepad_Mapping> input_devices;
Gamepad_Mapping player_1_mapping;
Gamepad_Mapping player_2_mapping;
SDL_Joystick *player_1_joystick;
SDL_Joystick *player_2_joystick;

/* Implementation in input.cpp */
extern Gamepad_Mapping map_to_edit;
bool input_modal_consume_event (SDL_Event event);
void snepulator_render_input_modal (void);


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
            if (ImGui::MenuItem ("Load ROM...", NULL))
            {
                state.running = false;
                open_state.title = "Load ROM...";
                open_state.regex = ".*\\.(BIN|bin|SMS|sms|sg|col)$";
                open_state.callback = snepulator_load_rom;
                open_modal = true;
            }
            if (ImGui::BeginMenu ("Load BIOS..."))
            {
                if (ImGui::MenuItem ("Master System", NULL))
                {
                    state.running = false;
                    open_state.title = "Load Master System BIOS...";
                    open_state.regex = ".*\\.(BIN|bin|SMS|sms)$";
                    open_state.callback = snepulator_load_sms_bios;
                    open_modal = true;
                }
                if (ImGui::MenuItem ("ColecoVision", NULL))
                {
                    state.running = false;
                    open_state.title = "Load ColecoVision BIOS...";
                    open_state.regex = ".*\\.(col)$";
                    open_state.callback = snepulator_load_colecovision_bios;
                    open_modal = true;
                }
                ImGui::EndMenu ();
            }

            if (ImGui::MenuItem ("Pause", NULL, !state.running)) { if (state.ready) { state.running = !state.running; } }
            ImGui::Separator ();
            if (ImGui::MenuItem ("Quit", NULL)) { state.abort = true; }
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
                if (ImGui::MenuItem ("GL_NEAREST", NULL, state.video_filter == VIDEO_FILTER_NEAREST))
                {
                    state.video_filter = VIDEO_FILTER_NEAREST;
                    config_string_set ("video", "filter", "GL_NEAREST");
                    config_write ();
                }
                if (ImGui::MenuItem ("GL_LINEAR",  NULL, state.video_filter == VIDEO_FILTER_LINEAR))
                {
                    state.video_filter = VIDEO_FILTER_LINEAR;
                    config_string_set ("video", "filter", "GL_LINEAR");
                    config_write ();
                }
                if (ImGui::MenuItem ("Scanlines",  NULL, state.video_filter == VIDEO_FILTER_SCANLINES))
                {
                    state.video_filter = VIDEO_FILTER_SCANLINES;
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
                system_init ();
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

            if (ImGui::MenuItem ("NTSC", NULL, state.system == VIDEO_SYSTEM_NTSC)) {
                state.system = VIDEO_SYSTEM_NTSC;
                config_string_set ("sms", "format", "NTSC");
                config_write ();
            }
            if (ImGui::MenuItem ("PAL",  NULL, state.system == VIDEO_SYSTEM_PAL))  {
                state.system = VIDEO_SYSTEM_PAL;
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

            if (ImGui::MenuItem ("Configure...", NULL))
            {
                state.running = false;
                input_modal = true;
            }
            ImGui::EndMenu ();
        }

        if (ImGui::BeginMenu ("DEBUG"))
        {
            ImGui::Text ("CPU");
            ImGui::Text ("PC : %04x", z80_regs.pc);

            ImGui::Separator ();

            ImGui::Text ("Video");
            ImGui::Text ("Mode : %s", tms9918a_mode_name_get (sms_vdp_mode_get ()));

            ImGui::Separator ();

            if (ImGui::BeginMenu ("Statistics"))
            {
                ImGui::Text ("Video");
                ImGui::Text ("Host: %.2f fps", state.host_framerate);
                ImGui::Text ("VDP:  %.2f fps", state.vdp_framerate);
                ImGui::Separator ();
                ImGui::Text ("Audio");
                ImGui::Text ("Ring buffer: %.2f%% full", state.audio_ring_utilisation * 100.0);

                ImGui::EndMenu ();
            }

            ImGui::Separator ();

            if (ImGui::MenuItem ("Time One Minute", NULL))
            {
                uint32_t start_time;
                uint32_t end_time;
                start_time = SDL_GetTicks ();
                state.run (60000.0); /* Simulate 60 seconds */
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
        ImGui::OpenPopup (open_state.title);
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
    state.system = VIDEO_SYSTEM_NTSC;
    if (config_string_get ("sms", "format", &string) == 0)
    {
        if (strcmp (string, "PAL") == 0)
        {
            state.system = VIDEO_SYSTEM_PAL;
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
    /* TODO: Make dialogues fit (full-screen?) */
    window = SDL_CreateWindow ("Snepulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                      VIDEO_BUFFER_WIDTH * 2, VIDEO_BUFFER_LINES * 2 + 16, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
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

    /* Set icon */
    SDL_Surface *icon = SDL_CreateRGBSurfaceFrom ((void *) snepulator_icon.pixel_data, snepulator_icon.width, snepulator_icon.height,
                                                  snepulator_icon.bytes_per_pixel * 8, snepulator_icon.bytes_per_pixel * snepulator_icon.width,
                                                  0xff << 0, 0xff << 8, 0xff << 16, 0xff << 24);
    if (icon == NULL)
    {
        fprintf (stderr, "Error: SDL_CreateRGBSurfaceFrom failed.\n");
        SDL_Quit ();
        return EXIT_FAILURE;
    }
    SDL_SetWindowIcon (window, icon);
    SDL_FreeSurface (icon);

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

    /* Create texture for video output */
    glGenTextures (1, &video_out_texture);
    glBindTexture (GL_TEXTURE_2D, video_out_texture);
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

        /* TODO: For now, we hard-code the USB Saturn gamepad on my desk. A "Configure gamepad" option needs to be created. */
        /* TODO: For any saved configuration, use UUID instead of "id". */
        while (SDL_PollEvent (&event))
        {
            ImGui_ImplSdlGL3_ProcessEvent (&event);

            if (event.type == SDL_QUIT)
            {
                state.abort = true;
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
                if (event.key.keysym.sym == player_1_mapping.direction_up.value)    { state.gamepad_1.up       = (event.type == SDL_KEYDOWN); }
                if (event.key.keysym.sym == player_1_mapping.direction_down.value)  { state.gamepad_1.down     = (event.type == SDL_KEYDOWN); }
                if (event.key.keysym.sym == player_1_mapping.direction_left.value)  { state.gamepad_1.left     = (event.type == SDL_KEYDOWN); }
                if (event.key.keysym.sym == player_1_mapping.direction_right.value) { state.gamepad_1.right    = (event.type == SDL_KEYDOWN); }
                if (event.key.keysym.sym == player_1_mapping.button_1.value)        { state.gamepad_1.button_1 = (event.type == SDL_KEYDOWN); }
                if (event.key.keysym.sym == player_1_mapping.button_2.value)        { state.gamepad_1.button_2 = (event.type == SDL_KEYDOWN); }
                if (event.key.keysym.sym == player_1_mapping.pause.value)           { state.pause_button       = (event.type == SDL_KEYDOWN); }
            }

            /* Mouse */
            if (event.type == SDL_MOUSEMOTION)
            {
                state.mouse_time = SDL_GetTicks ();
                state.show_gui = true;
                SDL_ShowCursor (SDL_ENABLE);
            }

            /* Joystick */
            else if ((event.type == SDL_JOYAXISMOTION) && event.jaxis.which == player_1_mapping.device_id)
            {
                /* TODO: Make the deadzone configurable */
                /* TODO: Shorten these lines via macros */
                if (player_1_mapping.direction_up.type    == SDL_JOYAXISMOTION && event.jaxis.axis == player_1_mapping.direction_up.value)    { state.gamepad_1.up       = (player_1_mapping.direction_up.negative    ? -event.jaxis.value : event.jaxis.value) > 1000; }
                if (player_1_mapping.direction_down.type  == SDL_JOYAXISMOTION && event.jaxis.axis == player_1_mapping.direction_down.value)  { state.gamepad_1.down     = (player_1_mapping.direction_down.negative  ? -event.jaxis.value : event.jaxis.value) > 1000; }
                if (player_1_mapping.direction_left.type  == SDL_JOYAXISMOTION && event.jaxis.axis == player_1_mapping.direction_left.value)  { state.gamepad_1.left     = (player_1_mapping.direction_left.negative  ? -event.jaxis.value : event.jaxis.value) > 1000; }
                if (player_1_mapping.direction_right.type == SDL_JOYAXISMOTION && event.jaxis.axis == player_1_mapping.direction_right.value) { state.gamepad_1.right    = (player_1_mapping.direction_right.negative ? -event.jaxis.value : event.jaxis.value) > 1000; }
                if (player_1_mapping.button_1.type        == SDL_JOYAXISMOTION && event.jaxis.axis == player_1_mapping.button_1.value)        { state.gamepad_1.button_1 = (player_1_mapping.button_1.negative        ? -event.jaxis.value : event.jaxis.value) > 1000; }
                if (player_1_mapping.button_2.type        == SDL_JOYAXISMOTION && event.jaxis.axis == player_1_mapping.button_2.value)        { state.gamepad_1.button_2 = (player_1_mapping.button_2.negative        ? -event.jaxis.value : event.jaxis.value) > 1000; }
                if (player_1_mapping.pause.type           == SDL_JOYAXISMOTION && event.jaxis.axis == player_1_mapping.pause.value)           { state.pause_button       = (player_1_mapping.pause.negative           ? -event.jaxis.value : event.jaxis.value) > 1000; }
            }
            else if ((event.type == SDL_JOYBUTTONDOWN || event.type == SDL_JOYBUTTONUP) && event.jbutton.which == player_1_mapping.device_id)
            {
                if (player_1_mapping.direction_up.type    == SDL_JOYBUTTONDOWN && event.jbutton.button == player_1_mapping.direction_up.value)    { state.gamepad_1.up       = (event.type == SDL_JOYBUTTONDOWN); }
                if (player_1_mapping.direction_down.type  == SDL_JOYBUTTONDOWN && event.jbutton.button == player_1_mapping.direction_down.value)  { state.gamepad_1.down     = (event.type == SDL_JOYBUTTONDOWN); }
                if (player_1_mapping.direction_left.type  == SDL_JOYBUTTONDOWN && event.jbutton.button == player_1_mapping.direction_left.value)  { state.gamepad_1.left     = (event.type == SDL_JOYBUTTONDOWN); }
                if (player_1_mapping.direction_right.type == SDL_JOYBUTTONDOWN && event.jbutton.button == player_1_mapping.direction_right.value) { state.gamepad_1.right    = (event.type == SDL_JOYBUTTONDOWN); }
                if (player_1_mapping.button_1.type        == SDL_JOYBUTTONDOWN && event.jbutton.button == player_1_mapping.button_1.value)        { state.gamepad_1.button_1 = (event.type == SDL_JOYBUTTONDOWN); }
                if (player_1_mapping.button_2.type        == SDL_JOYBUTTONDOWN && event.jbutton.button == player_1_mapping.button_2.value)        { state.gamepad_1.button_2 = (event.type == SDL_JOYBUTTONDOWN); }
                if (player_1_mapping.pause.type           == SDL_JOYBUTTONDOWN && event.jbutton.button == player_1_mapping.pause.value)           { state.pause_button       = (event.type == SDL_JOYBUTTONDOWN); }
            }

            /* Device removal */
            else if (event.type == SDL_JOYDEVICEREMOVED && event.jdevice.which == player_1_mapping.device_id)
                printf ("TODO: Player 1 joystick removed. Do something nice like pause the game until it's re-attached.\n");
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

                float_Colour *source = (float_Colour *) state.video_out_texture_data;
                float_Colour *dest   = (float_Colour *) state.video_out_texture_data_scanlines;

                /* Prescale by 2x3, then add scanlines */
                uint32_t output_width  = VIDEO_BUFFER_WIDTH * 2;
                uint32_t output_height = VIDEO_BUFFER_LINES * 3;
                for (int y = 0; y < output_height; y++)
                {
                    for (int x = 0; x < output_width; x++)
                    {
                        /* Prescale 2×3 */
                        dest [x + y * output_width] = source [x / 2 + y / 3 * VIDEO_BUFFER_WIDTH];

                        /* Scanlines (40%) */
                        if (y % 3 == 2)
                        {
                            /* The space between scanlines inherits light from the scanline above and the scanline below */
                            /* TODO: Better math for blending colours? This can make things darker than they should be. */
                            /* TODO: Should source [] be used instead of dest [] for the RHS? */
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
        ImGui_ImplSdlGL3_NewFrame (window);
        if (state.show_gui)
        {
            snepulator_render_menubar ();
            snepulator_render_open_modal ();
            snepulator_render_input_modal ();
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
            ImGui::PushStyleColor (ImGuiCol_WindowBg, ImColor (0.0f, 0.0f, 0.0f, 0.0f));
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
            state.host_framerate *= 0.95;
            state.host_framerate += 0.05 * (1000.0 / (host_current_time - host_previous_completion_time));
        }
        host_previous_completion_time = host_current_time;
    }

    fprintf (stdout, "EMULATION ENDED.\n");

    SDL_CloseAudioDevice (audio_device_id);
    glDeleteTextures (1, &video_out_texture);
    SDL_GL_DeleteContext (glcontext);
    SDL_DestroyWindow (window);
    SDL_Quit ();

    return EXIT_SUCCESS;
}
