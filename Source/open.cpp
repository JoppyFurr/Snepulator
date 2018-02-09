#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>

#include <vector>

#include "SDL2/SDL.h"
#include "SDL2/SDL_joystick.h"
#include <GL/gl3w.h>
#include "../Libraries/imgui-1.49/imgui.h"
#include "../Libraries/imgui-1.49/examples/sdl_opengl3_example/imgui_impl_sdl_gl3.h"

extern "C" {
#include "snepulator.h"
}

/* Global state */
extern Snepulator snepulator;

void snepulator_render_open_modal (void)
{
    static float f;
    if (ImGui::BeginPopupModal ("Open ROM...", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        /* State */
        static bool cwd_cached = false;
        static char cwd_path_buf[240]; /* TODO: Can we make this dynamic? */
        static const char *cwd_path = NULL;
        static std::vector<char *> file_list;
        static int selected_file = 0;

        if (!cwd_cached)
        {
            /* Current working directory string */
            cwd_path = getcwd (cwd_path_buf, 240);
            if (cwd_path == NULL)
            {
                cwd_path = "getcwd () -> NULL";
            }

            /* File listing */
            DIR *dir = opendir (".");
            struct dirent *entry = NULL;

            if (dir != NULL)
            {
                for (entry = readdir (dir); entry != NULL; entry = readdir (dir))
                {
                    file_list.push_back (strdup (entry->d_name));
                }
            }

            cwd_cached = true;

#if 0
            /* Only upon changing directories, unset cwd_cached and free the listing */
            for (int i = 0; i < file_list.size(); i++)
            {
                free ((void *) file_list[i]);
            }
#endif
        }

        ImGui::Text ("%s", cwd_path);

        /* Placeholder - Configured ROM Directories */
        ImGui::BeginChild ("Directories", ImVec2 (150, 400), true);
            ImGui::Button ("Master System", ImVec2 (ImGui::GetContentRegionAvailWidth (), 24));
            ImGui::Button ("SG-1000",       ImVec2 (ImGui::GetContentRegionAvailWidth (), 24));
        ImGui::EndChild ();

        ImGui::SameLine ();

        /* Current directory contents */
        ImGui::BeginChild ("Files", ImVec2 (350, 400), true);
        for (int i = 0; i < file_list.size() ; i++)
        {
            /* TODO: Can we get the text height rather than hard-coding it? */
            ImVec2 draw_cursor = ImGui::GetCursorScreenPos ();
            bool hovering = ImGui::IsMouseHoveringRect (draw_cursor, ImVec2 (draw_cursor.x + 350, draw_cursor.y + 16));

            if (hovering && ImGui::IsMouseClicked(0))
            {
                selected_file = i;
            }

            /* Render the selected file in green, hovered file in yellow, and others with the text default */
            if (i == selected_file)
            {
                ImGui::TextColored (ImVec4 (0.5f, 1.0f, 0.5f, 1.0f), "%s", file_list[i]);
            }
            else if (hovering)
            {
                ImGui::TextColored (ImVec4 (1.0f, 1.0f, 0.5f, 1.0f), "%s", file_list[i]);
            }
            else
            {
                ImGui::Text ("%s", file_list[i]);
            }

        }
        ImGui::EndChild ();

        /* Buttons */
        if (ImGui::Button ("Cancel", ImVec2 (120,0))) {
            snepulator.running = true;
            ImGui::CloseCurrentPopup ();
        }
        ImGui::SameLine ();
        if (ImGui::Button ("Open", ImVec2(120,0))) {
            snepulator.running = true;
            ImGui::CloseCurrentPopup ();
        }

        ImGui::EndPopup ();
    }
}
