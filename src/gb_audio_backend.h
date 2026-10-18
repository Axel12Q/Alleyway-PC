#ifndef GB_AUDIO_BACKEND_H
#define GB_AUDIO_BACKEND_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "gb_apu.h"
#include "alleyway_sfx.h"
#include "gb_scheduler.h"

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_MAX_SAMPLES_PER_FRAME GB_AUDIO_MAX_SAMPLES_PER_FRAME
#define AUDIO_SAMPLES_PER_FRAME AUDIO_MAX_SAMPLES_PER_FRAME
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
    AlleywaySfx sfx;
    bool enabled;
    GbAudioBufferState buffer_states[AUDIO_NUM_BUFFERS];
    int16_t sample_buffers[AUDIO_NUM_BUFFERS][AUDIO_MAX_SAMPLES_PER_FRAME];
    int current_buffer_idx;
    uint32_t audio_frac_acc;
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
        AlleywaySfx initial = {0};
        alleyway_sfx_step(&initial, ch, rom, rom_size, sfx_id);
        return;
    }
    /* Diagnostic-only finite tone; commercial SFX use the sequencer above. */
    gb_apu_ch1_write_regs(ch, 0x1A, 0x81, 0xF0, 0x9D, 0xC7);
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

/* Stop queued output immediately without closing the device. Used for desktop
 * pause, loss of focus, mute and reset; never changes the game simulation. */
static inline void gb_audio_silence(GbAudioContext *ctx) {
    if (ctx->enabled && ctx->device_ops && ctx->device_ops->reset)
        ctx->device_ops->reset(ctx->device_ops);
    for (int i=0;i<AUDIO_NUM_BUFFERS;i++) ctx->buffer_states[i]=BUFFER_FREE;
    ctx->current_buffer_idx=0;
    memset(&ctx->sfx,0,sizeof(ctx->sfx));
    gb_apu_ch1_init(&ctx->apu_ch1,AUDIO_SAMPLE_RATE);
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
    if (rom_loaded) alleyway_sfx_step(&ctx->sfx,&ctx->apu_ch1,rom,rom_size,sfx_trigger);
    else if (sfx_trigger != 0) gb_audio_dispatch_sfx(rom, rom_size, false, &ctx->apu_ch1, sfx_trigger);

    ctx->audio_frac_acc += GB_AUDIO_FRAC_NUM;
    int samples_this_frame = GB_AUDIO_SAMPLE_BASE;
    if (ctx->audio_frac_acc >= GB_AUDIO_FRAC_DENOM) {
        ctx->audio_frac_acc -= GB_AUDIO_FRAC_DENOM;
        samples_this_frame++;
    }

    /* Simulation audio samples MUST advance exactly once */
    int16_t current_samples[AUDIO_MAX_SAMPLES_PER_FRAME];
    gb_apu_ch1_render(&ctx->apu_ch1, current_samples, samples_this_frame);

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
    memcpy(ctx->sample_buffers[idx], current_samples, (size_t)samples_this_frame * sizeof(int16_t));

    /* Write to device */
    if (ctx->device_ops->write) {
        int res = ctx->device_ops->write(ctx->device_ops, idx, ctx->sample_buffers[idx], samples_this_frame);
        if (res == 0) {
            ctx->buffer_states[idx] = BUFFER_QUEUED;
            ctx->current_buffer_idx = (ctx->current_buffer_idx + 1) % AUDIO_NUM_BUFFERS;
            return true;
        }
    }

    return false;
}

#endif /* GB_AUDIO_BACKEND_H */
