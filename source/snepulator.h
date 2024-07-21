/*
 * Snepulator
 * Global state, enums, and prototypes.
 */

/* Headers that supply the types used in this file. */
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "snepulator_compat.h"
#include "snepulator_types.h"

#define VIDEO_SIDE_BORDER 8
#define VIDEO_BUFFER_WIDTH (256 + 2 * VIDEO_SIDE_BORDER)

#define VIDEO_BUFFER_LINES 240
#define VIDEO_TOP_BORDER_192 ((VIDEO_BUFFER_LINES - 192) / 2)

#define AUDIO_SAMPLE_RATE 48000

/* Clock Rates */
#define CLOCK_1_MHZ                 1000000
#define NTSC_COLOURBURST_FREQ       3579545
#define PAL_COLOURBURST_4_5_FREQ    3546895

typedef enum Run_State_e {
    RUN_STATE_INIT,     /* No ROM has been loaded. */
    RUN_STATE_RUNNING,  /* A ROM has been loaded and is running. */
    RUN_STATE_PAUSED,   /* A ROM has been loaded and is paused. */
    RUN_STATE_EXIT      /* Shut down. */
} Run_State;

typedef enum Console_e {
    CONSOLE_NONE = 0,
    CONSOLE_LOGO,
    CONSOLE_VGM_PLAYER,
    CONSOLE_COLECOVISION,
    CONSOLE_GAME_GEAR,
    CONSOLE_MASTER_SYSTEM,
    CONSOLE_SG_1000
} Console;

typedef enum Shader_e {
    SHADER_NEAREST = 0,
    SHADER_NEAREST_SOFT,
    SHADER_LINEAR,
    SHADER_SCANLINES,
    SHADER_DOT_MATRIX,
    SHADER_COUNT
} Shader;

typedef enum Video_Format_e {
    VIDEO_FORMAT_NTSC = 0,
    VIDEO_FORMAT_PAL,
    VIDEO_FORMAT_AUTO
} Video_Format;

typedef enum Console_Region_t {
    REGION_WORLD = 0,
    REGION_JAPAN
} Console_Region;

typedef enum Video_PAR_e {
    VIDEO_PAR_1_1 = 0,
    VIDEO_PAR_8_7,
    VIDEO_PAR_6_5,
    VIDEO_PAR_18_13
} Video_PAR;

typedef enum Video_3D_Mode_e {
    VIDEO_3D_RED_CYAN = 0,
    VIDEO_3D_RED_GREEN,
    VIDEO_3D_MAGENTA_GREEN,
    VIDEO_3D_LEFT_ONLY,
    VIDEO_3D_RIGHT_ONLY
} Video_3D_Mode;


typedef struct Snepulator_State_s {
    Run_State run;
    pthread_mutex_t run_mutex;
    pthread_mutex_t video_mutex;

    /* Interface */
    bool show_gui;
    uint32_t mouse_time;

    /* Timing */
    uint64_t run_timer;
    uint64_t micro_clocks;
    uint32_t sync_time;

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
    bool            fm_sound;               /* Enable FM sound support for the Master System. */
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
    uint32_t  clock_rate;
    void     *console_context;
    void      (*audio_callback) (void *, int16_t *stream, uint32_t count);
    void      (*cleanup) (void *);
    uint8_t * (*get_rom_hash) (void *);
    void      (*run_callback) (void *, uint32_t cycles);
    void      (*soft_reset) (void);
    void      (*sync) (void *);
    void      (*state_save) (void *, const char *filename);
    void      (*state_load) (void *, const char *filename);
    void      (*update_settings) (void *);
#ifdef DEVELOPER_BUILD
    void      (*diagnostics_print) (const char *, ...);
    void      (*diagnostics_show) (void);
#endif

    /* Console video output */
    /* Note: Unlike the audio rings, in this case the read index is not
     *       the next frame to be read; but rather the frame currently
     *       being shown. This is because the frame may be shown a second
     *       time, but we don't want to wait for another memcpy.
     *       To simplify the comparisons, the write index will also point
     *       to the most recently written frame rather than the next frame
     *       to be written.
     *       This should change to match the audio rings when we start
     *       passing pointers instead of copying buffers. */
    uint_pixel  video_ring [3] [VIDEO_BUFFER_WIDTH * VIDEO_BUFFER_LINES];
    uint32_t    video_read_index;
    uint32_t    video_write_index;
    uint32_t    video_start_x;              /* Start of active area. */
    uint32_t    video_start_y;
    uint32_t    video_width;                /* Size of the active area. */
    uint32_t    video_height;
    int16_t     cursor_x;                   /* User's cursor coordinate within active area. */
    int16_t     cursor_y;
    bool        cursor_button;
    bool        cursor_in_gui;              /* Cursor is interacting with the GUI. */

    /* Host video output */
    uint_pixel  video_pause_data [VIDEO_BUFFER_WIDTH * VIDEO_BUFFER_LINES];
    int         host_width;
    int         host_height;
    Shader      shader;
    bool        integer_scaling;
    float       video_scale;
    Video_PAR   video_par_setting;
    float       video_par;

    /* Statistics */
#ifdef DEVELOPER_BUILD
    double host_framerate;
    double vdp_framerate;
    double audio_ring_utilisation;
#endif

    /* Error reporting */
    char  error_buffer[80];
    char *error_title;
    char *error_message;

} Snepulator_State;


/* Set and run a console BIOS. */
void snepulator_bios_set (const char *path);

/* Clear the screen. */
void snepulator_clear_video (void);

/* Import settings from configuration from file. */
int snepulator_config_import (void);

/* Set state.console using the file extension. */
void snepulator_console_set_from_path (const char *path);

/* Disable screen blanking when the blanking bit is set. */
void snepulator_disable_blanking_set (bool disable_blanking);

/* Enable the FM Sound Unit for the SMS. */
void snepulator_fm_sound_set (bool enable);

/* Send a completed frame for display. */
void snepulator_frame_done (uint_pixel *frame);

/* Get a pointer to the currently displayed frame. */
uint_pixel *snepulator_get_current_frame (void);

/* Get a pointer to the currently displayed frame. */
uint_pixel *snepulator_get_next_frame (void);

/* Enable integer scaling. */
void snepulator_integer_scaling_set (bool integer_scaling);

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

/* Run emulation for the specified amount of time. */
void snepulator_run (uint32_t cycles);

/* Load the console state from file. */
void snepulator_state_load (void *context, const char *filename);

/* Save the console state to file. */
void snepulator_state_save (void *context, const char *filename);

/* Call the appropriate initialisation for the chosen ROM. */
void snepulator_system_init (void);

/* Set the video 3D mode. */
void snepulator_video_3d_mode_set (Video_3D_Mode mode);

/* Set the video 3D colour saturation. */
void snepulator_video_3d_saturation_set (double saturation);

/* Set the console video filter. */
void snepulator_video_filter_set (Shader shader);

/* Set the console video format. */
void snepulator_video_format_set (Video_Format format);

/* Set the pixel aspect ratio. */
void snepulator_video_par_set (Video_PAR par);

/***************************
 *  Implemented in main.c  *
 ***************************/

/* Open an SDL audio device. */
void snepulator_audio_device_open (const char *device);

/* Display an error message */
void snepulator_error (const char *title, const char *message);




