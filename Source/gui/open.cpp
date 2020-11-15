/*
 * File open modal.
 */

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

#include <GL/gl3w.h>
#include <SDL2/SDL.h>

#include "imgui.h"

extern "C" {
#include "util.h"
#include "snepulator.h"
#include "config.h"
#include "sms.h"
}

#include "open.h"

#define MAX_STRING_SIZE 1024

/* Global state */
extern Snepulator_State state;

/* Open-dialogue state */
File_Open_State open_state = { .title = "Open" };

/* Current path */
static char path [MAX_STRING_SIZE] = { '\0' };


/*
 * Change the regex used to filter files to open.
 */
void snepulator_set_open_regex (const char *regex)
{
    open_state.regex = regex;
    open_state.path_cached = false;
}


/*
 * Render the file-open modal.
 */
void snepulator_render_open_modal (void)
{
    /* Layout calculations */
    uint32_t width = state.host_width - 64;
    uint32_t height = state.host_height - 64;
    uint32_t font_height = ImGui::CalcTextSize ("Text", NULL, true).y;
    uint32_t titlebar_height = font_height + 6;
    uint32_t above_box = font_height + 12;
    uint32_t below_box = font_height + 16;

    /* Centre */
    ImGui::SetNextWindowSize (ImVec2 (width, height), ImGuiCond_Always);
    ImGui::SetNextWindowPos (ImVec2 (state.host_width / 2, state.host_height / 2), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

    if (ImGui::BeginPopupModal (open_state.title, NULL, ImGuiWindowFlags_AlwaysAutoResize |
                                                        ImGuiWindowFlags_NoMove |
                                                        ImGuiWindowFlags_NoScrollbar))
    {
        /* State */
        static std::vector<std::string> file_list;
        static std::vector<std::string> dir_list;
        static int selected_file = 0;
        bool open_action = false;

        std::regex file_regex (open_state.regex);

        if (!open_state.path_cached)
        {
            dir_list.clear ();
            file_list.clear ();

            /* Retrieve initial path from config */
            if (path [0] == '\0')
            {
                char *stored_path = NULL;

                if (config_string_get ("paths", "sms_roms", &stored_path) == 0)
                {
                    strncpy (path, stored_path, 159);
                }
            }

            /* Fallback to home directory */
            if (path [0] == '\0')
            {
                char *home_path = getenv ("HOME");

                if (home_path != NULL)
                {
                    sprintf (path, "%s/", home_path);
                }
                else
                {
                    snepulator_error ("Error", "${HOME} not defined.");
                    return;
                }
            }

            /* File listing */
            DIR *dir = opendir (path);
            struct dirent *entry = NULL;
            std::string entry_string;

            if (dir != NULL)
            {
                for (entry = readdir (dir); entry != NULL; entry = readdir (dir))
                {
                    /* Ignore "." directory */
                    if (strcmp (entry->d_name, ".") == 0)
                    {
                        continue;
                    }

                    /* Hide ".." directory if we're already at the root */
                    if ((strcmp (path, "/") == 0) && (strcmp (entry->d_name, "..") == 0))
                    {
                        continue;
                    }

                    entry_string = std::string (entry->d_name);

                    /* Show directories as ending with '/' */
                    if (entry->d_type == DT_DIR)
                    {
                        entry_string += '/';
                        dir_list.push_back (entry_string);
                    }
                    /* Only list files that have a valid ROM extension */
                    else if (std::regex_match (entry_string, file_regex))
                    {
                        file_list.push_back (entry_string);
                    }
                }

                /* Sort directories and files separately, then combine the lists */
                std::sort (dir_list.begin (), dir_list.end ());
                std::sort (file_list.begin (), file_list.end ());
                file_list.insert (file_list.begin (), dir_list.begin (), dir_list.end ());
            }

            open_state.path_cached = true;
        }

        ImGui::Text ("%s", path);

        /* Current directory contents */
        ImGui::BeginChild ("Files", ImVec2 (width - 16, height - (titlebar_height + above_box + below_box)), true);
        for (int i = 0; i < file_list.size (); i++)
        {
            ImVec2 draw_cursor = ImGui::GetCursorScreenPos ();
            ImVec2 row_size    = ImGui::CalcTextSize (file_list [i].c_str (), NULL, true);
            bool hovering      = ImGui::IsMouseHoveringRect (ImVec2 (draw_cursor.x,              draw_cursor.y + 2),
                                                             ImVec2 (draw_cursor.x + row_size.x, draw_cursor.y + row_size.y + 2));

            /* Click to select */
            if (hovering && ImGui::IsMouseClicked (0))
            {
                selected_file = i;
            }

            /* Double-click to open */
            if (hovering && ImGui::IsMouseDoubleClicked (0))
            {
                open_action = true;
            }

            /* Render the selected file in green, hovered file in yellow, and others with the text default */
            if (i == selected_file)
            {
                ImGui::TextColored (ImVec4 (0.5f, 1.0f, 0.5f, 1.0f), "%s", file_list [i].c_str ());
            }
            else if (hovering)
            {
                ImGui::TextColored (ImVec4 (1.0f, 1.0f, 0.5f, 1.0f), "%s", file_list [i].c_str ());
            }
            else
            {
                ImGui::Text ("%s", file_list [i].c_str ());
            }

        }
        ImGui::EndChild ();

        /* Buttons */
        ImGui::Spacing ();
        ImGui::SameLine (ImGui::GetContentRegionAvail().x + 16 - 128 - 128);
        if (ImGui::Button ("Cancel", ImVec2 (120,0))) {
            if (state.ready)
            {
                /* TODO: Preserve previous state */
                state.running = true;
            }
            ImGui::CloseCurrentPopup ();
        }
        ImGui::SameLine ();
        if (ImGui::Button ("Open", ImVec2 (120,0))) {
            open_action = true;
        }

        /* Open action */
        if (open_action)
        {
            /* Directory */
            if (file_list [selected_file].back () == '/')
            {
                char new_path [MAX_STRING_SIZE] = { '\0' };

                if (strcmp (file_list [selected_file].c_str (), "../") == 0)
                {
                    /* Go up to the parent directory */
                    /* First, remove the final slash from the path */
                    char *end_slash = strrchr (path, '/');
                    if (end_slash == NULL)
                    {
                        snepulator_error ("Error", "Failed to change directory.");
                        return;
                    }
                    end_slash [0] = '\0';

                    /* Now, zero the path name after the new final slash */
                    end_slash = strrchr (path, '/');
                    if (end_slash == NULL)
                    {
                        snepulator_error ("Error", "Failed to change directory.");
                        return;
                    }
                    memset (&end_slash [1], 0, strlen (&end_slash [1]));
                }
                else
                {
                    /* Enter new directory */
                    sprintf (new_path, "%s%s", path, file_list [selected_file].c_str ());
                    strcpy (path, new_path);
                }
                config_string_set ("paths", "sms_roms", path);
                config_write ();
            }
            /* ROM */
            else
            {
                char new_path [MAX_STRING_SIZE] = { '\0' };
                sprintf (new_path, "%s%s", path, file_list [selected_file].c_str ());

                open_state.callback (new_path);

                ImGui::CloseCurrentPopup ();
            }
            open_state.path_cached = false;
        }

        ImGui::EndPopup ();
    }
}
