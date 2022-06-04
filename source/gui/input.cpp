/*
 * Input configuration modal
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

#include <GL/gl3w.h>
#include <SDL2/SDL.h>

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
            ImVec4 White_V         = ImVec4 (1.0f, 1.0f, 1.0f, 1.0f);
            ImVec4 Dark_V          = ImVec4 (0.1f, 0.1f, 0.1f, 1.0f);
            ImVec4 ButtonDefault_V = ImVec4 (0.2f, 0.2f, 0.2f, 1.0f);
            ImVec4 ButtonWaiting_V = ImVec4 (0.8f, 0.5f, 0.1f, 1.0f);
            ImVec4 ButtonPressed_V = ImVec4 (0.4f, 0.8f, 0.6f, 1.0f);

            const ImU32 White         = ImColor (White_V);
            const ImU32 Dark          = ImColor (Dark_V);
            const ImU32 ButtonDefault = ImColor (ButtonDefault_V);
            const ImU32 ButtonWaiting = ImColor (ButtonWaiting_V);
            const ImU32 ButtonPressed = ImColor (ButtonPressed_V);

            /* Gamepad shape */
            draw_list->AddRectFilled (
                ImVec2 (origin.x + scale * 0.0,  origin.y + scale * 0.0 ),
                ImVec2 (origin.x + scale * 1.0,  origin.y + scale * 0.4 ), Dark);
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

            /* Button backgrounds */
            draw_list->AddRectFilled   (ImVec2 (origin.x + scale * 0.16, origin.y + scale * 0.08),
                                        ImVec2 (origin.x + scale * 0.38, origin.y + scale * 0.32), ButtonDefault, scale * 0.06);
            draw_list->AddCircleFilled (ImVec2 (origin.x + scale * 0.70, origin.y + scale * 0.25), scale * 0.06, ButtonDefault, 32);
            draw_list->AddCircleFilled (ImVec2 (origin.x + scale * 0.87, origin.y + scale * 0.25), scale * 0.06, ButtonDefault, 32);

            /* Highlight the button to remap */
            if (gamepad_remap_step != GAMEPAD_BUTTON_COUNT)
            {
                switch (gamepad_remap_step)
                {
                    case GAMEPAD_DIRECTION_UP:
                        draw_list->AddRectFilled (ImVec2 (origin.x + scale * 0.16, origin.y + scale * 0.08),
                                                  ImVec2 (origin.x + scale * 0.38, origin.y + scale * 0.20), ButtonWaiting, scale * 0.06);
                        break;
                    case GAMEPAD_DIRECTION_DOWN:
                        draw_list->AddRectFilled (ImVec2 (origin.x + scale * 0.16, origin.y + scale * 0.20),
                                                  ImVec2 (origin.x + scale * 0.38, origin.y + scale * 0.32), ButtonWaiting, scale * 0.06);
                        break;
                    case GAMEPAD_DIRECTION_LEFT:
                        draw_list->AddRectFilled (ImVec2 (origin.x + scale * 0.16, origin.y + scale * 0.08),
                                                  ImVec2 (origin.x + scale * 0.27, origin.y + scale * 0.32), ButtonWaiting, scale * 0.06);
                        break;
                    case GAMEPAD_DIRECTION_RIGHT:
                        draw_list->AddRectFilled (ImVec2 (origin.x + scale * 0.27, origin.y + scale * 0.08),
                                                  ImVec2 (origin.x + scale * 0.38, origin.y + scale * 0.32), ButtonWaiting, scale * 0.06);
                        break;
                    case GAMEPAD_BUTTON_1:
                        draw_list->AddCircleFilled (ImVec2 ( origin.x + scale * 0.70, origin.y + scale * 0.25), scale * 0.06, ButtonWaiting, 32);
                        break;
                    case GAMEPAD_BUTTON_2:
                        draw_list->AddCircleFilled (ImVec2 (origin.x + scale * 0.87, origin.y + scale * 0.25), scale * 0.06, ButtonWaiting, 32);
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
                draw_list->AddRectFilled (ImVec2 (origin.x + scale * 0.16, origin.y + scale * 0.08),
                                          ImVec2 (origin.x + scale * 0.38, origin.y + scale * 0.20), ButtonPressed, scale * 0.06);
                }
                if (gamepad [0].state[GAMEPAD_DIRECTION_DOWN])
                {
                    draw_list->AddRectFilled (ImVec2 (origin.x + scale * 0.16, origin.y + scale * 0.20),
                                              ImVec2 (origin.x + scale * 0.38, origin.y + scale * 0.32), ButtonPressed, scale * 0.06);
                }
                if (gamepad [0].state[GAMEPAD_DIRECTION_LEFT])
                {
                    draw_list->AddRectFilled (ImVec2 (origin.x + scale * 0.16, origin.y + scale * 0.08),
                                              ImVec2 (origin.x + scale * 0.27, origin.y + scale * 0.32), ButtonPressed, scale * 0.06);
                }
                if (gamepad [0].state[GAMEPAD_DIRECTION_RIGHT])
                {
                    draw_list->AddRectFilled (ImVec2 (origin.x + scale * 0.27, origin.y + scale * 0.08),
                                              ImVec2 (origin.x + scale * 0.38, origin.y + scale * 0.32), ButtonPressed, scale * 0.06);
                }
                if (gamepad [0].state[GAMEPAD_BUTTON_1])
                {
                    draw_list->AddCircleFilled (ImVec2 ( origin.x + scale * 0.70, origin.y + scale * 0.25), scale * 0.06, ButtonPressed, 32);
                }
                if (gamepad [0].state[GAMEPAD_BUTTON_2])
                {
                    draw_list->AddCircleFilled (ImVec2 (origin.x + scale * 0.87, origin.y + scale * 0.25), scale * 0.06, ButtonPressed, 32);
                }
                if (gamepad [0].state[GAMEPAD_BUTTON_START])
                {
                    /* Nothing to highlight for the pause button yet */
                }
            }

            /* Button outlines */
            draw_list->AddRect   (ImVec2 (origin.x + scale * 0.16, origin.y + scale * 0.08),
                                  ImVec2 (origin.x + scale * 0.38, origin.y + scale * 0.32), White, scale * 0.06);
            draw_list->AddCircle (ImVec2 (origin.x + scale * 0.70, origin.y + scale * 0.25), scale * 0.06, White, 32);
            draw_list->AddCircle (ImVec2 (origin.x + scale * 0.87, origin.y + scale * 0.25), scale * 0.06, White, 32);

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
