/*
 * File opening modal
 */

typedef struct File_Open_State_s {
    const char *title;
    const char *regex;
    void (*callback) (char *path);
} File_Open_State;

/* Rendering function for the open modal */
void snepulator_render_open_modal (void);

