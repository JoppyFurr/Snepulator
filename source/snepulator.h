/*
 * Snepulator common header file.
 */

#define VIDEO_SIDE_BORDER 8
#define VIDEO_BUFFER_WIDTH (256 + 2 * VIDEO_SIDE_BORDER)
#define VIDEO_BUFFER_LINES 240

#define HASH_LENGTH 12

typedef enum Run_State_e {
    RUN_STATE_INIT,     /* No ROM has been loaded. */
    RUN_STATE_RUNNING,  /* A ROM has been loaded and is running. */
    RUN_STATE_PAUSED,   /* A ROM has been loaded and is paused. */
    RUN_STATE_EXIT      /* Shut down. */
} Run_State;

typedef enum Console_e {
    CONSOLE_NONE = 0,
    CONSOLE_COLECOVISION,
    CONSOLE_GAME_GEAR,
    CONSOLE_MASTER_SYSTEM,
    CONSOLE_SG_1000
} Console;

typedef enum Video_Filter_e {
    VIDEO_FILTER_NEAREST = 0,
    VIDEO_FILTER_LINEAR,
    VIDEO_FILTER_SCANLINES,
    VIDEO_FILTER_DOT_MATRIX
} Video_Filter;

typedef enum Video_Format_e {
    VIDEO_FORMAT_NTSC = 0,
    VIDEO_FORMAT_PAL,
    VIDEO_FORMAT_AUTO
} Video_Format;

typedef enum Console_Region_t {
    REGION_WORLD = 0,
    REGION_JAPAN
} Console_Region;

typedef enum Video_3D_Mode_e {
    VIDEO_3D_RED_CYAN = 0,
    VIDEO_3D_RED_GREEN,
    VIDEO_3D_MAGENTA_GREEN,
    VIDEO_3D_LEFT_ONLY,
    VIDEO_3D_RIGHT_ONLY
} Video_3D_Mode;


typedef struct Snepulator_State_s {
    Run_State run;

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
    void (*run_callback) (uint32_t ms);
    void (*audio_callback) (void *userdata, uint8_t *stream, int len);
    uint32_t (*get_clock_rate) (void);
    bool (*get_int) (void);
    bool (*get_nmi) (void);
    void (*sync) (void);
    void (*state_save) (const char *filename);
    void (*state_load) (const char *filename);

    /* Console memory */
    uint8_t *ram;
    uint8_t *sram;
    uint8_t *vram;
    uint8_t *rom;
    uint8_t *bios;

    uint32_t rom_size;
    uint32_t rom_mask;
    uint32_t bios_size;

    /* Console configuration */
    bool            format_auto;
    Video_Format    format;                 /* 50 Hz PAL / 60 Hz NTSC. */
    Console_Region  region;
    uint32_t        overclock;              /* Extra CPU cycles to run per line. */
    bool            remove_sprite_limit;    /* Remove the single line sprite limit. */
    bool            disable_blanking;       /* Don't blank the screen when the blank bit is set. */

    /* Console video output */
    float_Colour video_out_data [VIDEO_BUFFER_WIDTH * VIDEO_BUFFER_LINES];
    uint32_t    render_start_x;
    uint32_t    render_start_y;
    uint32_t    video_start_x;
    uint32_t    video_start_y;
    uint32_t    video_width;
    uint32_t    video_height;
    uint32_t    video_blank_left;
    bool        video_has_border;
    int16_t     phaser_x;
    int16_t     phaser_y;

    /* Host video output */
    int          host_width;
    int          host_height;
    Video_Filter video_filter;
    int16_t      video_scale;
    bool         video_show_border;

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


/* Set and run a console BIOS. */
void snepulator_bios_set (const char *path);

/* Import settings from configuration from file. */
int snepulator_config_import (void);

/* Set state.console using the file extension. */
void snepulator_console_set_from_path (const char *path);

/* Disable screen blanking when the blanking bit is set. */
void snepulator_disable_blanking_set (bool disable_blanking);

/* Draw the logo to the output texture. */
void snepulator_draw_logo (void);

/* Set whether or not to overclock. */
void snepulator_overclock_set (bool overclock);

/* Pause or resume emulation. */
void snepulator_pause_set (bool pause);

/* Set the console region. */
void snepulator_region_set (Console_Region region);

/* Set whether or not to remove the sprite limit. */
void snepulator_remove_sprite_limit_set (bool remove_sprite_limit);

/* Clean up after the previously running system. */
void snepulator_reset (void);

/* Set the currently running ROM and initialise the system. */
void snepulator_rom_set (const char *path);

/* Call the appropriate initialisation for the chosen ROM. */
void snepulator_system_init (void);

/* Set the video 3D mode. */
void snepulator_video_3d_mode_set (Video_3D_Mode mode);

/* Set the video 3D colour saturation. */
void snepulator_video_3d_saturation_set (double saturation);

/* Set the console video filter. */
void snepulator_video_filter_set (Video_Filter filter);

/* Set the console video format. */
void snepulator_video_format_set (Video_Format format);

/***************************
 *  Implemented in main.c  *
 ***************************/

/* Open an SDL audio device. */
void snepulator_audio_device_open (const char *device);

/* Display an error message */
void snepulator_error (const char *title, const char *message);




