/*
 * Gamepad input.
 */

/* TODO: Make better use of the API:
 *
 *   device_id - Index from 0 to SDL_NumJoysticks (). Changes as joysticks are added / removed.
 *
 *   instance_id - Identifier of the current instance of the joystick. Increments if the joystick is reconnected.
 *
 *   GUID - Stable identifier for a model of joystick. Two joysticks with the same vendor / product / version may have the same GUID.
 *
 */

#define INSTANCE_ID_NONE     -1
#define INSTANCE_ID_KEYBOARD -2

#define GUID_NONE     (SDL_JoystickGUID) { .data = { 'K', 'e', 'y', 'b', 'o', 'a', 'r', 'd' } }
#define GUID_KEYBOARD (SDL_JoystickGUID) { .data = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } }

/*
 * Common indexes, valid both in the config list and in the gamepad list.
 */
typedef enum Gamepad_Index_e {
    GAMEPAD_INDEX_NONE = 0,
    GAMEPAD_INDEX_KEYBOARD
} Gamepad_Index;


typedef enum Gamepad_Type_e {
    GAMEPAD_TYPE_SMS = 0,
    GAMEPAD_TYPE_SMS_PADDLE
} Gamepad_Type;


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
        uint32_t hat;
    };
    union {
        uint32_t direction;
        int32_t sign;
    };
} Gamepad_Mapping;

#ifdef SDL_h_
/*
 * Stored gamepad configuration.
 */
typedef struct Gamepad_Config_s {
    SDL_JoystickGUID guid;
    Gamepad_Mapping mapping [GAMEPAD_BUTTON_COUNT];
} Gamepad_Config;
#endif /* SDL_h_ */


/*
 * Details for a detected gamepad.
 *
 * TODO: Recreate this structure each time a gamepad is connected / disconnected.
 */
typedef struct Gamepad_Instance_s {
    int32_t instance_id;
    uint32_t config_index;
    int32_t device_id;
} Gamepad_Instance;

/*
 * Current state of simulated gamepad.
 */
typedef struct Snepulator_Gamepad_t {
    Gamepad_Type type;
    bool         type_auto;
    int32_t      instance_id;
    bool         state [GAMEPAD_BUTTON_COUNT];
    float        paddle_velocity;
    uint8_t      paddle_position;
} Snepulator_Gamepad;

#ifdef SDL_h_
/* Update gamepad state when we see relevant SDL_Events. */
void gamepad_process_event (SDL_Event *event);

/* Update the mapping for a known gamepad. */
void gamepad_update_mapping (Gamepad_Config device);
#endif /* SDL_h_ */

/* Initialise gamepad support. */
void gamepad_init (void);

/* Refresh the list of detected gamepads. */
void gamepad_list_update (void);

/* Called to simulate gamepad hardware. (paddle) */
void gamepad_paddle_tick (uint32_t ms);

/* Return the number of players using the specified joystick instance_id. */
uint32_t gamepad_joystick_user_count (uint32_t instance_id);

/* Get the name of a gamepad in our config array. */
const char *gamepad_get_name (uint32_t index);

/* Change input device for a player's gamepad. */
void gamepad_change_device (uint32_t player, int32_t index);

