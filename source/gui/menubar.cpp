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
#include "database/sms_db.h"

#include "gamepad.h"
#include "video/tms9928a.h"
#include "video/sms_vdp.h"
#include "cpu/z80.h"

#include "sg-1000.h"
#include "sms.h"
#include "colecovision.h"

extern TMS9928A_Mode sms_vdp_mode_get (void);

/* TODO: Access through a function instead of accessing the array */
extern Gamepad_Instance gamepad_list[128];
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

void snepulator_audio_device_open (const char *device);

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
            if (ImGui::MenuItem ("Open ROM...", NULL))
            {
                snepulator_pause_set (true);
                open_state.title = "Open ROM...";
                snepulator_set_open_regex (".*\\.(bin|col|gg|sg|sms)$");
                open_state.callback = snepulator_rom_set;
                open_modal = true;
            }

            if (ImGui::BeginMenu ("Open BIOS"))
            {
                if (ImGui::MenuItem ("Master System...", NULL))
                {
                    snepulator_pause_set (true);
                    open_state.title = "Open Master System BIOS...";
                    snepulator_set_open_regex (".*\\.(bin|sms)$");
                    open_state.callback = snepulator_bios_set;
                    open_modal = true;
                }
                if (ImGui::MenuItem ("ColecoVision...", NULL))
                {
                    snepulator_pause_set (true);
                    open_state.title = "Open ColecoVision BIOS...";
                    snepulator_set_open_regex (".*\\.(col)$");
                    open_state.callback = snepulator_bios_set;
                    open_modal = true;
                }
                if (ImGui::BeginMenu ("Clear"))
                {
                    if (ImGui::MenuItem ("Master System", NULL))
                    {
                        config_entry_remove ("sms", "bios");
                        config_write ();
                        if (state.sms_bios_filename != NULL)
                        {
                            free (state.sms_bios_filename);
                            state.sms_bios_filename = NULL;
                        }
                    }
                    if (ImGui::MenuItem ("ColecoVision", NULL))
                    {
                        config_entry_remove ("colecovision", "bios");
                        config_write ();
                        if (state.colecovision_bios_filename != NULL)
                        {
                            free (state.colecovision_bios_filename);
                            state.colecovision_bios_filename = NULL;
                        }
                    }
                    ImGui::EndMenu ();
                }
                ImGui::EndMenu ();
            }

            if (ImGui::MenuItem ("Close ROM", NULL))
            {
                snepulator_reset ();
                snepulator_draw_logo ();
            }

            if (ImGui::MenuItem ("Pause", NULL, state.run == RUN_STATE_PAUSED))
            {
                snepulator_pause_set (state.run != RUN_STATE_PAUSED);
            }
            ImGui::Separator ();
            if (ImGui::MenuItem ("Quit", NULL))
            {
                state.run = RUN_STATE_EXIT;
            }
            ImGui::EndMenu ();
        }

        if (ImGui::BeginMenu ("Console"))
        {
            if (ImGui::MenuItem ("Hard Reset"))
            {
                snepulator_system_init ();
            }
            ImGui::Separator ();

            if (ImGui::MenuItem ("World", NULL, state.region == REGION_WORLD)) {
                snepulator_region_set (REGION_WORLD);
            }
            if (ImGui::MenuItem ("Japan", NULL, state.region == REGION_JAPAN)) {
                snepulator_region_set (REGION_JAPAN);
            }
            ImGui::Separator ();

            if (ImGui::MenuItem ("Auto", NULL, state.format_auto)) {
                snepulator_video_format_set (VIDEO_FORMAT_AUTO);
            }
            if (ImGui::MenuItem ("NTSC", NULL, state.format == VIDEO_FORMAT_NTSC)) {
                snepulator_video_format_set (VIDEO_FORMAT_NTSC);
            }
            if (ImGui::MenuItem ("PAL",  NULL, state.format == VIDEO_FORMAT_PAL))  {
                snepulator_video_format_set (VIDEO_FORMAT_PAL);
            }
            ImGui::Separator ();

            if (ImGui::MenuItem ("Overclock", NULL, !!state.overclock))
            {
                snepulator_overclock_set (!state.overclock);
            }
            if (ImGui::MenuItem ("Remove Sprite Limit", NULL, state.remove_sprite_limit))
            {
                snepulator_remove_sprite_limit_set (!state.remove_sprite_limit);
            }
            if (ImGui::MenuItem ("Disable Blanking", NULL, state.disable_blanking))
            {
                snepulator_disable_blanking_set (!state.disable_blanking);
            }
            ImGui::Separator ();

            if (ImGui::BeginMenu ("Info"))
            {
                ImGui::Text ("%s", state.console == CONSOLE_MASTER_SYSTEM ? "Master System" :
                                   state.console == CONSOLE_GAME_GEAR     ? "Game Gear" :
                                   state.console == CONSOLE_SG_1000       ? "SG-1000" :
                                   state.console == CONSOLE_COLECOVISION  ? "ColecoVision" :
                                   "N/A");

                ImGui::Separator ();

#if 0
                ImGui::Text ("CPU");
                ImGui::Text ("PC : %04x    SP : %04x", z80_state.pc, z80_state.sp);
                ImGui::Text ("AF : %04x    BC : %04x", z80_state.af, z80_state.bc);
                ImGui::Text ("DE : %04x    HL : %04x", z80_state.de, z80_state.hl);
                ImGui::Text ("IX : %04x    IY : %04x", z80_state.ix, z80_state.iy);
                ImGui::Text ("IM : %d       IFF: %d/%d", z80_state.im, z80_state.iff1, z80_state.iff2);

                ImGui::Separator ();
#endif

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
                snepulator_pause_set (true);
                start_time = snepulator_get_ticks ();
                state.run_callback (5 * 60000); /* Simulate five minutes */
                end_time = snepulator_get_ticks ();
                snepulator_pause_set (false);

                fprintf (stdout, "[DEBUG] Took %d ms to emulate five minutes. (%fx speed-up)\n",
                         end_time - start_time, (5.0 * 60000.0) / (end_time - start_time));
            }

            ImGui::EndMenu ();
        }

        if (ImGui::BeginMenu ("State"))
        {
            if (ImGui::MenuItem ("Quick Save", NULL)) {
                if ((state.run == RUN_STATE_RUNNING || state.run == RUN_STATE_PAUSED) && state.state_save)
                {
                    char *path = quicksave_path ();
                    state.state_save (path);
                    free (path);
                }
            }
            if (ImGui::MenuItem ("Quick Load", NULL)) {
                if ((state.run == RUN_STATE_RUNNING || state.run == RUN_STATE_PAUSED) && state.state_load)
                {
                    char *path = quicksave_path ();
                    state.state_load (path);
                    free (path);
                    snepulator_pause_set (false);
                }
            }

            ImGui::EndMenu ();
        }

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
                    if (ImGui::MenuItem ("SMS Light Phaser", NULL, gamepad_1.type == GAMEPAD_TYPE_SMS_PHASER))
                    {
                        gamepad_1.type = GAMEPAD_TYPE_SMS_PHASER;
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

                for (uint32_t i = 0; i < gamepad_list_count; i++)
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
                for (uint32_t i = 0; i < gamepad_list_count; i++)
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

        if (ImGui::BeginMenu ("Audio"))
        {
            if (ImGui::BeginMenu ("Device"))
            {
                static char current_device[80] = { '\0' };

                int count = SDL_GetNumAudioDevices (0);
                for (int i = 0; i < count; i++)
                {
                    const char *audio_device_name = SDL_GetAudioDeviceName (i, 0);
                    if (audio_device_name == NULL)
                        audio_device_name = "Unknown Audio Device";

                    if (ImGui::MenuItem (audio_device_name, NULL, !strncmp (audio_device_name, current_device, 80)))
                    {
                        strncpy (current_device, audio_device_name, 79);
                        snepulator_audio_device_open (audio_device_name);
                    }
                }
                ImGui::EndMenu ();
            }
            ImGui::EndMenu ();
        }

        if (ImGui::BeginMenu ("Video"))
        {
            if (ImGui::BeginMenu ("Filter"))
            {
                if (ImGui::MenuItem ("Nearest Neighbour", NULL, state.video_filter == VIDEO_FILTER_NEAREST))
                {
                    snepulator_video_filter_set (VIDEO_FILTER_NEAREST);
                }
                if (ImGui::MenuItem ("Linear Interpolation",  NULL, state.video_filter == VIDEO_FILTER_LINEAR))
                {
                    snepulator_video_filter_set (VIDEO_FILTER_LINEAR);
                }
                if (ImGui::MenuItem ("Scanlines",  NULL, state.video_filter == VIDEO_FILTER_SCANLINES))
                {
                    snepulator_video_filter_set (VIDEO_FILTER_SCANLINES);
                }
                if (ImGui::MenuItem ("Dot Matrix",  NULL, state.video_filter == VIDEO_FILTER_DOT_MATRIX))
                {
                    snepulator_video_filter_set (VIDEO_FILTER_DOT_MATRIX);
                }
                ImGui::EndMenu ();
            }

            if (ImGui::BeginMenu ("3D Mode"))
            {
                if (ImGui::MenuItem ("Red-Cyan", NULL, state.video_3d_mode == VIDEO_3D_RED_CYAN))
                {
                    snepulator_video_3d_mode_set (VIDEO_3D_RED_CYAN);
                }
                if (ImGui::MenuItem ("Red-Green", NULL, state.video_3d_mode == VIDEO_3D_RED_GREEN))
                {
                    snepulator_video_3d_mode_set (VIDEO_3D_RED_GREEN);
                }
                if (ImGui::MenuItem ("Magenta-Green", NULL, state.video_3d_mode == VIDEO_3D_MAGENTA_GREEN))
                {
                    snepulator_video_3d_mode_set (VIDEO_3D_MAGENTA_GREEN);
                }
                if (ImGui::MenuItem ("Left image only", NULL, state.video_3d_mode == VIDEO_3D_LEFT_ONLY))
                {
                    snepulator_video_3d_mode_set (VIDEO_3D_LEFT_ONLY);
                }
                if (ImGui::MenuItem ("Right image only", NULL, state.video_3d_mode == VIDEO_3D_RIGHT_ONLY))
                {
                    snepulator_video_3d_mode_set (VIDEO_3D_RIGHT_ONLY);
                }

                ImGui::Separator ();

                if (ImGui::BeginMenu ("Colour"))
                {
                    if (ImGui::MenuItem ("Saturation 0%", NULL, state.video_3d_saturation == 0.0))
                    {
                        snepulator_video_3d_saturation_set (0.0);
                    }
                    if (ImGui::MenuItem ("Saturation 25%", NULL, state.video_3d_saturation == 0.25))
                    {
                        snepulator_video_3d_saturation_set (0.25);
                    }
                    if (ImGui::MenuItem ("Saturation 50%", NULL, state.video_3d_saturation == 0.50))
                    {
                        snepulator_video_3d_saturation_set (0.50);
                    }
                    if (ImGui::MenuItem ("Saturation 75%", NULL, state.video_3d_saturation == 0.75))
                    {
                        snepulator_video_3d_saturation_set (0.75);
                    }
                    if (ImGui::MenuItem ("Saturation 100%", NULL, state.video_3d_saturation == 1.0))
                    {
                        snepulator_video_3d_saturation_set (1.0);
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
