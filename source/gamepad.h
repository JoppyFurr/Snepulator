/*
 * Snepulator
 * Gamepad support header.
 */

#define GAMEPAD_ID_NONE     -1
#define GAMEPAD_ID_KEYBOARD -2

#define GAMEPAD_HAT_CENTERED    0x00
#define GAMEPAD_HAT_UP          0x01
#define GAMEPAD_HAT_RIGHT       0x02
#define GAMEPAD_HAT_DOWN        0x04
#define GAMEPAD_HAT_LEFT        0x08


/*
 * Common indexes, valid both in the config list and in the gamepad list.
 */
typedef enum Gamepad_Index_e {
    GAMEPAD_INDEX_NONE = 0,
    GAMEPAD_INDEX_KEYBOARD
} Gamepad_Index;


typedef enum Gamepad_Type_e {
    GAMEPAD_TYPE_SMS = 0,
    GAMEPAD_TYPE_SMS_PHASER,
    GAMEPAD_TYPE_SMS_PADDLE,
    GAMEPAD_TYPE_SMS_SPORTS_PAD,
    GAMEPAD_TYPE_SMS_SPORTS_PAD_CONTROL
} Gamepad_Type;

typedef enum Trackball_State_e {
    TRACKBALL_STATE_X_MSB = 0,
    TRACKBALL_STATE_X_LSB,
    TRACKBALL_STATE_Y_MSB,
    TRACKBALL_STATE_Y_LSB
} Trackball_State;


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


typedef enum Gamepad_Mapping_Type_e {
    GAMEPAD_MAPPING_TYPE_AXIS = 0,
    GAMEPAD_MAPPING_TYPE_BUTTON,
    GAMEPAD_MAPPING_TYPE_HAT,
    GAMEPAD_MAPPING_TYPE_KEY
} Gamepad_Mapping_Type;


typedef struct Gamepad_Mapping_s {
    uint32_t type;
    union {
        uint32_t axis;
        uint32_t button;
        uint32_t hat;
        uint32_t key;
    };
    union {
        uint32_t direction;
        int32_t sign;
    };
} Gamepad_Mapping;


/*
 * Stored gamepad configuration.
 */
typedef struct Gamepad_Config_s {
    uint8_t uuid [UUID_SIZE];
    Gamepad_Mapping mapping [GAMEPAD_BUTTON_COUNT];
} Gamepad_Config;


/*
 * Details for a detected gamepad.
 */
typedef struct Gamepad_Instance_s {
    int32_t instance_id;    /* ID associated with events from the device. */
    uint32_t config_index;  /* Index into the configuration library. */
    int32_t device_index;   /* Index into array of detected input devices. */
} Gamepad_Instance;

/*
 * Current state of simulated gamepad.
 */
typedef struct Snepulator_Gamepad_t {
    Gamepad_Type    type;       /* Emulated gamepad type. */
    bool            type_auto;  /* Auto-select emulated gamepad type based on game. */
    int32_t         id;         /* ID associated with events for this gamepad. */
    Gamepad_Config *config;     /* Button mapping configuration */
    bool            state [GAMEPAD_BUTTON_COUNT];

    /* SMS Paddle Controller */
    float           paddle_velocity;
    float           paddle_position;
    union {
        uint8_t     paddle_data;
        struct {
            uint8_t paddle_data_low:4;
            uint8_t paddle_data_high:4;
        };
    };

    /* SMS Sports Pad */
    Trackball_State trackball_state;
    bool            trackball_strobe;
    uint32_t        trackball_strobe_time;  /* Timestamp of the most recent strobe, in cycles */
    double_point_t  trackball_delta;
    union {
        uint8_t     trackball_x;
        struct {
            uint8_t trackball_x_low:4;
            uint8_t trackball_x_high:4;
        };
    };
    union {
        uint8_t     trackball_y;
        struct {
            uint8_t trackball_y_low:4;
            uint8_t trackball_y_high:4;
        };
    };

    /* SMS Sports Pad, Control Mode */
    uint64_t control_last_poll;
    uint64_t control_up_event;
    uint64_t control_down_event;
    uint64_t control_left_event;
    uint64_t control_right_event;

} Snepulator_Gamepad;


/* Handle an axis event. */
void gamepad_process_axis_event (int32_t id, int32_t axis, int32_t value);

/* Handle a button event. */
void gamepad_process_button_event (int32_t id, int32_t device_button, bool button_down);

/* Handle a hat event. */
void gamepad_process_hat_event (int32_t id, int32_t hat, int32_t direction);

/* Handle a keyboard event. */
void gamepad_process_key_event (int32_t key, bool key_down);

/* Update the mapping for a known gamepad. */
void gamepad_update_mapping (Gamepad_Config device);

/* Initialise gamepad support. */
void gamepad_init (void);

/* Refresh the list of detected gamepads. */
void gamepad_list_update (void);

/* Called to simulate gamepad hardware. (paddle) */
void gamepad_paddle_tick (uint32_t cycles);

/* Handle strobe-signal for trackball. */
void gamepad_trackball_strobe (bool strobe, uint64_t current_time);

/* Get the trackball output pin values. */
uint8_t gamepad_trackball_get_port (uint64_t current_time);

/* Get the trackball output pin values (control-mode). */
uint8_t gamepad_trackball_control_get_port (uint64_t current_time);

/* Return the number of players using the specified joystick id. */
uint32_t gamepad_joystick_user_count (uint32_t id);

/* Get the name of a gamepad in our config array. */
const char *gamepad_get_name (uint32_t list_index);

/* Change input device for a player's gamepad. */
void gamepad_change_device (uint32_t player, int32_t list_index);

/* Store the gamepad configuration to the configuration file. */
void gamepad_config_export (void);

