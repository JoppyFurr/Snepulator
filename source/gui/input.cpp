/*
 * Snepulator
 * ImGui Input configuration modal implementation
 * TODO: Rename source file
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>

#include <algorithm>
#include <regex>
#include <string>
#include <vector>

#define GL_GLEXT_PROTOTYPES
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include "imgui.h"

extern "C" {
#include "../snepulator_types.h"
#include "../snepulator.h"
#include "../gamepad.h"

extern bool config_capture_events;
extern Snepulator_State state;
extern Gamepad_Instance gamepad_list [128];
extern uint32_t gamepad_list_count;
extern Snepulator_Gamepad gamepad [3];
extern Gamepad_Config remap_config;
extern uint32_t gamepad_remap_step;
}

static uint32_t input_combo_index = 0;


/*
 * Initialise the input dialogue.
 */
void input_start (void)
{
    snepulator_pause_set (true);
    input_combo_index = 0;

    /* Initially selected device is player-1's device */
    for (uint32_t i = 0; i < gamepad_list_count; i++)
    {
        if (gamepad_list [i].instance_id == gamepad [1].id)
        {
            gamepad_change_device (0, i);
            remap_config = *gamepad [0].config;
            gamepad_remap_step = GAMEPAD_BUTTON_COUNT;
            input_combo_index = i;
            break;
        }
    }
}


/*
 * Get a printable name for an input button.
 */
const char *button_mapping_to_string (Gamepad_Mapping b)
{
    static char *buff = NULL;

    if (buff == NULL)
    {
        buff = (char *) malloc (80);
    }

    switch (b.type)
    {
        case GAMEPAD_MAPPING_TYPE_AXIS:
            sprintf (buff, "Axis %d %s", b.axis, (b.sign < 0.0) ? "-" : "+");
            break;
        case GAMEPAD_MAPPING_TYPE_BUTTON:
            sprintf (buff, "Button %d", b.button);
            break;
        case GAMEPAD_MAPPING_TYPE_HAT:
            sprintf (buff, "Hat %d %s", b.hat, (b.direction == SDL_HAT_UP)    ? "Up"
                                             : (b.direction == SDL_HAT_DOWN)  ? "Down"
                                             : (b.direction == SDL_HAT_LEFT)  ? "Left"
                                             : (b.direction == SDL_HAT_RIGHT) ? "Right"
                                             : "Unknown");
            break;
        case GAMEPAD_MAPPING_TYPE_KEY:
            return SDL_GetKeyName (b.key);
        default:
            return "Unknown";
    }
    return buff;
}


/*
 * Render the input configuration modal.
 */
void snepulator_input_modal_render (void)
{
    /* Layout calculations */
    uint32_t width = state.host_width - 64;
    uint32_t height = state.host_height - 64;
    uint32_t font_height = ImGui::CalcTextSize ("Text", NULL, true).y;
    uint32_t titlebar_height = font_height + 6;
    uint32_t above_box = font_height + 18;
    uint32_t below_box = font_height + 16;

    /* Centre */
    ImGui::SetNextWindowSize (ImVec2 (width, height), ImGuiCond_Always);
    ImGui::SetNextWindowPos (ImVec2 (state.host_width / 2, state.host_height / 2), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

    if (ImGui::BeginPopupModal ("Configure device...", NULL, ImGuiWindowFlags_AlwaysAutoResize |
                                                             ImGuiWindowFlags_NoMove |
                                                             ImGuiWindowFlags_NoScrollbar))
    {

        ImGui::PushItemWidth (width - 16);
        if (ImGui::BeginCombo ("##Device", gamepad_get_name (input_combo_index)))
        {
            for (uint32_t i = GAMEPAD_INDEX_KEYBOARD; i < gamepad_list_count; i++)
            {
                if (ImGui::Selectable (gamepad_get_name (i), i == input_combo_index))
                {
                    gamepad_change_device (0, i);
                    remap_config = *gamepad [0].config;
                    gamepad_remap_step = GAMEPAD_BUTTON_COUNT;
                    input_combo_index = i;
                }
                if (i == 1)
                {
                    ImGui::SetItemDefaultFocus ();
                }
            }
            ImGui::EndCombo ();
        }
        ImGui::PopItemWidth ();

        /* Master System gamepad */
        ImGui::BeginChild ("SMS Gamepad", ImVec2 (width - 16, height - (titlebar_height + above_box + below_box)), true);
        {
            ImVec2 origin = ImGui::GetCursorScreenPos ();
            float scale = (width - 64);
            origin.x += 16;
            origin.y += 10;

            ImDrawList* draw_list  = ImGui::GetWindowDrawList ();
            ImVec4 White_V         = ImVec4 (1.00f, 1.00f, 1.00f, 1.0f);
            ImVec4 Grey_10_V       = ImVec4 (0.10f, 0.10f, 0.10f, 1.0f);
            ImVec4 Grey_15_V       = ImVec4 (0.15f, 0.15f, 0.15f, 1.0f);
            ImVec4 Grey_20_V       = ImVec4 (0.20f, 0.20f, 0.20f, 1.0f);
            ImVec4 Grey_50_V       = ImVec4 (0.50f, 0.50f, 0.50f, 0.5f);
            ImVec4 ButtonWaiting_V = ImVec4 (0.80f, 0.50f, 0.10f, 1.0f);
            ImVec4 ButtonPressed_V = ImVec4 (0.40f, 0.80f, 0.60f, 1.0f);

            const ImU32 White         = ImColor (White_V);
            const ImU32 Grey_10       = ImColor (Grey_10_V);
            const ImU32 Grey_15       = ImColor (Grey_15_V);
            const ImU32 Grey_50       = ImColor (Grey_50_V);
            const ImU32 ButtonDefault = ImColor (Grey_20_V);
            const ImU32 ButtonWaiting = ImColor (ButtonWaiting_V);
            const ImU32 ButtonPressed = ImColor (ButtonPressed_V);

            /* Shape values */
            ImVec2 button_1_centre   = ImVec2 (origin.x + 0.70 * scale, origin.y + 0.25 * scale);
            ImVec2 button_2_centre   = ImVec2 (origin.x + 0.87 * scale, origin.y + 0.25 * scale);
            float button_radius      = 0.06 * scale;
            ImVec2 dpad_centre       = ImVec2 (origin.x + 0.27 * scale, origin.y + 0.20 * scale);
            float dpad_width_r       = 0.11 * scale; /* Distance from centre of dpad to edge */
            float dpad_rounding      = 0.06 * scale; /* Radius of the dpad corner-rounding */
            float dpad_circle_r      = 0.044 * scale; /* Radius of flat circle in centre of dpad */
            float dpad_bump_r        = (dpad_width_r + dpad_circle_r) * 0.5; /* Radius of the ring which passes through the centre of the bumps */
            float dpad_bump_width_r  = 0.006 * scale;
            float dpad_bump_length_r = 0.015 * scale;
            float dpad_bump_rounding = dpad_bump_width_r;

            /* Controller outline */
            draw_list->AddRectFilled (
                ImVec2 (origin.x + scale * 0.0,  origin.y + scale * 0.0 ),
                ImVec2 (origin.x + scale * 1.0,  origin.y + scale * 0.4 ), Grey_10);
            draw_list->AddRect (
                ImVec2 (origin.x + scale * 0.0,  origin.y + scale * 0.0 ),
                ImVec2 (origin.x + scale * 1.0,  origin.y + scale * 0.4 ), White);
            draw_list->AddRect (
                ImVec2 (origin.x + scale * 0.0,  origin.y + scale * 0.0 ),
                ImVec2 (origin.x + scale * 0.08, origin.y + scale * 0.4 ), White);
            draw_list->AddRect (
                ImVec2 (origin.x + scale * 0.48, origin.y + scale * 0.0 ),
                ImVec2 (origin.x + scale * 1.0,  origin.y + scale * 0.12), White);
            draw_list->AddRect (
                ImVec2 (origin.x + scale * 0.48, origin.y + scale * 0.36),
                ImVec2 (origin.x + scale * 1.0,  origin.y + scale * 0.4 ), White);

            /* Default button and dpad backgrounds */
            draw_list->AddCircleFilled (button_1_centre, button_radius, ButtonDefault, 32);
            draw_list->AddCircleFilled (button_2_centre, button_radius, ButtonDefault, 32);
            draw_list->AddRectFilled   (ImVec2 (dpad_centre.x - dpad_width_r, dpad_centre.y - dpad_width_r),
                                        ImVec2 (dpad_centre.x + dpad_width_r, dpad_centre.y + dpad_width_r), ButtonDefault, dpad_rounding);
            draw_list->AddCircleFilled (ImVec2 (dpad_centre.x, dpad_centre.y), dpad_circle_r, Grey_15, 32);

            /* Highlight the button to remap */
            if (gamepad_remap_step != GAMEPAD_BUTTON_COUNT)
            {
                switch (gamepad_remap_step)
                {
                    case GAMEPAD_DIRECTION_UP:
                        draw_list->AddRectFilled (ImVec2 (dpad_centre.x - dpad_width_r, dpad_centre.y - dpad_width_r),
                                                  ImVec2 (dpad_centre.x + dpad_width_r, dpad_centre.y               ), ButtonWaiting, dpad_rounding);
                        break;
                    case GAMEPAD_DIRECTION_DOWN:
                        draw_list->AddRectFilled (ImVec2 (dpad_centre.x - dpad_width_r, dpad_centre.y         ),
                                                  ImVec2 (dpad_centre.x + dpad_width_r, dpad_centre.y + dpad_width_r), ButtonWaiting, dpad_rounding);
                        break;
                    case GAMEPAD_DIRECTION_LEFT:
                        draw_list->AddRectFilled (ImVec2 (dpad_centre.x - dpad_width_r, dpad_centre.y - dpad_width_r),
                                                  ImVec2 (dpad_centre.x               , dpad_centre.y + dpad_width_r), ButtonWaiting, dpad_rounding);
                        break;
                    case GAMEPAD_DIRECTION_RIGHT:
                        draw_list->AddRectFilled (ImVec2 (dpad_centre.x               , dpad_centre.y - dpad_width_r),
                                                  ImVec2 (dpad_centre.x + dpad_width_r, dpad_centre.y + dpad_width_r), ButtonWaiting, dpad_rounding);
                        break;
                    case GAMEPAD_BUTTON_1:
                        draw_list->AddCircleFilled (button_1_centre, button_radius, ButtonWaiting, 32);
                        break;
                    case GAMEPAD_BUTTON_2:
                        draw_list->AddCircleFilled (button_2_centre, button_radius, ButtonWaiting, 32);
                        break;
                    default:
                        /* Nothing to highlight for the pause button yet */
                        break;
                }

            }
            /* If we're not currently remapping, show the current gamepad state */
            else
            {
                if (gamepad [0].state[GAMEPAD_DIRECTION_UP])
                {
                    draw_list->AddRectFilled (ImVec2 (dpad_centre.x - dpad_width_r, dpad_centre.y - dpad_width_r),
                                              ImVec2 (dpad_centre.x + dpad_width_r, dpad_centre.y               ), ButtonPressed, dpad_rounding);
                }
                if (gamepad [0].state[GAMEPAD_DIRECTION_DOWN])
                {
                    draw_list->AddRectFilled (ImVec2 (dpad_centre.x - dpad_width_r, dpad_centre.y               ),
                                              ImVec2 (dpad_centre.x + dpad_width_r, dpad_centre.y + dpad_width_r), ButtonPressed, dpad_rounding);
                }
                if (gamepad [0].state[GAMEPAD_DIRECTION_LEFT])
                {
                    draw_list->AddRectFilled (ImVec2 (dpad_centre.x - dpad_width_r, dpad_centre.y - dpad_width_r),
                                              ImVec2 (dpad_centre.x               , dpad_centre.y + dpad_width_r), ButtonPressed, dpad_rounding);
                }
                if (gamepad [0].state[GAMEPAD_DIRECTION_RIGHT])
                {
                    draw_list->AddRectFilled (ImVec2 (dpad_centre.x               , dpad_centre.y - dpad_width_r),
                                              ImVec2 (dpad_centre.x + dpad_width_r, dpad_centre.y + dpad_width_r), ButtonPressed, dpad_rounding);
                }
                if (gamepad [0].state[GAMEPAD_BUTTON_1])
                {
                    draw_list->AddCircleFilled (button_1_centre, button_radius, ButtonPressed, 32);
                }
                if (gamepad [0].state[GAMEPAD_BUTTON_2])
                {
                    draw_list->AddCircleFilled (button_2_centre, button_radius, ButtonPressed, 32);
                }
                if (gamepad [0].state[GAMEPAD_BUTTON_START])
                {
                    /* Nothing to highlight for the pause button yet */
                }
            }

            /* Button outlines & labels */
            ImVec2 text_size;
            draw_list->AddCircle (button_1_centre, button_radius, White, 32);
            text_size = ImGui::CalcTextSize ("1");
            draw_list->AddText (ImVec2 (button_1_centre.x - text_size.x / 2, button_1_centre.y - text_size.y / 2), Grey_50, "1");
            draw_list->AddCircle (button_2_centre, button_radius, White, 32);
            text_size = ImGui::CalcTextSize ("2");
            draw_list->AddText (ImVec2 (button_2_centre.x - text_size.x / 2, button_2_centre.y - text_size.y / 2), Grey_50, "2");

            /* Dpad outline */
            draw_list->AddRect (ImVec2 (dpad_centre.x - dpad_width_r, dpad_centre.y - dpad_width_r),
                                ImVec2 (dpad_centre.x + dpad_width_r, dpad_centre.y + dpad_width_r), White, dpad_rounding);

            /* Dpad detail */
            draw_list->AddRect (ImVec2 (dpad_centre.x - dpad_bump_width_r, dpad_centre.y - dpad_bump_r - dpad_bump_length_r),
                                ImVec2 (dpad_centre.x + dpad_bump_width_r, dpad_centre.y - dpad_bump_r + dpad_bump_length_r), Grey_50, dpad_bump_rounding);
            draw_list->AddRect (ImVec2 (dpad_centre.x - dpad_bump_width_r, dpad_centre.y + dpad_bump_r - dpad_bump_length_r),
                                ImVec2 (dpad_centre.x + dpad_bump_width_r, dpad_centre.y + dpad_bump_r + dpad_bump_length_r), Grey_50, dpad_bump_rounding);
            draw_list->AddRect (ImVec2 (dpad_centre.x - dpad_bump_r - dpad_bump_length_r, dpad_centre.y - dpad_bump_width_r),
                                ImVec2 (dpad_centre.x - dpad_bump_r + dpad_bump_length_r, dpad_centre.y + dpad_bump_width_r), Grey_50, dpad_bump_rounding);
            draw_list->AddRect (ImVec2 (dpad_centre.x + dpad_bump_r - dpad_bump_length_r, dpad_centre.y - dpad_bump_width_r),
                                ImVec2 (dpad_centre.x + dpad_bump_r + dpad_bump_length_r, dpad_centre.y + dpad_bump_width_r), Grey_50, dpad_bump_rounding);
            draw_list->AddCircle (ImVec2 (dpad_centre.x, dpad_centre.y), dpad_circle_r, Grey_50, 32);

            /* Move cursor to below gamepad diagram */
            ImGui::SetCursorScreenPos (ImVec2 (origin.x - 10, origin.y + scale * 0.4 + 16));
            ImGui::TextColored ((gamepad_remap_step == GAMEPAD_DIRECTION_UP )     ? ButtonWaiting_V : White_V,
                                "  Up:        %s", button_mapping_to_string (remap_config.mapping [GAMEPAD_DIRECTION_UP]));
            ImGui::TextColored ((gamepad_remap_step == GAMEPAD_DIRECTION_DOWN )   ? ButtonWaiting_V : White_V,
                                "  Down:      %s", button_mapping_to_string (remap_config.mapping [GAMEPAD_DIRECTION_DOWN]));
            ImGui::TextColored ((gamepad_remap_step == GAMEPAD_DIRECTION_LEFT )   ? ButtonWaiting_V : White_V,
                                "  Left:      %s", button_mapping_to_string (remap_config.mapping [GAMEPAD_DIRECTION_LEFT]));
            ImGui::TextColored ((gamepad_remap_step == GAMEPAD_DIRECTION_RIGHT )  ? ButtonWaiting_V : White_V,
                                "  Right:     %s", button_mapping_to_string (remap_config.mapping [GAMEPAD_DIRECTION_RIGHT]));

            ImGui::SetCursorScreenPos (ImVec2 (origin.x + scale * 0.5, origin.y + scale * 0.4 + 16));
            ImGui::TextColored ((gamepad_remap_step == GAMEPAD_BUTTON_1 )         ? ButtonWaiting_V : White_V,
                                "  Button 1:  %s", button_mapping_to_string (remap_config.mapping [GAMEPAD_BUTTON_1]));
            ImGui::SetCursorScreenPos (ImVec2 (origin.x + scale * 0.5, origin.y + scale * 0.4 + 33));
            ImGui::TextColored ((gamepad_remap_step == GAMEPAD_BUTTON_2 )         ? ButtonWaiting_V : White_V,
                                "  Button 2:  %s", button_mapping_to_string (remap_config.mapping [GAMEPAD_BUTTON_2]));
            ImGui::SetCursorScreenPos (ImVec2 (origin.x + scale * 0.5, origin.y + scale * 0.4 + 50));
            ImGui::TextColored ((gamepad_remap_step == GAMEPAD_BUTTON_START )     ? ButtonWaiting_V : White_V,
                                "  Pause:     %s", button_mapping_to_string (remap_config.mapping [GAMEPAD_BUTTON_START]));
        }

        ImGui::EndChild ();

        /* Buttons */
        ImGui::Spacing ();
        ImGui::SameLine (ImGui::GetContentRegionAvail().x + 16 - 128 * 3);
        if (ImGui::Button ("Cancel", ImVec2 (120,0))) {

            /* TODO: Re-load the saved configuration */

            gamepad_remap_step = GAMEPAD_BUTTON_COUNT;
            gamepad_change_device (0, GAMEPAD_INDEX_NONE);
            config_capture_events = false;
            ImGui::CloseCurrentPopup ();

            snepulator_pause_set (false);
        }
        ImGui::SameLine ();
        if (ImGui::Button ("Remap", ImVec2 (120,0))) {
            remap_config.mapping [GAMEPAD_DIRECTION_UP].button       = SDLK_UNKNOWN;
            remap_config.mapping [GAMEPAD_DIRECTION_DOWN].button     = SDLK_UNKNOWN;
            remap_config.mapping [GAMEPAD_DIRECTION_LEFT].button     = SDLK_UNKNOWN;
            remap_config.mapping [GAMEPAD_DIRECTION_RIGHT].button    = SDLK_UNKNOWN;
            remap_config.mapping [GAMEPAD_BUTTON_1].button           = SDLK_UNKNOWN;
            remap_config.mapping [GAMEPAD_BUTTON_2].button           = SDLK_UNKNOWN;
            remap_config.mapping [GAMEPAD_BUTTON_START].button       = SDLK_UNKNOWN;
            gamepad_remap_step = GAMEPAD_DIRECTION_UP;
        }
        ImGui::SameLine ();
        if (ImGui::Button ("OK", ImVec2 (120,0))) {
            gamepad_remap_step = GAMEPAD_BUTTON_COUNT;
            gamepad_change_device (0, GAMEPAD_INDEX_NONE);
            config_capture_events = false;
            ImGui::CloseCurrentPopup ();
            gamepad_config_export ();

            snepulator_pause_set (false);
        }

        ImGui::EndPopup ();
    }
}
