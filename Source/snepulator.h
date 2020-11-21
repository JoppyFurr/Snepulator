/*
 * Snepulator common header file.
 */

#define VIDEO_SIDE_BORDER 8
#define VIDEO_BUFFER_WIDTH (256 + 2 * VIDEO_SIDE_BORDER)
#define VIDEO_BUFFER_LINES 240

#define HASH_LENGTH 12

typedef enum Video_Filter_e {
    VIDEO_FILTER_NEAREST,
    VIDEO_FILTER_LINEAR,
    VIDEO_FILTER_SCANLINES
} Video_Filter;

typedef enum Video_3D_Mode_e {
        VIDEO_3D_LEFT_ONLY,
        VIDEO_3D_RIGHT_ONLY,
        VIDEO_3D_RED_CYAN,
        VIDEO_3D_RED_GREEN,
        VIDEO_3D_MAGENTA_GREEN
} Video_3D_Mode;

typedef enum Video_Format_e {
    VIDEO_FORMAT_NTSC,
    VIDEO_FORMAT_PAL
} Video_Format;

typedef enum Console_Region_t {
    REGION_WORLD,
    REGION_JAPAN
} Console_Region;


typedef struct Snepulator_State_s {
    bool    ready;      /* Ready to run - A console and ROM are loaded. */
    bool    running;    /* Emulation is actively running. */
    bool    abort;      /* Attempt to cleanly exit. */

    /* Interface */
    bool show_gui;
    uint32_t mouse_time;

    /* Files */
    char *sms_bios_filename;
    char *colecovision_bios_filename;
    char *cart_filename;
    uint8_t rom_hash [HASH_LENGTH];
    uint8_t rom_hints;

    /* Console API */
    void (*run) (uint32_t ms);
    void (*audio_callback) (void *userdata, uint8_t *stream, int len);
    uint32_t (*get_clock_rate) (void);
    bool (*get_int) (void);
    bool (*get_nmi) (void);
    void (*sync) (void);

    /* Console memory */
    uint8_t *ram;
    uint8_t *sram;
    uint8_t *rom;
    uint8_t *bios;

    uint32_t rom_size;
    uint32_t rom_mask;
    uint32_t bios_size;

    /* Console configuration */
    bool           format_auto;
    Video_Format   format;
    Console_Region region;

    /* Video */
    Video_Filter video_filter;
    float_Colour video_out_data [VIDEO_BUFFER_WIDTH * VIDEO_BUFFER_LINES];
    float_Colour video_out_texture_data [VIDEO_BUFFER_WIDTH * 2 * VIDEO_BUFFER_LINES * 3];
    uint32_t     video_out_first_active_line;
    uint32_t     video_extra_left_border;
    uint32_t     video_height;
    uint32_t     video_width;
    int          host_width;
    int          host_height;

    /* 3D */
    Video_3D_Mode video_3d_mode;
    float         video_3d_saturation;

    /* Statistics */
    double host_framerate;
    double vdp_framerate;
    double audio_ring_utilisation;

    /* Error reporting */
    char  error_buffer[80];
    char *error_title;
    char *error_message;

} Snepulator_State;

#if 0
/* Update the in-memory button mapping for an input device. */
void snepulator_update_input_device (Gamepad_Mapping device);
#endif

/* Clean up after the previously running system */
void snepulator_reset (void);

/* Call the appropriate initialisation for the chosen ROM */
void system_init (void);

/* Display an error message */
void snepulator_error (const char *title, const char *message);

/* Pause emulation and show the pause screen. */
void snepulator_pause (void);
