/*
 * Snepulator
 * SDL gamepad support header.
 */

/* Set up function pointers. */
void gamepad_sdl_init (void);

/* Process an SDL_Event. */
void gamepad_sdl_process_event (SDL_Event *event);
