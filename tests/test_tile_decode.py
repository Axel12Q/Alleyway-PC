"""
test_tile_decode.py: Unit tests for Game Boy 2bpp tile decoding and rendering.

Uses synthetic 2bpp fixtures and algorithmic verification without requiring any commercial ROM.
"""

import unittest


def decode_2bpp_tile(tile_bytes, palette_reg=0xE4):
    """Pure Python reference implementation of DMG 2bpp tile decoding."""
    if len(tile_bytes) != 16:
        raise ValueError("2bpp tile must be exactly 16 bytes")
    grid = []
    for y in range(8):
        low = tile_bytes[y * 2]
        high = tile_bytes[y * 2 + 1]
        row = []
        for x in range(8):
            shift = 7 - x
            color_idx = ((low >> shift) & 1) | (((high >> shift) & 1) << 1)
            shade = (palette_reg >> (color_idx * 2)) & 3
            row.append(shade)
        grid.append(row)
    return grid


class GbTileDecodeTests(unittest.TestCase):
    def test_solid_tiles(self):
        # All zeros -> color 0 everywhere
        zero_tile = bytes([0x00] * 16)
        decoded = decode_2bpp_tile(zero_tile)
        self.assertEqual(len(decoded), 8)
        self.assertTrue(all(val == 0 for row in decoded for val in row))

        # Color 1 everywhere: low=0xFF, high=0x00
        c1_tile = bytes([0xFF, 0x00] * 8)
        decoded1 = decode_2bpp_tile(c1_tile)
        self.assertTrue(all(val == 1 for row in decoded1 for val in row))

        # Color 2 everywhere: low=0x00, high=0xFF
        c2_tile = bytes([0x00, 0xFF] * 8)
        decoded2 = decode_2bpp_tile(c2_tile)
        self.assertTrue(all(val == 2 for row in decoded2 for val in row))

        # Color 3 everywhere: low=0xFF, high=0xFF
        c3_tile = bytes([0xFF, 0xFF] * 8)
        decoded3 = decode_2bpp_tile(c3_tile)
        self.assertTrue(all(val == 3 for row in decoded3 for val in row))

    def test_checkerboard_pattern(self):
        # Checkerboard: alternating pixels (0b10101010 = 0xAA, 0b01010101 = 0x55)
        # Low plane = 0xAA, High plane = 0x00 -> alternating 0 and 1
        tile = bytes([0xAA, 0x00, 0x55, 0x00] * 4)
        decoded = decode_2bpp_tile(tile)
        expected_row0 = [1, 0, 1, 0, 1, 0, 1, 0]
        expected_row1 = [0, 1, 0, 1, 0, 1, 0, 1]
        self.assertEqual(decoded[0], expected_row0)
        self.assertEqual(decoded[1], expected_row1)

    def test_palette_remapping(self):
        # Tile with gradient row: colors 0, 1, 2, 3 in pixels 0..3
        # low bit:  0 1 0 1 0 0 0 0 = 0x50
        # high bit: 0 0 1 1 0 0 0 0 = 0x30
        tile = bytes([0x50, 0x30] + [0x00] * 14)

        # Standard DMG identity palette: 0xE4 (11 10 01 00 -> 3, 2, 1, 0)
        # Color 0 -> shade 0, Color 1 -> shade 1, Color 2 -> shade 2, Color 3 -> shade 3
        d_std = decode_2bpp_tile(tile, palette_reg=0xE4)
        self.assertEqual(d_std[0][:4], [0, 1, 2, 3])

        # Inverted DMG palette: 0x1B (00 01 10 11 -> 0, 1, 2, 3)
        # Color 0 -> shade 3, Color 1 -> shade 2, Color 2 -> shade 1, Color 3 -> shade 0
        d_inv = decode_2bpp_tile(tile, palette_reg=0x1B)
        self.assertEqual(d_inv[0][:4], [3, 2, 1, 0])


if __name__ == '__main__':
    unittest.main()
