/*
 * Snepulator
 * ImGui Input configuration modal implementation
 * TODO: Rename source file
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include <algorithm>
#include <regex>
#include <string>
#include <vector>

#include <GL/gl3w.h>
#include <SDL2/SDL.h>

#include "imgui.h"

static const ImVec4 colour_white_v   = ImVec4 (1.00f, 1.00f, 1.00f, 1.0f);
static const ImVec4 colour_grey_10_v = ImVec4 (0.10f, 0.10f, 0.10f, 1.0f);
static const ImVec4 colour_grey_15_v = ImVec4 (0.15f, 0.15f, 0.15f, 1.0f);
static const ImVec4 colour_grey_20_v = ImVec4 (0.20f, 0.20f, 0.20f, 1.0f);
static const ImVec4 colour_grey_50_v = ImVec4 (0.50f, 0.50f, 0.50f, 0.5f);
static const ImVec4 colour_waiting_v = ImVec4 (0.80f, 0.50f, 0.10f, 1.0f);
static const ImVec4 colour_pressed_v = ImVec4 (0.40f, 0.80f, 0.60f, 1.0f);

static const ImU32 colour_white          = ImColor (colour_white_v);
static const ImU32 colour_grey_10        = ImColor (colour_grey_10_v);
static const ImU32 colour_grey_15        = ImColor (colour_grey_15_v);
static const ImU32 colour_grey_50        = ImColor (colour_grey_50_v);
static const ImU32 colour_button_default = ImColor (colour_grey_20_v);
static const ImU32 colour_button_waiting = ImColor (colour_waiting_v);
static const ImU32 colour_button_pressed = ImColor (colour_pressed_v);

extern "C" {
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

/* Settings in-flight, not yet committed by the "OK" button */
static bool uncommitted_trackball_button_swap = false;
static float uncommitted_trackball_sensitivity = 0.0;
static float uncommitted_paddle_sensitivity = 0.0;


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

    /* Settings in the GUI start at the currently configured values */
    uncommitted_trackball_sensitivity = state.trackball_sensitivity;
    uncommitted_trackball_button_swap = state.trackball_button_swap;
    uncommitted_paddle_sensitivity = state.paddle_sensitivity;
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
 * Draw the D-Pad diagram.
 */
static void draw_dpad (ImVec2 centre, float scale)
{
    ImDrawList* draw_list  = ImGui::GetWindowDrawList ();

    float dpad_width_r       = 0.11 * scale; /* Distance from centre of dpad to edge */
    float dpad_rounding      = 0.06 * scale; /* Radius of the dpad corner-rounding */
    float dpad_circle_r      = 0.044 * scale; /* Radius of flat circle in centre of dpad */
    float dpad_bump_r        = (dpad_width_r + dpad_circle_r) * 0.5; /* Radius of the ring which passes through the centre of the bumps */
    float dpad_bump_width_r  = 0.006 * scale;
    float dpad_bump_length_r = 0.015 * scale;
    float dpad_bump_rounding = dpad_bump_width_r;

    ImU32 colour_up = 0;
    ImU32 colour_down = 0;
    ImU32 colour_left = 0;
    ImU32 colour_right = 0;

    /* Background */
    draw_list->AddRectFilled   (ImVec2 (centre.x - dpad_width_r, centre.y - dpad_width_r),
                                ImVec2 (centre.x + dpad_width_r, centre.y + dpad_width_r), colour_button_default, dpad_rounding);
    draw_list->AddCircleFilled (ImVec2 (centre.x, centre.y), dpad_circle_r, colour_grey_15, 32);

    /* Highlight if remapping remap */
    if (gamepad_remap_step == GAMEPAD_DIRECTION_UP)
    {
        colour_up = colour_button_waiting;
    }
    else if (gamepad_remap_step == GAMEPAD_DIRECTION_DOWN)
    {
        colour_down = colour_button_waiting;
    }
    else if (gamepad_remap_step == GAMEPAD_DIRECTION_LEFT)
    {
        colour_left = colour_button_waiting;
    }
    else if (gamepad_remap_step == GAMEPAD_DIRECTION_RIGHT)
    {
        colour_right = colour_button_waiting;
    }

    /* If not remapping, hilight the currently pressed direction */
    else if (gamepad_remap_step == GAMEPAD_BUTTON_COUNT)
    {
        if (gamepad [0].state[GAMEPAD_DIRECTION_UP])
        {
            colour_up = colour_button_pressed;
        }
        if (gamepad [0].state[GAMEPAD_DIRECTION_DOWN])
        {
            colour_down = colour_button_pressed;
        }
        if (gamepad [0].state[GAMEPAD_DIRECTION_LEFT])
        {
            colour_left = colour_button_pressed;
        }
        if (gamepad [0].state[GAMEPAD_DIRECTION_RIGHT])
        {
            colour_right = colour_button_pressed;
        }
    }

    if (colour_up)
    {
        draw_list->AddRectFilled (ImVec2 (centre.x - dpad_width_r, centre.y - dpad_width_r),
                                  ImVec2 (centre.x + dpad_width_r, centre.y               ), colour_up, dpad_rounding);
    }
    if (colour_down)
    {
        draw_list->AddRectFilled (ImVec2 (centre.x - dpad_width_r, centre.y         ),
                                  ImVec2 (centre.x + dpad_width_r, centre.y + dpad_width_r), colour_down, dpad_rounding);
    }
    if (colour_left)
    {
        draw_list->AddRectFilled (ImVec2 (centre.x - dpad_width_r, centre.y - dpad_width_r),
                                  ImVec2 (centre.x               , centre.y + dpad_width_r), colour_left, dpad_rounding);
    }
    if (colour_right)
    {
        draw_list->AddRectFilled (ImVec2 (centre.x               , centre.y - dpad_width_r),
                                  ImVec2 (centre.x + dpad_width_r, centre.y + dpad_width_r), colour_right, dpad_rounding);
    }

    /* Dpad outline */
    draw_list->AddRect (ImVec2 (centre.x - dpad_width_r, centre.y - dpad_width_r),
                        ImVec2 (centre.x + dpad_width_r, centre.y + dpad_width_r), colour_white, dpad_rounding);

    /* Dpad detail */
    draw_list->AddRect (ImVec2 (centre.x - dpad_bump_width_r, centre.y - dpad_bump_r - dpad_bump_length_r),
                        ImVec2 (centre.x + dpad_bump_width_r, centre.y - dpad_bump_r + dpad_bump_length_r), colour_grey_50, dpad_bump_rounding);
    draw_list->AddRect (ImVec2 (centre.x - dpad_bump_width_r, centre.y + dpad_bump_r - dpad_bump_length_r),
                        ImVec2 (centre.x + dpad_bump_width_r, centre.y + dpad_bump_r + dpad_bump_length_r), colour_grey_50, dpad_bump_rounding);
    draw_list->AddRect (ImVec2 (centre.x - dpad_bump_r - dpad_bump_length_r, centre.y - dpad_bump_width_r),
                        ImVec2 (centre.x - dpad_bump_r + dpad_bump_length_r, centre.y + dpad_bump_width_r), colour_grey_50, dpad_bump_rounding);
    draw_list->AddRect (ImVec2 (centre.x + dpad_bump_r - dpad_bump_length_r, centre.y - dpad_bump_width_r),
                        ImVec2 (centre.x + dpad_bump_r + dpad_bump_length_r, centre.y + dpad_bump_width_r), colour_grey_50, dpad_bump_rounding);
    draw_list->AddCircle (ImVec2 (centre.x, centre.y), dpad_circle_r, colour_grey_50, 32);
}


/*
 * Draw a button for the gamepad diagram.
 */
static void draw_button (ImVec2 centre, float radius, Gamepad_Button button, const char *label)
{
    ImDrawList* draw_list  = ImGui::GetWindowDrawList ();

    /* Default button background */
    draw_list->AddCircleFilled (centre, radius, colour_button_default, 32);

    /* Highlight the button to remap */
    if (gamepad_remap_step == button)
    {
        draw_list->AddCircleFilled (centre, radius, colour_button_waiting, 32);
    }

    /* If we're not currently remapping, show the current button state */
    else if (gamepad_remap_step == GAMEPAD_BUTTON_COUNT)
    {
        if (gamepad [0].state[button])
        {
            draw_list->AddCircleFilled (centre, radius, colour_button_pressed, 32);
        }
    }

    /* Button outlines & labels */
    ImVec2 text_size;
    draw_list->AddCircle (centre, radius, colour_white, 32);
    text_size = ImGui::CalcTextSize (label);
    draw_list->AddText (ImVec2 (centre.x - text_size.x / 2, centre.y - text_size.y / 2), colour_grey_50, label);
}


/*
 * Draw the mapping table, shown below the controller diagram.
 */
static void button_mapping_table (uint32_t button_count, const Gamepad_Button *buttons, const char **names)
{
    if (ImGui::BeginTable("MappingDisplay", 2))
    {
        ImGui::TableNextColumn ();
        for (uint32_t i = 0; i < button_count; i++)
        {
            /* For now, assume the column-split is after the D-Pad directions */
            if (i == 4)
            {
                ImGui::TableNextColumn ();
            }
            ImGui::TextColored ((buttons [i] == gamepad_remap_step) ? colour_waiting_v : colour_white_v, "  %-11s%s",
                                names [i],
                                button_mapping_to_string (remap_config.mapping [buttons [i]]));
        }

        ImGui::EndTable ();
    }
}


/*
 * Render the input configuration modal.
 */
void snepulator_input_modal_render (void)
{
    /* Layout calculations */
    uint32_t width;
    uint32_t height;
    uint32_t font_height = ImGui::CalcTextSize ("Text", NULL, true).y;
    uint32_t titlebar_height = font_height + 6;
    uint32_t tab_bar_height = font_height + 12;

    uint32_t above_box = font_height + 18;
    uint32_t below_box = font_height + 16;

    /* To give a more consistent appearance across different
     * host-window sizes, aim for a square modal. */
    if (state.host_width > state.host_height)
    {
        width = state.host_height - 64;
        height = state.host_height - 32;
    }
    else
    {
        width = state.host_width - 64;
        height = state.host_width - 32;
    }

    /* Centre */
    ImGui::SetNextWindowSize (ImVec2 (width, height), ImGuiCond_Always);
    ImGui::SetNextWindowPos (ImVec2 (state.host_width / 2, state.host_height / 2), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

    if (ImGui::BeginPopupModal ("Configure device...", NULL, ImGuiWindowFlags_AlwaysAutoResize |
                                                             ImGuiWindowFlags_NoMove |
                                                             ImGuiWindowFlags_NoScrollbar))
    {
        bool show_remap_button = false;

        if (ImGui::BeginTabBar ("##InputModalTabs"))
        {
            if (ImGui::BeginTabItem ("SMS Pad"))
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

                /* Master System diagram and labels */
                int config_box_width = width - 16;
                int config_box_height = height - titlebar_height - tab_bar_height - above_box - below_box;
                ImGui::BeginChild ("MS Gamepad", ImVec2 (config_box_width, config_box_height), true);
                {
                    ImVec2 origin = ImGui::GetCursorScreenPos ();
                    float scale = (width - 64);

                    /* For wide screens, limit the controller diagram to 2/3 the box height */
                    if (scale > config_box_height * 5 / 3)
                    {
                        scale = config_box_height * 5 / 3;
                    }

                    origin.x += 16;
                    origin.y += 10;

                    ImDrawList* draw_list  = ImGui::GetWindowDrawList ();

                    /* Shape values */
                    ImVec2 button_1_centre   = ImVec2 (origin.x + 0.70 * scale, origin.y + 0.25 * scale);
                    ImVec2 button_2_centre   = ImVec2 (origin.x + 0.87 * scale, origin.y + 0.25 * scale);
                    float button_radius      = 0.06 * scale;
                    ImVec2 dpad_centre       = ImVec2 (origin.x + 0.27 * scale, origin.y + 0.20 * scale);

                    /* Controller outline */
                    draw_list->AddRectFilled (
                        ImVec2 (origin.x + scale * 0.0,  origin.y + scale * 0.0 ),
                        ImVec2 (origin.x + scale * 1.0,  origin.y + scale * 0.4 ), colour_grey_10);
                    draw_list->AddRect (
                        ImVec2 (origin.x + scale * 0.0,  origin.y + scale * 0.0 ),
                        ImVec2 (origin.x + scale * 1.0,  origin.y + scale * 0.4 ), colour_white);
                    draw_list->AddRect (
                        ImVec2 (origin.x + scale * 0.0,  origin.y + scale * 0.0 ),
                        ImVec2 (origin.x + scale * 0.08, origin.y + scale * 0.4 ), colour_white);
                    draw_list->AddRect (
                        ImVec2 (origin.x + scale * 0.48, origin.y + scale * 0.0 ),
                        ImVec2 (origin.x + scale * 1.0,  origin.y + scale * 0.12), colour_white);
                    draw_list->AddRect (
                        ImVec2 (origin.x + scale * 0.48, origin.y + scale * 0.36),
                        ImVec2 (origin.x + scale * 1.0,  origin.y + scale * 0.4 ), colour_white);

                    draw_dpad (dpad_centre, scale);
                    draw_button (button_1_centre, button_radius, GAMEPAD_BUTTON_1, "1");
                    draw_button (button_2_centre, button_radius, GAMEPAD_BUTTON_2, "2");

                    /* Move cursor to below gamepad diagram */
                    ImGui::SetCursorScreenPos (ImVec2 (origin.x - 10, origin.y + scale * 0.4 + 16));
                    const Gamepad_Button buttons [7] = { GAMEPAD_DIRECTION_UP, GAMEPAD_DIRECTION_DOWN, GAMEPAD_DIRECTION_LEFT, GAMEPAD_DIRECTION_RIGHT,
                                                         GAMEPAD_BUTTON_1, GAMEPAD_BUTTON_2, GAMEPAD_BUTTON_START };
                    const char *names [7] = { "Up:", "Down:", "Left:", "Right:", "Button 1:", "Button 2:", "Pause:" };
                    button_mapping_table (7, buttons, names);
                }

                ImGui::EndChild ();
                ImGui::EndTabItem ();

                show_remap_button = true;
            }
            if (ImGui::BeginTabItem ("MD Pad"))
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

                /* Mega Drive diagram and labels */
                int config_box_width = width - 16;
                int config_box_height = height - titlebar_height - tab_bar_height - above_box - below_box;
                ImGui::BeginChild ("SMD Gamepad", ImVec2 (config_box_width, config_box_height), true);
                {
                    ImVec2 origin = ImGui::GetCursorScreenPos ();
                    float scale = (width - 64);

                    /* For wide screens, limit the controller diagram to 2/3 the box height */
                    if (scale > config_box_height * 5 / 3)
                    {
                        scale = config_box_height * 5 / 3;
                    }

                    origin.x += 16;
                    origin.y += 10;

                    ImDrawList* draw_list  = ImGui::GetWindowDrawList ();

                    /* Shape values */
                    ImVec2 button_a_centre   = ImVec2 (origin.x + 0.60 * scale, origin.y + 0.25 * scale);
                    ImVec2 button_b_centre   = ImVec2 (origin.x + 0.75 * scale, origin.y + 0.25 * scale);
                    ImVec2 button_c_centre   = ImVec2 (origin.x + 0.90 * scale, origin.y + 0.25 * scale);
                    float button_radius      = 0.06 * scale;
                    ImVec2 dpad_centre       = ImVec2 (origin.x + 0.27 * scale, origin.y + 0.20 * scale);

                    /* Controller outline */
                    draw_list->AddRectFilled (
                        ImVec2 (origin.x + scale * 0.0,  origin.y + scale * 0.0 ),
                        ImVec2 (origin.x + scale * 1.0,  origin.y + scale * 0.4 ), colour_grey_10);
                    draw_list->AddRect (
                        ImVec2 (origin.x + scale * 0.0,  origin.y + scale * 0.0 ),
                        ImVec2 (origin.x + scale * 1.0,  origin.y + scale * 0.4 ), colour_white);
                    draw_list->AddRect (
                        ImVec2 (origin.x + scale * 0.0,  origin.y + scale * 0.0 ),
                        ImVec2 (origin.x + scale * 0.08, origin.y + scale * 0.4 ), colour_white);
                    draw_list->AddRect (
                        ImVec2 (origin.x + scale * 0.48, origin.y + scale * 0.0 ),
                        ImVec2 (origin.x + scale * 1.0,  origin.y + scale * 0.12), colour_white);
                    draw_list->AddRect (
                        ImVec2 (origin.x + scale * 0.48, origin.y + scale * 0.36),
                        ImVec2 (origin.x + scale * 1.0,  origin.y + scale * 0.4 ), colour_white);

                    draw_dpad (dpad_centre, scale);
                    draw_button (button_a_centre, button_radius, GAMEPAD_BUTTON_1, "A");
                    draw_button (button_b_centre, button_radius, GAMEPAD_BUTTON_2, "B");
                    draw_button (button_c_centre, button_radius, GAMEPAD_BUTTON_3, "C");

                    /* Move cursor to below gamepad diagram */
                    ImGui::SetCursorScreenPos (ImVec2 (origin.x - 10, origin.y + scale * 0.4 + 16));
                    const Gamepad_Button buttons [8] = { GAMEPAD_DIRECTION_UP, GAMEPAD_DIRECTION_DOWN, GAMEPAD_DIRECTION_LEFT, GAMEPAD_DIRECTION_RIGHT,
                                                         GAMEPAD_BUTTON_1, GAMEPAD_BUTTON_2, GAMEPAD_BUTTON_3, GAMEPAD_BUTTON_START };
                    const char *names [8] = { "Up:", "Down:", "Left:", "Right:", "Button A:", "Button B:", "Button C:", "Start:" };
                    button_mapping_table (8, buttons, names);
                }

                ImGui::EndChild ();
                ImGui::EndTabItem ();

                show_remap_button = true;
            }
            if (ImGui::BeginTabItem ("Sports Pad"))
            {
                uint32_t available_width = ImGui::GetContentRegionAvail().x;

                ImGui::Spacing ();
                ImGui::Text ("Sensitivity:");
                ImGui::SetCursorPosX (available_width * 0.1);
                ImGui::SetNextItemWidth (available_width * 0.8);
                ImGui::SliderFloat ("##SP_Sensitivity", &uncommitted_trackball_sensitivity, 0.0f, 0.16f, "%.3f", 0);
                ImGui::SetCursorPosX ((available_width - 240) / 2);
                if (ImGui::Button ("Restore Default", ImVec2 (240,0))) {
                    uncommitted_trackball_sensitivity = 0.04;
                };
                ImGui::Spacing ();
                ImGui::Checkbox ("Swap left and right mouse buttons", &uncommitted_trackball_button_swap);
                ImGui::EndTabItem ();
            }
            if (ImGui::BeginTabItem ("Paddle"))
            {
                uint32_t available_width = ImGui::GetContentRegionAvail().x;

                ImGui::Spacing ();
                ImGui::Text ("Sensitivity:");
                ImGui::SetCursorPosX (available_width * 0.1);
                ImGui::SetNextItemWidth (available_width * 0.8);
                ImGui::SliderFloat ("##Paddle_Sensitivity", &uncommitted_paddle_sensitivity, 0.0f, 1.0f, "%.3f", 0);
                ImGui::SetCursorPosX ((available_width - 240) / 2);
                if (ImGui::Button ("Restore Default", ImVec2 (240,0))) {
                    uncommitted_paddle_sensitivity = 0.25;
                };
                ImGui::EndTabItem ();
            }
        }
        ImGui::EndTabBar ();

        /* Buttons */
        uint32_t bottom = ImGui::GetWindowContentRegionMax().y;
        ImGui::SetCursorPosY (bottom - font_height - 5);
        ImGui::Spacing ();

        if (show_remap_button)
        {
            ImGui::SameLine (ImGui::GetContentRegionAvail().x + 16 - 128 * 3);
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
        }
        else
        {
            ImGui::SameLine (ImGui::GetContentRegionAvail().x + 16 - 128 * 2);
        }

        if (ImGui::Button ("Cancel", ImVec2 (120,0))) {

            /* TODO: Re-load the saved control pad configuration */

            gamepad_remap_step = GAMEPAD_BUTTON_COUNT;
            gamepad_change_device (0, GAMEPAD_INDEX_NONE);
            config_capture_events = false;
            ImGui::CloseCurrentPopup ();

            snepulator_pause_set (false);
        }
        ImGui::SameLine ();
        if (ImGui::Button ("OK", ImVec2 (120,0))) {
            gamepad_remap_step = GAMEPAD_BUTTON_COUNT;
            gamepad_change_device (0, GAMEPAD_INDEX_NONE);
            config_capture_events = false;
            ImGui::CloseCurrentPopup ();

            gamepad_config_export ();

            if (uncommitted_trackball_sensitivity != state.trackball_sensitivity)
            {
                snepulator_trackball_sensitivity_set (uncommitted_trackball_sensitivity);
            }

            if (uncommitted_trackball_button_swap != state.trackball_button_swap)
            {
                snepulator_trackball_button_swap_set (uncommitted_trackball_button_swap);
            }

            if (uncommitted_paddle_sensitivity != state.paddle_sensitivity)
            {
                snepulator_paddle_sensitivity_set (uncommitted_paddle_sensitivity);
            }

            snepulator_pause_set (false);
        }


        ImGui::EndPopup ();
    }
}
