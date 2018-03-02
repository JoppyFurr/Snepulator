
#define VDP_BORDER 8
#define VDP_STRIDE (256 + VDP_BORDER * 2)


#define ID_NONE     -1
#define ID_KEYBOARD -2

typedef struct Float3_t {
    float data [3];
} Float3;


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
    float        sms_vdp_texture_data        [(256 + VDP_BORDER * 2) * (192 + VDP_BORDER * 2) * 3];
    float        sms_vdp_texture_data_output [(256 + VDP_BORDER * 2) * 2 * (192 + VDP_BORDER * 2) * 3 * 3];
    int          host_width;
    int          host_height;

    /* Statistics */
    double host_framerate;
    double vdp_framerate;
    double audio_ring_utilisation;

} Snepulator;

