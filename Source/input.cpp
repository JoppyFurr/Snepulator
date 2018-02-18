
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
}

/* Global state */
extern Snepulator snepulator;
extern Gamepad_Mapping test_keyboard;

void snepulator_render_input_modal (void)
{
    if (ImGui::BeginPopupModal ("Configure input...", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        /* Master System gamepad */
        ImGui::BeginChild ("SMS Gamepad", ImVec2 (450, 400), true);
        {
            ImVec2 origin = ImGui::GetCursorScreenPos ();
            float scale = 400;
            origin.x += 10;
            origin.y += 10;

            ImDrawList* draw_list = ImGui::GetWindowDrawList ();
            const ImU32 White      = ImColor (ImVec4 (1.0f, 1.0f, 1.0f, 1.0f));
            const ImU32 Dark       = ImColor (ImVec4 (0.1f, 0.1f, 0.1f, 1.0f));
            const ImU32 Unselected = ImColor (ImVec4 (0.2f, 0.2f, 0.2f, 1.0f));
            const ImU32 Prompt     = ImColor (ImVec4 (0.2f, 0.5f, 0.2f, 1.0f));
            const ImU32 Pressed    = ImColor (ImVec4 (0.5f, 0.2f, 0.2f, 1.0f));

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
                ImVec2 (origin.x + scale * 0.38, origin.y + scale * 0.32), Unselected, scale * 0.06);
            draw_list->AddRect (
                ImVec2 (origin.x + scale * 0.16, origin.y + scale * 0.08),
                ImVec2 (origin.x + scale * 0.38, origin.y + scale * 0.32), White, scale * 0.06);

            /* Buttons */
            draw_list->AddCircleFilled (
                ImVec2 ( origin.x + scale * 0.70, origin.y + scale * 0.25), scale * 0.06, Unselected, 32);
            draw_list->AddCircle (
                ImVec2 (origin.x + scale * 0.70, origin.y + scale * 0.25), scale * 0.06, White, 32);
            draw_list->AddCircleFilled (
                ImVec2 (origin.x + scale * 0.87, origin.y + scale * 0.25), scale * 0.06, Unselected, 32);
            draw_list->AddCircle (
                ImVec2 (origin.x + scale * 0.87, origin.y + scale * 0.25), scale * 0.06, White, 32);

            /* Move cursor to below gamepad diagram */
            ImGui::SetCursorScreenPos (ImVec2 (origin.x - 10, origin.y + scale * 0.5));

            ImGui::Text ("  Up:        %s", SDL_GetKeyName (test_keyboard.direction_up.value));
            ImGui::Text ("  Down:      %s", SDL_GetKeyName (test_keyboard.direction_down.value));
            ImGui::Text ("  Left:      %s", SDL_GetKeyName (test_keyboard.direction_left.value));
            ImGui::Text ("  Right:     %s", SDL_GetKeyName (test_keyboard.direction_right.value));
            ImGui::Text ("  Button 1:  %s", SDL_GetKeyName (test_keyboard.button_1.value));
            ImGui::Text ("  Button 2:  %s", SDL_GetKeyName (test_keyboard.button_2.value));
            ImGui::Text ("  Pause:     %s", SDL_GetKeyName (test_keyboard.pause.value));
        }

        ImGui::EndChild ();

        /* Buttons */
        if (ImGui::Button ("Cancel", ImVec2 (120,0))) {
            snepulator.running = true;
            ImGui::CloseCurrentPopup ();
        }
        ImGui::SameLine ();
        if (ImGui::Button ("Remap Device", ImVec2 (120,0))) {
            test_keyboard.direction_up.value    = SDLK_UNKNOWN;
            test_keyboard.direction_down.value  = SDLK_UNKNOWN;
            test_keyboard.direction_left.value  = SDLK_UNKNOWN;
            test_keyboard.direction_right.value = SDLK_UNKNOWN;
            test_keyboard.button_1.value        = SDLK_UNKNOWN;
            test_keyboard.button_2.value        = SDLK_UNKNOWN;
            test_keyboard.pause.value           = SDLK_UNKNOWN;
        }
        ImGui::SameLine ();
        if (ImGui::Button ("Apply", ImVec2(120,0))) {
            snepulator.running = true; /* TODO: Rather than going to "Running", restore to what the state was previously */
            ImGui::CloseCurrentPopup ();
        }

        ImGui::EndPopup ();
    }
}
