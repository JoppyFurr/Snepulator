
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "snepulator_types.h"
#include "gamepad.h"

extern Snepulator_Gamepad gamepad [3];

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

    /* Special case for Light Phaser (mouse) */
    if (gamepad [1].type == GAMEPAD_TYPE_SMS_PHASER)
    {
        if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT)
        {
            gamepad [1].state [GAMEPAD_BUTTON_1] = true;
        }
        else if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT)
        {
            gamepad [1].state [GAMEPAD_BUTTON_1] = false;
        }
    }
}


