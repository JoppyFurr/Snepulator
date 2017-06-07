#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include "SDL2/SDL.h"
#include <GL/gl3w.h>
#include "../Libraries/imgui-1.49/imgui.h"
#include "../Libraries/imgui-1.49/examples/sdl_opengl3_example/imgui_impl_sdl_gl3.h"

extern "C" {
#include "gpu/sega_vdp.h"
#include "sms.h"
}

/* Global state */
float  sms_vdp_texture_data [256 * 256 * 3];
float  sms_vdp_background [4] = { 0.125, 0.125, 0.125, 1.0 };
bool _abort_ = false;

int main (int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_GLContext glcontext = NULL;
    GLuint sms_vdp_texture = 0;
    char *bios_filename = NULL;
    char *cart_filename = NULL;
    GLenum video_filter = GL_NEAREST;
    int window_width;
    int window_height;


    printf ("Snepulator.\n");
    printf ("Built on " BUILD_DATE ".\n");

    /* Parse all CLI arguments */
    while (*(++argv))
    {
        if (!strcmp ("-b", *argv))
        {
            /* BIOS to load */
            bios_filename = *(++argv);
        }
        else if (!strcmp ("-r", *argv))
        {
            /* ROM to load */
            cart_filename = *(++argv);
        }
        else
        {
            /* Display usage */
            fprintf (stdout, "Usage: Snepulator [-b bios.sms] [-r rom.sms]\n");
            return EXIT_FAILURE;
        }
    }

    /* Initialize SDL */
    if (SDL_Init (SDL_INIT_EVERYTHING) == -1)
    {
        fprintf (stderr, "Error: SDL_Init failed.\n");
        return EXIT_FAILURE;
    }

    /* Create a window */
    /* For now, lets assume Master System resolution.
     * 256 × 192, with 16 pixels for left/right border, and 32 pixels for top/bottom border */
    SDL_GL_SetAttribute (SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute (SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute (SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute (SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute (SDL_GL_STENCIL_SIZE, 8); /* Do we need this? */
    SDL_GL_SetAttribute (SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute (SDL_GL_CONTEXT_MINOR_VERSION, 2);
    window = SDL_CreateWindow ("Snepulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                      256 + VDP_OVERSCAN_X * 2, 192 + VDP_OVERSCAN_Y * 2, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    if (window == NULL)
    {
        fprintf (stderr, "Error: SDL_CreateWindowfailed.\n");
        SDL_Quit ();
        return EXIT_FAILURE;
    }

    /* Setup ImGui binding */
    glcontext = SDL_GL_CreateContext (window);
    if (glcontext == NULL)
    {
        fprintf (stderr, "Error: SDL_GL_CreateContext failed.\n");
        SDL_Quit ();
        return EXIT_FAILURE;
    }
    gl3wInit();
    ImGui_ImplSdlGL3_Init (window);

    /* Create texture for VDP output */
    glGenTextures (1, &sms_vdp_texture);
    glBindTexture (GL_TEXTURE_2D, sms_vdp_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, sms_vdp_background);

    /* Initialise SMS */
    sms_init (bios_filename, cart_filename);

    /* Main loop */
    while (!_abort_)
    {
        /* INPUT */
        SDL_GetWindowSize (window, &window_width, &window_height);
        SDL_Event event;

        ImGui_ImplSdlGL3_ProcessEvent (&event);

        while (SDL_PollEvent (&event))
        {
            if (event.type == SDL_QUIT)
            {
                _abort_ = true;
            }
        }

        /* EMULATE */
        sms_run_frame ();

        /* RENDER VDP */
        vdp_render ();
        glBindTexture (GL_TEXTURE_2D, sms_vdp_texture);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, video_filter);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, video_filter);
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, sms_vdp_background);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, 256, 256, 0, GL_RGB, GL_FLOAT, sms_vdp_texture_data);

        /* RENDER GUI */
        ImGui_ImplSdlGL3_NewFrame (window);

        /* Draw main menu bar */
        /* What colour should this be? A "Snepulator" theme, or should it blend in with the overscan colour? */
        /* TODO: Some measure should be taken to prevent the menu from obscuring the gameplay */
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open", NULL)) {}
                ImGui::Separator();
                if (ImGui::MenuItem("Quit", NULL)) { _abort_ = true; }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Video"))
            {
                if (ImGui::BeginMenu("Filter"))
                {
                    if (ImGui::MenuItem("GL_NEAREST", NULL, video_filter == GL_NEAREST)) { video_filter = GL_NEAREST; }
                    if (ImGui::MenuItem("GL_LINEAR",  NULL, video_filter == GL_LINEAR))  { video_filter = GL_LINEAR;  }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        /* Window Contents */
        {
            /* Scale the image to a multiple of SMS resolution */
            uint8_t scale = (window_width / 256) > (window_height / 192) ? (window_height / 192) : (window_width / 256);
            if (scale < 1)
                scale = 1;
            ImGui::PushStyleColor (ImGuiCol_WindowBg, ImColor (0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::SetNextWindowSize (ImVec2 (window_width, window_height));
            ImGui::Begin ("VDP Output", NULL, ImGuiWindowFlags_NoTitleBar |
                                              ImGuiWindowFlags_NoResize |
                                              ImGuiWindowFlags_NoScrollbar |
                                              ImGuiWindowFlags_NoInputs |
                                              ImGuiWindowFlags_NoSavedSettings |
                                              ImGuiWindowFlags_NoFocusOnAppearing |
                                              ImGuiWindowFlags_NoBringToFrontOnFocus);

            /* Centre VDP output */
            ImGui::SetCursorPosX (window_width / 2 - (256 * scale) / 2);
            ImGui::SetCursorPosY (window_height / 2 - (192 * scale) / 2);
            ImGui::Image ((void *) (uintptr_t) sms_vdp_texture, ImVec2 (256 * scale, 192 * scale),
                          /* uv0 */  ImVec2 (0, 0),
                          /* uv1 */  ImVec2 (1, 0.75),
                          /* tint */ ImColor (255, 255, 255, 255),
                          /* border */ ImColor (0, 0, 0, 0));
            ImGui::End();
            ImGui::PopStyleColor (1);
        }

        /* Draw to HW */
        glViewport(0, 0, (int)ImGui::GetIO().DisplaySize.x, (int)ImGui::GetIO().DisplaySize.y);
        /* A thought: What about the option to dim the background? */
        glClearColor(sms_vdp_background[0] * 0.80, sms_vdp_background[1] * 0.80, sms_vdp_background[2] * 0.80, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui::Render();

        SDL_GL_SwapWindow (window);
        SDL_Delay (10); /* TODO: This should be V-Sync, not a delay */

    }

    fprintf (stdout, "EMULATION ENDED.\n");

    glDeleteTextures (1, &sms_vdp_texture);
    SDL_GL_DeleteContext (glcontext);
    SDL_DestroyWindow (window);
    SDL_Quit ();

    return EXIT_SUCCESS;
}
