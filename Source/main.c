#include <stdio.h>
#include <stdlib.h>

#include "SDL2/SDL.h"

#include "gpu/sega_vdp.h"

/* Global state */
SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;

int main (int argc, char **argv)
{
    printf ("Snepulator.\n");
    printf ("Built on " BUILD_DATE ".\n");

    /* Initialize SDL */
    if (SDL_Init (SDL_INIT_EVERYTHING) == -1)
    {
        fprintf (stderr, "Error: SDL_Init failed.\n");
        return EXIT_FAILURE;
    }

    /* Create a window */
    /* For now, lets assume Master System resolution.
     * 256 × 192, with 16 pixels for left/right border, and 32 pixels for top/bottom border */
    SDL_CreateWindowAndRenderer (256 + 32, 192 + 64, 0, &window, &renderer);
    if (window == NULL || renderer == NULL)
    {
        fprintf (stderr, "Error: SDL_CreateWindowAndRenderer failed.\n");
        SDL_Quit ();
        return EXIT_FAILURE;
    }

    /* Blank the screen */
    SDL_SetRenderDrawColor (renderer, 0, 0, 0, 255);
    SDL_RenderClear (renderer);
    SDL_RenderPresent (renderer);

    vdp_init ();

    /* TEST - Dark green background */
    vdp_access (0x10, VDP_PORT_CONTROL, VDP_OPERATION_WRITE); /* Address = 0x10 */
    vdp_access (0xc0, VDP_PORT_CONTROL, VDP_OPERATION_WRITE); /* Target = cram */

    vdp_access (0x04, VDP_PORT_DATA,    VDP_OPERATION_WRITE); /* Dark green */

    vdp_access (0x00, VDP_PORT_CONTROL, VDP_OPERATION_WRITE); /* data = 0x00 */
    vdp_access (0x27, VDP_PORT_CONTROL, VDP_OPERATION_WRITE); /* Write to background register */

    vdp_dump ();


    /* Loop until the window is closed */
    for (;;)
    {
        SDL_Event event;

        while (SDL_PollEvent (&event))
        {
            if (event.type == SDL_QUIT)
            {
                goto snepulator_close;
            }
        }

        vdp_render ();
        SDL_RenderPresent (renderer);

        SDL_Delay (16);
    }

snepulator_close:

    SDL_DestroyRenderer (renderer);
    SDL_DestroyWindow (window);
    SDL_Quit ();

    return EXIT_SUCCESS;
}
