
typedef struct Snepulator_t {
    bool    abort ;
    bool    running ;

    /* Files */
    char *bios_filename;
    char *cart_filename;

    /* Video */
    unsigned int video_filter; /* GLenum */
    float        video_background [4];
    float        sms_vdp_texture_data [256 * 192 * 3];

} Snepulator;

