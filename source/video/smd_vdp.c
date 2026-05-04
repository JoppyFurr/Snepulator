/*
 * Snepulator
 * Sega Mega Drive VDP implementation.
 *
 * TODO:
 *  - Sprites
 *  - Scrolling
 *  - Priority
 *  - Window
 *  - Line interrupts
 *  - Hacks (eg, removing sprite limit, disable blanking)
 *  - Hilight & Shadow
 *  - Width=256 mode
 *  - PAL mode
 *  - Interlace mode
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
                            do {
                                uint16_t data = context->memory_read_16 (context->parent, source_address);

                                /* TODO: Consider moving any endian-changes into m68k.c
                                 *       to avoid changing twice during dma. */
                                *(uint16_t *) (&context->state.vram [context->state.address]) = util_hton16 (data);

                                source_address += 2;
                                context->state.address += context->state.auto_increment;
                                dma_length--;
                            } while (dma_length > 0);
                            break;

                        case 3: /* CRAM */
                            printf ("[%s] DMA Operation: M68k Memory -> CRAM transfer (%d words).\n",
                                    __func__, context->state.dma_length);
                            dma_length = context->state.dma_length;
                            source_address = context->state.dma_source << 1;

                            /* TODO: Does the auto-incremented address used for DMA get written
                             *       back to the address register? */
                            do {
                                uint32_t cram_index = (context->state.address >> 1) & 0x3f;
                                uint16_t data = context->memory_read_16 (context->parent, source_address);

                                context->state.cram [cram_index] = (uint_pixel_t) { .r = ((data >> 1) & 0x07) * 0xff / 7,
                                                                                    .g = ((data >> 5) & 0x07) * 0xff / 7,
                                                                                    .b = ((data >> 9) & 0x07) * 0xff / 7};

                                source_address += 2;
                                context->state.address += context->state.auto_increment;
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
        context->state.fill_pending = false;
    }

    /* VRAM Write */
    else if (context->state.code == 0x01)
    {
        *(uint16_t *) &context->state.vram [context->state.address] = util_hton16 (data);
        context->state.address += context->state.auto_increment;
    }

    /* CRAM Write */
    else if (context->state.code == 0x03)
    {
        uint32_t index = (context->state.address >> 1) & 0x3f;

        context->state.cram [index] = (uint_pixel_t) { .r = ((data >> 1) & 0x07) * 0xff / 7,
                                                       .g = ((data >> 5) & 0x07) * 0xff / 7,
                                                       .b = ((data >> 9) & 0x07) * 0xff / 7};
        context->state.address += context->state.auto_increment;
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
 * Render one line of an 8×8 pattern.
 * Background version.
 * Supports vertical and horizontal mirroring.
 */
static void smd_vdp_draw_pattern_background (SMD_VDP_Context *context, uint16_t line, SMD_VDP_Pattern *pattern_base,
                                             uint_pixel_t *palette, int_point_t position,
                                             bool flip_h, bool flip_v)
{
    uint32_t pattern_line;
    uint32_t pixel_bit;

    /* Get the line within the pattern */
    if (flip_v)
    {
        pattern_line = ((uint32_t *) pattern_base) [position.y - line + 7];
    }
    else
    {
        pattern_line = ((uint32_t *) pattern_base) [line - position.y];
    }

    int32_t destination_start = position.x + line * context->frame_buffer.width;

    for (int32_t x = 0; x < 8; x++)
    {
        /* Nothing to do outside of the active area. Continue if we're to the left,
         * as the next pixel might be on-screen. Return if we're to the right. */
        if (x + position.x < 0)
        {
            continue;
        }
        else if (x + position.x >= context->frame_buffer.width)
        {
            return;
        }

        if (flip_h)
        {
            pixel_bit = x;
        }
        else
        {
            pixel_bit = 7 - x;
        }

        uint8_t colour_index = ((pattern_line >> (pixel_bit     )) & 0x01) |
                               ((pattern_line >> (pixel_bit +  7)) & 0x02) |
                               ((pattern_line >> (pixel_bit + 14)) & 0x04) |
                               ((pattern_line >> (pixel_bit + 21)) & 0x08);

        if (colour_index != 0)
        {
            uint_pixel_t pixel = palette [colour_index];
            context->frame_buffer.active_area [destination_start + x] = pixel;
        }
    }
}


/*
 * Render one line of the background layer.
 */
static void smd_vdp_draw_background (SMD_VDP_Context *context, uint16_t line, uint16_t name_table_base)
{
    uint16_t num_rows;
    uint16_t num_cols;

    /* Plane size - Width */
    switch (context->state.plane_size & 0x03)
    {
        case 0x00:
            num_cols = 32;
            break;
        case 0x01:
            num_cols = 64;
            break;
        case 0x02:
            /* Invalid */
            return;
        case 0x03:
        default:
            num_cols = 128;
            break;
    }

    /* Plane size - Height */
    switch (context->state.plane_size & 0x30)
    {
        case 0x00:
            num_rows = 32;
            break;
        case 0x10:
            num_rows = 64;
            break;
        case 0x20:
            /* Invalid */
            return;
        case 0x30:
        default:
            num_rows = 128;
            break;
    }

    /* Name table cannot exceed 8 KiB */
    if (num_rows * num_cols > 4096)
    {
        /* Invalid */
        return;
    }

    /* Name-table row and starting-column for this line */
    /* TODO: Scrolling */
    uint16_t tile_y = (line >> 3) % num_rows;

    int_point_t position; /* Position of the pattern on the display */

    for (uint32_t tile_x = 0; tile_x < num_cols; tile_x++)
    {
        uint16_t tile_address = name_table_base + (tile_x + tile_y * num_cols) * 2;

        SMD_VDP_Name_Table_Entry tile;
        tile.data = util_ntoh16 (* (uint16_t *) &context->state.vram [tile_address]);

        SMD_VDP_Pattern *pattern = (SMD_VDP_Pattern *) &context->state.vram [(tile.pattern) * sizeof (SMD_VDP_Pattern)];

        uint_pixel_t *palette = &context->state.cram [tile.palette << 4];

        position.x = 8 * tile_x;
        position.y = 8 * tile_y;
        smd_vdp_draw_pattern_background (context, line, pattern, palette, position, tile.h_flip, tile.v_flip);
    }
}


/*
 * Render one active line of output for the Mega Drive VDP.
 * Note: For now, this assumes Mode-5, 320x224.
 */
void smd_vdp_render_line (SMD_VDP_Context *context, uint16_t line)
{
    /* Backdrop */
    uint_pixel_t video_backdrop = context->state.cram [context->state.backdrop_colour & 0x3f];
    context->frame_buffer.backdrop [line] = video_backdrop;

    /* If blanking is enabled, fill the active area with the backdrop colour. */
    if (!context->state.mode_2_blank)
    {
        uint32_t line_start = line * context->frame_buffer.width;
        for (int x = 0; x < context->frame_buffer.width; x++)
        {
            context->frame_buffer.active_area [line_start + x] = video_backdrop;
        }

        /* TODO: Any work that occurs even when blanking is enabled.
         *       Eg, like sprite-overflow on the SMS */
        return;
    }

    /* Draw Plane B */
    uint16_t plane_b_base = (context->state.plane_b_name_table_base & 0x07) << 13;
    smd_vdp_draw_background (context, line, plane_b_base);

    /* Draw Plane A */
    uint16_t plane_a_base = (context->state.plane_a_name_table_base & 0x38) << 10;
    smd_vdp_draw_background (context, line, plane_a_base);

    /* TODO: Draw sprites */
}


/*
 * Run one scanline on the VDP.
 */
void smd_vdp_run_one_scanline (SMD_VDP_Context *context)
{
    /* Update the V-Counter */
    /* TODO: For now, hard-coded for NTSC mode. PAL is 313 lines. */
    context->state.line = (context->state.line + 1) % 262;

    /* TODO: If this is the first line, update the mode.
     *       For now, the typical NTSC resolution is hard-coded. */
    context->frame_buffer.width = 320;
    context->frame_buffer.height = 224;
    context->lines_active = 224;

    /* If this is an active line, render it */
    if (context->state.line < context->lines_active)
    {
        smd_vdp_render_line (context, context->state.line);
    }

    /* If this the final active line, copy the frame for output to the user */
    if (context->state.line == context->lines_active - 1)
    {
        snepulator_frame_done (&context->frame_buffer);
    }

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
    context->frame_done = frame_done;

    return context;
}
