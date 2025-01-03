/*
 * Snepulator
 * Sega Mega Drive implementation.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snepulator.h"
#include "util.h"

#include "cpu/m68k.h"
#include "cpu/z80.h"
#include "video/smd_vdp.h"
#include "sound/band_limit.h"
#include "sound/sn76489.h"
#include "smd.h"

extern Snepulator_State state;


/*
 * Declarations.
 */
static uint8_t smd_z80_memory_read (void *context_ptr, uint16_t addr);
static void    smd_z80_memory_write (void *context_ptr, uint16_t addr, uint8_t data);


/*
 * Callback to supply audio frames.
 */
static void smd_audio_callback (void *context_ptr, int16_t *stream, uint32_t count)
{
    SMD_Context *context = (SMD_Context *) context_ptr;

    sn76489_get_samples (context->psg_context, stream, count);
}


/*
 * Process a frame completion by the VDP.
 */
static void smd_frame_done (void *context_ptr)
{
    SMD_Context *context = (SMD_Context *) context_ptr;
    SMD_VDP_Context *vdp_context = context->vdp_context;

    snepulator_frame_done (vdp_context->frame_buffer);

    /* TODO: Have these as a parameter for snepulator_frame_done?,
     *       or, remove start_x / start_y and only have a width / height. */
    state.video_start_x     = VIDEO_SIDE_BORDER;
    state.video_start_y     = (VIDEO_BUFFER_LINES - 224) / 2;
    state.video_width       = vdp_context->video_width;
    state.video_height      = vdp_context->video_height;
}


/*
 * Handle 8-bit memory reads.
 * TODO: Duplication between 8-bit and 16-bit reads. Can we do better?
 */
static uint8_t smd_memory_read_8 (void *context_ptr, uint32_t addr)
{
    SMD_Context *context = (SMD_Context *) context_ptr;

    /* Cartridge ROM */
    if (addr <= 0x3fffff)
    {
        if (context->rom != NULL)
        {
            return context->rom [addr & context->rom_mask];
        }
        else
        {
            return 0xff;
        }
    }

    /* Unused areas */
    else if (addr <= 0x9fffff)
    {
        snepulator_error (__func__, "Unused address %06x not implemented.", addr);
        return 0xff;
    }

    /* Z80 Address Space */
    else if (addr <= 0xa0ffff)
    {
        snepulator_error (__func__, "Z80 address-space %06x not implemented.", addr);
        return 0xff;
    }

    /* I/O */
    else if (addr <= 0xa1001f)
    {
        /* Last bit is masked out; register is available at even and odd addresses. */
        switch (addr & 0xfffffe)
        {
            case 0xa10000: /* Version Register */
                /* TODO: Value is different on PAL */
                return 0xa1; /* Export NTSC console with no expansion unit. */
            case 0xa10008: /* Player 1 - Control Register */
                return context->state.port1_ctrl;
            case 0xa1000a: /* Player 1 - Control Register */
                return context->state.port2_ctrl;
            case 0xa1000c: /* Player 1 - Control Register */
                return context->state.ext_ctrl;
            default:
                snepulator_error (__func__, "I/O address %06x not implemented.", addr);
        }
    }

    /* Internal Registers and Expansion */
    else if (addr <= 0xbfffff)
    {
        /* Z80 Bus Request */
        if (addr == 0xa11100)
        {
            return !context->state.z80_busreq;
        }

        snepulator_error (__func__, "Internal registers / expansion address %06x not implemented.", addr);
        return 0xff;
    }

    /* VDP */
    else if (addr <= 0xdfffff)
    {
        snepulator_error (__func__, "VDP address %06x not implemented.", addr);
        return 0xff;
    }

    /* RAM */
    else
    {
        return * (uint16_t *) &context->ram [addr & 0x00ffff];
    }

    snepulator_error (__func__, "Unmapped address %06x.", addr);
    return 0xff;
}


/*
 * Handle 16-bit memory reads.
 */
static uint16_t smd_memory_read_16 (void *context_ptr, uint32_t addr)
{
    SMD_Context *context = (SMD_Context *) context_ptr;

    /* Cartridge ROM */
    if (addr <= 0x3fffff)
    {
        if (context->rom != NULL)
        {
            return util_ntoh16 (* (uint16_t *) &context->rom [addr & context->rom_mask]);
        }
        else
        {
            return 0xffff;
        }
    }

    /* Unused areas */
    else if (addr <= 0x9fffff)
    {
        snepulator_error (__func__, "Unused address %06x not implemented.", addr);
        return 0xffff;
    }

    /* Z80 Address Space */
    else if (addr <= 0xa0ffff)
    {
        snepulator_error (__func__, "Z80 address-pace %06x not implemented.", addr);
        return 0xffff;
    }

    /* I/O */
    else if (addr <= 0xa1001f)
    {
        switch (addr & 0xfffffe)
        {
            /* TODO: What is the correct behaviour for a 16-bit read?
             * Should the value be repeated in both bytes?
             * What about a non-aligned 16-bit read? */
            case 0xa10008: /* Player 1 - Control Register */
                return context->state.port1_ctrl;
            case 0xa1000a: /* Player 1 - Control Register */
                return context->state.port2_ctrl;
            case 0xa1000c: /* Player 1 - Control Register */
                return context->state.ext_ctrl;
            default:
                snepulator_error (__func__, "I/O address %06x not implemented.", addr);
        }
    }

    /* Internal Registers and Expansion */
    else if (addr <= 0xbfffff)
    {
        snepulator_error (__func__, "Internal registers / expansion address %06x not implemented.", addr);
        return 0xffff;
    }

    /* VDP */
    else if (addr <= 0xdfffff)
    {
        switch (addr)
        {
            case 0xc00000:
            case 0xc00002:
                return smd_vdp_data_read (context->vdp_context);
            case 0xc00004:
            case 0xc00006:
                return smd_vdp_status_read (context->vdp_context);

                break;
            default:
                snepulator_error (__func__, "VDP address %06x not implemented.", addr);
                return 0xffff;
        }
    }

    /* RAM */
    else
    {
        return util_ntoh16 (* (uint16_t *) &context->ram [addr & 0x00ffff]);
    }

    snepulator_error (__func__, "Unmapped address %06x.", addr);
    return 0xffff;
}


/*
 * Handle 8-bit memory writes.
 */
static void smd_memory_write_8 (void *context_ptr, uint32_t addr, uint8_t data)
{
    SMD_Context *context = (SMD_Context *) context_ptr;

    /* Z80 Address Space access */
    if (addr >= 0xa00000 && addr <= 0xa0ffff)
    {
        smd_z80_memory_write (context, addr, data);
    }

    /* PSG */
    else if (addr == 0xc00011 || addr == 0xc00013)
    {
        sn76489_data_write (context->psg_context, data);
    }

    /* RAM */
    else if (addr >= 0xe00000 && addr <= 0xffffff)
    {
        context->ram [addr & 0x00ffff] = data;
    }

    else
    {
        snepulator_error (__func__, "Unmapped address %06x.", addr);
    }
}


/*
 * Handle 16-bit memory writes.
 */
static void smd_memory_write_16 (void *context_ptr, uint32_t addr, uint16_t data)
{
    SMD_Context *context = (SMD_Context *) context_ptr;

    /* Z80 Address Space access */
    if (addr >= 0xa00000 && addr <= 0xa0ffff)
    {
        snepulator_error (__func__, "Z80 address-space access %06x not implemented.", addr);
    }

    /* I/O */
    else if (addr >= 0xa10000 && addr <= 0xa1001f)
    {
        snepulator_error (__func__, "I/O access %06x not implemented.", addr);
    }

    /* Internal Registers and Expansion */
    else if (addr >= 0xa10020 && addr <= 0xbfffff)
    {
        /* TMSS register */
        if (addr == 0xa14000 || addr == 0xa14002)
        {
            return;
        }

        /* Z80 Bus Request */
        else if (addr == 0xa11100)
        {
            context->state.z80_busreq = (data >> 8) & 0x01;
            printf ("[%s] z80_busreq ← %d.\n", __func__, context->state.z80_busreq);
        }

        /* Z80 and YM2612 Reset */
        else if (addr == 0xa11200)
        {
            context->state.z80_reset_n = (data >> 8) & 0x01;

            if (!context->state.z80_reset_n)
            {
                z80_reset (context->z80_context);
            }
            printf ("[%s] z80_reset_n ← %d.\n", __func__, context->state.z80_reset_n);
        }

        else
        {
            snepulator_error (__func__, "Internal register / expansion access %06x not implemented.", addr);
        }
    }

    /* VDP */
    else if (addr >= 0xc00000 && addr <= 0xdfffff)
    {
        switch (addr)
        {
            case 0xc00000:
            case 0xc00002:
                return smd_vdp_data_write (context->vdp_context, data);
            case 0xc00004:
            case 0xc00006:
                smd_vdp_control_write (context->vdp_context, data);
                return;
            default:
                snepulator_error (__func__, "VDP access %06x not implemented.", addr);
        }
    }

    /* RAM */
    else if (addr >= 0xe00000 && addr <= 0xffffff)
    {
        * (uint16_t *) &context->ram [addr & 0x00ffff] = util_hton16 (data);
    }

    /* Invalid write */
    else
    {
        snepulator_error (__func__, "Unmapped address %06x.", addr);
    }
}


/*
 * Handle Z80 address-space memory writes.
 */
static void smd_z80_memory_write (void *context_ptr, uint16_t addr, uint8_t data)
{
    SMD_Context *context = (SMD_Context *) context_ptr;

    /* RAM */
    if (addr >= 0x0000 && addr <= 0x1fff)
    {
        context->z80_ram [addr & (SMD_Z80_RAM_SIZE - 1)] = data;
    }
    else
    {
        snepulator_error (__func__, "Unmapped Z80 address %04x.", addr);
    }
}


/*
 * Handle Z80 address-space memory reads.
 */
static uint8_t smd_z80_memory_read (void *context_ptr, uint16_t addr)
{
    SMD_Context *context = (SMD_Context *) context_ptr;

    /* RAM */
    if (addr >= 0x0000 && addr <= 0x1fff)
    {
        return context->z80_ram [addr & (SMD_Z80_RAM_SIZE - 1)];
    }
    else
    {
        snepulator_error (__func__, "Unmapped Z80 address %04x.", addr);
        return 0xff;
    }
}


/*
 * Handle Z80 I/O writes.
 */
static void smd_z80_io_write (void *context_ptr, uint8_t addr, uint8_t data)
{
    snepulator_error (__func__, "Z80 I/O write [%02x] not implemented.", addr);
}


/*
 * Handle Z80 I/O reads.
 */
static uint8_t smd_z80_io_read (void *context_ptr, uint8_t addr)
{
    snepulator_error (__func__, "Z80 I/O read [%02x] not implemented.", addr);
    return 0xff;
}


/*
 * Returns true if there is an interrupt for the Z80.
 */
static bool smd_z80_get_int (void *context_ptr)
{
    return false;
}


/*
 * Returns true if there is a non-maskable interrupt for the Z80.
 */
static bool smd_z80_get_nmi (void *context_ptr)
{
    return false;
}


/*
 * Emulate the Mega Drive for the specified number of clock cycles.
 * Called with the run_mutex held.
 */
static void smd_run (void *context_ptr, uint32_t cycles)
{
    SMD_Context *context = (SMD_Context *) context_ptr;
    uint32_t lines;

    /*
     * Clock Notes:
     *  -> m68k: master_clock / 7.
     *  -> z80:  master_clock / 15.
     *  -> PSG:  master_clock / 15.
     *  -> VDP (256 pixels per line): master_clock / 10
     *  -> VDP (320 pixels per line): master_clock / 8 during active-line, or master_clock / 10 during sync
     *
     * Speculation:
     *   On the Master System, we get 228 Z80 cycles per scanline. If we
     *   assume the Mega Drive has similar timing, multiplying by 15, then
     *   we'd get 3420 master clocks per scanline. This may be accurate in
     *   some modes, but not all. Things may get funky around different
     *   combinations of resulution and internal vs external pixel-clocks.
     *   This does however result in a non-integer number of m68k cycles
     *   per scanline.
     */

    /* Convert cycles into lines to run, storing the remainder for next time. */
    context->pending_cycles += cycles;
    lines = context->pending_cycles / 3420;
    context->pending_cycles -= lines * 3420;

    while (lines--)
    {
        /* TODO: 489 is rounded up. A better solution is to add 3420 master_clock
         *       cycles to a pool and subtract as mayn full m68k cycles as we can,
         *       saving any leftovers for the next line. */
        m68k_run_cycles (context->m68k_context, 489);

        if (context->state.z80_reset_n && !context->state.z80_busreq)
        {
            z80_run_cycles (context->z80_context, 228);
        }

        smd_vdp_run_one_scanline (context->vdp_context);

        /* TODO: Variable clock rate when able to switch between PAL and NTSC */
        sn76489_run_cycles (context->psg_context, SMD_NTSC_MASTER_CLOCK / 15 , 228);
    }
}


/*
 * Returns a pointer to the rom hash.
 */
static uint8_t *smd_get_rom_hash (void *context_ptr)
{
    SMD_Context *context = (SMD_Context *) context_ptr;

    return context->rom_hash;
}


/*
 * Returns the current interrupt priortiy.
 */
static uint8_t smd_get_int (void *context_ptr)
{
    return 0;
}


/*
 * Clean up any console-specific structures.
 */
static void smd_cleanup (void *context_ptr)
{
    SMD_Context *context = (SMD_Context *) context_ptr;

    if (context->m68k_context != NULL)
    {
        free (context->m68k_context);
        context->m68k_context = NULL;
    }

    if (context->z80_context != NULL)
    {
        free (context->z80_context);
        context->z80_context = NULL;
    }

    if (context->vdp_context != NULL)
    {
        free (context->vdp_context);
        context->vdp_context = NULL;
    }

    /* TODO: Free the YM2612 context once we have one. */

    if (context->psg_context != NULL)
    {
        if (context->psg_context->bandlimit_context_l)
        {
            free (context->psg_context->bandlimit_context_l);
        }
        if (context->psg_context->bandlimit_context_r)
        {
            free (context->psg_context->bandlimit_context_r);
        }

        free (context->psg_context);
        context->psg_context = NULL;
    }

    if (context->rom != NULL)
    {
        free (context->rom);
        context->rom = NULL;
    }
}


/*
 * Reset the Mega Drive and load a new ROM.
 */
SMD_Context *smd_init (void)
{
    SMD_Context *context;
    M68000_Context *m68k_context;
    Z80_Context *z80_context;
    SMD_VDP_Context *vdp_context;
    SN76489_Context *psg_context;

    context = calloc (1, sizeof (SMD_Context));
    if (context == NULL)
    {
        snepulator_error ("Error", "Unable to allocate memory for SMD_Context");
        return NULL;
    }

    /* Initialise CPUs */
    /* TODO: Could we combine the 16-bit and 8-bit memory accesses? */
    m68k_context = m68k_init (context,
                              smd_memory_read_16, smd_memory_write_16,
                              smd_memory_read_8, smd_memory_write_8,
                              smd_get_int);
    context->m68k_context = m68k_context;

    z80_context = z80_init (context, smd_z80_memory_read, smd_z80_memory_write,
                            smd_z80_io_read, smd_z80_io_write,
                            smd_z80_get_int, smd_z80_get_nmi);
    context->z80_context = z80_context;

    context->state.z80_reset_n = false;
    context->state.z80_busreq = false;

    /* Initialise VDP */
    /* TODO: Instead of centering the image on a fixed-size canvas, it may
     *       be better to treat the buffer as if it were of varying-size.
     *       Always have it allocated to take the largest image that may be
     *       needed, but only use what is needed. Remove the start-x / start-y
     *       and always start at zero. Pass around a struct to describe the
     *       size that has been used.  */
    vdp_context = smd_vdp_init (context, smd_frame_done);
    context->vdp_context = vdp_context;

    /* Initialise sound chips */
    psg_context = sn76489_init ();
    context->psg_context = psg_context;

    /* Defaults */

    /* Load ROM cart */
    if (state.cart_filename)
    {
        if (util_load_rom (&context->rom, &context->rom_size, state.cart_filename) == -1)
        {
            smd_cleanup (context);
            return NULL;
        }
        fprintf (stdout, "%d KiB ROM %s loaded.\n", context->rom_size >> 10, state.cart_filename);
        context->rom_mask = util_round_up (context->rom_size) - 1;
        util_hash_rom (context->rom, context->rom_size, context->rom_hash);
    }

    /* Initial video parameters */
    state.video_width   = vdp_context->video_width;
    state.video_height  = vdp_context->video_height;

    /* Hook up callbacks */
    state.audio_callback = smd_audio_callback;
    state.cleanup = smd_cleanup;
    state.get_rom_hash = smd_get_rom_hash;
    state.run_callback = smd_run;
#ifdef DEVELOPER_BUILD
#if 0
    state.diagnostics_show = smd_diagnostics_show;
#endif
#endif

    /* For now, assume NTSC */
    state.clock_rate = SMD_NTSC_MASTER_CLOCK;

    /* Begin emulation */
    m68k_reset (m68k_context);
    state.run = RUN_STATE_RUNNING;

    return context;
}
