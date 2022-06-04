/*
 * File opening modal
 */

typedef struct File_Open_State_s {
    const char *title;
    const char *regex;
    bool path_cached;
    void (*callback) (const char *path);
} File_Open_State;

/* Change the regex used to filter files to open. */
void snepulator_set_open_regex (const char *regex);

/* Rendering function for the open modal */
void snepulator_open_modal_render (void);

