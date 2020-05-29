/*
 * Gamepad input.
 */

#define ID_NONE     -1
#define ID_KEYBOARD -2

typedef enum Gamepad_Button_e {
    GAMEPAD_DIRECTION_UP = 0,
    GAMEPAD_DIRECTION_DOWN,
    GAMEPAD_DIRECTION_LEFT,
    GAMEPAD_DIRECTION_RIGHT,
    GAMEPAD_BUTTON_1,
    GAMEPAD_BUTTON_2,
    GAMEPAD_BUTTON_START,
    GAMEPAD_BUTTON_COUNT
} Gamepad_Button;

typedef struct Gamepad_Mapping_s {
    uint32_t type;
    union {
        uint32_t key;
        uint32_t button;
        uint32_t axis;
    };
    int32_t sign;
} Gamepad_Mapping;

/* TODO: Make better use of the API:
 *
 *   device_id - Index from 0 to SDL_NumJoysticks (). Changes as joysticks are added / removed.
 *
 *   instance_id - Identifier of the current instance of the joystick. Increments if the joystick is reconnected.
 *
 *   GUID - Stable identifier
 *
 */

typedef struct Gamepad_Config_s {
    /* SDL_JoystickGUID guid; */
    Gamepad_Mapping mapping [GAMEPAD_BUTTON_COUNT];
    int32_t device_id;
} Gamepad_Config;

typedef struct Snepulator_Gamepad_t {
    bool state [GAMEPAD_BUTTON_COUNT];
} Snepulator_Gamepad;

#ifdef SDL_h_
/* Update gamepad state when we see relevant SDL_Events. */
void gamepad_process_event (SDL_Event *event);
#endif /* SDL_h_ */

/* Update the mapping for a known gamepad. */
void gamepad_update_mapping (Gamepad_Config device);

/* Detect input devices and populate the in-memory mapping list.  */
void gamepad_init_input_devices (void);

