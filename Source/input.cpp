
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
            const ImU32 White = ImColor (ImVec4 (1.0f, 1.0f, 1.0f, 1.0f));

            /* Gamepad */
            draw_list->AddRect (ImVec2 (origin.x + scale * 0.0,  origin.y + scale * 0.0 ),
                                ImVec2 (origin.x + scale * 1.0,  origin.y + scale * 0.4 ), White);
            draw_list->AddRect (ImVec2 (origin.x + scale * 0.0,  origin.y + scale * 0.0 ),
                                ImVec2 (origin.x + scale * 0.08, origin.y + scale * 0.4 ), White);
            draw_list->AddRect (ImVec2 (origin.x + scale * 0.48, origin.y + scale * 0.0 ),
                                ImVec2 (origin.x + scale * 1.0,  origin.y + scale * 0.12), White);
            draw_list->AddRect (ImVec2 (origin.x + scale * 0.48, origin.y + scale * 0.36),
                                ImVec2 (origin.x + scale * 1.0,  origin.y + scale * 0.4 ), White);

            /* D-pad */
            draw_list->AddRect (ImVec2 (origin.x + scale * 0.16, origin.y + scale * 0.08),
                                ImVec2 (origin.x + scale * 0.38, origin.y + scale * 0.32), White, scale * 0.06);

            /* Buttons */
            draw_list->AddCircle(ImVec2 (origin.x + scale * 0.70, origin.y + scale * 0.25), scale * 0.06, White, 32, 1);
            draw_list->AddCircle(ImVec2 (origin.x + scale * 0.87, origin.y + scale * 0.25), scale * 0.06, White, 32, 1);
        }

        ImGui::EndChild ();

        /* Buttons */
        if (ImGui::Button ("Cancel", ImVec2 (120,0))) {
            snepulator.running = true;
            ImGui::CloseCurrentPopup ();
        }
        ImGui::SameLine ();
        if (ImGui::Button ("Apply", ImVec2(120,0))) {
            snepulator.running = true; /* TODO: Rather than going to "Running", restore to what the state was previously */
            ImGui::CloseCurrentPopup ();
        }

        ImGui::EndPopup ();
    }
}
