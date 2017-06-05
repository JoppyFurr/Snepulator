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
#include "cpu/z80.h"
}

/* Global state */
SDL_Window *window = NULL;
SDL_GLContext glcontext = NULL;
GLuint sms_vdp_texture = 0;
float  sms_vdp_texture_data [256 * 256 * 3];
float  sms_vdp_background [3];
int window_width;
int window_height;

/* This should also be moved somewhere Master System specific */
uint8_t *bios = NULL;
uint8_t *cart = NULL;
static uint32_t bios_size = 0;
static uint32_t cart_size = 0;

uint8_t ram[8 << 10];
uint8_t memory_control = 0x00;
uint8_t io_control = 0x00;

/* Sega Mapper */
uint8_t mapper_bank[3] = { 0x00, 0x01, 0x02 };

/* 0: Output
 * 1: Input */
#define SMS_IO_TR_A_DIRECTION (1 << 0)
#define SMS_IO_TH_A_DIRECTION (1 << 1)
#define SMS_IO_TR_B_DIRECTION (1 << 2)
#define SMS_IO_TH_B_DIRECTION (1 << 3)
/* 0: Low
 * 1: High */
#define SMS_IO_TR_A_LEVEL (1 << 4)
#define SMS_IO_TH_A_LEVEL (1 << 5)
#define SMS_IO_TR_B_LEVEL (1 << 6)
#define SMS_IO_TH_B_LEVEL (1 << 7)

#define SMS_MEMORY_CTRL_BIOS_DISABLE 0x08
#define SMS_MEMORY_CTRL_CART_DISABLE 0x40
#define SMS_MEMORY_CTRL_IO_DISABLE   0x04

/* TODO: Eventually move Master System code to its own file */
static void sms_memory_write (uint16_t addr, uint8_t data)
{
    /* No early breaks - Register writes also affect RAM */

    /* 3D glasses */
    if (addr >= 0xfff8 && addr <= 0xfffb)
    {
    }

    /* Sega Mapper */
    if (addr == 0xfffc)
    {
        /* fprintf (stderr, "Error: Sega Memory Mapper register 0xfffc not implemented.\n"); */
    }
    else if (addr == 0xfffd)
    {
        /* fprintf (stdout, "[DEBUG]: MAPPER[0] set to %02x.\n", data & 0x07); */
        mapper_bank[0] = data & 0x1f;
    }
    else if (addr == 0xfffe)
    {
        /* fprintf (stdout, "[DEBUG]: MAPPER[1] set to %02x.\n", data & 0x07); */
        mapper_bank[1] = data & 0x1f;
    }
    else if (addr == 0xffff)
    {
        /* fprintf (stdout, "[DEBUG]: MAPPER[2] set to %02x.\n", data & 0x07); */
        mapper_bank[2] = data & 0x1f;
    }

    /* Mapping (CodeMasters) */
    if (addr == 0x8000)
    {
    }

    /* Cartridge, card, BIOS, expansion slot */
    if (addr >= 0x0000 && addr <= 0xbfff)
    {
    }

    /* RAM + mirror */
    if (addr >= 0xc000 && addr <= 0xffff)
    {
        ram[(addr - 0xc000) & ((8 << 10) - 1)] = data;
    }
}

/* TODO: Currently assuming a power-of-two size for the ROM */

static uint8_t sms_memory_read (uint16_t addr)
{
    /* Cartridge, card, BIOS, expansion slot */
    if (addr >= 0x0000 && addr <= 0xbfff)
    {
        uint32_t bank_base = mapper_bank[(addr >> 14)] * ((uint32_t)16 << 10);
        uint16_t offset    = addr & 0x3fff;

        if (bios && !(memory_control & SMS_MEMORY_CTRL_BIOS_DISABLE))
            return bios[(bank_base + offset) & (bios_size - 1)];

        if (cart && !(memory_control & SMS_MEMORY_CTRL_CART_DISABLE))
            return cart[(bank_base + offset) & (cart_size - 1)];
    }

    /* 8 KiB RAM + mirror */
    if (addr >= 0xc000 && addr <= 0xffff)
    {
        return ram[(addr - 0xc000) & ((8 << 10) - 1)];
    }

    return 0xff;
}

extern Z80_Regs z80_regs;

static void sms_io_write (uint8_t addr, uint8_t data)
{
    if (addr >= 0x00 && addr <= 0x3f)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* Memory Control Register */
            memory_control = data;
            fprintf (stderr, "[DEBUG(sms)]: Memory Control Register <- %02x:\n", memory_control);
            fprintf (stderr, "              -> PC is %04x.\n", z80_regs.pc);
            fprintf (stderr, "              -> Cartridge is %s.\n", (memory_control & 0x40) ? "DISABLED" : "ENABLED");
            fprintf (stderr, "              -> Work RAM is  %s.\n", (memory_control & 0x10) ? "DISABLED" : "ENABLED");
            fprintf (stderr, "              -> BIOS ROM is  %s.\n", (memory_control & 0x08) ? "DISABLED" : "ENABLED");
            fprintf (stderr, "              -> I/O Chip is  %s.\n", (memory_control & 0x04) ? "DISABLED" : "ENABLED");
        }
        else
        {
            /* I/O Control Register */
            io_control = data;
            fprintf (stderr, "[DEBUG(sms)] I/O control register not implemented.\n");
        }

    }

    /* PSG */
    else if (addr >= 0x40 && addr <= 0x7f)
    {
        /* Not implemented */
        fprintf (stderr, "Error: PSG not implemented.\n");
    }


    /* VDP */
    else if (addr >= 0x80 && addr <= 0xbf)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* VDP Data Register */
            vdp_data_write (data);
        }
        else
        {
            /* VDP Control Register */
            vdp_control_write (data);
        }
    }

    /* Minimal SDSC Debug Console */
    if (addr == 0xfd && (memory_control & 0x04))
    {
        fprintf (stdout, "%c", data);
        fflush (stdout);
    }
}

static uint8_t sms_io_read (uint8_t addr)
{
    if ((memory_control & 0x04) && addr >= 0xC0 && addr <= 0xff)
    {
        /* SMS2/GG return 0xff */
        return 0xff;
    }

    if (addr >= 0x00 && addr <= 0x3f)
    {
        /* SMS2/GG return 0xff */
        return 0xff;
    }

    else if (addr >= 0x40 && addr <= 0x7f)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* V Counter */
            return vdp_get_v_counter ();
        }
        else
        {
            /* H Counter */
            fprintf (stderr, "Error: H Counter not implemented.\n");
        }
    }


    /* VDP */
    else if (addr >= 0x80 && addr <= 0xbf)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* VDP Data Register */
            return vdp_data_read ();
        }
        else
        {
            /* VDP Status Flags */
            return vdp_status_read ();
        }
    }

    /* A pressed button returns zero */
    else if (addr >= 0xc0 && addr <= 0xff)
    {
        if ((addr & 0x01) == 0x00)
        {
            /* I/O Port A/B */
            return 0xff;
        }
        else
        {
            /* I/O Port B/misc */
            return 0xff;
        }
    }

    /* DEFAULT */
    return 0xff;
}

int32_t sms_load_rom (uint8_t **buffer, uint32_t *filesize, char *filename)
{
    uint32_t bytes_read = 0;

    /* Open ROM file */
    FILE *rom_file = fopen (filename, "rb");
    if (!rom_file)
    {
        perror ("Error: Unable to open ROM");
        return -1;
    }

    /* Get ROM size */
    fseek(rom_file, 0, SEEK_END);
    *filesize = ftell(rom_file);
    fseek(rom_file, 0, SEEK_SET);

    /* Allocate memory */
    *buffer = (uint8_t *) malloc (*filesize);
    if (!*buffer)
    {
        perror ("Error: Unable to allocate memory for ROM.\n");
        return -1;
    }

    /* Copy to memory */
    while (bytes_read < *filesize)
    {
        bytes_read += fread (*buffer + bytes_read, 1, *filesize - bytes_read, rom_file);
    }

    fclose (rom_file);

    return EXIT_SUCCESS;
}

bool _abort_ = false;

/* TODO: Move these somewhere SMS-specific */
extern uint64_t z80_cycle;
#define SMS_CLOCK_RATE_PAL  3546895
#define SMS_CLOCK_RATE_NTSC 3579545

int main (int argc, char **argv)
{
    char *bios_filename = NULL;
    char *cart_filename = NULL;

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
    SDL_GL_SetAttribute (SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); /* TODO: Why does this make everything fail? */
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
    glcontext = SDL_GL_CreateContext (window); /* TODO: glcontext is never used directly. Is it needed? */
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
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    /* Load BIOS */
    if (sms_load_rom (&bios, &bios_size, bios_filename) == -1)
    {
        _abort_ = true;
    }
    fprintf (stdout, "%d KiB BIOS %s loaded.\n", bios_size >> 10, bios_filename);

    /* Load cart */
    if (cart_filename)
    {
        if (sms_load_rom (&cart, &cart_size, cart_filename) == -1)
        {
            _abort_ = true;
        }
        fprintf (stdout, "%d KiB cart %s loaded.\n", cart_size >> 10, cart_filename);
    }

    z80_init (sms_memory_read, sms_memory_write, sms_io_read, sms_io_write);
    vdp_init ();

    /* Master System loop */
    uint64_t next_frame_cycle = 0;
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
        z80_run_until_cycle (next_frame_cycle);

        /* RENDER VDP */
        vdp_render ();
        glBindTexture (GL_TEXTURE_2D, sms_vdp_texture);
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

        next_frame_cycle = z80_cycle + (SMS_CLOCK_RATE_PAL / 50);
    }

    fprintf (stdout, "EMULATION ENDED.\n");

    glDeleteTextures (1, &sms_vdp_texture);
    SDL_GL_DeleteContext (glcontext);
    SDL_DestroyWindow (window);
    SDL_Quit ();

    return EXIT_SUCCESS;
}
