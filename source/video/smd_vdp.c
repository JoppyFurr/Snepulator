/*
 * Snepulator
 * Sega Mega Drive VDP implementation.
 *
 * TODO:
 *  - Vertical Scrolling (strips-of-16)
 *  - Horizontal Scrolling (strips-of-8 and invalid)
 *  - Priority
 *  - Window
 *  - Line interrupts
 *  - Hacks (eg, removing sprite limit, disable blanking)
 *  - Hilight & Shadow
 *  - Width=256 mode, consider adjustment of aspect ratio to keep the image size the same?
 *  - PAL mode
 *  - Interlace mode
 *  - Sprite masking (X=0, X=1)
 *  - Sprite collisions
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
 * Supports vertical and horizontal mirroring.
 * Note: This assumes that the pattern requested is on the line.
 */
static void smd_vdp_draw_pattern_line (SMD_VDP_Context *context, uint16_t line, SMD_VDP_Pattern *pattern,
                                       uint_pixel_t *palette, int_point_t position, bool flip_h, bool flip_v)
{

    /* Get the line within the pattern. Endian is chosen such that the
     * pixel within the line can be selected with a single bit-shift. */
    uint32_t pattern_line_index = (flip_v) ? position.y - line + 7 : line - position.y;
    uint32_t pattern_line = util_ntoh32 (pattern->line [pattern_line_index]);

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

        uint32_t pattern_pixel = (flip_h) ? x : 7 - x;
        uint32_t colour_index = pattern_line >> (pattern_pixel * 4) & 0x0f;

        if (colour_index != 0)
        {
            context->frame_buffer.active_area [destination_start + x] = palette [colour_index];
        }
    }
}


/*
 * Render one line of the sprite layer.
 */
static void smd_vdp_draw_sprites (SMD_VDP_Context *context, uint16_t line, bool priority)
{
    /* TODO: In Width=320 mode, the base-address register only provides 6 bits
     *       of the address. In Width=256 mode, it provides 7 bits. */
    uint16_t *sprite_table = (uint16_t *) &context->state.vram [(context->state.sprite_table_base << 9) & 0xfc00];

    uint32_t line_sprite_buffer [20];
    uint32_t line_sprite_count = 0;
    uint32_t sprite_index = 0;

    /* Traverse the sprite list, filling the line sprite buffer */
    /* TODO: When implementing Width=256 mode, the maximum is 64 sprites */
    /* TODO: Only do this once - It doesn't need to happen for both low & high priority passes */
    uint32_t pixel_count = 0;
    for (int count = 0; count < 80; count++)
    {
        SMD_VDP_Sprite_Table_Entry sprite;
        sprite.data [0] = util_ntoh16 (sprite_table [sprite_index * 4]);
        sprite.data [1] = util_ntoh16 (sprite_table [sprite_index * 4 + 1]);
        int_point_t position = { .y = sprite.y - 128};

        /* If the sprite is on this line, add it to the buffer */
        if (line >= position.y && line < position.y + 8 + sprite.height * 8)
        {
            line_sprite_buffer [line_sprite_count++] = sprite_index;
            pixel_count += 8 * (sprite.width + 1);
        }

        /* TODO: When implementing Width=256 mode, there are only 16 sprites per line */
        /* TODO: When implementing Width=256 mode, only 256 pixels worth of spite-data */
        if (sprite.link == 0 || line_sprite_count == 20 || pixel_count >= 320)
        {
            break;
        }

        sprite_index = sprite.link;
    }

    /* Render the sprites in the line sprite buffer.
     * Done in reverse order so that the first sprite is the one left on the screen */
    while (line_sprite_count--)
    {
        SMD_VDP_Sprite_Table_Entry sprite;
        sprite.data [0] = util_ntoh16 (sprite_table [line_sprite_buffer [line_sprite_count] * 4]);
        sprite.data [1] = util_ntoh16 (sprite_table [line_sprite_buffer [line_sprite_count] * 4 + 1]);
        sprite.data [2] = util_ntoh16 (sprite_table [line_sprite_buffer [line_sprite_count] * 4 + 2]);
        sprite.data [3] = util_ntoh16 (sprite_table [line_sprite_buffer [line_sprite_count] * 4 + 3]);

        if (sprite.priority != priority)
        {
            continue;
        }

        uint_pixel_t *palette = &context->state.cram [sprite.palette << 4];
        int_point_t position = { .x = sprite.x - 128, .y=sprite.y - 128};

        uint32_t tile_y = (line - position.y) / 8;
        int_point_t tile_position = { .y = position.y + tile_y * 8 };

        /* Update tile_y to account for v-flip, for use by the pattern-index calculation below */
        tile_y = (sprite.v_flip) ? sprite.height - tile_y : tile_y;

        for (uint32_t tile_x = 0; tile_x < sprite.width + 1; tile_x++)
        {
            /* For the pattern index account for h-flip. */
            uint32_t pattern_index = sprite.pattern + tile_y + (sprite.h_flip ? sprite.width - tile_x : tile_x) * (sprite.height + 1);

            SMD_VDP_Pattern *pattern = (SMD_VDP_Pattern *) &context->state.vram [pattern_index * sizeof (SMD_VDP_Pattern)];
            tile_position.x = position.x + tile_x * 8;

            smd_vdp_draw_pattern_line (context, line, pattern, palette, tile_position, sprite.h_flip, sprite.v_flip);
        }
    }
}


/*
 * Render one line of the background layer.
 */
static void smd_vdp_draw_background (SMD_VDP_Context *context, uint16_t line, uint16_t *name_table,
                                     uint16_t h_scroll, uint16_t v_scroll, bool priority)
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

    uint16_t h_scroll_fine = h_scroll & 0x07;
    uint16_t h_scroll_coarse = h_scroll >> 3;
    uint16_t v_scroll_fine = v_scroll & 0x07;

    /* Name-table row for this line */
    uint16_t tile_y = ((line + v_scroll) >> 3) % num_rows;
    uint16_t screen_tile_y = (line + v_scroll_fine) >> 3;

    uint16_t *name_table_row = &name_table [tile_y * num_cols];

    int_point_t position; /* Position of the pattern on the display */
    position.y = 8 * screen_tile_y - v_scroll_fine;

    /* TODO: Stop when screen_tile_x reaches 32 for Width=256 mode */
    for (int32_t screen_tile_x = -1; screen_tile_x < 40; screen_tile_x++)
    {
        /* Note: Starting at -1, subtracting a maximum coarse-scroll of 127 gives
         *       a minimum value of -128. Add +128 to keep the result positive. */
        SMD_VDP_Name_Table_Entry tile;
        tile.data = util_ntoh16 (name_table_row [(screen_tile_x - h_scroll_coarse + 128) % num_cols]);

        if (tile.priority != priority)
        {
            continue;
        }

        SMD_VDP_Pattern *pattern = (SMD_VDP_Pattern *) &context->state.vram [(tile.pattern) * sizeof (SMD_VDP_Pattern)];

        uint_pixel_t *palette = &context->state.cram [tile.palette << 4];

        position.x = 8 * screen_tile_x + h_scroll_fine;
        smd_vdp_draw_pattern_line (context, line, pattern, palette, position, tile.h_flip, tile.v_flip);
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

    /* Start by filling the screen with the backdrop colour */
    uint32_t line_start = line * context->frame_buffer.width;
    for (int x = 0; x < context->frame_buffer.width; x++)
    {
        context->frame_buffer.active_area [line_start + x] = video_backdrop;
    }

    /* If blanking is enabled, stop now, leaving the active area with only the backdrop colour. */
    if (!context->state.mode_2_blank)
    {
        /* TODO: Any work that occurs even when blanking is enabled.
         *       Eg, like sprite-overflow on the SMS */
        return;
    }

    uint16_t v_scroll_a = 0;
    uint16_t v_scroll_b = 0;

    if (context->state.mode_3 & 0x04)
    {
        printf ("[%s] Scroll per 2-cell column not implemented.\n", __func__);
    }
    else
    {
        v_scroll_a = context->state.vsram [0];
        v_scroll_b = context->state.vsram [1];
    }

    uint16_t *h_scroll_table = (uint16_t *) &context->state.vram [(context->state.h_scroll_data_base & 0x3f) << 10];
    uint16_t h_scroll_a = 0;
    uint16_t h_scroll_b = 0;

    switch (context->state.mode_3 & 0x03)
    {
        case 0: /* Full screen scrolling */
            h_scroll_a = util_ntoh16 (h_scroll_table [0]) & 0x03ff;
            h_scroll_b = util_ntoh16 (h_scroll_table [1]) & 0x03ff;
            break;

        case 1: /* Invalid */
            printf ("[%s] Invalid scroll mode not implemented.\n", __func__);
            break;

        case 2: /* Scrolling per strip of 8 lines */
            printf ("[%s] Scroll per 8-lines not implemented.\n", __func__);
            break;

        case 3: /* Scrolling per line */
            h_scroll_a = util_ntoh16 (h_scroll_table [line * 2 + 0]) & 0x03ff;
            h_scroll_b = util_ntoh16 (h_scroll_table [line * 2 + 1]) & 0x03ff;
            break;
    }

    uint16_t *name_table_b = (uint16_t *) &context->state.vram [(context->state.plane_b_name_table_base & 0x07) << 13];
    uint16_t *name_table_a = (uint16_t *) &context->state.vram [(context->state.plane_a_name_table_base & 0x38) << 10];

    /* First pass - Low priority */
    smd_vdp_draw_background (context, line, name_table_b, h_scroll_b, v_scroll_b, false);
    smd_vdp_draw_background (context, line, name_table_a, h_scroll_a, v_scroll_a, false);
    smd_vdp_draw_sprites (context, line, false);

    /* Second pass - High priority */
    smd_vdp_draw_background (context, line, name_table_b, h_scroll_b, v_scroll_b, true);
    smd_vdp_draw_background (context, line, name_table_a, h_scroll_a, v_scroll_a, true);
    smd_vdp_draw_sprites (context, line, true);
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
