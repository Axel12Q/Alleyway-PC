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

#include "gb_audio_backend.h"

static void test_apu_dispatcher_production_nr10_zero(void) {
    /* Test production gb_audio_dispatch_sfx using synthetic/invented fixtures (not copied ROM) */
    uint8_t synth_rom[32768];
    memset(synth_rom, 0, sizeof(synth_rom));

    /* Invented fixture table for SFX 1 at 0x6FDE: NR10 = 0x00 (NO SWEEP), freq low = 0x34, freq high = 0x85 */
    synth_rom[0x6FDE] = 0x00; /* NR10 = 0 is VALID */
    synth_rom[0x6FDF] = 0x81;
    synth_rom[0x6FE0] = 0xF0;
    synth_rom[0x6FE1] = 0x34;
    synth_rom[0x6FE2] = 0x85;

    /* Invented fixture table for SFX 2 at 0x6FBB: NR10 = 0x00, freq low = 0x78, freq high = 0xC6 */
    synth_rom[0x6FBB] = 0x00;
    synth_rom[0x6FBC] = 0x81;
    synth_rom[0x6FBD] = 0xF0;
    synth_rom[0x6FBE] = 0x78;
    synth_rom[0x6FBF] = 0xC6;

    GbApuChannel1 ch1, ch2;
    gb_apu_ch1_init(&ch1, 44100);
    gb_apu_ch1_init(&ch2, 44100);

    /* Invoke the ACTUAL production dispatcher gb_audio_dispatch_sfx! */
    gb_audio_dispatch_sfx(synth_rom, sizeof(synth_rom), true, &ch1, 1);
    ASSERT_TRUE(ch1.active, "production dispatcher must activate channel for SFX 1 with NR10 == 0");
    ASSERT_EQ(ch1.nr10, 0x00, "SFX 1 NR10 is 0x00");
    ASSERT_EQ(ch1.raw_frequency, ((0x85 & 7) << 8) | 0x34, "SFX 1 frequency matches synth fixture");

    gb_audio_dispatch_sfx(synth_rom, sizeof(synth_rom), true, &ch2, 2);
    ASSERT_TRUE(ch2.active, "production dispatcher must activate channel for SFX 2 with NR10 == 0");
    ASSERT_EQ(ch2.raw_frequency, ((0xC6 & 7) << 8) | 0x78, "SFX 2 frequency matches synth fixture");
    ASSERT_TRUE(ch1.raw_frequency != ch2.raw_frequency, "SFX 1 and SFX 2 produce distinct frequencies");

    /* Render PCM and verify buffers differ */
    int16_t buf1[200], buf2[200];
    gb_apu_ch1_render(&ch1, buf1, 200);
    gb_apu_ch1_render(&ch2, buf2, 200);
    bool diff = false;
    for (int i = 0; i < 200; ++i) {
        if (buf1[i] != buf2[i]) { diff = true; break; }
    }
    ASSERT_TRUE(diff, "production dispatcher generates distinct PCM for SFX 1 vs SFX 2");
}

/* Mock device ops for testing production gb_audio_step_frame */
typedef struct {
    int open_calls;
    int write_calls;
    int close_calls;
    int reset_calls;
    bool fail_open;
    bool fail_write;
    bool buffer_done_status[AUDIO_NUM_BUFFERS];
    int last_written_buffer;
    int16_t written_data[AUDIO_NUM_BUFFERS][AUDIO_SAMPLES_PER_FRAME];
} MockAudioDeviceState;

static int mock_dev_open(GbAudioDeviceOps *ops, uint32_t sample_rate) {
    (void)sample_rate;
    MockAudioDeviceState *s = (MockAudioDeviceState *)ops->user_data;
    s->open_calls++;
    return s->fail_open ? -1 : 0;
}

static int mock_dev_write(GbAudioDeviceOps *ops, int buffer_index, const int16_t *samples, int num_samples) {
    MockAudioDeviceState *s = (MockAudioDeviceState *)ops->user_data;
    s->write_calls++;
    s->last_written_buffer = buffer_index;
    if (s->fail_write) return -2;
    memcpy(s->written_data[buffer_index], samples, (size_t)num_samples * sizeof(int16_t));
    s->buffer_done_status[buffer_index] = false; /* Now in playback */
    return 0;
}

static bool mock_dev_is_buffer_done(GbAudioDeviceOps *ops, int buffer_index) {
    MockAudioDeviceState *s = (MockAudioDeviceState *)ops->user_data;
    return s->buffer_done_status[buffer_index];
}

static void mock_dev_reset(GbAudioDeviceOps *ops) {
    MockAudioDeviceState *s = (MockAudioDeviceState *)ops->user_data;
    s->reset_calls++;
}

static void mock_dev_close(GbAudioDeviceOps *ops) {
    MockAudioDeviceState *s = (MockAudioDeviceState *)ops->user_data;
    s->close_calls++;
}

static void test_production_audio_backend_integration(void) {
    MockAudioDeviceState dev_state;
    memset(&dev_state, 0, sizeof(dev_state));
    for (int i = 0; i < AUDIO_NUM_BUFFERS; ++i) dev_state.buffer_done_status[i] = true;

    GbAudioDeviceOps ops = {
        .user_data = &dev_state,
        .open = mock_dev_open,
        .write = mock_dev_write,
        .is_buffer_done = mock_dev_is_buffer_done,
        .reset = mock_dev_reset,
        .close = mock_dev_close,
    };

    GbAudioContext ctx;
    uint8_t synth_rom[32768];
    memset(synth_rom, 0, sizeof(synth_rom));

    /* 1. Init without prior submission */
    gb_audio_init(&ctx, &ops);
    ASSERT_TRUE(ctx.enabled, "audio context initialized successfully");
    ASSERT_EQ(dev_state.open_calls, 1, "open called once");
    ASSERT_EQ(ctx.current_buffer_idx, 0, "initial buffer index is 0");
    for (int i = 0; i < AUDIO_NUM_BUFFERS; ++i) {
        ASSERT_EQ(ctx.buffer_states[i], BUFFER_FREE, "all buffers initially BUFFER_FREE");
    }

    /* 2. Successful write on frame 0 */
    bool sent = gb_audio_step_frame(&ctx, synth_rom, sizeof(synth_rom), true, 0);
    ASSERT_TRUE(sent, "frame 0 audio successfully written to device");
    ASSERT_EQ(dev_state.write_calls, 1, "device write called");
    ASSERT_EQ(ctx.buffer_states[0], BUFFER_QUEUED, "buffer 0 marked BUFFER_QUEUED");
    ASSERT_EQ(ctx.current_buffer_idx, 1, "current buffer advanced to 1");

    /* 3. Successful write on frames 1, 2, 3 (filling queue) */
    gb_audio_step_frame(&ctx, synth_rom, sizeof(synth_rom), true, 0);
    gb_audio_step_frame(&ctx, synth_rom, sizeof(synth_rom), true, 0);
    gb_audio_step_frame(&ctx, synth_rom, sizeof(synth_rom), true, 0);
    ASSERT_EQ(dev_state.write_calls, 4, "4 buffers written to device");
    ASSERT_EQ(ctx.current_buffer_idx, 0, "wrapped around to buffer 0");

    /* 4. Queue busy: buffer 0 is still in flight (buffer_done_status == false) */
    dev_state.buffer_done_status[0] = false;
    /* Record APU cycle accumulator before stepping frame to verify APU advances even if dropped */
    uint64_t prev_acc = ctx.apu_ch1.cycle_frac_acc;
    sent = gb_audio_step_frame(&ctx, synth_rom, sizeof(synth_rom), true, 0);
    ASSERT_TRUE(!sent, "must safely drop/skip when buffer 0 is still queued");
    ASSERT_EQ(ctx.buffer_states[0], BUFFER_QUEUED, "buffer 0 remains QUEUED");
    ASSERT_EQ(ctx.current_buffer_idx, 0, "buffer index does NOT advance when queue busy");
    ASSERT_EQ(dev_state.write_calls, 4, "device write must NOT be called on busy buffer");
    ASSERT_TRUE(ctx.apu_ch1.cycle_frac_acc != prev_acc, "APU simulation must advance exactly once even on busy queue");

    /* 5. Driver completes playback of buffer 0: buffer_done_status becomes true */
    dev_state.buffer_done_status[0] = true;
    sent = gb_audio_step_frame(&ctx, synth_rom, sizeof(synth_rom), true, 0);
    ASSERT_TRUE(sent, "buffer 0 written once driver finishes");
    ASSERT_EQ(dev_state.write_calls, 5, "device write called after driver done");
    ASSERT_EQ(ctx.current_buffer_idx, 1, "advanced to buffer 1");

    /* 6. Device write error handling */
    dev_state.buffer_done_status[1] = true;
    dev_state.fail_write = true;
    sent = gb_audio_step_frame(&ctx, synth_rom, sizeof(synth_rom), true, 0);
    ASSERT_TRUE(!sent, "write error returns false");
    ASSERT_EQ(ctx.buffer_states[1], BUFFER_FREE, "failed write leaves buffer FREE");
    ASSERT_EQ(ctx.current_buffer_idx, 1, "failed write does not advance index");
    dev_state.fail_write = false;

    /* 7. Shutdown */
    gb_audio_shutdown(&ctx);
    ASSERT_TRUE(!ctx.enabled, "audio disabled after shutdown");
    ASSERT_EQ(dev_state.reset_calls, 1, "reset called on shutdown");
    ASSERT_EQ(dev_state.close_calls, 1, "close called on shutdown");
    for (int i = 0; i < AUDIO_NUM_BUFFERS; ++i) {
        ASSERT_EQ(ctx.buffer_states[i], BUFFER_FREE, "all buffers cleared to FREE after shutdown");
    }

    /* 8. Partial / failed open test */
    dev_state.fail_open = true;
    gb_audio_init(&ctx, &ops);
    ASSERT_TRUE(!ctx.enabled, "context disabled when open fails");
    sent = gb_audio_step_frame(&ctx, synth_rom, sizeof(synth_rom), true, 0);
    ASSERT_TRUE(!sent, "step_frame safely returns false when disabled");
}

static void test_production_sfx_lifetime(void) {
    GbAudioContext ctx;
    uint8_t rom[32768]={0};
    gb_audio_init(&ctx,NULL);
    gb_audio_step_frame(&ctx,rom,sizeof rom,true,12);
    for (int i=0;i<120;i++) gb_audio_step_frame(&ctx,rom,sizeof rom,true,0);
    ASSERT_EQ(ctx.sfx.active,0,"wall sequencer completes without arbitrary timeout");
    ASSERT_TRUE(!ctx.apu_ch1.active,"wall DAC is off after its ROM driver sequence");
    int16_t tail[256];
    gb_apu_ch1_render(&ctx.apu_ch1,tail,256);
    for (int i=0;i<256;i++) ASSERT_EQ(tail[i],0,"completed wall effect has silent PCM tail");
    gb_audio_step_frame(&ctx,rom,sizeof rom,true,11);
    gb_audio_step_frame(&ctx,rom,sizeof rom,true,0);
    gb_audio_silence(&ctx);
    ASSERT_EQ(ctx.sfx.active,0,"desktop silence clears the sequencer");
    ASSERT_TRUE(!ctx.apu_ch1.active,"desktop silence clears the oscillator");
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
    test_apu_dispatcher_production_nr10_zero();
    test_production_audio_backend_integration();
    test_production_sfx_lifetime();

    if (g_failed > 0) {
        fprintf(stderr, "FAILURES: %d\n", g_failed);
        return 1;
    }

    printf("ALL C TESTS PASSED\n");
    return 0;
}
