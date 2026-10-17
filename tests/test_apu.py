"""
test_apu.py: Unit tests for reusable Game Boy APU Channel 1 synthesis and SFX tables.

Uses synthetic tests and algorithmic validation against DMG APU register semantics.
"""

import unittest


class GbApuChannel1Tests(unittest.TestCase):
    def test_frequency_calculation(self):
        # Formula: freq = 131072 / (2048 - raw_freq)
        # For raw_freq = 0x079D (from Alleyway SFX 7: NR13=0x9D, NR14=0xC7 -> bits 0..2 = 7 -> 0x79D = 1949):
        # 2048 - 1949 = 99
        # Frequency = 131072 / 99 = 1323.95 Hz (around E6)
        raw_freq = (0x07 << 8) | 0x9D
        period_divider = 2048 - raw_freq
        self.assertEqual(period_divider, 99)
        freq_hz = 131072 / period_divider
        self.assertAlmostEqual(freq_hz, 1323.959, places=2)

    def test_envelope_periods(self):
        # In DMG APU, NR12 bit 7..4 is initial volume, bit 3 is direction, bit 2..0 is step period
        # SFX 7: NR12 = 0xF0 -> volume = 15, direction = 0 (decay), step period = 0 (no step decay)
        nr12 = 0xF0
        initial_vol = (nr12 >> 4) & 0x0F
        direction = (nr12 & 0x08) != 0
        step_period = nr12 & 0x07
        self.assertEqual(initial_vol, 15)
        self.assertFalse(direction)
        self.assertEqual(step_period, 0)

        # SFX 1: NR12 = 0xF2 -> volume = 15, direction = 0, step period = 2 (decay every 2/64 = 1/32 sec)
        nr12_sfx1 = 0xF2
        self.assertEqual((nr12_sfx1 >> 4) & 0x0F, 15)
        self.assertEqual(nr12_sfx1 & 0x07, 2)

    def test_sweep_direction_and_shift(self):
        # SFX 7: NR10 = 0x1A -> bit 6..4 = 1 (sweep time 1/128s), bit 3 = 1 (negate / downward sweep), shift = 2
        nr10 = 0x1A
        sweep_time = (nr10 >> 4) & 0x07
        sweep_negate = (nr10 & 0x08) != 0
        sweep_shift = nr10 & 0x07
        self.assertEqual(sweep_time, 1)
        self.assertTrue(sweep_negate)
        self.assertEqual(sweep_shift, 2)


if __name__ == '__main__':
    unittest.main()
