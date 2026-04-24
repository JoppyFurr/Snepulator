/*
 * Snepulator
 * Sega Mega Drive VDP implementation.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../snepulator.h"
#include "../util.h"
#include "smd_vdp.h"

/*
 * Read the VDP status register.
 */
uint16_t smd_vdp_status_read (SMD_VDP_Context *context)
{
    context->state.second_half_pending = false;

    /* TODO: VBlank / HBlank bits */
    /* TODO: Bit  [8:9] - FIFO status (full / empty) */
    /* TODO: Bit  [ 7 ] - Vertical Interrupt Occurred */
    /* TODO: Bit  [ 6 ] - Sprite Overflow */
    /* TODO: Bit  [ 5 ] - Sprite Collision */
    /* TODO: Bit  [ 4 ] - Odd vs Even frame (interlace mode) */
    /* TODO: Bits [3:2] - Blanking */
    /* TODO: Bit  [ 1 ] - DMA in progress */
    /* TODO: Bit  [ 0 ] - PAL */
    return 0x3400;
}


/*
 * Write to the VDP control port.
 */
void smd_vdp_control_write (SMD_VDP_Context *context, uint16_t data)
{
    /* Second half of address */
    if (context->state.second_half_pending)
    {
        context->state.address &= 0x3fff;
        context->state.address |= data << 14;

        context->state.code &= 0x03;
        context->state.code |= (data & 0xf0) >> 2;

        context->state.second_half_pending = false;

        uint16_t dma_length;
        uint32_t source_address;
        uint16_t dest_address;

        /* Begin DMA */
        if (context->state.mode_2_dma_en && (context->state.code & ADDRESS_CODE_DMA))
        {
            /* TODO: Initial implementation ignores timing */
            switch (context->state.dma_operation)
            {
                case 0x0: /* Memory Transfer */
                case 0x1:
                    switch (context->state.code & 0x07)
                    {
                        case 1: /* VRAM */
                            printf ("[%s] DMA Operation: M68k Memory -> VRAM transfer (%d words).\n",
                                    __func__, context->state.dma_length);
                            /* TODO: For VRAM, if source-address bit 0 is set, data may be byte-swapped */
                            dma_length = context->state.dma_length;
                            source_address = context->state.dma_source << 1;
                            dest_address = context->state.address;
                            do {
                                uint16_t data = context->memory_read_16 (context->parent, source_address);

                                /* TODO: Consider moving any endian-changes into m68k.c
                                 *       to avoid changing twice during dma. */
                                *(uint16_t *) (&context->state.vram [dest_address]) = util_hton16 (data);

                                source_address += 2;
                                dest_address += context->state.auto_increment;
                                dma_length--;
                            } while (dma_length > 0);
                            break;

                        case 3: /* CRAM */
                            printf ("[%s] DMA Operation: M68k Memory -> CRAM transfer (%d words).\n",
                                    __func__, context->state.dma_length);
                            dma_length = context->state.dma_length;
                            source_address = context->state.dma_source << 1;
                            dest_address = context->state.address;

                            /* TODO: Does the auto-incremented address used for DMA get written
                             *       back to the address register? */
                            do {
                                uint32_t cram_index = (context->state.address >> 1) & 0x3f;
                                uint16_t data = context->memory_read_16 (context->parent, source_address);

                                context->state.cram [cram_index] = (uint_pixel_t) { .r = ((data >> 1) & 0x07) * 0xff / 7,
                                                                                    .g = ((data >> 5) & 0x07) * 0xff / 7,
                                                                                    .b = ((data >> 9) & 0x07) * 0xff / 7};

                                if (data != 0)
                                {
                                    printf ("[%s] cram [%d] = rgb (%d, %d, %d)\n", __func__, cram_index,
                                             (data >> 1) & 0x07, (data >> 5) & 0x07, (data >> 9) & 0x07);
                                    snepulator_error ("SMD VDP", "First nonzero colour, time to start on drawing.");
                                }

                                source_address += 2;
                                dest_address += context->state.auto_increment;
                                dma_length--;
                            } while (dma_length > 0);
                            break;

                        case 5: /* VSRAM */
                            printf ("[%s] DMA Operation: M68k Memory -> VSRAM transfer. (Not implemented)\n", __func__);
                            printf ("[%s] DMA Source: 0x%06x.\n", __func__, context->state.dma_source << 1);
                            printf ("[%s] DMA Dest:   0x%04x.\n", __func__, context->state.address);
                            printf ("[%s] DMA Length: %d words.\n", __func__, context->state.dma_length);
                            printf ("[%s] Increment:  %d.\n", __func__, context->state.auto_increment);
                            break;

                        default:
                            printf ("[%s] Invalid DMA Operation: M68k Memory -> ???\n", __func__);
                            break;
                    }

                    break;
                case 0x2: /* VRAM Fill */
                    context->state.fill_pending = true;
                    break;

                case 0x3: /* VRAM Copy */
                    printf ("[%s] DMA Operation: VRAM Copy. (Not implemented)\n", __func__);
                    printf ("[%s] DMA Source Address = %06x.\n", __func__, context->state.dma_source);
                    printf ("[%s] DMA Dest Address = %04x.\n", __func__, context->state.address);
                    printf ("[%s] DMA Length = %04x words.\n", __func__, context->state.dma_length);
                    printf ("[%s] Increment = %d.\n", __func__, context->state.auto_increment);
                    break;
            }

        }
    }

    /* Register Writes */
    else if ((data & 0xe000) == 0x8000)
    {
        /* TODO: Writes may clear the code bits too */

        uint8_t addr = (data >> 8) & 0x1f;
        if (addr < 0x18)
        {
            context->state.regs [addr] = data;
            // printf ("[%s] reg[%02x] = %02x.\n", __func__, addr, data & 0xff);
        }
    }

    /* First half of address */
    else
    {
        /* TODO: Consider using bitfields to update these */
        context->state.address &= 0xc000;
        context->state.address |= data & 0x3fff;

        context->state.code &= 0x3c;
        context->state.code |= data >> 14;

        context->state.second_half_pending = true;
    }

    return;
}


/*
 * Read from the VDP data port.
 */
uint16_t smd_vdp_data_read (SMD_VDP_Context *context)
{
    context->state.second_half_pending = false;

    printf ("[%s] VDP data port read not implemented.\n", __func__);
    return 0xffff;
}


/*
 * Write to the VDP data port.
 */
void smd_vdp_data_write (SMD_VDP_Context *context, uint16_t data)
{
    context->state.second_half_pending = false;

    /* TODO: Initial fill implementation. 'instant', no timing considered. */
    /* TODO: Currently, only using the most-significant data byte.. Real
     *       hardware may write the least-significant to the very first
     *       address. */
    if (context->state.mode_2_dma_en && context->state.fill_pending)
    {
        uint8_t fill_value = data >> 8;
        for (uint16_t length = context->state.dma_length; length > 0; length--)
        {
            context->state.vram [context->state.address] = fill_value;
            context->state.address += context->state.auto_increment;
        }
    }

    /* VRAM Write */
    else if (context->state.code == 0x01)
    {
        printf ("[%s] VRAM Write not implemented.\n", __func__);
    }

    /* CRAM Write */
    else if (context->state.code == 0x03)
    {
        uint32_t index = (context->state.address >> 1) & 0x3f;
        context->state.address += context->state.auto_increment;

        context->state.cram [index] = (uint_pixel_t) { .r = ((data >> 1) & 0x07) * 0xff / 7,
                                                       .g = ((data >> 5) & 0x07) * 0xff / 7,
                                                       .b = ((data >> 9) & 0x07) * 0xff / 7};

        /* TODO: Wait until nonzero colours get written before implementing drawing */
        if (data != 0)
        {
            printf ("[%s] cram [%d] = rgb (%d, %d, %d)\n", __func__, index,
                     (data >> 1) & 0x07, (data >> 5) & 0x07, (data >> 9) & 0x07);
            snepulator_error ("SMD VDP", "First nonzero colour, time to start on drawing.");
        }
    }

    /* VSRAM write */
    else if (context->state.code == 0x05)
    {
        uint32_t index = (context->state.address >> 1) & 0x3f;
        context->state.address += context->state.auto_increment;
        if (index < 40)
        {
            context->state.vsram [index] = data & 0x03ff;
        }
    }

    else
    {
        printf ("[%s] VDP data port write for code %02x not implemented.\n", __func__, context->state.code);
    }
}


/*
 * Check if the VDP is currently requesting an interrupt.
 */
uint8_t smd_vdp_get_interrupt (SMD_VDP_Context *context)
{
    uint8_t interrupt = context->state.interrupt;

    context->state.interrupt = 0;

    return interrupt;
}


/*
 * Run one scanline on the VDP.
 */
void smd_vdp_run_one_scanline (SMD_VDP_Context *context)
{
    /* Update the V-Counter */
    /* TODO: For now, hard-coded for NTSC mode. PAL is 313 lines. */
    context->state.line = (context->state.line + 1) % 262;

    /* VBlank Interrupt */
    if (context->state.line == 224 && context->state.mode_2_vertical_int_en)
    {
        /* VBlank has interrupt priority 6 (highest from the VDP) */
        context->state.interrupt = 6;
    }

    return;
}


/*
 * Create an SMD VDP context with power-on defaults.
 */
SMD_VDP_Context *smd_vdp_init (void *parent,
                               uint16_t (* memory_read_16)  (void *, uint32_t),
                               void (* frame_done) (void *))
{
    SMD_VDP_Context *context;

    context = calloc (1, sizeof (SMD_VDP_Context));
    if (context == NULL)
    {
        snepulator_error ("Error", "Unable to allocate memory for SMD_VDP_Context");
        return NULL;
    }

    context->parent = parent;
    context->memory_read_16  = memory_read_16;
    context->video_width = 256;
    context->video_height = 224;

    context->frame_done = frame_done;

    return context;
}
