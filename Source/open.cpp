#include <dirent.h>
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

void snepulator_render_open_modal (void)
{
    if (ImGui::BeginPopupModal ("Open ROM...", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        /* State */
        static bool cwd_cached = false;
        static char cwd_path_buf[240]; /* TODO: Can we make this dynamic? */
        static const char *cwd_path = NULL;
        static std::vector<std::string> file_list;
        static int selected_file = 0;
        bool open_action = false;

        std::regex sms_regex(".*\\.(BIN|bin|SMS|sms)$");

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
            std::string entry_string;

            if (dir != NULL)
            {
                for (entry = readdir (dir); entry != NULL; entry = readdir (dir))
                {
                    if (!strcmp (".", entry->d_name))
                    {
                        continue;
                    }

                    entry_string = std::string (entry->d_name);

                    /* Show directories as ending with '/' */
                    if (entry->d_type == DT_DIR)
                    {
                        entry_string += '/';
                    }
                    /* Only list files that have a valid ROM extension */
                    else if (!std::regex_match(entry_string, sms_regex))
                    {
                            continue;
                    }

                    file_list.push_back (entry_string);
                }
                std::sort (file_list.begin(), file_list.end());
            }

            cwd_cached = true;
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
        for (int i = 0; i < file_list.size (); i++)
        {
            /* TODO: Can we get the text height rather than hard-coding it? */
            ImVec2 draw_cursor = ImGui::GetCursorScreenPos ();
            bool hovering = ImGui::IsMouseHoveringRect (draw_cursor, ImVec2 (draw_cursor.x + 350, draw_cursor.y + 16));

            /* Click to select */
            if (hovering && ImGui::IsMouseClicked(0))
            {
                selected_file = i;
            }

            /* Double-click to open */
            if (hovering && ImGui::IsMouseDoubleClicked(0))
            {
                open_action = true;
            }

            /* Render the selected file in green, hovered file in yellow, and others with the text default */
            if (i == selected_file)
            {
                ImGui::TextColored (ImVec4 (0.5f, 1.0f, 0.5f, 1.0f), "%s", file_list[i].c_str());
            }
            else if (hovering)
            {
                ImGui::TextColored (ImVec4 (1.0f, 1.0f, 0.5f, 1.0f), "%s", file_list[i].c_str());
            }
            else
            {
                ImGui::Text ("%s", file_list[i].c_str());
            }

        }
        ImGui::EndChild ();

        /* Buttons */
        if (ImGui::Button ("Cancel", ImVec2 (120,0))) {
            snepulator.running = true;
            ImGui::CloseCurrentPopup ();
        }
        ImGui::SameLine ();
        if (ImGui::Button ("Open", ImVec2(120,0))) { /* TODO: Do we want an "Open BIOS"? */
            open_action = true;
        }

        /* Open action */
        if (open_action)
        {
            /* Directory */
            if (file_list[selected_file].back() == '/')
            {
                chdir (file_list[selected_file].c_str());
            }
            /* ROM */
            else
            {
                free (snepulator.cart_filename);
                snepulator.cart_filename = strdup(file_list[selected_file].c_str());
                sms_init (snepulator.bios_filename, snepulator.cart_filename);
                snepulator.running = true;

                ImGui::CloseCurrentPopup ();
            }
            cwd_cached = false;
            file_list.clear();
        }

        ImGui::EndPopup ();
    }
}
