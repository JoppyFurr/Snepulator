/*
 * Snepulator
 * Sega Mega Drive VDP implementation.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../snepulator.h"
#include "smd_vdp.h"

/*
 * Read the VDP status register.
 */
uint16_t smd_vdp_status_read (SMD_VDP_Context *context)
{
    context->state.second_half_pending = false;

    /* TODO: Bit 0 for PAL */
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

        /* Begin DMA */
        if (context->state.mode_2_dma_en && (context->state.code & ADDRESS_CODE_DMA))
        {
            /* TODO: Initial implementation ignores timing */
            switch (context->state.dma_operation)
            {
                case 0x0: /* Memory Transfer */
                case 0x1:
                    printf ("[%s] DMA Operation: M68k Memory -> VDP transfer. (Not implemented)\n", __func__);
                    printf ("[%s] DMA Source Address = %06x.\n", __func__, context->state.dma_source);
                    printf ("[%s] DMA Dest Address = %04x.\n", __func__, context->state.address);
                    printf ("[%s] DMA Length = %04x words.\n", __func__, context->state.dma_length);
                    printf ("[%s] Increment = %d.\n", __func__, context->state.auto_increment);
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
            context->vram [context->state.address] = fill_value;
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
        printf ("[%s] cram [%d] = rgb (%d, %d, %d)\n", __func__, index,
                 (data >> 1) & 0x07, (data >> 5) & 0x07, (data >> 9) & 0x07);
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
 * Run one scanline on the VDP.
 */
void smd_vdp_run_one_scanline (SMD_VDP_Context *context)
{
    /* Update the V-Counter */
    /* TODO: For now, hard-coded for NTSC mode. PAL is 313 lines. */
    context->state.line = (context->state.line + 1) % 262;

    return;
}


/*
 * Create an SMD VDP context with power-on defaults.
 */
SMD_VDP_Context *smd_vdp_init (void *parent, void (* frame_done) (void *))
{
    SMD_VDP_Context *context;

    context = calloc (1, sizeof (SMD_VDP_Context));
    if (context == NULL)
    {
        snepulator_error ("Error", "Unable to allocate memory for SMD_VDP_Context");
        return NULL;
    }

    context->parent = parent;
    context->video_width = 256;
    context->video_height = 224;

    context->frame_done = frame_done;

    return context;
}
