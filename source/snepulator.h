/*
 * Snepulator common header file.
 */

#define VIDEO_SIDE_BORDER 8
#define VIDEO_BUFFER_WIDTH (256 + 2 * VIDEO_SIDE_BORDER)

#define VIDEO_BUFFER_LINES 240
#define VIDEO_TOP_BORDER_192 ((VIDEO_BUFFER_LINES - 192) / 2)

typedef enum Run_State_e {
    RUN_STATE_INIT,     /* No ROM has been loaded. */
    RUN_STATE_RUNNING,  /* A ROM has been loaded and is running. */
    RUN_STATE_PAUSED,   /* A ROM has been loaded and is paused. */
    RUN_STATE_EXIT      /* Shut down. */
} Run_State;

typedef enum Console_e {
    CONSOLE_NONE = 0,
    CONSOLE_LOGO,
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
    char *cart_filename; /* TODO: Rename to rom_filename */

    /* User emulator settings */
    bool            disable_blanking;       /* Don't blank the screen when the blank bit is set. */
    uint32_t        overclock;              /* Extra CPU cycles to run per line. */
    uint_pixel     *override_tms_palette;   /* Override default tms9928a palette. NULL for default. */
    Video_Format    format;                 /* 50 Hz PAL / 60 Hz NTSC. */
    bool            format_auto;            /* Automatically select PAL for games that require it. */
    Console_Region  region;                 /* Japan / World. */
    bool            remove_sprite_limit;    /* Remove the single line sprite limit. */
    Video_3D_Mode   video_3d_mode;          /* Left / Right / Anaglyph selection. */
    float           video_3d_saturation;    /* Colour saturation for anaglyph modes. */

    /* Host API */
    uint32_t     (*os_gamepad_create_default_config) (int32_t device_index);
    int32_t      (*os_gamepad_open) (uint32_t device_index);
    void         (*os_gamepad_close) (uint32_t id);
    uint32_t     (*os_gamepad_get_count) (void);
    int32_t      (*os_gamepad_get_id) (uint32_t device_index);
    const char * (*os_gamepad_get_name) (uint32_t device_index);
    void         (*os_gamepad_get_uuid) (int32_t device_index, uint8_t *uuid);

    /* Console API */
    Console   console;
    void     *console_context;
    void      (*audio_callback) (void *userdata, uint8_t *stream, int len);
    void      (*cleanup) (void *);
    void      (*diagnostics_print) (const char *, ...);
    void      (*diagnostics_show) (void);
    uint32_t  (*get_clock_rate) (void *);
    uint8_t * (*get_rom_hash) (void *);
    void      (*run_callback) (void *, uint32_t ms);
    void      (*soft_reset) (void);
    void      (*sync) (void *);
    void      (*state_save) (void *, const char *filename);
    void      (*state_load) (void *, const char *filename);
    void      (*update_settings) (void *);

    /* Console video output */
    uint_pixel  video_out_data [VIDEO_BUFFER_WIDTH * VIDEO_BUFFER_LINES];
    uint32_t    video_start_x;              /* Start of active area. */
    uint32_t    video_start_y;
    uint32_t    video_width;                /* Size of the active area. */
    uint32_t    video_height;
    uint32_t    video_blank_left;
    bool        video_has_border;
    int16_t     phaser_x;
    int16_t     phaser_y;

    /* Host video output */
    uint_pixel   video_pause_data [VIDEO_BUFFER_WIDTH * VIDEO_BUFFER_LINES];
    int          host_width;
    int          host_height;
    Video_Filter video_filter;
    int16_t      video_scale;
    bool         video_show_border;

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

/* Clear the screen. */
void snepulator_clear_screen (void);

/* Import settings from configuration from file. */
int snepulator_config_import (void);

/* Set state.console using the file extension. */
void snepulator_console_set_from_path (const char *path);

/* Disable screen blanking when the blanking bit is set. */
void snepulator_disable_blanking_set (bool disable_blanking);

/* Set whether or not to overclock. */
void snepulator_overclock_set (bool overclock);

/* Override the palette used for tms9928a modes. */
void snepulator_override_tms9928a_palette (uint_pixel *palette);

/* Animate the pause screen. */
void snepulator_pause_animate (void);

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




