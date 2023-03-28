
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "snepulator_types.h"
#include "snepulator.h"
#include "gamepad.h"

extern Snepulator_State state;
extern Snepulator_Gamepad gamepad [3];

/* Stored gamepad configuration */
extern Gamepad_Config gamepad_config [128];
extern uint32_t gamepad_config_count;


/*
 * Open an SDL Joystick by index, if it is not already open.
 */
static int32_t gamepad_sdl_os_gamepad_open (uint32_t device_index)
{
    uint32_t id = SDL_JoystickGetDeviceInstanceID (device_index);
    SDL_Joystick *joystick = SDL_JoystickFromInstanceID (id);

    /* Open the joystick if needed */
    if (joystick == NULL || SDL_JoystickGetAttached (joystick) == false)
    {
        joystick = SDL_JoystickOpen (device_index);
    }

    return SDL_JoystickInstanceID (joystick);
}


/*
 * Close an SDL Joystick by id.
 */
static void gamepad_sdl_os_gamepad_close (uint32_t id)
{
    SDL_Joystick *joystick = SDL_JoystickFromInstanceID (id);
    SDL_JoystickClose (joystick);
}


/*
 * Get the number of gamepads attached to the system.
 */
static uint32_t gamepad_sdl_os_gamepad_get_count (void)
{
    return SDL_NumJoysticks ();
}


/*
 * Get the number of gamepads attached to the system.
 */
static int32_t gamepad_sdl_os_gamepad_get_id (uint32_t device_index)
{
    return SDL_JoystickGetDeviceInstanceID (device_index);
}


/*
 * Generate a default configuration for an input device.
 */
static uint32_t gamepad_sdl_os_gamepad_create_default_config (int32_t device_index)
{
    Gamepad_Config new_config = { };

    if (device_index == GAMEPAD_ID_KEYBOARD)
    {
        memcpy (new_config.uuid, "KEYBOARD", 8);

        /* Different default keyboard mappings for different layouts */
        switch (SDL_GetKeyFromScancode (SDL_SCANCODE_S))
        {
            /* Dvorak */
            case SDLK_o:
                new_config.mapping [GAMEPAD_DIRECTION_UP]     = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_KEY, .key = SDLK_COMMA };
                new_config.mapping [GAMEPAD_DIRECTION_DOWN]   = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_KEY, .key = SDLK_o };
                new_config.mapping [GAMEPAD_DIRECTION_LEFT]   = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_KEY, .key = SDLK_a };
                new_config.mapping [GAMEPAD_DIRECTION_RIGHT]  = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_KEY, .key = SDLK_e };
                new_config.mapping [GAMEPAD_BUTTON_1]         = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_KEY, .key = SDLK_v };
                new_config.mapping [GAMEPAD_BUTTON_2]         = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_KEY, .key = SDLK_z };
                new_config.mapping [GAMEPAD_BUTTON_START]     = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_KEY, .key = SDLK_RETURN };
                break;

            /* Colemak */
            case SDLK_r:
                new_config.mapping [GAMEPAD_DIRECTION_UP]     = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_KEY, .key = SDLK_w };
                new_config.mapping [GAMEPAD_DIRECTION_DOWN]   = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_KEY, .key = SDLK_r };
                new_config.mapping [GAMEPAD_DIRECTION_LEFT]   = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_KEY, .key = SDLK_a };
                new_config.mapping [GAMEPAD_DIRECTION_RIGHT]  = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_KEY, .key = SDLK_s };
                new_config.mapping [GAMEPAD_BUTTON_1]         = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_KEY, .key = SDLK_PERIOD };
                new_config.mapping [GAMEPAD_BUTTON_2]         = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_KEY, .key = SDLK_SLASH };
                new_config.mapping [GAMEPAD_BUTTON_START]     = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_KEY, .key = SDLK_RETURN };
                break;

            /* Qwerty */
            case SDLK_s:
            default:
                new_config.mapping [GAMEPAD_DIRECTION_UP]     = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_KEY, .key = SDLK_w };
                new_config.mapping [GAMEPAD_DIRECTION_DOWN]   = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_KEY, .key = SDLK_s };
                new_config.mapping [GAMEPAD_DIRECTION_LEFT]   = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_KEY, .key = SDLK_a };
                new_config.mapping [GAMEPAD_DIRECTION_RIGHT]  = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_KEY, .key = SDLK_d };
                new_config.mapping [GAMEPAD_BUTTON_1]         = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_KEY, .key = SDLK_PERIOD };
                new_config.mapping [GAMEPAD_BUTTON_2]         = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_KEY, .key = SDLK_SLASH };
                new_config.mapping [GAMEPAD_BUTTON_START]     = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_KEY, .key = SDLK_RETURN };
                break;
        }
    }
    else
    {
        state.os_gamepad_get_uuid (device_index, new_config.uuid);

        /* Try to pull the config from the SDL_GameController interface */
        /* TODO: Fallback to SDL_GameControllerGetBindForAxis if DPAD buttons are not found. */
        if (SDL_IsGameController (device_index))
        {
            SDL_GameController *game_controller = SDL_GameControllerOpen (device_index);
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
                        new_config.mapping [i] = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_BUTTON, .button = bind.value.button };
                        break;
                    case SDL_CONTROLLER_BINDTYPE_AXIS:
                        new_config.mapping [i] = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_AXIS, .axis = bind.value.axis, .sign = axis_sign [i] };
                        break;
                    case SDL_CONTROLLER_BINDTYPE_HAT:
                        new_config.mapping [i] = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_HAT, .hat = bind.value.hat.hat, .direction = bind.value.hat.hat_mask };
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
            new_config.mapping [GAMEPAD_DIRECTION_UP]       = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_AXIS, .axis = 1, .sign = -1 };
            new_config.mapping [GAMEPAD_DIRECTION_DOWN]     = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_AXIS, .axis = 1, .sign =  1 };
            new_config.mapping [GAMEPAD_DIRECTION_LEFT]     = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_AXIS, .axis = 0, .sign = -1 };
            new_config.mapping [GAMEPAD_DIRECTION_RIGHT]    = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_AXIS, .axis = 0, .sign =  1 };
            new_config.mapping [GAMEPAD_BUTTON_1]           = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_BUTTON, .button = 2 };
            new_config.mapping [GAMEPAD_BUTTON_2]           = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_BUTTON, .button = 1 };
            new_config.mapping [GAMEPAD_BUTTON_START]       = (Gamepad_Mapping) { .type = GAMEPAD_MAPPING_TYPE_BUTTON, .button = 9 };
        }
    }

    gamepad_config [gamepad_config_count] = new_config;

    return gamepad_config_count++;
}


/*
 * Query SDL for the name of a gamepad.
 */
static const char *gamepad_sdl_os_gamepad_get_name (uint32_t device_index)
{
    const char *name = NULL;

    /* Next, try the GameController API */
    if (SDL_IsGameController (device_index))
    {
        name = SDL_GameControllerNameForIndex (device_index);
    }

    /* Next, try the Joystick API */
    if (name == NULL)
    {
        name = SDL_JoystickNameForIndex (device_index);
    }

    return name;
}


/*
 * SDL implementation for getting gamepad UUID.
 */
static void gamepad_sdl_os_gamepad_get_uuid (int32_t device_index, uint8_t *uuid)
{
    SDL_JoystickGUID sdl_guid = SDL_JoystickGetDeviceGUID (device_index);
    memcpy (uuid, sdl_guid.data, UUID_SIZE);
}


/*
 * Process an SDL_Event.
 */
void gamepad_sdl_process_event (SDL_Event *event)
{
    /* Axis */
    if (event->type == SDL_JOYAXISMOTION)
    {
        gamepad_process_axis_event (event->jaxis.which, event->jaxis.axis, event->jaxis.value);
    }

    /* Button */
    if (event->type == SDL_JOYBUTTONDOWN || event->type == SDL_JOYBUTTONUP)
    {
        gamepad_process_button_event (event->jbutton.which, event->jbutton.button, event->type == SDL_JOYBUTTONDOWN);
    }

    /* Hat */
    if (event->type == SDL_JOYHATMOTION)
    {
        gamepad_process_hat_event (event->jhat.which, event->jhat.hat, event->jhat.value);
    }

    /* Keyboard */
    if (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP)
    {
        gamepad_process_key_event (event->key.keysym.sym, event->type == SDL_KEYDOWN);
    }

    /* Mouse */
    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT &&
        state.cursor_in_gui == false)
    {
        state.cursor_button = true;
    }
    else if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT)
    {
        state.cursor_button = false;
    }
}


/*
 * Set up function pointers.
 */
void gamepad_sdl_init (void)
{
    state.os_gamepad_create_default_config = gamepad_sdl_os_gamepad_create_default_config;
    state.os_gamepad_open = gamepad_sdl_os_gamepad_open;
    state.os_gamepad_close = gamepad_sdl_os_gamepad_close;
    state.os_gamepad_get_count = gamepad_sdl_os_gamepad_get_count;
    state.os_gamepad_get_id = gamepad_sdl_os_gamepad_get_id;
    state.os_gamepad_get_name = gamepad_sdl_os_gamepad_get_name;
    state.os_gamepad_get_uuid = gamepad_sdl_os_gamepad_get_uuid;
}
