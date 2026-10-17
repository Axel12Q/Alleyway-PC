#ifndef GB_AUDIO_BACKEND_H
#define GB_AUDIO_BACKEND_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "gb_apu.h"

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_SAMPLES_PER_FRAME (AUDIO_SAMPLE_RATE / 60)
#define AUDIO_NUM_BUFFERS 4

/*
 * Explicit buffer states:
 * BUFFER_FREE: Available for application write.
 * BUFFER_QUEUED: In hardware playback / queued in device. Must NOT be overwritten.
 */
typedef enum {
    BUFFER_FREE = 0,
    BUFFER_QUEUED = 1
} GbAudioBufferState;

/*
 * Interface for audio device output hooks (injectable for unit testing and mock drivers).
 */
typedef struct GbAudioDeviceOps {
    void *user_data;
    /* Returns 0 on success, non-zero on failure */
    int (*open)(struct GbAudioDeviceOps *ops, uint32_t sample_rate);
    /* Returns 0 on success, non-zero on failure */
    int (*write)(struct GbAudioDeviceOps *ops, int buffer_index, const int16_t *samples, int num_samples);
    /* Query if the queued buffer has finished playing and is free again */
    bool (*is_buffer_done)(struct GbAudioDeviceOps *ops, int buffer_index);
    /* Reset / stop active playback */
    void (*reset)(struct GbAudioDeviceOps *ops);
    /* Close device */
    void (*close)(struct GbAudioDeviceOps *ops);
} GbAudioDeviceOps;

typedef struct {
    GbApuChannel1 apu_ch1;
    bool enabled;
    GbAudioBufferState buffer_states[AUDIO_NUM_BUFFERS];
    int16_t sample_buffers[AUDIO_NUM_BUFFERS][AUDIO_SAMPLES_PER_FRAME];
    int current_buffer_idx;
    GbAudioDeviceOps *device_ops;
} GbAudioContext;

/*
 * Dispatches authentic or synthetic SFX tables to APU Channel 1.
 *
 * NOTE: NR10 = 0 is a completely valid DMG value (no frequency sweep).
 * Tables must NOT check nr10 != 0 as an existence check.
 */
static inline void gb_audio_dispatch_sfx(
    const uint8_t *rom,
    size_t rom_size,
    bool rom_loaded,
    GbApuChannel1 *ch,
    uint8_t sfx_id
) {
    if (sfx_id == 0) return;

    if (rom_loaded && rom != NULL) {
        if (sfx_id == 7 && 0x701Au + 5u <= rom_size) {
            gb_apu_ch1_write_regs(ch, rom[0x701A], rom[0x701B], rom[0x701C], rom[0x701D], rom[0x701E]);
            return;
        } else if (sfx_id == 1 && 0x6FDEu + 5u <= rom_size) {
            gb_apu_ch1_write_regs(ch, rom[0x6FDE], rom[0x6FDF], rom[0x6FE0], rom[0x6FE1], rom[0x6FE2]);
            return;
        } else if (sfx_id == 2 && 0x6FBBu + 5u <= rom_size) {
            gb_apu_ch1_write_regs(ch, rom[0x6FBB], rom[0x6FBC], rom[0x6FBD], rom[0x6FBE], rom[0x6FBF]);
            return;
        } else if (sfx_id == 3 && 0x6FCAu + 5u <= rom_size) {
            gb_apu_ch1_write_regs(ch, rom[0x6FCA], rom[0x6FCB], rom[0x6FCC], rom[0x6FCD], rom[0x6FCE]);
            return;
        } else if (sfx_id == 11) {
            /* Partial / static approximation for ceiling bounce */
            gb_apu_ch1_write_regs(ch, 0x00, 0x81, 0xF0, 0xC0, 0x87);
            return;
        } else if (sfx_id == 12) {
            /* Partial / static approximation for software-swept wall bounce */
            gb_apu_ch1_write_regs(ch, 0x00, 0x81, 0xF0, 0x85, 0x87);
            return;
        }
    }

    /* Fallback / synthetic tone */
    gb_apu_ch1_write_regs(ch, 0x1A, 0x81, 0xF0, 0x9D, 0x87);
}

static inline void gb_audio_init(GbAudioContext *ctx, GbAudioDeviceOps *ops) {
    memset(ctx, 0, sizeof(*ctx));
    gb_apu_ch1_init(&ctx->apu_ch1, AUDIO_SAMPLE_RATE);
    ctx->device_ops = ops;
    ctx->current_buffer_idx = 0;
    for (int i = 0; i < AUDIO_NUM_BUFFERS; ++i) {
        ctx->buffer_states[i] = BUFFER_FREE;
    }

    if (ops && ops->open) {
        if (ops->open(ops, AUDIO_SAMPLE_RATE) == 0) {
            ctx->enabled = true;
        } else {
            ctx->enabled = false;
        }
    } else {
        ctx->enabled = false;
    }
}

static inline void gb_audio_shutdown(GbAudioContext *ctx) {
    ctx->enabled = false;
    if (ctx->device_ops) {
        if (ctx->device_ops->reset) ctx->device_ops->reset(ctx->device_ops);
        if (ctx->device_ops->close) ctx->device_ops->close(ctx->device_ops);
    }
    for (int i = 0; i < AUDIO_NUM_BUFFERS; ++i) {
        ctx->buffer_states[i] = BUFFER_FREE;
    }
}

/*
 * Steps one audio frame:
 * 1. Dispatches sfx_trigger if non-zero.
 * 2. Advances APU simulation EXACTLY ONCE into current_samples (deterministic).
 * 3. Updates buffer free/queued state using device_ops->is_buffer_done.
 * 4. If current buffer is FREE, writes to device and marks QUEUED.
 *    If current buffer is QUEUED (device busy), safely skips write without advancing or corrupting.
 */
static inline bool gb_audio_step_frame(
    GbAudioContext *ctx,
    const uint8_t *rom,
    size_t rom_size,
    bool rom_loaded,
    uint8_t sfx_trigger
) {
    if (sfx_trigger != 0) {
        gb_audio_dispatch_sfx(rom, rom_size, rom_loaded, &ctx->apu_ch1, sfx_trigger);
    }

    /* Simulation audio samples MUST advance exactly once */
    int16_t current_samples[AUDIO_SAMPLES_PER_FRAME];
    gb_apu_ch1_render(&ctx->apu_ch1, current_samples, AUDIO_SAMPLES_PER_FRAME);

    if (!ctx->enabled || ctx->device_ops == NULL) {
        return false;
    }

    /* Refresh buffer state from device if queued */
    int idx = ctx->current_buffer_idx;
    if (ctx->buffer_states[idx] == BUFFER_QUEUED) {
        if (ctx->device_ops->is_buffer_done && ctx->device_ops->is_buffer_done(ctx->device_ops, idx)) {
            ctx->buffer_states[idx] = BUFFER_FREE;
        }
    }

    /* If still queued, the device is busy: do NOT overwrite in-flight audio */
    if (ctx->buffer_states[idx] == BUFFER_QUEUED) {
        return false;
    }

    /* Buffer is FREE: copy rendered samples */
    memcpy(ctx->sample_buffers[idx], current_samples, sizeof(current_samples));

    /* Write to device */
    if (ctx->device_ops->write) {
        int res = ctx->device_ops->write(ctx->device_ops, idx, ctx->sample_buffers[idx], AUDIO_SAMPLES_PER_FRAME);
        if (res == 0) {
            ctx->buffer_states[idx] = BUFFER_QUEUED;
            ctx->current_buffer_idx = (ctx->current_buffer_idx + 1) % AUDIO_NUM_BUFFERS;
            return true;
        }
    }

    return false;
}

#endif /* GB_AUDIO_BACKEND_H */
