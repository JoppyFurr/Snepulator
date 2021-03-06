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
#include "util.h"
#include "snepulator.h"
#include "gamepad.h"
extern bool config_capture_events;

extern Snepulator_State state;

extern Gamepad_Instance gamepad_list [128];
extern uint32_t gamepad_list_count;

extern Gamepad_Config *gamepad_3_config;
extern Snepulator_Gamepad gamepad_3;
extern Snepulator_Gamepad gamepad_1;
}

/* Global state */
Gamepad_Config map_to_edit;
uint32_t remap_button = GAMEPAD_BUTTON_COUNT;

static uint32_t input_combo_index = 0;

/*
 * Pass an event to the input modal.
 * Returns true when an event is consumed.
 *
 * TODO: Move input handling to gamepad.c
 */
bool input_modal_consume_event (SDL_Event event)
{
    bool consumed = false;

    if (remap_button >= GAMEPAD_BUTTON_COUNT)
    {
        consumed = false;
    }

    /* For now, hard-code that we are only interested in the keyboard */
    else if (event.type == SDL_KEYDOWN && gamepad_3.instance_id == INSTANCE_ID_KEYBOARD)
    {
        map_to_edit.mapping [remap_button].key = event.key.keysym.sym;
        remap_button++;
        consumed = true;
    }

    else if (event.type == SDL_JOYBUTTONDOWN && event.jbutton.which == gamepad_3.instance_id)
    {
        map_to_edit.mapping [remap_button].type = SDL_JOYBUTTONDOWN;
        map_to_edit.mapping [remap_button].button = event.jbutton.button;
        remap_button++;
        consumed = true;
    }

    else if (event.type == SDL_JOYHATMOTION && event.jhat.which == gamepad_3.instance_id)
    {
        /* If a hat is triggered, ignore all hat motion while it returns to the centre */
        static bool waiting = false;
        static uint8_t waiting_hat;

        if (waiting)
        {
            if (event.jhat.hat == waiting_hat && event.jhat.value == SDL_HAT_CENTERED)
            {
                waiting = false;
            }
        }
        else if (event.jhat.value == SDL_HAT_UP || event.jhat.value == SDL_HAT_DOWN ||
                 event.jhat.value == SDL_HAT_LEFT || event.jhat.value == SDL_HAT_RIGHT)
        {
            waiting = true;
            waiting_hat = event.jhat.hat;

            map_to_edit.mapping [remap_button].type = SDL_JOYHATMOTION;
            map_to_edit.mapping [remap_button].hat = event.jhat.hat;
            map_to_edit.mapping [remap_button].direction = event.jhat.value;
            remap_button++;
        }

        consumed = true;
    }

    else if (event.type == SDL_JOYAXISMOTION && event.jaxis.which == gamepad_3.instance_id)
    {
        /* If an axis is triggered, ignore all axis motions while it returns to the centre */
        static bool waiting = false;
        static uint32_t waiting_axis;

        if (waiting)
        {
            if (event.jaxis.axis == waiting_axis && event.jaxis.value < 4000 && event.jaxis.value > -4000)
            {
                waiting = false;
            }
        }
        else if (event.jaxis.value > 16000 || event.jaxis.value < -16000)
        {
            int32_t sign = (event.jaxis.value < 0) ? -1 : 1;

            waiting = true;
            waiting_axis = event.jaxis.axis;

            map_to_edit.mapping [remap_button].type = SDL_JOYAXISMOTION;
            map_to_edit.mapping [remap_button].axis = event.jaxis.axis;
            map_to_edit.mapping [remap_button].sign = sign;
            remap_button++;
        }

        consumed = true;
    }

    if (consumed == true && remap_button == GAMEPAD_BUTTON_COUNT)
    {
        /* Apply the new mapping */
        gamepad_update_mapping (map_to_edit);
    }

    return consumed;
}


/*
 * Open the input dialogue.
 */
void input_start (void)
{
    state.running = false;
    input_combo_index = 0;

    /* Initially selected device is player-1's device */
    for (uint32_t i = 0; i < gamepad_list_count; i++)
    {
        if (gamepad_list [i].instance_id == gamepad_1.instance_id)
        {
            gamepad_change_device (3, i);
            map_to_edit = *gamepad_3_config;
            remap_button = GAMEPAD_BUTTON_COUNT;
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
        case SDL_KEYDOWN:
            return SDL_GetKeyName (b.key);
        case SDL_JOYAXISMOTION:
            sprintf (buff, "Axis %d %s", b.axis, (b.sign < 0.0) ? "-" : "+");
            break;
        case SDL_JOYHATMOTION:
            sprintf (buff, "Hat %d %s", b.hat, (b.direction == SDL_HAT_UP)    ? "Up"
                                             : (b.direction == SDL_HAT_DOWN)  ? "Down"
                                             : (b.direction == SDL_HAT_LEFT)  ? "Left"
                                             : (b.direction == SDL_HAT_RIGHT) ? "Right"
                                             : "Unknown");
            break;
        case SDL_JOYBUTTONDOWN:
            sprintf (buff, "Button %d", b.button);
            break;
        default:
            return "Unknown";
    }
    return buff;
}


/*
 * Render the input configuration modal.
 */
void snepulator_render_input_modal (void)
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
            for (uint32_t i = 0; i < gamepad_list_count; i++)
            {
                if (ImGui::Selectable (gamepad_get_name (i), i == input_combo_index))
                {
                    gamepad_change_device (3, i);
                    map_to_edit = *gamepad_3_config;
                    remap_button = GAMEPAD_BUTTON_COUNT;
                    input_combo_index = i;
                }
                if (i == 0)
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
            if (remap_button != GAMEPAD_BUTTON_COUNT)
            {
                switch (remap_button)
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
                if (gamepad_3.state[GAMEPAD_DIRECTION_UP])
                {
                draw_list->AddRectFilled (ImVec2 (origin.x + scale * 0.16, origin.y + scale * 0.08),
                                          ImVec2 (origin.x + scale * 0.38, origin.y + scale * 0.20), ButtonPressed, scale * 0.06);
                }
                if (gamepad_3.state[GAMEPAD_DIRECTION_DOWN])
                {
                    draw_list->AddRectFilled (ImVec2 (origin.x + scale * 0.16, origin.y + scale * 0.20),
                                              ImVec2 (origin.x + scale * 0.38, origin.y + scale * 0.32), ButtonPressed, scale * 0.06);
                }
                if (gamepad_3.state[GAMEPAD_DIRECTION_LEFT])
                {
                    draw_list->AddRectFilled (ImVec2 (origin.x + scale * 0.16, origin.y + scale * 0.08),
                                              ImVec2 (origin.x + scale * 0.27, origin.y + scale * 0.32), ButtonPressed, scale * 0.06);
                }
                if (gamepad_3.state[GAMEPAD_DIRECTION_RIGHT])
                {
                    draw_list->AddRectFilled (ImVec2 (origin.x + scale * 0.27, origin.y + scale * 0.08),
                                              ImVec2 (origin.x + scale * 0.38, origin.y + scale * 0.32), ButtonPressed, scale * 0.06);
                }
                if (gamepad_3.state[GAMEPAD_BUTTON_1])
                {
                    draw_list->AddCircleFilled (ImVec2 ( origin.x + scale * 0.70, origin.y + scale * 0.25), scale * 0.06, ButtonPressed, 32);
                }
                if (gamepad_3.state[GAMEPAD_BUTTON_2])
                {
                    draw_list->AddCircleFilled (ImVec2 (origin.x + scale * 0.87, origin.y + scale * 0.25), scale * 0.06, ButtonPressed, 32);
                }
                if (gamepad_3.state[GAMEPAD_BUTTON_START])
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
            ImGui::TextColored ((remap_button == GAMEPAD_DIRECTION_UP )     ? ButtonWaiting_V : White_V,
                                "  Up:        %s", button_mapping_to_string (map_to_edit.mapping [GAMEPAD_DIRECTION_UP]));
            ImGui::TextColored ((remap_button == GAMEPAD_DIRECTION_DOWN )   ? ButtonWaiting_V : White_V,
                                "  Down:      %s", button_mapping_to_string (map_to_edit.mapping [GAMEPAD_DIRECTION_DOWN]));
            ImGui::TextColored ((remap_button == GAMEPAD_DIRECTION_LEFT )   ? ButtonWaiting_V : White_V,
                                "  Left:      %s", button_mapping_to_string (map_to_edit.mapping [GAMEPAD_DIRECTION_LEFT]));
            ImGui::TextColored ((remap_button == GAMEPAD_DIRECTION_RIGHT )  ? ButtonWaiting_V : White_V,
                                "  Right:     %s", button_mapping_to_string (map_to_edit.mapping [GAMEPAD_DIRECTION_RIGHT]));

            ImGui::SetCursorScreenPos (ImVec2 (origin.x + scale * 0.5, origin.y + scale * 0.4 + 16));
            ImGui::TextColored ((remap_button == GAMEPAD_BUTTON_1 )         ? ButtonWaiting_V : White_V,
                                "  Button 1:  %s", button_mapping_to_string (map_to_edit.mapping [GAMEPAD_BUTTON_1]));
            ImGui::SetCursorScreenPos (ImVec2 (origin.x + scale * 0.5, origin.y + scale * 0.4 + 33));
            ImGui::TextColored ((remap_button == GAMEPAD_BUTTON_2 )         ? ButtonWaiting_V : White_V,
                                "  Button 2:  %s", button_mapping_to_string (map_to_edit.mapping [GAMEPAD_BUTTON_2]));
            ImGui::SetCursorScreenPos (ImVec2 (origin.x + scale * 0.5, origin.y + scale * 0.4 + 50));
            ImGui::TextColored ((remap_button == GAMEPAD_BUTTON_START )     ? ButtonWaiting_V : White_V,
                                "  Pause:     %s", button_mapping_to_string (map_to_edit.mapping [GAMEPAD_BUTTON_START]));
        }

        ImGui::EndChild ();

        /* Buttons */
        ImGui::Spacing ();
        ImGui::SameLine (ImGui::GetContentRegionAvail().x + 16 - 128 * 3);
        if (ImGui::Button ("Cancel", ImVec2 (120,0))) {

            /* TODO: Restore the saved configuration */

            remap_button = GAMEPAD_BUTTON_COUNT;
            gamepad_change_device (3, GAMEPAD_INDEX_NONE);
            if (state.ready)
            {
                state.running = true;
            }
            config_capture_events = false;
            ImGui::CloseCurrentPopup ();
        }
        ImGui::SameLine ();
        if (ImGui::Button ("Remap", ImVec2 (120,0))) {
            map_to_edit.mapping [GAMEPAD_DIRECTION_UP].button       = SDLK_UNKNOWN;
            map_to_edit.mapping [GAMEPAD_DIRECTION_DOWN].button     = SDLK_UNKNOWN;
            map_to_edit.mapping [GAMEPAD_DIRECTION_LEFT].button     = SDLK_UNKNOWN;
            map_to_edit.mapping [GAMEPAD_DIRECTION_RIGHT].button    = SDLK_UNKNOWN;
            map_to_edit.mapping [GAMEPAD_BUTTON_1].button           = SDLK_UNKNOWN;
            map_to_edit.mapping [GAMEPAD_BUTTON_2].button           = SDLK_UNKNOWN;
            map_to_edit.mapping [GAMEPAD_BUTTON_START].button       = SDLK_UNKNOWN;
            remap_button = GAMEPAD_DIRECTION_UP;
        }
        ImGui::SameLine ();
        if (ImGui::Button ("OK", ImVec2 (120,0))) {

            if (state.ready)
            {
                /* TODO: Rather than going to "Running", restore to what the state was previously */
                state.running = true;
            }

            remap_button = GAMEPAD_BUTTON_COUNT;
            gamepad_change_device (3, GAMEPAD_INDEX_NONE);
            config_capture_events = false;
            ImGui::CloseCurrentPopup ();

            gamepad_config_export ();
        }

        ImGui::EndPopup ();
    }
}
