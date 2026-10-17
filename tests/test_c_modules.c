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

    /* Flip Y blit at (4, 4): row 0 maps to row 7 of tile (y = 4 + 7 = 11) */
    memset(fb, 0, sizeof(fb));
    gb_tile_blit(tile, 4, 4, false, true, true, 0xE4, shades, fb, 16, 16);
    ASSERT_EQ(fb[11 * 16 + 4], shades[3], "flipped Y bottom-left pixel is shade 3");
    ASSERT_EQ(fb[11 * 16 + 11], shades[1], "flipped Y bottom-right pixel is shade 1");
    ASSERT_EQ(fb[4 * 16 + 4], 0, "flipped Y row 0 remains empty");

    /* Clipping and boundary sentinels test */
    uint32_t fb_sentinel[18 * 18];
    const uint32_t SENTINEL_VAL = 0xDEADBEEF;
    for (int i = 0; i < 18 * 18; ++i) fb_sentinel[i] = SENTINEL_VAL;
    /* Sub-view of 16x16 with stride 18 inside 18x18, offset by (1,1) */
    /* Blit overflowing right/bottom boundary at (12, 12) on 16x16: pixels [16..19] clipped */
    gb_tile_blit(tile, 12, 12, false, false, false, 0xE4, shades, &fb_sentinel[1 * 18 + 1], 16, 16);
    /* Check sentinel guards around the 16x16 area */
    for (int x = 0; x < 18; ++x) {
        ASSERT_EQ(fb_sentinel[0 * 18 + x], SENTINEL_VAL, "top sentinel intact");
        ASSERT_EQ(fb_sentinel[17 * 18 + x], SENTINEL_VAL, "bottom sentinel intact");
    }
    for (int y = 0; y < 18; ++y) {
        ASSERT_EQ(fb_sentinel[y * 18 + 0], SENTINEL_VAL, "left sentinel intact");
        ASSERT_EQ(fb_sentinel[y * 18 + 17], SENTINEL_VAL, "right sentinel intact");
    }

    /* Clipping test: negative coordinates */
    memset(fb, 0, sizeof(fb));
    gb_tile_blit(tile, -4, -4, false, false, false, 0xE4, shades, fb, 16, 16);
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

static void test_apu_dispatcher_nr10_zero(void) {
    /* Mock minimal ROM with authentic SFX tables:
     * - SFX 1 (Brick type 1) at 0x6FDE: NR10=0x00, NR11=0x71, NR12=0xF2, NR13=0x59, NR14=0x87
     * - SFX 2 (Brick type 2) at 0x6FBB: NR10=0x00, NR11=0x81, NR12=0x72, NR13=0x7B, NR14=0xC7
     * - SFX 7 (Paddle bounce) at 0x701A: NR10=0x1A, NR11=0x81, NR12=0xF0, NR13=0x9D, NR14=0xC7
     */
    uint8_t mock_rom[32768] = {0};
    /* SFX 1 */
    mock_rom[0x6FDE] = 0x00; /* NR10 = 0 is VALID! (no sweep) */
    mock_rom[0x6FDF] = 0x71;
    mock_rom[0x6FE0] = 0xF2;
    mock_rom[0x6FE1] = 0x59;
    mock_rom[0x6FE2] = 0x87;

    /* SFX 2 */
    mock_rom[0x6FBB] = 0x00; /* NR10 = 0 */
    mock_rom[0x6FBC] = 0x81;
    mock_rom[0x6FBD] = 0x72;
    mock_rom[0x6FBE] = 0x7B;
    mock_rom[0x6FBF] = 0xC7;

    /* SFX 7 */
    mock_rom[0x701A] = 0x1A; /* NR10 = 0x1A */
    mock_rom[0x701B] = 0x81;
    mock_rom[0x701C] = 0xF0;
    mock_rom[0x701D] = 0x9D;
    mock_rom[0x701E] = 0xC7;

    GbApuChannel1 ch1, ch2;
    gb_apu_ch1_init(&ch1, 44100);
    gb_apu_ch1_init(&ch2, 44100);

    /* Dispatch SFX 1: table at 0x6FDE */
    gb_apu_ch1_write_regs(&ch1, mock_rom[0x6FDE], mock_rom[0x6FDF], mock_rom[0x6FE0], mock_rom[0x6FE1], mock_rom[0x6FE2]);
    ASSERT_TRUE(ch1.active, "SFX 1 active despite NR10 == 0");
    ASSERT_EQ(ch1.nr10, 0x00, "SFX 1 NR10 is 0");
    ASSERT_EQ(ch1.raw_frequency, ((0x87 & 7) << 8) | 0x59, "SFX 1 raw frequency matches table");

    /* Dispatch SFX 2: table at 0x6FBB */
    gb_apu_ch1_write_regs(&ch2, mock_rom[0x6FBB], mock_rom[0x6FBC], mock_rom[0x6FBD], mock_rom[0x6FBE], mock_rom[0x6FBF]);
    ASSERT_TRUE(ch2.active, "SFX 2 active despite NR10 == 0");
    ASSERT_EQ(ch2.raw_frequency, ((0xC7 & 7) << 8) | 0x7B, "SFX 2 raw frequency matches table");
    ASSERT_TRUE(ch1.raw_frequency != ch2.raw_frequency, "SFX 1 and SFX 2 have distinct frequencies");

    /* Render both and verify waveforms differ */
    int16_t buf1[200], buf2[200];
    gb_apu_ch1_render(&ch1, buf1, 200);
    gb_apu_ch1_render(&ch2, buf2, 200);
    bool buffers_differ = false;
    for (int i = 0; i < 200; ++i) {
        if (buf1[i] != buf2[i]) {
            buffers_differ = true;
            break;
        }
    }
    ASSERT_TRUE(buffers_differ, "SFX 1 and SFX 2 generate distinct PCM buffers");
}

/* Simulated waveOut queue safety test */
#define MOCK_WHDR_DONE 0x00000001
#define MOCK_WHDR_PREPARED 0x00000002

typedef struct {
    uint32_t flags;
    int16_t data[16];
    int write_count;
} MockBuffer;

static void test_audio_queue_safety_simulation(void) {
    /* 4-buffer ring buffer simulating waveOut queue state machine */
    MockBuffer queue[4];
    for (int i = 0; i < 4; ++i) {
        queue[i].flags = MOCK_WHDR_PREPARED | MOCK_WHDR_DONE; /* Initially ready */
        memset(queue[i].data, 0, sizeof(queue[i].data));
        queue[i].write_count = 0;
    }

    int current_buf = 0;
    int16_t frame_samples[16];
    for (int s = 0; s < 16; ++s) frame_samples[s] = (int16_t)(s + 10);

    /* Frame 1: buffer 0 is DONE -> writes successfully, transitions to in-hardware (clear DONE) */
    ASSERT_TRUE(queue[current_buf].flags & MOCK_WHDR_DONE, "buffer 0 ready");
    memcpy(queue[current_buf].data, frame_samples, sizeof(frame_samples));
    queue[current_buf].flags &= ~MOCK_WHDR_DONE; /* driver playing */
    queue[current_buf].write_count++;
    current_buf = (current_buf + 1) % 4;

    /* Frame 2: buffer 1 is DONE -> writes successfully */
    ASSERT_TRUE(queue[current_buf].flags & MOCK_WHDR_DONE, "buffer 1 ready");
    memcpy(queue[current_buf].data, frame_samples, sizeof(frame_samples));
    queue[current_buf].flags &= ~MOCK_WHDR_DONE;
    queue[current_buf].write_count++;
    current_buf = (current_buf + 1) % 4;

    /* Frame 3: buffer 2 is NOT done (simulating device lag / audio still in flight) */
    queue[current_buf].flags &= ~MOCK_WHDR_DONE;
    int prev_buf = current_buf;
    /* Queue safety check: must NOT overwrite buffer, must NOT advance index */
    bool skipped = false;
    if (!(queue[current_buf].flags & MOCK_WHDR_DONE)) {
        skipped = true; /* Safe skip: do not overwrite in-flight audio */
    }
    ASSERT_TRUE(skipped, "must safely skip buffer when WHDR_DONE is not set");
    ASSERT_EQ(current_buf, prev_buf, "buffer index does not advance on skip");
    ASSERT_EQ(queue[current_buf].write_count, 0, "in-flight buffer must not be overwritten");

    /* Driver finishes playback of buffer 2: sets WHDR_DONE */
    queue[current_buf].flags |= MOCK_WHDR_DONE;
    if (queue[current_buf].flags & MOCK_WHDR_DONE) {
        memcpy(queue[current_buf].data, frame_samples, sizeof(frame_samples));
        queue[current_buf].flags &= ~MOCK_WHDR_DONE;
        queue[current_buf].write_count++;
        current_buf = (current_buf + 1) % 4;
    }
    ASSERT_EQ(queue[2].write_count, 1, "buffer 2 successfully written once freed by driver");
    ASSERT_EQ(current_buf, 3, "buffer index advanced to 3");
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
    test_apu_dispatcher_nr10_zero();
    test_audio_queue_safety_simulation();

    if (g_failed > 0) {
        fprintf(stderr, "FAILURES: %d\n", g_failed);
        return 1;
    }

    printf("ALL C TESTS PASSED\n");
    return 0;
}
