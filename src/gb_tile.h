#ifndef GB_TILE_H
#define GB_TILE_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Generic Game Boy 2bpp (2 bits per pixel) planar tile decoder.
 *
 * Game Boy tile format:
 * - Each 8x8 pixel tile consists of 16 contiguous bytes.
 * - Each 8-pixel horizontal row is encoded as 2 bytes:
 *     low_byte  (bit 0 of each pixel)
 *     high_byte (bit 1 of each pixel)
 * - Bit 7 corresponds to the leftmost pixel (X=0), bit 0 to the rightmost (X=7).
 * - Pixel color index = (low_bit) | (high_bit << 1), range 0..3.
 *
 * Palette mapping:
 * - A standard Game Boy palette register (BGP, OBP0, OBP1) maps 2-bit color indices
 *   to 2-bit display shades:
 *     shade = (palette_reg >> (color_index * 2)) & 3
 *   where 0 = lightest, 3 = darkest.
 */

#define GB_TILE_SIZE_BYTES 16
#define GB_TILE_WIDTH 8
#define GB_TILE_HEIGHT 8

/*
 * Decodes a single 8x8 2bpp tile into an 8x8 array of 2-bit color indices (0..3).
 *
 * @param tile_bytes Pointer to exactly 16 bytes of 2bpp tile data.
 * @param out_indices Pointer to uint8_t[8][8] buffer receiving color indices (0..3).
 */
static inline void gb_tile_decode_indices(const uint8_t tile_bytes[GB_TILE_SIZE_BYTES], uint8_t out_indices[8][8]) {
    for (int y = 0; y < 8; ++y) {
        uint8_t low = tile_bytes[y * 2];
        uint8_t high = tile_bytes[y * 2 + 1];
        for (int x = 0; x < 8; ++x) {
            int shift = 7 - x;
            uint8_t bit0 = (uint8_t)((low >> shift) & 1u);
            uint8_t bit1 = (uint8_t)((high >> shift) & 1u);
            out_indices[y][x] = (uint8_t)(bit0 | (bit1 << 1));
        }
    }
}

/*
 * Decodes a single 8x8 2bpp tile into an 8x8 array of 32-bit ARGB/XRGB pixels
 * using a palette register (e.g. 0xE4 for DMG identity mapping) and an array of 4 shades.
 *
 * @param tile_bytes Pointer to 16 bytes of 2bpp data.
 * @param palette_reg DMG palette register byte (BGP, OBP0, or OBP1).
 * @param shades Array of 4 32-bit color values (shade 0..3).
 * @param out_pixels Pointer to uint32_t[8][8] destination buffer.
 */
static inline void gb_tile_decode_pixels(
    const uint8_t tile_bytes[GB_TILE_SIZE_BYTES],
    uint8_t palette_reg,
    const uint32_t shades[4],
    uint32_t out_pixels[8][8]
) {
    for (int y = 0; y < 8; ++y) {
        uint8_t low = tile_bytes[y * 2];
        uint8_t high = tile_bytes[y * 2 + 1];
        for (int x = 0; x < 8; ++x) {
            int shift = 7 - x;
            uint8_t color_idx = (uint8_t)(((low >> shift) & 1u) | (((high >> shift) & 1u) << 1));
            uint8_t shade = (uint8_t)((palette_reg >> (color_idx * 2)) & 3u);
            out_pixels[y][x] = shades[shade];
        }
    }
}

/*
 * Blits an 8x8 2bpp tile directly onto a framebuffer of size fb_w x fb_h.
 *
 * @param tile_bytes Pointer to 16 bytes of 2bpp data.
 * @param dest_x Top-left X destination on framebuffer (supports negative / clipping).
 * @param dest_y Top-left Y destination on framebuffer (supports negative / clipping).
 * @param flip_x If true, flips horizontally (for DMG sprite OAM attribute bit 5).
 * @param flip_y If true, flips vertically (for DMG sprite OAM attribute bit 6).
 * @param transparent_color_0 If true, color index 0 is not drawn (DMG sprite transparent pixel).
 * @param palette_reg DMG palette register byte (BGP, OBP0, or OBP1).
 * @param shades Array of 4 32-bit color values (shade 0..3).
 * @param fb Pointer to flat framebuffer pixels (stride = fb_w).
 * @param fb_w Width of destination framebuffer.
 * @param fb_h Height of destination framebuffer.
 */
static inline void gb_tile_blit(
    const uint8_t tile_bytes[GB_TILE_SIZE_BYTES],
    int dest_x,
    int dest_y,
    bool flip_x,
    bool flip_y,
    bool transparent_color_0,
    uint8_t palette_reg,
    const uint32_t shades[4],
    uint32_t *fb,
    int fb_w,
    int fb_h
) {
    for (int ty = 0; ty < 8; ++ty) {
        int py = flip_y ? (7 - ty) : ty;
        int fy = dest_y + ty;
        if (fy < 0 || fy >= fb_h) continue;

        uint8_t low = tile_bytes[py * 2];
        uint8_t high = tile_bytes[py * 2 + 1];

        for (int tx = 0; tx < 8; ++tx) {
            int px = flip_x ? (7 - tx) : tx;
            int fx = dest_x + tx;
            if (fx < 0 || fx >= fb_w) continue;

            int shift = 7 - px;
            uint8_t color_idx = (uint8_t)(((low >> shift) & 1u) | (((high >> shift) & 1u) << 1));
            if (transparent_color_0 && color_idx == 0) {
                continue;
            }
            uint8_t shade = (uint8_t)((palette_reg >> (color_idx * 2)) & 3u);
            fb[fy * fb_w + fx] = shades[shade];
        }
    }
}

#endif /* GB_TILE_H */
