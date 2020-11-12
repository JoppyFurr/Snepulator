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
#include "video/tms9928a.h"
#include "video/sms_vdp.h"
#include "cpu/z80.h"

#include "sg-1000.h"
#include "sms.h"
#include "colecovision.h"

extern Z80_Regs z80_regs;
extern TMS9928A_Mode sms_vdp_mode_get (void);

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

            if (ImGui::BeginMenu ("3D Mode"))
            {
                if (ImGui::MenuItem ("Left image only", NULL, state.video_3d_mode == VIDEO_3D_LEFT_ONLY))
                {
                    state.video_3d_mode = VIDEO_3D_LEFT_ONLY;
                }
                if (ImGui::MenuItem ("Right image only", NULL, state.video_3d_mode == VIDEO_3D_RIGHT_ONLY))
                {
                    state.video_3d_mode = VIDEO_3D_RIGHT_ONLY;
                }
                if (ImGui::MenuItem ("Red-Cyan", NULL, state.video_3d_mode == VIDEO_3D_RED_CYAN))
                {
                    state.video_3d_mode = VIDEO_3D_RED_CYAN;
                }
                if (ImGui::MenuItem ("Red-Green", NULL, state.video_3d_mode == VIDEO_3D_RED_GREEN))
                {
                    state.video_3d_mode = VIDEO_3D_RED_GREEN;
                }
                if (ImGui::MenuItem ("Magenta-Green", NULL, state.video_3d_mode == VIDEO_3D_MAGENTA_GREEN))
                {
                    state.video_3d_mode = VIDEO_3D_MAGENTA_GREEN;
                }

                ImGui::Separator ();

                if (ImGui::BeginMenu ("Colour"))
                {
                    if (ImGui::MenuItem ("Saturation 0%", NULL, state.video_3d_saturation == 0.0))
                    {
                        state.video_3d_saturation = 0.0;
                    }
                    if (ImGui::MenuItem ("Saturation 25%", NULL, state.video_3d_saturation == 0.25))
                    {
                        state.video_3d_saturation = 0.25;
                    }
                    if (ImGui::MenuItem ("Saturation 50%", NULL, state.video_3d_saturation == 0.50))
                    {
                        state.video_3d_saturation = 0.50;
                    }
                    if (ImGui::MenuItem ("Saturation 75%", NULL, state.video_3d_saturation == 0.75))
                    {
                        state.video_3d_saturation = 0.75;
                    }
                    if (ImGui::MenuItem ("Saturation 100%", NULL, state.video_3d_saturation == 1.0))
                    {
                        state.video_3d_saturation = 1.0;
                    }
                    ImGui::EndMenu ();
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

            if (ImGui::MenuItem ("Auto", NULL, state.format_auto)) {
                state.format_auto = true;
                config_string_set ("sms", "format", "Auto");
                config_write ();
            }
            if (ImGui::MenuItem ("NTSC", NULL, state.format == VIDEO_FORMAT_NTSC)) {
                state.format = VIDEO_FORMAT_NTSC;
                state.format_auto = false;
                config_string_set ("sms", "format", "NTSC");
                config_write ();
            }
            if (ImGui::MenuItem ("PAL",  NULL, state.format == VIDEO_FORMAT_PAL))  {
                state.format = VIDEO_FORMAT_PAL;
                state.format_auto = false;
                config_string_set ("sms", "format", "PAL");
                config_write ();
            }
            ImGui::Separator ();

            if (ImGui::BeginMenu ("Info"))
            {
                ImGui::Text ("CPU");
                ImGui::Text ("PC : %04x    SP : %04x", z80_regs.pc, z80_regs.sp);
                ImGui::Text ("AF : %04x    BC : %04x", z80_regs.af, z80_regs.bc);
                ImGui::Text ("DE : %04x    HL : %04x", z80_regs.de, z80_regs.hl);
                ImGui::Text ("IX : %04x    IY : %04x", z80_regs.ix, z80_regs.iy);
                ImGui::Text ("IM : %d       IFF: %d/%d", z80_regs.im, z80_regs.iff1, z80_regs.iff2);

                ImGui::Separator ();

                ImGui::Text ("Video");
                ImGui::Text ("Mode : %s", tms9928a_mode_name_get (sms_vdp_mode_get ()));

                ImGui::EndMenu ();
            }
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
            if (ImGui::MenuItem ("Time Five Minutes", NULL))
            {
                uint32_t start_time;
                uint32_t end_time;
                state.running = false;
                start_time = SDL_GetTicks ();
                state.run (5 * 60000); /* Simulate five minutes */
                end_time = SDL_GetTicks ();
                state.running = true;

                fprintf (stdout, "[DEBUG] Took %d ms to emulate five minutes. (%fx speed-up)\n",
                         end_time - start_time, (5.0 * 60000.0) / (end_time - start_time));
            }

            ImGui::EndMenu ();
        }

        /* TODO: Deal with joysticks being removed from the system mid-game. Maybe auto-pause? */
        if (ImGui::BeginMenu ("Input"))
        {

            if (ImGui::BeginMenu ("Player 1"))
            {
                if (ImGui::BeginMenu ("Type"))
                {
                    if (ImGui::MenuItem ("Auto", NULL, gamepad_1.type_auto))
                    {
                        gamepad_1.type_auto = true;
                    }
                    if (ImGui::MenuItem ("SMS Gamepad", NULL, gamepad_1.type == GAMEPAD_TYPE_SMS))
                    {
                        gamepad_1.type = GAMEPAD_TYPE_SMS;
                        gamepad_1.type_auto = false;
                    }
                    if (ImGui::MenuItem ("SMS Paddle", NULL, gamepad_1.type == GAMEPAD_TYPE_SMS_PADDLE))
                    {
                        gamepad_1.type = GAMEPAD_TYPE_SMS_PADDLE;
                        gamepad_1.type_auto = false;
                    }
                    ImGui::EndMenu ();
                }

                ImGui::Separator ();

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
