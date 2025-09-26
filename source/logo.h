/*
 * Snepulator
 * Snepulator Logo.
 */
typedef struct Logo_Context_s {

    uint32_t frame;
    Video_Frame frame_buffer;

} Logo_Context;

/* Initialise the logo animation.  */
Logo_Context *logo_init (void);
