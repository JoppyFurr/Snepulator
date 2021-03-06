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
#include "config.h"

#define MAX_STRING_SIZE 1024

const char *button_names [] = { "up", "down", "left", "right", "button_1", "button_2", "start" };


/* Stored gamepad configuration */
Gamepad_Config gamepad_config [128];
uint32_t gamepad_config_count = 0;

/* List of detected gamepads */
Gamepad_Instance gamepad_list [128];
uint32_t gamepad_list_count = 0;

/* Current gamepad configuration */
Gamepad_Config *gamepad_1_config;
Gamepad_Config *gamepad_2_config;
Gamepad_Config *gamepad_3_config;

/* Current gamepad state */
Snepulator_Gamepad gamepad_1;
Snepulator_Gamepad gamepad_2;
Snepulator_Gamepad gamepad_3; /* Used for the 'configure' dialogue */

/*
 * Process an SDL_Event.
 */
void gamepad_process_event (SDL_Event *event)
{
    Gamepad_Config *config;
    Snepulator_Gamepad *gamepad;

    for (uint32_t player = 1; player <= 3; player++)
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
            case 3:
                config = gamepad_3_config;
                gamepad = &gamepad_3;
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
                    gamepad->state [i] = ((event->jaxis.value * config->mapping [i].sign ) > 16000) ? 1 : 0;
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

    /* Special case for Light Phaser (mouse) */
    if (gamepad_1.type == GAMEPAD_TYPE_SMS_PHASER)
    {
        if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT)
        {
            gamepad_1.state [GAMEPAD_BUTTON_1] = true;
        }
        else if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT)
        {
            gamepad_1.state [GAMEPAD_BUTTON_1] = false;
        }
    }
}


/*
 * Called to apply paddle velocity.
 *
 * TODO: * Make paddle_speed configurable
 *       * Support variable speeds with analogue input
 *       * Mouse support?
 *       * Direct vs relative?
 */
void gamepad_paddle_tick (uint32_t ms)
{
    static float remainder = 0.0;
    float paddle_speed = 250.0;
    float delta;
    int16_t new_position = gamepad_1.paddle_position;

    /* Temporary™ digital-only support */
    if (gamepad_1.state [GAMEPAD_DIRECTION_LEFT])
    {
        gamepad_1.paddle_velocity = -1.0;
    }
    else if (gamepad_1.state [GAMEPAD_DIRECTION_RIGHT])
    {
        gamepad_1.paddle_velocity = 1.0;
    }
    else
    {
        gamepad_1.paddle_velocity = 0.0;
    }

    /* Calculate and apply movement */
    delta = gamepad_1.paddle_velocity * paddle_speed * (ms * 0.001) + remainder;
    new_position += (int16_t) delta;
    remainder = fmodf (delta, 1.0);

    /* Bounds checking */
    if (new_position > 255)
    {
        new_position = 255;
    }
    else if (new_position < 0)
    {
        new_position = 0;
    }

    gamepad_1.paddle_position = new_position;
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
        for (uint32_t i = 0; i < gamepad_config_count; i++)
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
}


void gamepad_config_import (void);

/*
 * Initialise gamepad support.
 */
void gamepad_init (void)
{
    Gamepad_Config no_gamepad = { .guid = GUID_NONE };

    /* Add entry for GAMEPAD_INDEX_NONE */
    gamepad_config [gamepad_config_count++] = no_gamepad;

    /* Initialise both players to GAMEPAD_INDEX_NONE */
    gamepad_1.instance_id = INSTANCE_ID_NONE;
    gamepad_1.type = GAMEPAD_TYPE_SMS;
    gamepad_1.type_auto = true;
    gamepad_1_config = &gamepad_config [GAMEPAD_INDEX_NONE];
    gamepad_1.paddle_velocity = 0;
    gamepad_1.paddle_position = 128;

    gamepad_2.instance_id = INSTANCE_ID_NONE;
    gamepad_2.type = GAMEPAD_TYPE_SMS;
    gamepad_2_config = &gamepad_config [GAMEPAD_INDEX_NONE];

    gamepad_3.instance_id = INSTANCE_ID_NONE;
    gamepad_3.type = GAMEPAD_TYPE_SMS;
    gamepad_3_config = &gamepad_config [GAMEPAD_INDEX_NONE];

    /* Add entry for GAMEPAD_INDEX_KEYBOARD */
    Gamepad_Config default_keyboard = { .guid = GUID_KEYBOARD };

    /* Different default keyboard mappings for different layouts */
    switch (SDL_GetKeyFromScancode (SDL_SCANCODE_S))
    {
        /* Dvorak */
        case SDLK_o:
            default_keyboard.mapping [GAMEPAD_DIRECTION_UP]     = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_COMMA };
            default_keyboard.mapping [GAMEPAD_DIRECTION_DOWN]   = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_o };
            default_keyboard.mapping [GAMEPAD_DIRECTION_LEFT]   = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_a };
            default_keyboard.mapping [GAMEPAD_DIRECTION_RIGHT]  = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_e };
            default_keyboard.mapping [GAMEPAD_BUTTON_1]         = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_v };
            default_keyboard.mapping [GAMEPAD_BUTTON_2]         = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_z };
            default_keyboard.mapping [GAMEPAD_BUTTON_START]     = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_RETURN };
            break;

        /* Colemak */
        case SDLK_r:
            default_keyboard.mapping [GAMEPAD_DIRECTION_UP]     = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_w };
            default_keyboard.mapping [GAMEPAD_DIRECTION_DOWN]   = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_r };
            default_keyboard.mapping [GAMEPAD_DIRECTION_LEFT]   = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_a };
            default_keyboard.mapping [GAMEPAD_DIRECTION_RIGHT]  = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_s };
            default_keyboard.mapping [GAMEPAD_BUTTON_1]         = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_PERIOD };
            default_keyboard.mapping [GAMEPAD_BUTTON_2]         = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_SLASH };
            default_keyboard.mapping [GAMEPAD_BUTTON_START]     = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_RETURN };
            break;

        /* Qwerty */
        case SDLK_s:
        default:
            default_keyboard.mapping [GAMEPAD_DIRECTION_UP]     = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_w };
            default_keyboard.mapping [GAMEPAD_DIRECTION_DOWN]   = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_s };
            default_keyboard.mapping [GAMEPAD_DIRECTION_LEFT]   = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_a };
            default_keyboard.mapping [GAMEPAD_DIRECTION_RIGHT]  = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_d };
            default_keyboard.mapping [GAMEPAD_BUTTON_1]         = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_PERIOD };
            default_keyboard.mapping [GAMEPAD_BUTTON_2]         = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_SLASH };
            default_keyboard.mapping [GAMEPAD_BUTTON_START]     = (Gamepad_Mapping) { .type = SDL_KEYDOWN, .key = SDLK_RETURN };
            break;
    }
    gamepad_config [gamepad_config_count++] = default_keyboard;

    /* Read saved config from file */
    gamepad_config_import ();
    /* Populate the gamepad list */
    gamepad_list_update ();

    /* Set default devices for players */
    gamepad_change_device (1, GAMEPAD_INDEX_KEYBOARD);
    gamepad_change_device (2, GAMEPAD_INDEX_NONE);
    gamepad_change_device (3, GAMEPAD_INDEX_NONE);
}


/*
 * Update the mapping for a known gamepad.
 */
void gamepad_update_mapping (Gamepad_Config new_config)
{
    char guid_string [33];

    for (uint32_t i = 0; i < gamepad_config_count; i++)
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
    static char name_data [128] [MAX_STRING_SIZE];
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

    /* Finally, store the name, along with a unique tag */
    sprintf (name_data [index], "%s##%d", name, index);

    return name_data [index];
}


/*
 * Return the number of players using the specified joystick instance_id.
 */
uint32_t gamepad_joystick_user_count (uint32_t instance_id)
{
    uint32_t count = 0;

    if (gamepad_1.instance_id == instance_id)
        count++;

    if (gamepad_2.instance_id == instance_id)
        count++;

    if (gamepad_3.instance_id == instance_id)
        count++;

    return count;
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
        case 3:
            config = &gamepad_3_config;
            gamepad = &gamepad_3;
            break;
        default:
            return;
    }

    /* Close the previous joystick if we are the only user */
    if (gamepad->instance_id > INSTANCE_ID_NONE && gamepad_joystick_user_count (gamepad->instance_id) == 1)
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
        joystick = SDL_JoystickFromInstanceID (gamepad_list [index].instance_id);

        /* Open the joystick if needed */
        if (joystick == NULL || SDL_JoystickGetAttached (joystick) == false)
        {
            joystick = SDL_JoystickOpen (gamepad_list [index].device_id);
        }

        if (joystick)
        {
            *config = &gamepad_config [gamepad_list [index].config_index];
            gamepad->instance_id = SDL_JoystickInstanceID (joystick);
        }
    }
}


/*
 * Store the gamepad configuration to the configuration file.
 */
void gamepad_config_export (void)
{
    char section_name [MAX_STRING_SIZE] = { '\0' };
    char mapping_data [MAX_STRING_SIZE] = { '\0' };

    for (uint32_t i = 0; i < gamepad_config_count; i++)
    {
        /* Section name */
        snprintf (section_name, 1023, "gamepad-%02x%02x.%02x%02x.%02x%02x.%02x%02x.%02x%02x.%02x%02x.%02x%02x.%02x%02x",
                gamepad_config [i].guid.data [ 0], gamepad_config [i].guid.data [ 1], gamepad_config [i].guid.data [ 2],
                gamepad_config [i].guid.data [ 3], gamepad_config [i].guid.data [ 4], gamepad_config [i].guid.data [ 5],
                gamepad_config [i].guid.data [ 6], gamepad_config [i].guid.data [ 7], gamepad_config [i].guid.data [ 8],
                gamepad_config [i].guid.data [ 9], gamepad_config [i].guid.data [10], gamepad_config [i].guid.data [11],
                gamepad_config [i].guid.data [12], gamepad_config [i].guid.data [13], gamepad_config [i].guid.data [14],
                gamepad_config [i].guid.data [15]);

        for (uint32_t button = 0; button < GAMEPAD_BUTTON_COUNT; button++)
        {
            snprintf (mapping_data, 1023, "%x-%x-%x",
                      gamepad_config [i].mapping [button].type,
                      gamepad_config [i].mapping [button].key,
                      gamepad_config [i].mapping [button].direction);
            config_string_set (section_name, button_names [button], mapping_data);
        }
    }

    config_write ();
}


/*
 * Retrieve the gamepad configuration from the configuration file.
 */
void gamepad_config_import (void)
{
    unsigned int guid_buffer [16] = { 0x00 };
    Gamepad_Config new_config = { };

    for (uint32_t i = 0; config_get_section_name (i) != NULL; i++)
    {
        const char *section_name = config_get_section_name (i);

        if (strncmp (section_name, "gamepad-", 8) == 0)
        {
            SDL_JoystickGUID guid = { 0x00 };

            sscanf (section_name, "gamepad-%02x%02x.%02x%02x.%02x%02x.%02x%02x.%02x%02x.%02x%02x.%02x%02x.%02x%02x",
                    &guid_buffer [ 0], &guid_buffer [ 1], &guid_buffer [ 2], &guid_buffer [ 3], &guid_buffer [ 4],
                    &guid_buffer [ 5], &guid_buffer [ 6], &guid_buffer [ 7], &guid_buffer [ 8], &guid_buffer [ 9],
                    &guid_buffer [10], &guid_buffer [11], &guid_buffer [12], &guid_buffer [13], &guid_buffer [14],
                    &guid_buffer [15]);

            for (uint32_t j = 0; j < 16; j++)
            {
                guid.data [j] = guid_buffer [j];
            }

            /* Skip the 'None' device */
            if (memcmp (&guid, &gamepad_config [GAMEPAD_INDEX_NONE].guid, sizeof (SDL_JoystickGUID)) == 0)
            {
                continue;
            }

            /* Read in the stored mappings */
            for (uint32_t button = 0; button < GAMEPAD_BUTTON_COUNT; button++)
            {
                unsigned int type, key, direction;
                char *mapping_string = NULL;

                if (config_string_get (section_name, button_names [button], &mapping_string) == 0)
                {
                    sscanf (mapping_string, "%x-%x-%x", &type, &key, &direction);

                    new_config.mapping [button].type = type;
                    new_config.mapping [button].key = key;
                    new_config.mapping [button].direction = direction;
                }

                new_config.guid = guid;
            }

            /* Keyboard has fixed index */
            if (memcmp (&guid, &gamepad_config [GAMEPAD_INDEX_KEYBOARD].guid, sizeof (SDL_JoystickGUID)) == 0)
            {
                gamepad_config [GAMEPAD_INDEX_KEYBOARD] = new_config;
            }
            else
            {
                gamepad_config [gamepad_config_count++] = new_config;
            }
        }

    }
}
