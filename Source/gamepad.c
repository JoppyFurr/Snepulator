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
/* TODO: Once the config array is dynamic, an index may be better than a pointer */
Gamepad_Config *gamepad_1_config;
Gamepad_Config *gamepad_2_config;

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

        /* Joystick Hat */
        else if ((event->type == SDL_JOYHATMOTION) && event->jhat.which == gamepad->instance_id)
        {
            for (int i = 0; i < GAMEPAD_BUTTON_COUNT; i++)
            {
                if ((config->mapping [i].type == SDL_JOYHATMOTION) && (event->jhat.hat == config->mapping [i].hat))
                {
                    gamepad->state [i] = (event->jhat.value & config->mapping [i].direction) ? 1 : 0;
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
 * Generate a configuration entry for a new gamepad.
 *
 * Returns the configuration index of the new entry.
 *
 * TODO: Fallback to SDL_GameControllerGetBindForAxis if DPAD buttons are not found.
 */
static uint32_t gamepad_config_create (int32_t device_id)
{
    Gamepad_Config new_config = { };

    new_config.guid = SDL_JoystickGetDeviceGUID (device_id);

    /* If we can, pull the config from the SDL_GameController interface */
    if (SDL_IsGameController (device_id))
    {
        SDL_GameController *game_controller = SDL_GameControllerOpen (device_id);
        SDL_GameControllerButtonBind bind;

        uint32_t sdl_button_name [GAMEPAD_BUTTON_COUNT] = { SDL_CONTROLLER_BUTTON_DPAD_UP,      SDL_CONTROLLER_BUTTON_DPAD_DOWN,
                                                            SDL_CONTROLLER_BUTTON_DPAD_LEFT,    SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
                                                            SDL_CONTROLLER_BUTTON_A,            SDL_CONTROLLER_BUTTON_B,
                                                            SDL_CONTROLLER_BUTTON_START };
        uint32_t axis_sign [GAMEPAD_BUTTON_COUNT] = { -1, 1, -1, 1 };

        for (int i = 0; i < GAMEPAD_BUTTON_COUNT; i++)
        {
            bind = SDL_GameControllerGetBindForButton (game_controller, sdl_button_name [i]);

            switch (bind.bindType)
            {
                case SDL_CONTROLLER_BINDTYPE_BUTTON:
                    new_config.mapping [i] = (Gamepad_Mapping) { .type = SDL_JOYBUTTONDOWN, .button = bind.value.button };
                    break;
                case SDL_CONTROLLER_BINDTYPE_AXIS:
                    new_config.mapping [i] = (Gamepad_Mapping) { .type = SDL_JOYAXISMOTION, .axis = bind.value.axis, .sign = axis_sign [i] };
                    break;
                case SDL_CONTROLLER_BINDTYPE_HAT:
                    new_config.mapping [i] = (Gamepad_Mapping) { .type = SDL_JOYHATMOTION, .hat = bind.value.hat.hat, .direction = bind.value.hat.hat_mask };
                    break;
                default:
                    /* Do nothing */
                    continue;
            }
        }

        SDL_GameControllerClose (game_controller);
    }
    else
    {
        /* Default config if this is not an identified game controller */
        new_config.mapping [GAMEPAD_DIRECTION_UP]       = (Gamepad_Mapping) { .type = SDL_JOYAXISMOTION, .axis = 1, .sign = -1 };
        new_config.mapping [GAMEPAD_DIRECTION_DOWN]     = (Gamepad_Mapping) { .type = SDL_JOYAXISMOTION, .axis = 1, .sign =  1 };
        new_config.mapping [GAMEPAD_DIRECTION_LEFT]     = (Gamepad_Mapping) { .type = SDL_JOYAXISMOTION, .axis = 0, .sign = -1 };
        new_config.mapping [GAMEPAD_DIRECTION_RIGHT]    = (Gamepad_Mapping) { .type = SDL_JOYAXISMOTION, .axis = 0, .sign =  1 };
        new_config.mapping [GAMEPAD_BUTTON_1]           = (Gamepad_Mapping) { .type = SDL_JOYBUTTONDOWN, .button = 2 };
        new_config.mapping [GAMEPAD_BUTTON_2]           = (Gamepad_Mapping) { .type = SDL_JOYBUTTONDOWN, .button = 1 };
        new_config.mapping [GAMEPAD_BUTTON_START]       = (Gamepad_Mapping) { .type = SDL_JOYBUTTONDOWN, .button = 9 };
    }

    gamepad_config [gamepad_config_count] = new_config;

    return gamepad_config_count++;
}

/*
 * Refresh the list of detected gamepads.
 */
void gamepad_list_update (void)
{
    gamepad_list_count = 0;

    gamepad_list [gamepad_list_count++] = (Gamepad_Instance) { .instance_id = INSTANCE_ID_NONE, .config_index = GAMEPAD_INDEX_NONE };
    gamepad_list [gamepad_list_count++] = (Gamepad_Instance) { .instance_id = INSTANCE_ID_KEYBOARD, .config_index = GAMEPAD_INDEX_KEYBOARD };

    for (int device_id = 0; device_id < SDL_NumJoysticks (); device_id++)
    {
        int32_t instance_id = SDL_JoystickGetDeviceInstanceID (device_id);
        int32_t config_index = GAMEPAD_INDEX_NONE;
        SDL_JoystickGUID guid = SDL_JoystickGetDeviceGUID (device_id);

        /* Find config for device */
        for (int i = 0; i < gamepad_config_count; i++)
        {
            if (memcmp (&gamepad_config [i].guid, &guid, sizeof (SDL_JoystickGUID)) == 0)
            {
                config_index = i;
                break;
            }
        }

        /* If no config was found, generate a new config */
        if (config_index == GAMEPAD_INDEX_NONE)
        {
            config_index = gamepad_config_create (device_id);
        }

        gamepad_list [gamepad_list_count++] = (Gamepad_Instance) { .instance_id = instance_id,
                                                                   .device_id = device_id,
                                                                   .config_index = config_index };
    }

    /* TODO: Check if an in-use gamepad was disconnected and pause the game */
}

/*
 * Initialise gamepad support.
 *
 * TODO: Load saved mappings from the config file.
 */
void gamepad_init (void)
{
    Gamepad_Config no_gamepad = { .guid = GUID_NONE };

    /* Add entry for GAMEPAD_INDEX_NONE */
    gamepad_config [gamepad_config_count++] = no_gamepad;

    /* Initialise both players to GAMEPAD_INDEX_NONE */
    gamepad_1.instance_id = INSTANCE_ID_NONE;
    gamepad_1_config = &gamepad_config [GAMEPAD_INDEX_NONE];
    gamepad_2.instance_id = INSTANCE_ID_NONE;
    gamepad_2_config = &gamepad_config [GAMEPAD_INDEX_NONE];

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

    /* Populate the gamepad list */
    gamepad_list_update ();

    /* Set default devices for players */
    gamepad_change_device (1, GAMEPAD_INDEX_KEYBOARD);
    gamepad_change_device (2, GAMEPAD_INDEX_NONE);
}


/*
 * Update the mapping for a known gamepad.
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

    /* TODO: Instead of printing an error, create a new entry */
    SDL_JoystickGetGUIDString (new_config.guid, guid_string, 33);
    fprintf (stderr, "Warning: Unable to find device %s.\n", guid_string);
}


/*
 * Get the name of a gamepad in our gamepad_list array.
 */
const char *gamepad_get_name (uint32_t index)
{
    const char *name = NULL;

    /* First, check for hard-coded names */
    if (index == GAMEPAD_INDEX_NONE)
    {
        name = "None";
    }
    else if (index == GAMEPAD_INDEX_KEYBOARD)
    {
        name = "Keyboard";
    }

    /* Next, try the GameController API */
    else if (SDL_IsGameController (gamepad_list [index].device_id))
    {
        name = SDL_GameControllerNameForIndex (gamepad_list [index].device_id);
    }

    /* Next, try the Joystick API */
    if (name == NULL)
    {
        name = SDL_JoystickNameForIndex (gamepad_list [index].device_id);
    }

    /* Fallback if no name was found */
    if (name == NULL)
    {
        name = "Unknown Joystick";
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
    SDL_Joystick *joystick;
    Gamepad_Config **config;
    Snepulator_Gamepad *gamepad;

    switch (player)
    {
        case 1:
            config = &gamepad_1_config;
            gamepad = &gamepad_1;
            break;
        case 2:
            config = &gamepad_2_config;
            gamepad = &gamepad_2;
            break;
        default:
            return;
    }

    /* Close the previous device */
    /* TODO: Check if it is being used by the other player */
    if (gamepad->instance_id > INSTANCE_ID_KEYBOARD)
    {
        joystick = SDL_JoystickFromInstanceID (gamepad->instance_id);
        SDL_JoystickClose (joystick);
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
        joystick = SDL_JoystickOpen (gamepad_list [index].device_id);
        if (joystick)
        {
            *config = &gamepad_config [gamepad_list [index].config_index];
            gamepad->instance_id = SDL_JoystickInstanceID (joystick);
        }
    }
}
