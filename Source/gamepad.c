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


/* TODO: Make array size dynamic */

/* Stored gamepad configuration */
Gamepad_Config gamepad_config [10];
uint32_t gamepad_config_count = 0;

/* Current gamepad configuration */
Gamepad_Config *gamepad_1_config;
Gamepad_Config *gamepad_2_config;
SDL_Joystick *gamepad_1_joystick;
SDL_Joystick *gamepad_2_joystick;

/* Current gamepad state */
Snepulator_Gamepad gamepad_1;
Snepulator_Gamepad gamepad_2;

/*
 * Process an SDL_Event.
 */
void gamepad_process_event (SDL_Event *event)
{
    Gamepad_Config *config;
    Snepulator_Gamepad *gamepad;

    for (uint32_t player = 1; player <= 2; player++)
    {
        switch (player)
        {
            case 1:
                config = gamepad_1_config;
                gamepad = &gamepad_1;
                break;
            case 2:
                config = gamepad_2_config;
                gamepad = &gamepad_2;
                break;
            default:
                return;
        }

        /* Keyboard */
        if ((event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) && config->device_id == ID_KEYBOARD)
        {
            for (int i = 0; i < GAMEPAD_BUTTON_COUNT; i++)
            {
                if (config->mapping [i].type == SDL_KEYDOWN)
                {
                    if (event->key.keysym.sym == config->mapping [i].key)
                    {
                        gamepad->state [i] = (event->type == SDL_KEYDOWN);
                    }
                }
            }
        }

        /* Joystick Axis */
        else if ((event->type == SDL_JOYAXISMOTION) && event->jaxis.which == config->device_id)
        {
            for (int i = 0; i < GAMEPAD_BUTTON_COUNT; i++)
            {
                if ((config->mapping [i].type == SDL_JOYAXISMOTION) && (event->jaxis.axis == config->mapping [i].axis))
                {
                    /* TODO: Make the deadzone configurable */
                    gamepad->state [i] = ((event->jaxis.value * config->mapping [i].sign ) > 1000) ? 1 : 0;
                }
            }
        }

        /* Joystick Button */
        else if ((event->type == SDL_JOYBUTTONDOWN || event->type == SDL_JOYBUTTONUP) && event->jbutton.which == config->device_id)
        {
            for (int i = 0; i < GAMEPAD_BUTTON_COUNT; i++)
            {
                if ((config->mapping [i].type == SDL_JOYBUTTONDOWN) && (event->jbutton.button == config->mapping [i].button))
                {
                    gamepad->state [i] = (event->type == SDL_JOYBUTTONDOWN);
                }
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

    /* GAMEPAD_INDEX_NONE */
    gamepad_config [gamepad_config_count++] = no_gamepad;
    gamepad_1_config = &gamepad_config [GAMEPAD_INDEX_NONE];
    gamepad_2_config = &gamepad_config [GAMEPAD_INDEX_NONE];

    /* GAMEPAD_INDEX_KEYBOARD */
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
    gamepad_change_device (1, GAMEPAD_INDEX_KEYBOARD);
    gamepad_change_device (2, GAMEPAD_INDEX_NONE);
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


/*
 * Get the name of a gamepad in our config array.
 */
const char *gamepad_get_name (uint32_t index)
{
    const char *joystick_name;
    if (gamepad_config [index].device_id == ID_NONE)
    {
        joystick_name = "None";
    }
    else if (gamepad_config [index].device_id == ID_KEYBOARD)
    {
        joystick_name = "Keyboard";
    }
    else
    {
        joystick_name = SDL_JoystickNameForIndex (gamepad_config [index].device_id);
        if (joystick_name == NULL)
        {
            joystick_name = "Unknown Joystick";
        }
    }

    return joystick_name;
}


/*
 * Change input device for a player's gamepad.
 *
 * TODO: To support hot-swapping of joysticks, device_id shouldn't be used here.
 */

void gamepad_change_device (uint32_t player, int32_t index)
{
    Gamepad_Config **config;
    SDL_Joystick **joystick;

    switch (player)
    {
        case 1:
            config = &gamepad_1_config;
            joystick = &gamepad_1_joystick;
            break;
        case 2:
            config = &gamepad_2_config;
            joystick = &gamepad_2_joystick;
            break;
        default:
            return;
    }

    /* Check that this is not already the active joystick */
    if ((*config)->device_id != gamepad_config [index].device_id)
    {
        /* Close the previous device */
        if (*joystick != NULL)
        {
            SDL_JoystickClose (*joystick);
            *joystick = NULL;
        }

        /* Open the new device */
        if (gamepad_config [index].device_id == ID_NONE || gamepad_config [index].device_id == ID_KEYBOARD)
        {
            *config = &gamepad_config [index];
        }
        else
        {
            *joystick = SDL_JoystickOpen (gamepad_config [index].device_id);
            if (*joystick)
            {
                *config = &gamepad_config [index];
            }
        }
    }
}
