#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "gb_tile.h"
#include "gb_apu.h"

static int g_failed = 0;

#define ASSERT_EQ(actual, expected, msg) do { \
    if ((actual) != (expected)) { \
        fprintf(stderr, "FAIL: %s (got %lld, expected %lld) at %s:%d\n", \
                (msg), (long long)(actual), (long long)(expected), __FILE__, __LINE__); \
        g_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s at %s:%d\n", (msg), __FILE__, __LINE__); \
        g_failed++; \
        return; \
    } \
} while (0)

/* --- gb_tile.h tests --- */

static void test_tile_decode_indices(void) {
    /* 16 bytes: low=0xAA, high=0x55 (alternating bit planes) */
    uint8_t tile[16];
    for (int y = 0; y < 8; ++y) {
        tile[y * 2] = 0xAA;     /* 10101010 */
        tile[y * 2 + 1] = 0x55; /* 01010101 */
    }
    uint8_t indices[8][8];
    gb_tile_decode_indices(tile, indices);

    /* In binary: bit0 from low, bit1 from high
     * x=0: low bit7=1, high bit7=0 -> 1
     * x=1: low bit6=0, high bit6=1 -> 2
     */
    for (int y = 0; y < 8; ++y) {
        ASSERT_EQ(indices[y][0], 1, "tile index bit0");
        ASSERT_EQ(indices[y][1], 2, "tile index bit1");
        ASSERT_EQ(indices[y][2], 1, "tile index bit0");
        ASSERT_EQ(indices[y][3], 2, "tile index bit1");
    }
}

static void test_tile_decode_pixels_palette(void) {
    /* Solid color 1 tile: low=0xFF, high=0x00 */
    uint8_t tile[16];
    for (int y = 0; y < 8; ++y) {
        tile[y * 2] = 0xFF;
        tile[y * 2 + 1] = 0x00;
    }
    const uint32_t shades[4] = {0x111111, 0x222222, 0x333333, 0x444444};
    uint32_t out[8][8];

    /* Standard palette 0xE4 (color 1 -> shade 1) */
    gb_tile_decode_pixels(tile, 0xE4, shades, out);
    ASSERT_EQ(out[0][0], shades[1], "palette 0xE4 shade 1");

    /* Inverted palette 0x1B (color 1 -> shade 2) */
    gb_tile_decode_pixels(tile, 0x1B, shades, out);
    ASSERT_EQ(out[0][0], shades[2], "palette 0x1B shade 2");
}

static void test_tile_blit_flips_and_transparency(void) {
    /* 8x8 tile: pixel (0,0) is color 3, pixel (7,0) is color 1, others color 0 */
    uint8_t tile[16] = {0};
    /* Row 0: low bit 7=1 (x=0), low bit 0=1 (x=7) -> low=0x81 */
    tile[0] = 0x81;
    /* high bit 7=1 (x=0), high bit 0=0 (x=7) -> high=0x80 */
    tile[1] = 0x80;

    const uint32_t shades[4] = {0x000000, 0x111111, 0x222222, 0x333333};
    uint32_t fb[16 * 16];
    memset(fb, 0, sizeof(fb));

    /* Normal blit at (4, 4) with transparency of color 0 */
    gb_tile_blit(tile, 4, 4, false, false, true, 0xE4, shades, fb, 16, 16);
    ASSERT_EQ(fb[4 * 16 + 4], shades[3], "unflipped top-left pixel");
    ASSERT_EQ(fb[4 * 16 + 11], shades[1], "unflipped top-right pixel");
    ASSERT_EQ(fb[4 * 16 + 5], 0, "transparent pixel preserved");

    /* Flip X blit at (4, 4) */
    memset(fb, 0, sizeof(fb));
    gb_tile_blit(tile, 4, 4, true, false, true, 0xE4, shades, fb, 16, 16);
    ASSERT_EQ(fb[4 * 16 + 4], shades[1], "flipped X top-left pixel is now shade 1");
    ASSERT_EQ(fb[4 * 16 + 11], shades[3], "flipped X top-right pixel is now shade 3");

    /* Clipping test: negative coordinates */
    memset(fb, 0, sizeof(fb));
    gb_tile_blit(tile, -4, -4, false, false, false, 0xE4, shades, fb, 16, 16);
    /* Pixel (7,0) from tile would be at dest_x = -4 + 7 = 3, dest_y = -4 + 0 = -4 (clipped) */
    /* No crash or out-of-bounds writes */
    ASSERT_EQ(fb[0], 0, "clipped negative coords do not corrupt memory");
}

/* --- gb_apu.h tests --- */

static void test_apu_dac_off(void) {
    GbApuChannel1 ch;
    gb_apu_ch1_init(&ch, 44100);

    /* Write NR12 with bits 7-3 = 0 -> DAC OFF */
    gb_apu_ch1_write_regs(&ch, 0x00, 0x80, 0x00, 0x9D, 0x87);
    ASSERT_TRUE(!ch.active, "channel should not be active when DAC is OFF");

    int16_t buf[64];
    gb_apu_ch1_render(&ch, buf, 64);
    for (int i = 0; i < 64; ++i) {
        ASSERT_EQ(buf[i], 0, "DAC off must render silence");
    }
}

static void test_apu_length_counter(void) {
    GbApuChannel1 ch;
    gb_apu_ch1_init(&ch, 44100);

    /* Length = 64 - 2 = 2 length ticks (each tick is 1/256s ~ 172 samples at 44100 Hz).
     * NR11: length=62 -> remaining=2 ticks.
     * NR14: 0xC7 -> bit 7 (trigger=1), bit 6 (length_enable=1), freq=0x07 (high).
     * NR12: 0xF0 -> vol 15, no env decay.
     */
    gb_apu_ch1_write_regs(&ch, 0x00, 0x3E, 0xF0, 0x9D, 0xC7);
    ASSERT_TRUE(ch.active, "channel should be active after trigger");
    ASSERT_EQ(ch.length_counter, 2, "initial length counter should be 2");

    /* Render 1000 samples (~22.6 ms). 2 length ticks = 2 / 256s = 7.8 ms (~344 samples).
     * After 1000 samples, length counter must have expired and deactivated the channel!
     */
    int16_t buf[1000];
    gb_apu_ch1_render(&ch, buf, 1000);
    ASSERT_TRUE(!ch.active, "channel must be deactivated when length counter expires");
    ASSERT_EQ(buf[999], 0, "samples after length expiration must be 0");
}

static void test_apu_sweep_overflow(void) {
    GbApuChannel1 ch;
    gb_apu_ch1_init(&ch, 44100);

    /* Frequency close to maximum (2040).
     * Sweep: time=1, shift=1, negate=0 (frequency increases).
     * Next freq = 2040 + (2040 >> 1) = 2040 + 1020 = 3060 > 2047 -> OVERFLOW!
     */
    gb_apu_ch1_write_regs(&ch, 0x11, 0x80, 0xF0, 0xF8, 0x87); /* raw_freq = 0x7F8 = 2040 */
    /* Overflow check on trigger must deactivate channel immediately */
    ASSERT_TRUE(!ch.active, "sweep overflow on trigger must deactivate channel");
}

static void test_apu_pcm_rendering(void) {
    GbApuChannel1 ch;
    gb_apu_ch1_init(&ch, 44100);

    /* Alleyway SFX 7 parameters: NR10=0x1A, NR11=0x81, NR12=0xF0, NR13=0x9D, NR14=0x87 */
    gb_apu_ch1_write_regs(&ch, 0x1A, 0x81, 0xF0, 0x9D, 0x87);
    ASSERT_TRUE(ch.active, "SFX 7 triggered");

    int16_t buf[500];
    gb_apu_ch1_render(&ch, buf, 500);

    /* Verify non-zero output and waveform polarity changes (pulse wave) */
    bool has_pos = false, has_neg = false;
    for (int i = 0; i < 500; ++i) {
        if (buf[i] > 0) has_pos = true;
        if (buf[i] < 0) has_neg = true;
    }
    ASSERT_TRUE(has_pos && has_neg, "pulse wave must output alternating positive and negative values");
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--deliberate-fail") == 0) {
        fprintf(stderr, "Intentional failure mode for verification harness\n");
        return 1;
    }

    test_tile_decode_indices();
    test_tile_decode_pixels_palette();
    test_tile_blit_flips_and_transparency();
    test_apu_dac_off();
    test_apu_length_counter();
    test_apu_sweep_overflow();
    test_apu_pcm_rendering();

    if (g_failed > 0) {
        fprintf(stderr, "FAILURES: %d\n", g_failed);
        return 1;
    }

    printf("ALL C TESTS PASSED\n");
    return 0;
}
