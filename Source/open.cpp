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

#include "SDL2/SDL.h"
#include "SDL2/SDL_joystick.h"
#include <GL/gl3w.h>
#include "../Libraries/imgui-1.49/imgui.h"
#include "../Libraries/imgui-1.49/examples/sdl_opengl3_example/imgui_impl_sdl_gl3.h"

extern "C" {
#include "snepulator.h"
#include "config.h"
#include "open.h"

#include "sms.h"
}


/* Global state */
extern Snepulator_State state;

/* Open-dialogue state */
File_Open_State open_state = { .title = "Open" };

/* Current path */
/* TODO: Make size dynamic */
static char path[160] = { '\0' };


/*
 * Render the file-open modal.
 */
void snepulator_render_open_modal (void)
{
    if (ImGui::BeginPopupModal (open_state.title, NULL, ImGuiWindowFlags_AlwaysAutoResize |
                                                     ImGuiWindowFlags_NoMove |
                                                     ImGuiWindowFlags_NoScrollbar))
    {
        int width = (state.host_width / 2 > 512) ? state.host_width / 2 : 512;
        int height = (state.host_height / 2 > 384) ? state.host_width / 2 : 384;

        /* State */
        static bool path_cached = false;
        static std::vector<std::string> file_list;
        static int selected_file = 0;
        bool open_action = false;

        std::regex file_regex (open_state.regex);

        if (!path_cached)
        {
            /* Retrieve initial path from config */
            if (path [0] == '\0')
            {
                char *stored_path = NULL;

                if (config_string_get ("paths", "sms_roms", &stored_path) == 0)
                {
                    strncpy (path, stored_path, 179);
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
                    fprintf (stderr, "Error: ${HOME} not defined.");
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
                    }
                    /* Only list files that have a valid ROM extension */
                    else if (!std::regex_match (entry_string, file_regex))
                    {
                            continue;
                    }

                    file_list.push_back (entry_string);
                }
                std::sort (file_list.begin (), file_list.end ());
            }

            path_cached = true;
        }

        ImGui::Text ("%s", path);

        /* Current directory contents */
        ImGui::BeginChild ("Files", ImVec2 (width, height - 64), true);
        for (int i = 0; i < file_list.size (); i++)
        {
            /* TODO: Can we get the text height rather than hard-coding it? */
            ImVec2 draw_cursor = ImGui::GetCursorScreenPos ();
            bool hovering = ImGui::IsMouseHoveringRect (draw_cursor, ImVec2 (draw_cursor.x + 350, draw_cursor.y + 16));

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
                ImGui::TextColored (ImVec4 (0.5f, 1.0f, 0.5f, 1.0f), "%s", file_list[i].c_str ());
            }
            else if (hovering)
            {
                ImGui::TextColored (ImVec4 (1.0f, 1.0f, 0.5f, 1.0f), "%s", file_list[i].c_str ());
            }
            else
            {
                ImGui::Text ("%s", file_list[i].c_str ());
            }

        }
        ImGui::EndChild ();

        /* Buttons */
        if (ImGui::Button ("Cancel", ImVec2 (120,0))) {
            if (state.ready)
            {
                /* TODO: Preserve previous state */
                state.running = true;
            }
            ImGui::CloseCurrentPopup ();
        }
        ImGui::SameLine ();
        if (ImGui::Button ("Open", ImVec2 (120,0))) { /* TODO: Do we want an "Open BIOS"? */
            open_action = true;
        }

        /* Open action */
        if (open_action)
        {
            /* Directory */
            if (file_list[selected_file].back () == '/')
            {
                char new_path[160] = { '\0' };

                if (strcmp (file_list [selected_file].c_str (), "../") == 0)
                {
                    /* Go up to the parent directory */
                    /* First, remove the final slash from the path */
                    char *end_slash = strrchr (path, '/');
                    if (end_slash == NULL)
                    {
                        fprintf (stderr, "Error: Failed to change directory.");
                        return;
                    }
                    end_slash [0] = '\0';

                    /* Now, zero the path name after the new final slash */
                    end_slash = strrchr (path, '/');
                    if (end_slash == NULL)
                    {
                        fprintf (stderr, "Error: Failed to change directory.");
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
                char new_path[160] = { '\0' };
                sprintf (new_path, "%s%s", path, file_list [selected_file].c_str ());

                open_state.callback (new_path);

                ImGui::CloseCurrentPopup ();
            }
            path_cached = false;
            file_list.clear ();
        }

        ImGui::EndPopup ();
    }
}
