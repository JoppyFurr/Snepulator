/*
 * Snepulator Logo.
 */
typedef struct Logo_Context_s {

    uint32_t frame;
    uint_pixel frame_buffer [VIDEO_BUFFER_WIDTH * VIDEO_BUFFER_LINES];

} Logo_Context;

/* Initialize the logo animation.  */
Logo_Context *logo_init (void);
