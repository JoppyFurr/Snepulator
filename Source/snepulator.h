/*
 * Snepulator common header file.
 */

#include "sega_gamepad.h"

#define VIDEO_SIDE_BORDER 8
#define VIDEO_BUFFER_WIDTH (256 + 2 * VIDEO_SIDE_BORDER)
#define VIDEO_BUFFER_LINES 240

#define ID_NONE     -1
#define ID_KEYBOARD -2

typedef struct Button_Mapping_s {
    uint32_t type;
    uint32_t value;
    bool negative;
} Button_Mapping;

typedef struct Gamepad_Mapping_s {
    int32_t device_id;
    Button_Mapping direction_up;
    Button_Mapping direction_down;
    Button_Mapping direction_left;
    Button_Mapping direction_right;
    Button_Mapping button_1;
    Button_Mapping button_2;
    Button_Mapping pause;
} Gamepad_Mapping;

typedef enum Video_Filter_e {
    VIDEO_FILTER_NEAREST,
    VIDEO_FILTER_LINEAR,
    VIDEO_FILTER_SCANLINES,
} Video_Filter;

typedef enum Video_System_e {
    VIDEO_SYSTEM_NTSC,
    VIDEO_SYSTEM_PAL
} Video_System;

typedef struct Snepulator_State_s {
    bool    ready;      /* Ready to run - A console and ROM are loaded. */
    bool    running;    /* Emulation is actively running. */
    bool    abort;      /* Attempt to cleanly exit. */

    /* Files */
    char *bios_filename;
    char *cart_filename;

    /* Console API */
    void (*run) (double ms);
    void (*audio_callback) (void *userdata, uint8_t *stream, int len);
    uint32_t (*get_clock_rate) (void);

    /* Console memory */
    uint8_t *ram;
    uint8_t *rom;
    uint8_t *bios;

    uint32_t rom_size;
    uint32_t bios_size;

    Video_System system;

    /* Console inputs */
    Sega_Gamepad gamepad_1;
    Sega_Gamepad gamepad_2;
    bool pause_button;

    /* Video */
    Video_Filter video_filter;
    float        video_out_texture_data [VIDEO_BUFFER_WIDTH * VIDEO_BUFFER_LINES * 3];
    float        video_out_texture_data_scanlines [VIDEO_BUFFER_WIDTH * 2 * (VIDEO_BUFFER_LINES) * 3 * 3];
    int          host_width;
    int          host_height;

    /* Statistics */
    double host_framerate;
    double vdp_framerate;
    double audio_ring_utilisation;

} Snepulator_State;

/* Update the in-memory button mapping for an input device. */
void snepulator_update_input_device (Gamepad_Mapping device);

/* Clean up after the previously running system */
void snepulator_reset (void);
