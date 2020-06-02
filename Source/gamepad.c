/*
 * Gamepad input implementation.
 *
 * This file deals with taking SDL_Event inputs, and provides an abstract
 * Snepulator_Gamepad interface to be used in the emulated consoles.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "gamepad.h"


/* TODO: Make array size dynamic */

/* Stored gamepad configuration */
Gamepad_Config gamepad_config [10];
uint32_t gamepad_config_count = 0;

/* List of detected gamepads */
/* TODO: Make dynamic */
Gamepad_Instance gamepad_list [10];
uint32_t gamepad_list_count = 0;

/* Current gamepad configuration */
Gamepad_Config *gamepad_1_config;
Gamepad_Config *gamepad_2_config;

/* Current gamepad state */
SDL_Joystick *gamepad_1_joystick; /* TODO: Replace with SDL_JoysticckFromInstanceID () */
SDL_Joystick *gamepad_2_joystick;
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
        if ((event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) && gamepad->instance_id == INSTANCE_ID_KEYBOARD)
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
        else if ((event->type == SDL_JOYAXISMOTION) && event->jaxis.which == gamepad->instance_id)
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
        else if ((event->type == SDL_JOYBUTTONDOWN || event->type == SDL_JOYBUTTONUP) && event->jbutton.which == gamepad->instance_id)
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
    Gamepad_Config no_gamepad = { .guid = GUID_NONE };

    /* Add entry for GAMEPAD_INDEX_NONE */
    gamepad_config [gamepad_config_count++] = no_gamepad;
    gamepad_list [gamepad_list_count++] = (Gamepad_Instance) { .instance_id = INSTANCE_ID_NONE, .config_index = GAMEPAD_INDEX_NONE };

    /* TODO: Detect user's keyboard layout and adjust accordingly */
    /* Add entry for GAMEPAD_INDEX_KEYBOARD */
    Gamepad_Config default_keyboard = { .guid = GUID_KEYBOARD };
    default_keyboard.mapping [GAMEPAD_DIRECTION_UP]     = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_COMMA };
    default_keyboard.mapping [GAMEPAD_DIRECTION_DOWN]   = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_o };
    default_keyboard.mapping [GAMEPAD_DIRECTION_LEFT]   = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_a };
    default_keyboard.mapping [GAMEPAD_DIRECTION_RIGHT]  = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_e };
    default_keyboard.mapping [GAMEPAD_BUTTON_1]         = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_v };
    default_keyboard.mapping [GAMEPAD_BUTTON_2]         = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_z };
    default_keyboard.mapping [GAMEPAD_BUTTON_START]     = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_RETURN };
    gamepad_config [gamepad_config_count++] = default_keyboard;
    gamepad_list [gamepad_list_count++] = (Gamepad_Instance) { .instance_id = INSTANCE_ID_KEYBOARD, .config_index = GAMEPAD_INDEX_KEYBOARD };

    /* TODO: Recall saved mappings from a file */
    Gamepad_Config default_gamepad = { };
    default_gamepad.mapping [GAMEPAD_DIRECTION_UP]      = (Gamepad_Mapping) { .type = SDL_JOYAXISMOTION, .axis = 1, .sign = -1 };
    default_gamepad.mapping [GAMEPAD_DIRECTION_DOWN]    = (Gamepad_Mapping) { .type = SDL_JOYAXISMOTION, .axis = 1, .sign =  1 };
    default_gamepad.mapping [GAMEPAD_DIRECTION_LEFT]    = (Gamepad_Mapping) { .type = SDL_JOYAXISMOTION, .axis = 0, .sign = -1 };
    default_gamepad.mapping [GAMEPAD_DIRECTION_RIGHT]   = (Gamepad_Mapping) { .type = SDL_JOYAXISMOTION, .axis = 0, .sign =  1 };
    default_gamepad.mapping [GAMEPAD_BUTTON_1]          = (Gamepad_Mapping) { .type = SDL_JOYBUTTONDOWN, .button = 2 };
    default_gamepad.mapping [GAMEPAD_BUTTON_2]          = (Gamepad_Mapping) { .type = SDL_JOYBUTTONDOWN, .button = 1 };
    default_gamepad.mapping [GAMEPAD_BUTTON_START]      = (Gamepad_Mapping) { .type = SDL_JOYBUTTONDOWN, .button = 9 };

    for (int i = 0; i < SDL_NumJoysticks (); i++)
    {
        default_gamepad.guid = SDL_JoystickGetDeviceGUID (i);
        gamepad_config [gamepad_config_count++] = default_gamepad;
        gamepad_list [gamepad_list_count++] = (Gamepad_Instance) { .instance_id = SDL_JoystickGetDeviceInstanceID (i),
                                                                   .device_id = i, .config_index = gamepad_config_count };
    }

    gamepad_1.instance_id = INSTANCE_ID_NONE;
    gamepad_1_config = &gamepad_config [GAMEPAD_INDEX_NONE];
    gamepad_2.instance_id = INSTANCE_ID_NONE;
    gamepad_2_config = &gamepad_config [GAMEPAD_INDEX_NONE];

    /* Set default devices for players */
    gamepad_change_device (1, GAMEPAD_INDEX_KEYBOARD);
    gamepad_change_device (2, GAMEPAD_INDEX_NONE);
}


/*
 * Update the mapping for a known gamepad.
 *
 * TODO: Switch to GUID
 */
void gamepad_update_mapping (Gamepad_Config new_config)
{
    char guid_string [33];

    for (int i = 0; i < gamepad_config_count; i++)
    {
        if (memcmp (&gamepad_config [i].guid, &new_config.guid, sizeof (SDL_JoystickGUID)) == 0)
        {
            /* Replace the old entry with the new one */
            gamepad_config [i] = new_config;
            return;
        }
    }

    SDL_JoystickGetGUIDString (new_config.guid, guid_string, 33);
    fprintf (stderr, "Warning: Unable to find device %s.\n", guid_string);
}


/*
 * Get the name of a gamepad in our gamepad_list array.
 */
const char *gamepad_get_name (uint32_t index)
{
    const char *name;

    if (index == GAMEPAD_INDEX_NONE)
    {
        name = "None";
    }
    else if (index == GAMEPAD_INDEX_KEYBOARD)
    {
        name = "Keyboard";
    }
    else
    {
        name = SDL_JoystickNameForIndex (gamepad_list [index].device_id);
        if (name == NULL)
        {
            name = "Unknown Joystick";
        }
    }

    return name;
}


/*
 * Change input device for a player's gamepad.
 *
 * Index is into the gamepad_list.
 */

void gamepad_change_device (uint32_t player, int32_t index)
{
    Gamepad_Config **config;
    Snepulator_Gamepad *gamepad;
    SDL_Joystick **joystick;

    switch (player)
    {
        case 1:
            config = &gamepad_1_config;
            gamepad = &gamepad_1;
            joystick = &gamepad_1_joystick;
            break;
        case 2:
            config = &gamepad_2_config;
            gamepad = &gamepad_2;
            joystick = &gamepad_2_joystick;
            break;
        default:
            return;
    }

    /* Close the previous device */
    /* TODO: Check if it is being used by the other player */
    if (*joystick != NULL)
    {
        SDL_JoystickClose (*joystick);
        *joystick = NULL;
    }
    gamepad->instance_id = INSTANCE_ID_NONE;
    *config = &gamepad_config [GAMEPAD_INDEX_NONE];

    if (index == GAMEPAD_INDEX_NONE)
    {
        return;
    }

    /* Open the new device */
    if (index == GAMEPAD_INDEX_KEYBOARD)
    {
        *config = &gamepad_config [GAMEPAD_INDEX_KEYBOARD];
        gamepad->instance_id = INSTANCE_ID_KEYBOARD;
    }
    else
    {
        /* TODO: Check if is already open by the other player */
        *joystick = SDL_JoystickOpen (gamepad_list [index].device_id);
        if (*joystick)
        {
            *config = &gamepad_config [index];
            gamepad->instance_id = SDL_JoystickInstanceID (*joystick);
        }
    }
}
