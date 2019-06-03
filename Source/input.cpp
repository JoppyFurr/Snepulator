
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

#include "SDL2/SDL.h"
#include "SDL2/SDL_joystick.h"
#include <GL/gl3w.h>
#include "../Libraries/imgui-1.49/imgui.h"
#include "../Libraries/imgui-1.49/examples/sdl_opengl3_example/imgui_impl_sdl_gl3.h"

extern "C" {
#include "snepulator.h"
#include "sms.h"
extern bool config_capture_events;
}

typedef enum Remap_State_t {
    REMAP_STATE_DEFAULT,
    REMAP_STATE_UP,
    REMAP_STATE_DOWN,
    REMAP_STATE_LEFT,
    REMAP_STATE_RIGHT,
    REMAP_STATE_BUTTON_1,
    REMAP_STATE_BUTTON_2,
    REMAP_STATE_PAUSE,
} Remap_State;

/* Global state */
Gamepad_Mapping map_to_edit;
Remap_State remap_state = REMAP_STATE_DEFAULT;

extern Snepulator snepulator;
extern Gamepad_Mapping player_1_mapping;

/* Returns true when an event is consumed */
bool input_modal_consume_event (SDL_Event event)
{
    if (remap_state == REMAP_STATE_DEFAULT)
    {
        /* Eventually it will be nice to have a joystick-test mode here */
        return false;
    }

    /* For now, hard-code that we are only interested in the keyboard */
    if (map_to_edit.device_id == ID_KEYBOARD && event.type == SDL_KEYDOWN)
    {
        switch (remap_state)
        {
            case REMAP_STATE_UP:       map_to_edit.direction_up.value    = event.key.keysym.sym; remap_state = REMAP_STATE_DOWN;     break;
            case REMAP_STATE_DOWN:     map_to_edit.direction_down.value  = event.key.keysym.sym; remap_state = REMAP_STATE_LEFT;     break;
            case REMAP_STATE_LEFT:     map_to_edit.direction_left.value  = event.key.keysym.sym; remap_state = REMAP_STATE_RIGHT;    break;
            case REMAP_STATE_RIGHT:    map_to_edit.direction_right.value = event.key.keysym.sym; remap_state = REMAP_STATE_BUTTON_1; break;
            case REMAP_STATE_BUTTON_1: map_to_edit.button_1.value        = event.key.keysym.sym; remap_state = REMAP_STATE_BUTTON_2; break;
            case REMAP_STATE_BUTTON_2: map_to_edit.button_2.value        = event.key.keysym.sym; remap_state = REMAP_STATE_PAUSE;    break;
            case REMAP_STATE_PAUSE:    map_to_edit.pause.value           = event.key.keysym.sym; remap_state = REMAP_STATE_DEFAULT;  break;
            default: return false;
        }
        return true;
    }

    else if (event.type == SDL_JOYBUTTONDOWN && event.jbutton.which == map_to_edit.device_id)
    {
        switch (remap_state)
        {
            case REMAP_STATE_UP:       map_to_edit.direction_up.type     = event.type;
                                       map_to_edit.direction_up.value    = event.jbutton.button;
                                       remap_state = REMAP_STATE_DOWN;          break;
            case REMAP_STATE_DOWN:     map_to_edit.direction_down.type   = event.type;
                                       map_to_edit.direction_down.value  = event.jbutton.button;
                                       remap_state = REMAP_STATE_LEFT;          break;
            case REMAP_STATE_LEFT:     map_to_edit.direction_left.type   = event.type;
                                       map_to_edit.direction_left.value  = event.jbutton.button;
                                       remap_state = REMAP_STATE_RIGHT;         break;
            case REMAP_STATE_RIGHT:    map_to_edit.direction_right.type  = event.type;
                                       map_to_edit.direction_right.value = event.jbutton.button;
                                       remap_state = REMAP_STATE_BUTTON_1;      break;
            case REMAP_STATE_BUTTON_1: map_to_edit.button_1.type         = event.type;
                                       map_to_edit.button_1.value        = event.jbutton.button;
                                       remap_state = REMAP_STATE_BUTTON_2;      break;
            case REMAP_STATE_BUTTON_2: map_to_edit.button_2.type         = event.type;
                                       map_to_edit.button_2.value        = event.jbutton.button;
                                       remap_state = REMAP_STATE_PAUSE;         break;
            case REMAP_STATE_PAUSE:    map_to_edit.pause.type            = event.type;
                                       map_to_edit.pause.value           = event.jbutton.button;
                                       remap_state = REMAP_STATE_DEFAULT;       break;
            default: return false;
        }
        return true;
    }

    else if (event.type == SDL_JOYAXISMOTION && event.jaxis.which == map_to_edit.device_id)
    {
        if (event.jaxis.value > -1000 && event.jaxis.value < 1000)
        {
            return false;
        }

        bool negative = event.jaxis.value < 0;

        switch (remap_state)
        {
            case REMAP_STATE_UP:       map_to_edit.direction_up.type        = event.type;
                                       map_to_edit.direction_up.value       = event.jaxis.axis;
                                       map_to_edit.direction_up.negative    = negative;
                                       remap_state = REMAP_STATE_DOWN;      break;
            case REMAP_STATE_DOWN:     map_to_edit.direction_down.type      = event.type;
                                       map_to_edit.direction_down.value     = event.jaxis.axis;
                                       map_to_edit.direction_down.negative  = negative;
                                       remap_state = REMAP_STATE_LEFT;      break;
            case REMAP_STATE_LEFT:     map_to_edit.direction_left.type      = event.type;
                                       map_to_edit.direction_left.value     = event.jaxis.axis;
                                       map_to_edit.direction_left.negative  = negative;
                                       remap_state = REMAP_STATE_RIGHT;     break;
            case REMAP_STATE_RIGHT:    map_to_edit.direction_right.type     = event.type;
                                       map_to_edit.direction_right.value    = event.jaxis.axis;
                                       map_to_edit.direction_right.negative = negative;
                                       remap_state = REMAP_STATE_BUTTON_1;  break;
            case REMAP_STATE_BUTTON_1: map_to_edit.button_1.type            = event.type;
                                       map_to_edit.button_1.value           = event.jaxis.axis;
                                       map_to_edit.button_1.negative        = negative;
                                       remap_state = REMAP_STATE_BUTTON_2;  break;
            case REMAP_STATE_BUTTON_2: map_to_edit.button_2.type            = event.type;
                                       map_to_edit.button_2.value           = event.jaxis.axis;
                                       map_to_edit.button_2.negative        = negative;
                                       remap_state = REMAP_STATE_PAUSE;     break;
            case REMAP_STATE_PAUSE:    map_to_edit.pause.type               = event.type;
                                       map_to_edit.pause.value              = event.jaxis.axis;
                                       map_to_edit.pause.negative           = negative;
                                       remap_state = REMAP_STATE_DEFAULT;   break;
            default: return false;
        }
        return true;
    }

    return false;
}

const char *button_mapping_to_string (Button_Mapping b)
{
    static char *buff = NULL;

    if (buff == NULL)
    {
        buff = (char *) malloc (80);
    }

    switch (b.type)
    {
        case SDL_KEYDOWN:
            return SDL_GetKeyName (b.value);
        case SDL_JOYAXISMOTION:
            sprintf (buff, "Axis %d %s", b.value, b.negative ? "-" : "+");
            break;
        case SDL_JOYBUTTONDOWN:
            sprintf (buff, "Button %d", b.value);
            break;
        default:
            return "Unknown";
    }
    return buff;
}

/* TODO: It would be nice to re-centre upon window resizing */
void snepulator_render_input_modal (void)
{
    if (ImGui::BeginPopupModal ("Configure input...", NULL, ImGuiWindowFlags_AlwaysAutoResize |
                                                            ImGuiWindowFlags_NoMove |
                                                            ImGuiWindowFlags_NoScrollbar))
    {
        int width = 512; /* TODO: Something more responsive */
        int height = 384;

        /* TODO: Show name of the device we're configuring */
        /* Master System gamepad */
        ImGui::BeginChild ("SMS Gamepad", ImVec2 (width, height - 64), true);
        {
            ImVec2 origin = ImGui::GetCursorScreenPos ();
            float scale = width * 0.9;
            origin.x += 10;
            origin.y += 10;

            ImDrawList* draw_list  = ImGui::GetWindowDrawList ();
            ImVec4 White_V         = ImVec4 (1.0f, 1.0f, 1.0f, 1.0f);
            ImVec4 Dark_V          = ImVec4 (0.1f, 0.1f, 0.1f, 1.0f);
            ImVec4 ButtonDefault_V = ImVec4 (0.2f, 0.2f, 0.2f, 1.0f);
            ImVec4 ButtonWaiting_V = ImVec4 (0.8f, 0.5f, 0.1f, 1.0f);
            ImVec4 ButtonPressed_V = ImVec4 (0.8f, 0.5f, 0.1f, 1.0f);

            const ImU32 White         = ImColor (White_V);
            const ImU32 Dark          = ImColor (Dark_V);
            const ImU32 ButtonDefault = ImColor (ButtonDefault_V);
            const ImU32 ButtonWaiting = ImColor (ButtonWaiting_V);
            const ImU32 ButtonPressed = ImColor (ButtonPressed_V);

            /* Gamepad */
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

            /* D-pad */
            draw_list->AddRectFilled (
                ImVec2 (origin.x + scale * 0.16, origin.y + scale * 0.08),
                ImVec2 (origin.x + scale * 0.38, origin.y + scale * 0.32), ButtonDefault, scale * 0.06);

            if (remap_state == REMAP_STATE_UP)
            {
                draw_list->AddRectFilled (
                    ImVec2 (origin.x + scale * 0.16, origin.y + scale * 0.08),
                    ImVec2 (origin.x + scale * 0.38, origin.y + scale * 0.20), ButtonWaiting, scale * 0.06);
            }
            else if (remap_state == REMAP_STATE_DOWN)
            {
                draw_list->AddRectFilled (
                    ImVec2 (origin.x + scale * 0.16, origin.y + scale * 0.20),
                    ImVec2 (origin.x + scale * 0.38, origin.y + scale * 0.32), ButtonWaiting, scale * 0.06);
            }
            else if (remap_state == REMAP_STATE_LEFT)
            {
                draw_list->AddRectFilled (
                    ImVec2 (origin.x + scale * 0.16, origin.y + scale * 0.08),
                    ImVec2 (origin.x + scale * 0.27, origin.y + scale * 0.32), ButtonWaiting, scale * 0.06);
            }
            else if (remap_state == REMAP_STATE_RIGHT)
            {
                draw_list->AddRectFilled (
                    ImVec2 (origin.x + scale * 0.27, origin.y + scale * 0.08),
                    ImVec2 (origin.x + scale * 0.38, origin.y + scale * 0.32), ButtonWaiting, scale * 0.06);
            }

            draw_list->AddRect (
                ImVec2 (origin.x + scale * 0.16, origin.y + scale * 0.08),
                ImVec2 (origin.x + scale * 0.38, origin.y + scale * 0.32), White, scale * 0.06);

            /* Button 1 */
            draw_list->AddCircleFilled (
                ImVec2 ( origin.x + scale * 0.70, origin.y + scale * 0.25), scale * 0.06,
                (remap_state == REMAP_STATE_BUTTON_1) ? ButtonWaiting : ButtonDefault, 32);
            draw_list->AddCircle (
                ImVec2 (origin.x + scale * 0.70, origin.y + scale * 0.25), scale * 0.06, White, 32);

            /* Button 2 */
            draw_list->AddCircleFilled (
                ImVec2 (origin.x + scale * 0.87, origin.y + scale * 0.25), scale * 0.06,
                (remap_state == REMAP_STATE_BUTTON_2) ? ButtonWaiting : ButtonDefault, 32);
            draw_list->AddCircle (
                ImVec2 (origin.x + scale * 0.87, origin.y + scale * 0.25), scale * 0.06, White, 32);

            /* Move cursor to below gamepad diagram */
            ImGui::SetCursorScreenPos (ImVec2 (origin.x - 10, origin.y + scale * 0.4 + 16));
            ImGui::TextColored (
                (remap_state == REMAP_STATE_UP )       ? ButtonWaiting_V : White_V,
                "  Up:        %s", button_mapping_to_string (map_to_edit.direction_up));
            ImGui::TextColored (
                (remap_state == REMAP_STATE_DOWN )     ? ButtonWaiting_V : White_V,
                "  Down:      %s", button_mapping_to_string (map_to_edit.direction_down));
            ImGui::TextColored (
                (remap_state == REMAP_STATE_LEFT )     ? ButtonWaiting_V : White_V,
                "  Left:      %s", button_mapping_to_string (map_to_edit.direction_left));
            ImGui::TextColored (
                (remap_state == REMAP_STATE_RIGHT )    ? ButtonWaiting_V : White_V,
                "  Right:     %s", button_mapping_to_string (map_to_edit.direction_right));

            ImGui::SetCursorScreenPos (ImVec2 (origin.x + scale * 0.5, origin.y + scale * 0.4 + 16));
            ImGui::TextColored (
                (remap_state == REMAP_STATE_BUTTON_1 ) ? ButtonWaiting_V : White_V,
                "  Button 1:  %s", button_mapping_to_string (map_to_edit.button_1));
            ImGui::SetCursorScreenPos (ImVec2 (origin.x + scale * 0.5, origin.y + scale * 0.4 + 33));
            ImGui::TextColored (
                (remap_state == REMAP_STATE_BUTTON_2 ) ? ButtonWaiting_V : White_V,
                "  Button 2:  %s", button_mapping_to_string (map_to_edit.button_2));
            ImGui::SetCursorScreenPos (ImVec2 (origin.x + scale * 0.5, origin.y + scale * 0.4 + 50));
            ImGui::TextColored (
                (remap_state == REMAP_STATE_PAUSE )    ? ButtonWaiting_V : White_V,
                "  Pause:     %s", button_mapping_to_string (map_to_edit.pause));
        }

        ImGui::EndChild ();

        /* Buttons */
        if (ImGui::Button ("Cancel", ImVec2 (120,0))) {
            remap_state = REMAP_STATE_DEFAULT;
            snepulator.running = true;
            config_capture_events = false;
            ImGui::CloseCurrentPopup ();
        }
        ImGui::SameLine ();
        if (ImGui::Button ("Remap Device", ImVec2 (120,0))) {
            map_to_edit.direction_up.value    = SDLK_UNKNOWN;
            map_to_edit.direction_down.value  = SDLK_UNKNOWN;
            map_to_edit.direction_left.value  = SDLK_UNKNOWN;
            map_to_edit.direction_right.value = SDLK_UNKNOWN;
            map_to_edit.button_1.value        = SDLK_UNKNOWN;
            map_to_edit.button_2.value        = SDLK_UNKNOWN;
            map_to_edit.pause.value           = SDLK_UNKNOWN;
            remap_state = REMAP_STATE_UP;
        }
        ImGui::SameLine ();
        if (ImGui::Button ("OK", ImVec2 (120,0))) {
            /* Store and use the new configuration */
            snepulator_update_input_device (map_to_edit);
            player_1_mapping = map_to_edit;

            snepulator.running = true; /* TODO: Rather than going to "Running", restore to what the state was previously */

            remap_state = REMAP_STATE_DEFAULT;
            config_capture_events = false;
            ImGui::CloseCurrentPopup ();
        }

        ImGui::EndPopup ();
    }
}
