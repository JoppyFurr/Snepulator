/*
 * Snepulator common header file.
 */

#define VIDEO_SIDE_BORDER 8
#define VIDEO_BUFFER_WIDTH (256 + 2 * VIDEO_SIDE_BORDER)
#define VIDEO_BUFFER_LINES 240

#define ID_NONE     -1
#define ID_KEYBOARD -2

typedef struct Button_Mapping_t {
    uint32_t type;
    uint32_t value;
    bool negative;
} Button_Mapping;

typedef struct Gamepad_Mapping_t {
    int32_t device_id;
    Button_Mapping direction_up;
    Button_Mapping direction_down;
    Button_Mapping direction_left;
    Button_Mapping direction_right;
    Button_Mapping button_1;
    Button_Mapping button_2;
    Button_Mapping pause;
} Gamepad_Mapping;

typedef enum Video_Filter_t {
    VIDEO_FILTER_NEAREST,
    VIDEO_FILTER_LINEAR,
    VIDEO_FILTER_SCANLINES,
} Video_Filter;

typedef struct Snepulator_t {
    bool    abort ;
    bool    running ;

    /* Files */
    char *bios_filename;
    char *cart_filename;

    /* Video */
    Video_Filter video_filter;
    float        sms_vdp_texture_data [VIDEO_BUFFER_WIDTH * VIDEO_BUFFER_LINES * 3];
    float        sms_vdp_texture_data_scanlines [VIDEO_BUFFER_WIDTH * 2 * (VIDEO_BUFFER_LINES) * 3 * 3];
    int          host_width;
    int          host_height;

    /* Statistics */
    double host_framerate;
    double vdp_framerate;
    double audio_ring_utilisation;

} Snepulator;

/* Update the in-memory button mapping for an input device. */
void snepulator_update_input_device (Gamepad_Mapping device);
