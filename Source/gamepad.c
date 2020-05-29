/*
 * Gamepad input implementation.
 *
 * This file deals with taking SDL_Event inputs, and provides an abstract
 * Snepulator_Gamepad interface to be used in the emulated consoles.
 */

#include <stdbool.h>
#include <stdint.h>

#include <SDL2/SDL.h>

#include "gamepad.h"

/* Gamepad configuration */
/* TODO: Make dynamic */
Gamepad_Config gamepad_config [10];
uint32_t gamepad_config_count = 0;
Gamepad_Config gamepad_1_config;
Gamepad_Config gamepad_2_config;

/* Gamepad state */
Snepulator_Gamepad gamepad_1;
Snepulator_Gamepad gamepad_2;

/*
 * Process an SDL_Event.
 */
void gamepad_process_event (SDL_Event *event)
{
    /* Keyboard */
    if ((event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) && gamepad_1_config.device_id == ID_KEYBOARD)
    {
        for (int i = 0; i < GAMEPAD_BUTTON_COUNT; i++)
        {
            if (gamepad_1_config.mapping [i].type == SDL_KEYDOWN)
            {
                if (event->key.keysym.sym == gamepad_1_config.mapping [i].key)
                {
                    gamepad_1.state [i] = (event->type == SDL_KEYDOWN);
                }
            }
        }
    }

    /* Joystick Axis */
    else if ((event->type == SDL_JOYAXISMOTION) && event->jaxis.which == gamepad_1_config.device_id)
    {
        for (int i = 0; i < GAMEPAD_BUTTON_COUNT; i++)
        {
            if ((gamepad_1_config.mapping [i].type == SDL_JOYAXISMOTION) && (event->jaxis.axis == gamepad_1_config.mapping [i].axis))
            {
                /* TODO: Make the deadzone configurable */
                gamepad_1.state [i] = ((event->jaxis.value * gamepad_1_config.mapping [i].sign ) > 1000) ? 1 : 0;
            }
        }
    }

    /* Joystick Button */
    else if ((event->type == SDL_JOYBUTTONDOWN || event->type == SDL_JOYBUTTONUP) && event->jbutton.which == gamepad_1_config.device_id)
    {
        for (int i = 0; i < GAMEPAD_BUTTON_COUNT; i++)
        {
            if ((gamepad_1_config.mapping [i].type == SDL_JOYBUTTONDOWN) && (event->jbutton.button == gamepad_1_config.mapping [i].button))
            {
                gamepad_1.state [i] = (event->type == SDL_JOYBUTTONDOWN);
            }
        }
    }
}


/*
 * Detect input devices and populate the in-memory mapping list.
 * Note: It'd be nice to automatically add/remove mappings as devices are plugged in and removed.
 */
void gamepad_init_input_devices (void)
{
    Gamepad_Config no_gamepad = {
        .device_id       = ID_NONE,
    };

    /* TODO: Detect user's keyboard layout and adjust accordingly */
    Gamepad_Config default_keyboard = { .device_id = ID_KEYBOARD };
    default_keyboard.mapping [GAMEPAD_DIRECTION_UP]     = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_COMMA };
    default_keyboard.mapping [GAMEPAD_DIRECTION_DOWN]   = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_o };
    default_keyboard.mapping [GAMEPAD_DIRECTION_LEFT]   = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_a };
    default_keyboard.mapping [GAMEPAD_DIRECTION_RIGHT]  = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_e };
    default_keyboard.mapping [GAMEPAD_BUTTON_1]         = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_v };
    default_keyboard.mapping [GAMEPAD_BUTTON_2]         = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_z };
    default_keyboard.mapping [GAMEPAD_BUTTON_START]     = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_RETURN };
    gamepad_config [gamepad_config_count++] = default_keyboard;

    /* TODO: Recall saved mappings from a file */
    Gamepad_Config default_gamepad = { .device_id = ID_NONE };
    default_gamepad.mapping [GAMEPAD_DIRECTION_UP]      = (Gamepad_Mapping) { .type = SDL_JOYAXISMOTION, .axis = 1, .sign = -1 };
    default_gamepad.mapping [GAMEPAD_DIRECTION_DOWN]    = (Gamepad_Mapping) { .type = SDL_JOYAXISMOTION, .axis = 1, .sign =  1 };
    default_gamepad.mapping [GAMEPAD_DIRECTION_LEFT]    = (Gamepad_Mapping) { .type = SDL_JOYAXISMOTION, .axis = 0, .sign = -1 };
    default_gamepad.mapping [GAMEPAD_DIRECTION_RIGHT]   = (Gamepad_Mapping) { .type = SDL_JOYAXISMOTION, .axis = 0, .sign =  1 };
    default_gamepad.mapping [GAMEPAD_BUTTON_1]          = (Gamepad_Mapping) { .type = SDL_JOYBUTTONDOWN, .button = 2 };
    default_gamepad.mapping [GAMEPAD_BUTTON_2]          = (Gamepad_Mapping) { .type = SDL_JOYBUTTONDOWN, .button = 1 };
    default_gamepad.mapping [GAMEPAD_BUTTON_START]      = (Gamepad_Mapping) { .type = SDL_JOYBUTTONDOWN, .button = 9 };

    for (int i = 0; i < SDL_NumJoysticks (); i++)
    {
        default_gamepad.device_id = i;
        gamepad_config [gamepad_config_count++] = default_gamepad;
    }

    /* Set default devices for players */
    gamepad_1_config = default_keyboard;
    gamepad_2_config = no_gamepad;
}


/*
 * Update the mapping for a known gamepad.
 */
void gamepad_update_mapping (Gamepad_Config device)
{
    for (int i = 0; i < gamepad_config_count; i++)
    {
        if (gamepad_config [i].device_id == device.device_id)
        {
            /* Replace the old entry with the new one */
            gamepad_config [i] = device;
            return;
        }
    }

    fprintf (stderr, "Warning: Unable to find device %d.\n", device.device_id);
}


