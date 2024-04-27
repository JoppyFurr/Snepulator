/*
 * Snepulator
 * ImGui File open modal implementation.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>

#include <sys/stat.h>

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
#include "../config.h"
}

#include "open.h"

#undef PATH_MAX
#define PATH_MAX 4096

/* Global state */
extern Snepulator_State state;

/* Open-dialogue state */
File_Open_State open_state = { .title = "Open" };
static char path [PATH_MAX] = { '\0' };

#ifdef TARGET_WINDOWS
#define DRIVES_MAX 32
#include <windows.h>
static char drive_letters_raw [PATH_MAX];
static char *drive_letters [DRIVES_MAX];
static uint32_t drive_count = 0;
static uint32_t drive_selected = 0;
#endif


/*
 * Change the regex used to filter files to open.
 */
void snepulator_set_open_regex (const char *regex)
{
    open_state.regex = regex;
    open_state.path_cached = false;

    /* Piggy back off this as a good time to update the list of drive letters */
#ifdef TARGET_WINDOWS
    int count = GetLogicalDriveStrings (PATH_MAX, drive_letters_raw);
    if (count <= 0)
    {
        snepulator_error ("Error", "Unable to list drive letters.");
    }

    drive_count = 0;
    char *ptr = &drive_letters_raw [0];
    for (int i = 0; *ptr != '\0' && i < count; i++)
    {
        drive_letters [drive_count++] = ptr;
        ptr = strchr (ptr, '\0') + 1;

        if (drive_count == DRIVES_MAX)
        {
            break;
        }
    }

    /* For now, use only '/' as a path separator */
    for (uint32_t drive = 0; drive < drive_count; drive++)
    {
        for (uint32_t i = 0; i < strlen (drive_letters [drive]); i++)
        {
            if (drive_letters [drive] [i] == '\\')
            {
                drive_letters [drive] [i] = '/';
            }
        }
    }

#endif
}


/*
 * Render the file-open modal.
 */
void snepulator_open_modal_render (void)
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
        static uint32_t selected_file = 0;
        bool open_action = false;
        DIR *dir = NULL;

        std::regex file_regex (open_state.regex, std::regex_constants::icase);

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

            /* Attempt to open */
            dir = opendir (path);

            /* Fallback to home directory */
            if (dir == NULL)
            {
#ifdef TARGET_WINDOWS
                char *home_path = getenv ("USERPROFILE");
#else
                char *home_path = getenv ("HOME");
#endif

                if (home_path != NULL)
                {
                    sprintf (path, "%s/", home_path);

#ifdef TARGET_WINDOWS
                    /* For now, use only '/' as a path separator */
                    for (uint32_t i = 0; i < strlen (path); i++)
                    {
                        if (path [i] == '\\')
                        {
                            path [i] = '/';
                        }
                    }
#endif
                }
                else
                {
                    snepulator_error ("Error", "${HOME} not defined.");
                    return;
                }

                dir = opendir (path);
            }

#ifdef TARGET_WINDOWS
            /* Show the correct drive letter for the current path */
            for (uint32_t i = 0; i < drive_count; i++)
            {
                if (strncmp (path, drive_letters [i], 3) == 0)
                {
                    drive_selected = i;
                    break;
                }
            }
#endif

            /* File listing */
            struct dirent *entry = NULL;
            std::string entry_string;

            if (dir != NULL)
            {
                struct stat stat_buf;

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

                    /* Not all systems have entry->d_type, so use stat
                     * to find out if this is a file or a directory. */
                    char *stat_path = NULL;
                    asprintf (&stat_path, "%s%s", path, entry->d_name);
                    int ret = stat (stat_path, &stat_buf);
                    free (stat_path);

                    /* Skip anything that stat can't read */
                    if (ret < 0)
                    {
                        continue;
                    }

                    entry_string = std::string (entry->d_name);

                    /* Show directories as ending with '/' */
                    if (S_ISDIR (stat_buf.st_mode))
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

                closedir (dir);
                dir = NULL;
            }

            open_state.path_cached = true;
        }

        ImGui::Text ("%s", path);

        /* Current directory contents */
        ImGui::BeginChild ("Files", ImVec2 (width - 16, height - (titlebar_height + above_box + below_box)), true);
        for (uint32_t i = 0; i < file_list.size (); i++)
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

#ifdef TARGET_WINDOWS
        /* Drive letter */
        ImGui::PushItemWidth (96);
        if (ImGui::BeginCombo ("##Drive", drive_letters [drive_selected]))
        {
            for (uint32_t i = 0; i < drive_count; i++)
            {
                if (ImGui::Selectable (drive_letters [i], i == drive_selected))
                {
                    drive_selected = i;
                    strcpy (path, drive_letters [i]);
                    open_state.path_cached = false;
                }
            }
            ImGui::EndCombo ();
        }
        ImGui::PopItemWidth ();
        ImGui::SameLine ();
#endif

        /* Buttons */
        ImGui::Spacing ();
        ImGui::SameLine (ImGui::GetContentRegionAvail().x + 16 - 128 - 128);
        if (ImGui::Button ("Cancel", ImVec2 (120,0))) {
            ImGui::CloseCurrentPopup ();
            snepulator_pause_set (false);
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
                char new_path [PATH_MAX] = { '\0' };

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
                char new_path [PATH_MAX] = { '\0' };
                sprintf (new_path, "%s%s", path, file_list [selected_file].c_str ());

                open_state.callback (new_path);

                ImGui::CloseCurrentPopup ();
            }
            open_state.path_cached = false;
        }

        ImGui::EndPopup ();
    }
}
