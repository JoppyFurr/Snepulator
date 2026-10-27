/*
 * Snepulator
 * Sega Mega Drive implementation.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snepulator.h"
#include "path.h"
#include "util.h"

#include "gamepad.h"
#include "cpu/m68k.h"
#include "cpu/z80.h"
#include "video/smd_vdp.h"
#include "sound/band_limit.h"
#include "sound/sn76489.h"
#include "sound/ym2612.h"
#include "smd.h"

extern Snepulator_State state;
extern Snepulator_Gamepad gamepad [3];


/*
 * Declarations.
 */
static uint8_t smd_z80_memory_read (void *context_ptr, uint16_t addr);
static void    smd_z80_memory_write (void *context_ptr, uint16_t addr, uint8_t data);


/*
 * Callback to supply audio frames.
 */
static void smd_audio_callback (void *context_ptr, int32_t *stream, uint32_t count)
{
    SMD_Context *context = (SMD_Context *) context_ptr;

    ym2612_get_samples (context->ym2612_context, stream, count);
    sn76489_get_samples (context->psg_context, stream, count);
}


/*
 * Process a frame completion by the VDP.
 */
static void smd_frame_done (void *context_ptr)
{
    SMD_Context *context = (SMD_Context *) context_ptr;
    SMD_VDP_Context *vdp_context = context->vdp_context;

    snepulator_frame_done (&vdp_context->frame_buffer);
}


/*
 * Handle 8-bit memory reads.
 * TODO: Duplication between 8-bit and 16-bit reads. Can we do better?
 */
static uint8_t smd_memory_read_8 (void *context_ptr, uint32_t addr)
{
    SMD_Context *context = (SMD_Context *) context_ptr;

    /* Cartridge ROM: 0x000000 -- 0x3fffff */
    if (addr <= 0x3fffff)
    {
        /* If SRAM is present, check for access */
        if (context->sram_size && addr >= context->sram_start && addr < context->sram_start + context->sram_size * 2)
        {
            /* SRAM is always enabled if it is placed above the ROM, or
             * gated by the sram control register if it overlaps the ROM. */
            if (context->sram_start >= context->rom_size || context->state.sram_control & 0x01)
            {
                /* Read from SRAM */
                return context->sram [((addr - context->sram_start) / 2) & (context->sram_size - 1)];
            }
        }

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

    /* Z80 Address Space: 0xa00000 -- 0xa0ffff */
    else if (addr <= 0xa0ffff)
    {
        /* TODO: Find out if there is any mirroring going on here around reading
         *       addresses in z80-space that point back to m68k-space */
        return smd_z80_memory_read (context_ptr, addr);
    }

    /* I/O */
    else if (addr <= 0xa1001f)
    {
        /* Last bit is masked out; register is available at even and odd addresses. */
        switch (addr & 0xfffffe)
        {
            case 0xa10000: /* Version Register */
            case 0xa10001:
                /* TODO: Value is different on PAL */
                return 0xa1; /* Export NTSC console with no expansion unit. */

            case 0xa10002: /* Player 1 - Data Register */
            case 0xa10003:
                /* TODO: For now, to get things going just assume TH is an output
                 *       and ignore the most-significant bit behaviour. */
                if ((context->state.port1_data & 0x40) == 0)
                {
                    return (gamepad [1].state [GAMEPAD_DIRECTION_UP]    ? 0 : BIT_0) |
                           (gamepad [1].state [GAMEPAD_DIRECTION_DOWN]  ? 0 : BIT_1) |
                           (gamepad [1].state [GAMEPAD_BUTTON_1]        ? 0 : BIT_4) |
                           (gamepad [1].state [GAMEPAD_BUTTON_START]    ? 0 : BIT_5);
                }
                else
                {
                    return (gamepad [1].state [GAMEPAD_DIRECTION_UP]    ? 0 : BIT_0) |
                           (gamepad [1].state [GAMEPAD_DIRECTION_DOWN]  ? 0 : BIT_1) |
                           (gamepad [1].state [GAMEPAD_DIRECTION_LEFT]  ? 0 : BIT_2) |
                           (gamepad [1].state [GAMEPAD_DIRECTION_RIGHT] ? 0 : BIT_3) |
                           (gamepad [1].state [GAMEPAD_BUTTON_2]        ? 0 : BIT_4) |
                           (gamepad [1].state [GAMEPAD_BUTTON_3]        ? 0 : BIT_5) | BIT_6;
                }

            case 0xa10004: /* Player 2 - Data Register */
            case 0xa10005:
                /* TODO: For now, to get things going just assume TH is an output
                 *       and ignore the most-significant bit behaviour. */
                if ((context->state.port2_data & 0x40) == 0)
                {
                    return (gamepad [2].state [GAMEPAD_DIRECTION_UP]    ? 0 : BIT_0) |
                           (gamepad [2].state [GAMEPAD_DIRECTION_DOWN]  ? 0 : BIT_1) |
                           (gamepad [2].state [GAMEPAD_BUTTON_1]        ? 0 : BIT_4) |
                           (gamepad [2].state [GAMEPAD_BUTTON_START]    ? 0 : BIT_5);
                }
                else
                {
                    return (gamepad [2].state [GAMEPAD_DIRECTION_UP]    ? 0 : BIT_0) |
                           (gamepad [2].state [GAMEPAD_DIRECTION_DOWN]  ? 0 : BIT_1) |
                           (gamepad [2].state [GAMEPAD_DIRECTION_LEFT]  ? 0 : BIT_2) |
                           (gamepad [2].state [GAMEPAD_DIRECTION_RIGHT] ? 0 : BIT_3) |
                           (gamepad [2].state [GAMEPAD_BUTTON_2]        ? 0 : BIT_4) |
                           (gamepad [2].state [GAMEPAD_BUTTON_3]        ? 0 : BIT_5) | BIT_6;
                }

            case 0xa10006: /* Player 3 - Data Register */
            case 0xa10007:
                snepulator_error (__func__, "Player 3 input not implemented.");
                return 0xff;

            case 0xa10008: /* Player 1 - Control Register */
            case 0xa10009:
                return context->state.port1_ctrl;

            case 0xa1000a: /* Player 1 - Control Register */
            case 0xa1000b:
                return context->state.port2_ctrl;

            case 0xa1000c: /* Player 1 - Control Register */
            case 0xa1000d:
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

    /* VDP: 0xc00000 -- 0xdfffff*/
    else if (addr <= 0xdfffff)
    {
        switch (addr)
        {
            case 0xc00004:
            case 0xc00006:
                return smd_vdp_status_read (context->vdp_context) >> 8;

            case 0xc00005:
            case 0xc00007:
                return smd_vdp_status_read (context->vdp_context);

            case 0xc00008: /* V Counter */
                return smd_vdp_hv_counter_read (context->vdp_context) >> 8;;

            case 0xc00009: /* H Counter */
                return smd_vdp_hv_counter_read (context->vdp_context);

            default:
                snepulator_error (__func__, "VDP address %06x not implemented.", addr);
                return 0xff;
        }
    }

    /* RAM: 0xff0000 -- 0xffffff */
    else
    {
        return context->ram [addr & 0x00ffff];
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

    /* Cartridge ROM: 0x000000 -- 0x3fffff */
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

    /* Z80 Address Space: 0xa00000 -- 0xa0ffff */
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

    /* VDP: 0xc00000 -- 0xdfffff*/
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

    /* RAM: 0xff0000 -- 0xffffff */
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

    /* Cartridge ROM: 0x000000 -- 0x3fffff */
    if (addr <= 0x3fffff)
    {
        /* If SRAM is present, check for access */
        if (context->sram_size && addr >= context->sram_start && addr < context->sram_start + context->sram_size * 2)
        {
            /* SRAM is always enabled if it is placed above the ROM, or
             * gated by the sram control register if it overlaps the ROM. */
            if (context->sram_start >= context->rom_size || context->state.sram_control & 0x01)
            {
                /* Read from SRAM */
                context->sram [((addr - context->sram_start) / 2) & (context->sram_size - 1)] = data;
            }
        }
        else
        {
            printf ("[%s]: Attempt to write value %02x to cartridge address %06x.\n", __func__, data, addr);
            printf ("[%s]:   -> PC=%06x.\n", __func__, context->m68k_context->state.pc);
        }
    }

    /* Z80 Address Space access */
    else if (addr >= 0xa00000 && addr <= 0xa0ffff)
    {
        smd_z80_memory_write (context, addr, data);
    }

    /* Controller Port registers */
    else if (addr >= 0xa10002 && addr <= 0xa1001f)
    {
        switch (addr)
        {
            case 0xa10002:
            case 0xa10003:
                context->state.port1_data = data;
                break;

            case 0xa10004:
            case 0xa10005:
                context->state.port2_data = data;
                break;

            case 0xa10006:
            case 0xa10007:
                context->state.ext_data = data;
                break;

            case 0xa10008:
            case 0xa10009:
                context->state.port1_ctrl = data;
                break;

            case 0xa1000a:
            case 0xa1000b:
                context->state.port2_ctrl = data;
                break;

            case 0xa1000c:
            case 0xa1000d:
                context->state.ext_ctrl = data;
                break;

            case 0xa10012:
            case 0xa10013:
                if (data & 0x30)
                {
                    snepulator_error (__func__, "Controller 1 serial mode not implemented.", addr);
                }
                break;

            case 0xa10018:
            case 0xa10019:
                if (data & 0x30)
                {
                    snepulator_error (__func__, "Controller 2 serial mode not implemented.", addr);
                }
                break;

            case 0xa1001e:
            case 0xa1001f:
                if (data & 0x30)
                {
                    snepulator_error (__func__, "Ext port serial mode not implemented.", addr);
                }
                break;

            default:
                snepulator_error (__func__, "Unmapped Controller register %06x.", addr);
        }
    }

    /* Internal Registers and Expansion */
    else if (addr >= 0xa10020 && addr <= 0xbfffff)
    {
        /* Z80 Bus Request */
        if (addr == 0xa11100)
        {
            context->state.z80_busreq = data & 0x01;
        }

        /* Z80 and YM2612 Reset */
        /* TODO: YM2612 reset */
        else if (addr == 0xa11200)
        {
            context->state.z80_reset_n = data & 0x01;

            if (!context->state.z80_reset_n)
            {
                z80_reset (context->z80_context);
            }
        }

        else if (addr == 0xa130f1)
        {
            context->state.sram_control = data;
        }

        else
        {
            snepulator_error (__func__, "Internal register / expansion access %06x not implemented.", addr);
        }
    }

    /* VDP & PSG */
    else if (addr >= 0xc00000 && addr <= 0xdfffff)
    {
        switch (addr)
        {
            case 0xc00000:
            case 0xc00002:
                return smd_vdp_data_write (context->vdp_context, (data << 8) | data);
            case 0xc00004:
            case 0xc00006:
                smd_vdp_control_write (context->vdp_context, (data << 8) | data);
                return;
            case 0xc00011:
            case 0xc00013:
                sn76489_data_write (context->psg_context, data);
                return;
            default:
                snepulator_error (__func__, "VDP access %06x not implemented.", addr);
        }
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

    /* Cartridge ROM: 0x000000 -- 0x3fffff */
    if (addr <= 0x3fffff)
    {
        printf ("[%s]: Attempt to write value %04x to cartridge address %06x.\n", __func__, data, addr);
        printf ("[%s]: -> PC=%06x.\n", __func__, context->m68k_context->state.pc);
    }

    /* Z80 Address Space access */
    else if (addr >= 0xa00000 && addr <= 0xa0ffff)
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

        /* Memory Mode - Ignored */
        if (addr == 0xa11000)
        {
            return;
        }

        /* Z80 Bus Request */
        else if (addr == 0xa11100)
        {
            context->state.z80_busreq = (data >> 8) & 0x01;
        }

        /* Z80 and YM2612 Reset */
        /* TODO: YM2612 reset */
        else if (addr == 0xa11200)
        {
            context->state.z80_reset_n = (data >> 8) & 0x01;

            if (!context->state.z80_reset_n)
            {
                z80_reset (context->z80_context);
            }
        }

        /* TMSS register */
        else if (addr == 0xa14000 || addr == 0xa14002)
        {
            return;
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
 * Handle Z80 address-space memory reads.
 */
static uint8_t smd_z80_memory_read (void *context_ptr, uint16_t addr)
{
    SMD_Context *context = (SMD_Context *) context_ptr;

    /* 8 KiB RAM + mirror */
    if (addr >= 0x0000 && addr <= 0x3fff)
    {
        return context->z80_ram [addr & (SMD_Z80_RAM_SIZE - 1)];
    }
    /* YM2612 */
    else if (addr >= 0x4000 && addr <= 0x5fff)
    {
        return ym2612_status_read (context->ym2612_context);
    }
    /* Bank Register - Always reads 0xff */
    else if (addr >= 0x6000 && addr <= 0x60ff)
    {
        return 0xff;
    }
    /* M68k address space (banked) */
    else if (addr >= 0x8000 && addr <= 0xffff)
    {
        return smd_memory_read_8 (context, context->state.z80_bank | (addr & 0x7fff));
    }
    else
    {
        snepulator_error (__func__, "Unmapped Z80 address %04x.", addr);
        return 0xff;
    }
}


/*
 * Handle Z80 address-space memory writes.
 */
static void smd_z80_memory_write (void *context_ptr, uint16_t addr, uint8_t data)
{
    SMD_Context *context = (SMD_Context *) context_ptr;

    /* 8 KiB RAM + mirror */
    if (addr >= 0x0000 && addr <= 0x3fff)
    {
        context->z80_ram [addr & (SMD_Z80_RAM_SIZE - 1)] = data;
    }
    /* YM2612 */
    else if (addr >= 0x4000 && addr <= 0x5fff)
    {
        switch (addr & 0x0003)
        {
            case 0:
                ym2612_addr1_write (context->ym2612_context, data);
                break;
            case 2:
                ym2612_addr2_write (context->ym2612_context, data);
                break;
            default:
                ym2612_data_write (context->ym2612_context, data);
                break;
        }
    }
    /* Bank Register */
    else if (addr >= 0x6000 && addr <= 0x60ff)
    {
        context->state.z80_bank = (context->state.z80_bank >> 1) & 0xff8000;

        if (data & 0x01)
        {
            context->state.z80_bank |= 0x800000;
        }
    }
    /* PSG */
    else if (addr == 0x7f11 || addr == 0x7f13)
    {
        sn76489_data_write (context->psg_context, data);
    }
    /* M68k address space (banked) */
    else if (addr >= 0x8000 && addr <= 0xffff)
    {
        smd_memory_write_8 (context, context->state.z80_bank | (addr & 0x7fff), data);
    }
    else
    {
        snepulator_error (__func__, "Unmapped Z80 address %04x.", addr);
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
    SMD_Context *context = (SMD_Context *) context_ptr;

    return smd_vdp_get_z80_interrupt (context->vdp_context);
}


/*
 * Returns true if there is a non-maskable interrupt for the Z80.
 */
static bool smd_z80_get_nmi (void *context_ptr)
{
    return false;
}


/*
 * Backup the on-cartridge SRAM.
 */
static void smd_sync (void *context_ptr)
{
    SMD_Context *context = (SMD_Context *) context_ptr;

    if (context->sram_size)
    {
        /* Only write the file if there has been a change */
        if (memcmp (context->sram, context->sram_last_write, SMD_MAX_SRAM_SIZE) == 0)
        {
            return;
        }

        uint32_t bytes_written = 0;
        char *path = path_sram (context->rom_hash);
        FILE *sram_file = fopen (path, "wb");

        if (sram_file != NULL)
        {
            while (bytes_written < context->sram_size)
            {
                bytes_written += fwrite (context->sram + bytes_written, 1, context->sram_size - bytes_written, sram_file);
            }

            fclose (sram_file);
        }

        /* Update sram_last_write */
        memcpy (context->sram_last_write, context->sram, SMD_MAX_SRAM_SIZE);

        free (path);
    }
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
     *   combinations of resolution and internal vs external pixel-clocks.
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
         *       cycles to a pool and subtract as many full m68k cycles as we can,
         *       saving any leftovers for the next line. */
        m68k_run_cycles (context->m68k_context, 489);

        if (context->state.z80_reset_n && !context->state.z80_busreq)
        {
            z80_run_cycles (context->z80_context, 228);
        }

        smd_vdp_run_one_scanline (context->vdp_context);

        /* TODO: Variable clock rate when able to switch between PAL and NTSC */
        ym2612_run_cycles (context->ym2612_context, SMD_NTSC_MASTER_CLOCK / 7, 489); /* TODO: Same note as m68k */
        sn76489_run_cycles (context->psg_context, SMD_NTSC_MASTER_CLOCK / 15 , 228);
    }

    /* Periodic sync of cartridge-ram to file every five minutes.
     * This is in addition to the save that occurs when exiting. */
    if (util_get_ticks () > state.sync_time + 300000)
    {
        smd_sync (context);
        state.sync_time = util_get_ticks ();
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
 * Returns the current interrupt priority.
 */
static uint8_t smd_get_int (void *context_ptr)
{
    SMD_Context *context = (SMD_Context *) context_ptr;

    return smd_vdp_get_interrupt (context->vdp_context);
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

    if (context->ym2612_context != NULL)
    {
        free (context->ym2612_context);
        context->ym2612_context = NULL;
    }

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
 * Process the Mega Drive ROM's header.
 * Note that this is not the Super Magic Drive header.
 */
void smd_process_header (SMD_Context *context)
{
    /* 0x100: System Type - Sanity-check that a header is present */
    if (strncmp ((char *) &context->rom [0x100], "SEGA", 4) != 0)
    {
        fprintf (stdout, "Invalid ROM header.\n");
        return;
    }

    /* 0x150: Print the overseas game title */
    char title [49] = { '\0' };
    size_t title_length = 0;
    for (size_t i = 0; i < 48; i++)
    {
        char c = context->rom [0x150 + i];

        /* Skip over back-to-back whitespace */
        if (title_length && isspace (title [title_length - 1]) && isspace (c))
        {
            continue;
        }

        /* Avoid all-caps titles */
        if (title_length && isalpha (title [title_length - 1]))
        {
            title [title_length++] = tolower (context->rom [0x150 + i]);
        }
        else
        {
            title [title_length++] = context->rom [0x150 + i];
        }
    }
    fprintf (stdout, "Title: %s\n", title);

    /* 0x1b0: Extra Memory */
    if (strncmp ((char *) &context->rom [0x1b0], "RA", 2) == 0)
    {
        if (context->rom [0x1b3] == 0x20)
        {
            uint32_t sram_start = util_ntoh32 (* (uint32_t *) &context->rom [0x1b4]);
            uint32_t sram_end = util_ntoh32 (* (uint32_t *) &context->rom [0x1b8]);
            uint32_t sram_range = util_round_up (sram_end - sram_start);
            switch (context->rom [0x1b2])
            {
                case 0xa0:
                case 0xe0:
                    fprintf (stdout, "Warning: 16-bit SRAM not implemented.\n");
                    break;

                case 0xb0: /* Even bytes */
                case 0xf0:
                case 0xb8: /* Odd bytes */
                case 0xf8:
                    context->sram_start = sram_start;
                    context->sram_size = sram_range / 2;

                    if (context->sram_size > SMD_MAX_SRAM_SIZE)
                    {
                        fprintf (stdout, "Warning: Oversized SRAM not implemented.\n");
                        context->sram_size = SMD_MAX_SRAM_SIZE;
                    }

                    break;
            }
        }
        else if (context->rom [0x1b3] == 0x40)
        {
            fprintf (stdout, "Warning: EEPROM not implemented.\n");
        }
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
    YM2612_Context *ym2612_context;
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
    vdp_context = smd_vdp_init (context, smd_memory_read_16, smd_frame_done);
    context->vdp_context = vdp_context;

    /* Initialise sound chips */
    ym2612_context = ym2612_init ();
    context->ym2612_context = ym2612_context;
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
        smd_process_header (context);
    }

    /* Load SRAM if it exists */
    char *sram_path = path_sram (context->rom_hash);
    FILE *sram_file = fopen (sram_path, "rb");
    if (sram_file != NULL)
    {
        uint32_t bytes_read = 0;

        while (bytes_read < context->sram_size && !feof(sram_file))
        {
            bytes_read += fread (context->sram + bytes_read, 1, context->sram_size - bytes_read, sram_file);

            if (ferror (sram_file))
            {
                snepulator_error ("Error", "Error reading SRAM data.");
                fclose (sram_file);
                smd_cleanup (context);
                return NULL;
            }
        }
        fclose (sram_file);

        /* Update sram_last_write */
        memcpy (context->sram_last_write, context->sram, SMD_MAX_SRAM_SIZE);
    }
    free (sram_path);

    /* Hook up callbacks */
    state.audio_callback = smd_audio_callback;
    state.cleanup = smd_cleanup;
    state.get_rom_hash = smd_get_rom_hash;
    state.run_callback = smd_run;
    state.sync = smd_sync;
#ifdef DEVELOPER_BUILD
    /* state.diagnostics_show = smd_diagnostics_show; */
#endif

    /* For now, assume NTSC */
    state.clock_rate = SMD_NTSC_MASTER_CLOCK;

    /* Set controller mapping */
    gamepad [1].group = GAMEPAD_MAPPING_GROUP_SMD;
    gamepad [2].group = GAMEPAD_MAPPING_GROUP_SMD;

    /* Begin emulation */
    m68k_reset (m68k_context);
    state.run = RUN_STATE_RUNNING;

    return context;
}
