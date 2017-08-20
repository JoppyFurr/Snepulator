
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
    float        video_background [4];
    float        sms_vdp_texture_data [256 * 192 * 3];
    float        sms_vdp_texture_data_scanlines [256 * 192 * 3 * 3];

} Snepulator;

