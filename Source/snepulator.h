/*
 * Snepulator common header file.
 */

#define VIDEO_SIDE_BORDER 8
#define VIDEO_BUFFER_WIDTH (256 + 2 * VIDEO_SIDE_BORDER)
#define VIDEO_BUFFER_LINES 240

#define HASH_LENGTH 12

typedef enum Console_e {
    CONSOLE_NONE = 0,
    CONSOLE_COLECOVISION,
    CONSOLE_GAME_GEAR,
    CONSOLE_MASTER_SYSTEM,
    CONSOLE_SG_1000
} Console;

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
    Console console;
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

    /* Console video output */
    float_Colour video_out_data [VIDEO_BUFFER_WIDTH * VIDEO_BUFFER_LINES];
    uint32_t     video_start_x; /* TODO: SDL_Rect? */
    uint32_t     video_start_y;
    uint32_t     video_width;
    uint32_t     video_height;
    bool         video_has_border;
    uint32_t     video_border_left_extend;
    int16_t      phaser_x;
    int16_t      phaser_y;

    /* Host video output */
    float_Colour video_out_texture_data [VIDEO_BUFFER_WIDTH * 4 * VIDEO_BUFFER_LINES * 4];
    uint32_t     video_out_texture_width;
    uint32_t     video_out_texture_height;
    int          host_width;
    int          host_height;
    Video_Filter video_filter;
    int16_t      video_scale;

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

/* Draw the logo to the output texture. */
void snepulator_draw_logo (void);

/* Display an error message */
void snepulator_error (const char *title, const char *message);

/* Pause emulation and show the pause screen. */
void snepulator_pause (void);

/* Clean up after the previously running system */
void snepulator_reset (void);

/* Call the appropriate initialisation for the chosen ROM */
void snepulator_system_init (void);
