/*
 * Gamepad input implementation.
 *
 * This file deals with taking input inputs, and provides an abstract
 * Snepulator_Gamepad interface to be used in the emulated consoles.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "snepulator_types.h"
#include "snepulator.h"
#include "gamepad.h"
#include "config.h"

extern Snepulator_State state;

#define MAX_STRING_SIZE 1024

const char *button_names [] = { "up", "down", "left", "right", "button-1", "button-2", "start" };


/* Stored gamepad configuration */
Gamepad_Config gamepad_config [128];
uint32_t gamepad_config_count = 0;

Gamepad_Config remap_config;
uint32_t gamepad_remap_step = GAMEPAD_BUTTON_COUNT;

/* List of detected gamepads */
Gamepad_Instance gamepad_list [128];
uint32_t gamepad_list_count = 0;

/*
 * Gamepad State
 *
 *   gamepad [0]: Gamepad selected in settings menu.
 *   gamepad [1]: Player 1.
 *   gamepad [2]: Player 2.
 *   remap_config: Uncommitted remap
 */
Snepulator_Gamepad gamepad [3];


/*
 * Use an axis-input to set a gamepad mapping.
 */
static void gamepad_process_axis_event_remap (int32_t axis, int32_t value)
{
    /* If an axis is triggered, ignore all axis motions while it returns to the centre */
    static bool waiting = false;
    static uint32_t waiting_axis;

    if (waiting)
    {
        if (axis == waiting_axis && value < 4000 && value > -4000)
        {
            waiting = false;
        }
    }
    else if (value > 16000 || value < -16000)
    {
        int32_t sign = (value < 0) ? -1 : 1;

        waiting = true;
        waiting_axis = axis;

        remap_config.mapping [gamepad_remap_step].type = GAMEPAD_MAPPING_TYPE_AXIS;
        remap_config.mapping [gamepad_remap_step].axis = axis;
        remap_config.mapping [gamepad_remap_step].sign = sign;
        gamepad_remap_step++;
    }

    /* If this was the last button, commit the remap */
    if (gamepad_remap_step == GAMEPAD_BUTTON_COUNT)
    {
        gamepad_update_mapping (remap_config);
    }
}


/*
 * Handle an axis event.
 *
 * Expected axis-range is -32768 through +32767
 */
void gamepad_process_axis_event (int32_t id, int32_t axis, int32_t value)
{
    if (gamepad_remap_step < GAMEPAD_BUTTON_COUNT && id == gamepad [0].id)
    {
        gamepad_process_axis_event_remap (axis, value);
        return;
    }

    for (uint32_t player = 0; player < 3; player++)
    {
        if (gamepad [player].id != id)
        {
            continue;
        }

        for (uint32_t button = 0; button < GAMEPAD_BUTTON_COUNT; button++)
        {
            if (gamepad [player].config->mapping [button].type == GAMEPAD_MAPPING_TYPE_AXIS)
            {
                if (gamepad [player].config->mapping [button].axis == axis)
                {
                    gamepad [player].state [button] = (value * gamepad [player].config->mapping [button].sign) > 16000 ? 1 : 0;
                }
            }
        }
    }
}


/*
 * Use a button-input to set a gamepad mapping.
 */
static void gamepad_process_button_event_remap (int32_t device_button)
{
    remap_config.mapping [gamepad_remap_step].type = GAMEPAD_MAPPING_TYPE_BUTTON;
    remap_config.mapping [gamepad_remap_step].button = device_button;
    gamepad_remap_step++;

    /* If this was the last button, commit the remap */
    if (gamepad_remap_step == GAMEPAD_BUTTON_COUNT)
    {
        gamepad_update_mapping (remap_config);
    }
}


/*
 * Handle a button event.
 */
void gamepad_process_button_event (int32_t id, int32_t device_button, bool button_down)
{
    if (gamepad_remap_step < GAMEPAD_BUTTON_COUNT && id == gamepad [0].id && button_down)
    {
        gamepad_process_button_event_remap (device_button);
        return;
    }

    for (uint32_t player = 0; player < 3; player++)
    {
        if (gamepad [player].id != id)
        {
            continue;
        }

        for (uint32_t button = 0; button < GAMEPAD_BUTTON_COUNT; button++)
        {
            if (gamepad [player].config->mapping [button].type == GAMEPAD_MAPPING_TYPE_BUTTON)
            {
                if (gamepad [player].config->mapping [button].button == device_button)
                {
                    gamepad [player].state [button] = button_down;
                }
            }
        }
    }
}


/*
 * Use a hat-input to set a gamepad mapping.
 */
static void gamepad_process_hat_event_remap (int32_t hat, int32_t direction)
{
    /* If a hat is triggered, ignore all hat motion while it returns to the centre */
    static bool waiting = false;
    static uint8_t waiting_hat;

    if (waiting)
    {
        if (hat == waiting_hat && direction == GAMEPAD_HAT_CENTERED)
        {
            waiting = false;
        }
    }
    else if (direction == GAMEPAD_HAT_UP || direction == GAMEPAD_HAT_DOWN ||
             direction == GAMEPAD_HAT_LEFT || direction == GAMEPAD_HAT_RIGHT)
    {
        waiting = true;
        waiting_hat = hat;

        remap_config.mapping [gamepad_remap_step].type = GAMEPAD_MAPPING_TYPE_HAT;
        remap_config.mapping [gamepad_remap_step].hat = hat;
        remap_config.mapping [gamepad_remap_step].direction = direction;
        gamepad_remap_step++;
    }

    /* If this was the last button, commit the remap */
    if (gamepad_remap_step == GAMEPAD_BUTTON_COUNT)
    {
        gamepad_update_mapping (remap_config);
    }
}


/*
 * Handle a hat event.
 */
void gamepad_process_hat_event (int32_t id, int32_t hat, int32_t direction)
{
    if (gamepad_remap_step < GAMEPAD_BUTTON_COUNT && id == gamepad [0].id)
    {
        gamepad_process_hat_event_remap (hat, direction);
        return;
    }

    for (uint32_t player = 0; player < 3; player++)
    {
        if (gamepad [player].id != id)
        {
            continue;
        }

        for (uint32_t button = 0; button < GAMEPAD_BUTTON_COUNT; button++)
        {
            if (gamepad [player].config->mapping [button].type == GAMEPAD_MAPPING_TYPE_HAT)
            {
                if (gamepad [player].config->mapping [button].hat == hat)
                {
                    gamepad [player].state [button] = (gamepad [player].config->mapping [button].direction & direction) ? 1 : 0;
                }
            }
        }
    }
}


/*
 * Use a keyboard-input to set a gamepad mapping.
 */
static void gamepad_process_key_event_remap (int32_t key)
{
    remap_config.mapping [gamepad_remap_step].type = GAMEPAD_MAPPING_TYPE_KEY;
    remap_config.mapping [gamepad_remap_step].key = key;
    gamepad_remap_step++;

    /* If this was the last button, commit the remap */
    if (gamepad_remap_step == GAMEPAD_BUTTON_COUNT)
    {
        gamepad_update_mapping (remap_config);
    }
}


/*
 * Handle a keyboard event.
 */
void gamepad_process_key_event (int32_t key, bool key_down)
{
    if (gamepad_remap_step < GAMEPAD_BUTTON_COUNT && gamepad [0].id == GAMEPAD_ID_KEYBOARD && key_down)
    {
        gamepad_process_key_event_remap (key);
        return;
    }

    for (uint32_t player = 0; player < 3; player++)
    {
        for (uint32_t button = 0; button < GAMEPAD_BUTTON_COUNT; button++)
        {
            if (gamepad [player].config->mapping [button].type == GAMEPAD_MAPPING_TYPE_KEY)
            {
                if (gamepad [player].config->mapping [button].key == key)
                {
                    gamepad [player].state [button] = key_down;
                }
            }
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
    int16_t new_position = gamepad [1].paddle_position;

    /* Temporary™ digital-only support */
    if (gamepad [1].state [GAMEPAD_DIRECTION_LEFT])
    {
        gamepad [1].paddle_velocity = -1.0;
    }
    else if (gamepad [1].state [GAMEPAD_DIRECTION_RIGHT])
    {
        gamepad [1].paddle_velocity = 1.0;
    }
    else
    {
        gamepad [1].paddle_velocity = 0.0;
    }

    /* Calculate and apply movement */
    delta = gamepad [1].paddle_velocity * paddle_speed * (ms * 0.001) + remainder;
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

    gamepad [1].paddle_position = new_position;
}


/*
 * Refresh the list of detected gamepads.
 */
void gamepad_list_update (void)
{
    gamepad_list_count = 0;

    gamepad_list [gamepad_list_count++] = (Gamepad_Instance) { .instance_id = GAMEPAD_ID_NONE, .config_index = GAMEPAD_INDEX_NONE };
    gamepad_list [gamepad_list_count++] = (Gamepad_Instance) { .instance_id = GAMEPAD_ID_KEYBOARD, .config_index = GAMEPAD_INDEX_KEYBOARD };

    if (state.os_gamepad_get_uuid == NULL ||
        state.os_gamepad_get_count == NULL ||
        state.os_gamepad_get_id == NULL)
    {
        return;
    }

    for (int device_index = 0; device_index < state.os_gamepad_get_count (); device_index++)
    {
        int32_t instance_id = state.os_gamepad_get_id (device_index);
        int32_t config_index = GAMEPAD_INDEX_NONE;
        uint8_t uuid [UUID_SIZE] = { };

        state.os_gamepad_get_uuid (device_index, uuid);

        /* Find config for device */
        for (uint32_t i = 0; i < gamepad_config_count; i++)
        {
            if (memcmp (&gamepad_config [i].uuid, &uuid, UUID_SIZE) == 0)
            {
                config_index = i;
                break;
            }
        }

        /* If no config was found, generate a new config */
        if (config_index == GAMEPAD_INDEX_NONE)
        {
            config_index = state.os_gamepad_create_default_config (device_index);
        }

        gamepad_list [gamepad_list_count++] = (Gamepad_Instance) { .instance_id = instance_id,
                                                                   .device_index = device_index,
                                                                   .config_index = config_index };
    }
}


void gamepad_config_import (void);

/*
 * Initialise gamepad support.
 */
void gamepad_init (void)
{
    Gamepad_Config no_gamepad = { };

    /* Add entry for GAMEPAD_INDEX_NONE */
    gamepad_config [gamepad_config_count++] = no_gamepad;

    /* Initialise all gamepads to GAMEPAD_INDEX_NONE */
    gamepad [0].id = GAMEPAD_ID_NONE;
    gamepad [0].type = GAMEPAD_TYPE_SMS;
    gamepad [0].config = &gamepad_config [GAMEPAD_INDEX_NONE];

    gamepad [1].id = GAMEPAD_ID_NONE;
    gamepad [1].type = GAMEPAD_TYPE_SMS;
    gamepad [1].type_auto = true;
    gamepad [1].config = &gamepad_config [GAMEPAD_INDEX_NONE];
    gamepad [1].paddle_velocity = 0;
    gamepad [1].paddle_position = 128;

    gamepad [2].id = GAMEPAD_ID_NONE;
    gamepad [2].type = GAMEPAD_TYPE_SMS;
    gamepad [2].config = &gamepad_config [GAMEPAD_INDEX_NONE];

    state.os_gamepad_create_default_config (GAMEPAD_ID_KEYBOARD);

    /* Read saved config from file */
    gamepad_config_import ();

    /* Populate the gamepad list */
    gamepad_list_update ();

    /* Set default devices for players */
    gamepad_change_device (0, GAMEPAD_INDEX_NONE);
    gamepad_change_device (1, GAMEPAD_INDEX_KEYBOARD);
    gamepad_change_device (2, GAMEPAD_INDEX_NONE);
}


/*
 * Update the mapping for a known gamepad.
 */
void gamepad_update_mapping (Gamepad_Config new_config)
{
    for (uint32_t i = 0; i < gamepad_config_count; i++)
    {
        if (memcmp (&gamepad_config [i].uuid, &new_config.uuid, UUID_SIZE) == 0)
        {
            /* Replace the old entry with the new one */
            gamepad_config [i] = new_config;
            return;
        }
    }

    fprintf (stderr, "Warning: Unable to find device.\n");
}


/*
 * Get the name of a gamepad in our gamepad_list array.
 */
const char *gamepad_get_name (uint32_t list_index)
{
    static char name_data [128] [MAX_STRING_SIZE];
    const char *name = NULL;

    if (list_index == GAMEPAD_INDEX_NONE)
    {
        name = "None";
    }
    else if (list_index == GAMEPAD_INDEX_KEYBOARD)
    {
        name = "Keyboard";
    }
    else if (state.os_gamepad_get_name != NULL)
    {
        name = state.os_gamepad_get_name (gamepad_list [list_index].device_index);
    }

    /* Fallback if no name was found */
    if (name == NULL)
    {
        name = "Unknown Gamepad";
    }

    /* Finally, store the name, along with a unique tag */
    sprintf (name_data [list_index], "%s##%d", name, list_index);

    return name_data [list_index];
}


/*
 * Return the number of players using the specified joystick id.
 */
uint32_t gamepad_joystick_user_count (uint32_t id)
{
    uint32_t count = 0;

    for (uint32_t player = 0; player < 3; player++)
    {
        if (gamepad [player].id == id)
        {
            count++;
        }
    }

    return count;
}


/*
 * Change input device for a player's gamepad.
 */
void gamepad_change_device (uint32_t player, int32_t list_index)
{
    if (list_index > GAMEPAD_INDEX_KEYBOARD &&
        (state.os_gamepad_open == NULL ||
         state.os_gamepad_close == NULL))
    {
        return;
    }

    /* Close the previous joystick if we are the only user */
    if (gamepad [player].id > GAMEPAD_ID_NONE && gamepad_joystick_user_count (gamepad [player].id) == 1)
    {
        state.os_gamepad_close (gamepad [player].id);
    }
    gamepad [player].id = GAMEPAD_ID_NONE;
    gamepad [player].config = &gamepad_config [GAMEPAD_INDEX_NONE];

    if (list_index == GAMEPAD_INDEX_NONE)
    {
        return;
    }

    /* Open the new device */
    if (list_index == GAMEPAD_INDEX_KEYBOARD)
    {
        gamepad [player].config = &gamepad_config [GAMEPAD_INDEX_KEYBOARD];
        gamepad [player].id = GAMEPAD_ID_KEYBOARD;
    }
    else
    {
        uint32_t id = state.os_gamepad_open (gamepad_list [list_index].device_index);

        if (id >= 0)
        {
            gamepad [player].config = &gamepad_config [gamepad_list [list_index].config_index];
            gamepad [player].id = id;
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

    for (uint32_t i = GAMEPAD_INDEX_KEYBOARD; i < gamepad_config_count; i++)
    {
        /* Section name */
        snprintf (section_name, 1023, "gamepad-%02x%02x.%02x%02x.%02x%02x.%02x%02x.%02x%02x.%02x%02x.%02x%02x.%02x%02x",
                gamepad_config [i].uuid [ 0], gamepad_config [i].uuid [ 1], gamepad_config [i].uuid [ 2],
                gamepad_config [i].uuid [ 3], gamepad_config [i].uuid [ 4], gamepad_config [i].uuid [ 5],
                gamepad_config [i].uuid [ 6], gamepad_config [i].uuid [ 7], gamepad_config [i].uuid [ 8],
                gamepad_config [i].uuid [ 9], gamepad_config [i].uuid [10], gamepad_config [i].uuid [11],
                gamepad_config [i].uuid [12], gamepad_config [i].uuid [13], gamepad_config [i].uuid [14],
                gamepad_config [i].uuid [15]);

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
    unsigned int uuid_buffer [16] = { 0x00 };
    Gamepad_Config new_config = { };

    for (uint32_t i = 0; config_get_section_name (i) != NULL; i++)
    {
        const char *section_name = config_get_section_name (i);

        if (strncmp (section_name, "gamepad-", 8) == 0)
        {
            uint8_t uuid [UUID_SIZE] = { };

            sscanf (section_name, "gamepad-%02x%02x.%02x%02x.%02x%02x.%02x%02x.%02x%02x.%02x%02x.%02x%02x.%02x%02x",
                    &uuid_buffer [ 0], &uuid_buffer [ 1], &uuid_buffer [ 2], &uuid_buffer [ 3], &uuid_buffer [ 4],
                    &uuid_buffer [ 5], &uuid_buffer [ 6], &uuid_buffer [ 7], &uuid_buffer [ 8], &uuid_buffer [ 9],
                    &uuid_buffer [10], &uuid_buffer [11], &uuid_buffer [12], &uuid_buffer [13], &uuid_buffer [14],
                    &uuid_buffer [15]);

            for (uint32_t j = 0; j < 16; j++)
            {
                uuid [j] = uuid_buffer [j];
            }

            /* Skip the 'None' device */
            if (memcmp (uuid, &gamepad_config [GAMEPAD_INDEX_NONE].uuid, UUID_SIZE) == 0)
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

                memcpy (new_config.uuid, uuid, sizeof (new_config.uuid));
            }

            /* Keyboard has fixed index */
            if (memcmp (&uuid, &gamepad_config [GAMEPAD_INDEX_KEYBOARD].uuid, UUID_SIZE) == 0)
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
