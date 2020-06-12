/*
 * Main Menu-bar
 */

#include <stdint.h>
#include <stdio.h>

#include <vector>

#include <SDL2/SDL.h>

#include "imgui.h"

extern "C" {

#include "util.h"
#include "snepulator.h"
#include "config.h"

#include "gamepad.h"
#include "video/tms9918a.h"
#include "video/sms_vdp.h"
#include "cpu/z80.h"

#include "sg-1000.h"
#include "sms.h"
#include "colecovision.h"

extern Z80_Regs z80_regs;
extern TMS9918A_Mode sms_vdp_mode_get (void);

/* TODO: Access through a function instead of accessing the array */
extern Gamepad_Instance gamepad_list[10];
extern uint32_t gamepad_list_count;
extern Snepulator_Gamepad gamepad_1;
extern Snepulator_Gamepad gamepad_2;
}

#include "gui/input.h"
#include "gui/menubar.h"
#include "gui/open.h"
extern Snepulator_State state;
extern File_Open_State open_state;
extern bool config_capture_events; /* TODO: Move into state */

void snepulator_load_rom (char *path);
void snepulator_load_sms_bios (char *path);
void snepulator_load_colecovision_bios (char *path);

/*
 * Render the menubar.
 */
void snepulator_render_menubar (void)
{
    bool open_modal = false;
    bool input_modal = false;

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

            if (ImGui::MenuItem ("Pause", NULL, !state.running)) {
                if (state.ready) {
                    if (state.running)
                    {
                        snepulator_pause ();
                    }
                    else
                    {
                        state.running = true;
                    }
                }
            }
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

            if (ImGui::MenuItem ("Take Screenshot"))
            {
                snepulator_take_screenshot ();
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

            if (ImGui::MenuItem ("World", NULL, state.region == REGION_WORLD)) {
                state.region = REGION_WORLD;
                config_string_set ("sms", "region", "World");
                config_write ();
            }
            if (ImGui::MenuItem ("Japan", NULL, state.region == REGION_JAPAN)) {
                state.region = REGION_JAPAN;
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
                for (int i = 0; i < gamepad_list_count; i++)
                {
                    if (ImGui::MenuItem (gamepad_get_name (i), NULL, gamepad_list [i].instance_id == gamepad_1.instance_id))
                    {
                        gamepad_change_device (1, i);
                    }
                }
                ImGui::EndMenu ();
            }
            if (ImGui::BeginMenu ("Player 2"))
            {
                for (int i = 0; i < gamepad_list_count; i++)
                {
                    if (ImGui::MenuItem (gamepad_get_name (i), NULL, gamepad_list [i].instance_id == gamepad_2.instance_id))
                    {
                        gamepad_change_device (2, i);
                    }
                }
                ImGui::EndMenu ();
            }

            if (ImGui::MenuItem ("Configure...", NULL))
            {
                input_start ();
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
        config_capture_events = true;
        ImGui::OpenPopup ("Configure device...");
    }

}
