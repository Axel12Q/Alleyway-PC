#ifndef GB_SCHEDULER_H
#define GB_SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Primary Game Boy DMG Hardware Timing Constants:
 * - Master CPU Clock: 4,194,304 Hz (4.194304 MHz)
 * - Dots / cycles per scanline: 456
 * - Scanlines per vertical refresh: 154 (144 active + 10 VBlank)
 * - CPU cycles per frame: 154 * 456 = 70,224 cycles
 * - Exact DMG frame rate: 4,194,304 / 70,224 = 59.72750056961936... Hz
 * - Exact frame duration: 70,224 / 4,194,304 = 0.016742706298828125 seconds (~16.7427 ms)
 */
#ifndef GB_CPU_CLOCK_HZ
#define GB_CPU_CLOCK_HZ 4194304u
#endif
#define GB_CYCLES_PER_LINE 456u
#define GB_LINES_PER_FRAME 154u
#define GB_CYCLES_PER_FRAME (GB_LINES_PER_FRAME * GB_CYCLES_PER_LINE) /* 70,224 */
#define GB_FRAMES_PER_SEC ((double)GB_CPU_CLOCK_HZ / (double)GB_CYCLES_PER_FRAME) /* ~59.7275 Hz */
#define GB_FRAME_DURATION_SEC ((double)GB_CYCLES_PER_FRAME / (double)GB_CPU_CLOCK_HZ) /* ~0.0167427 s */

/*
 * Fractional audio sample calculations at standard 44,100 Hz output:
 * Exact samples per DMG frame:
 *   44,100 * 70,224 / 4,194,304 = 3,096,878,400 / 4,194,304
 *   Dividing numerator and denominator by 64 yields:
 *   48,388,725 / 65,536 = 738 + 23,157 / 65,536
 *
 * Each frame produces 738 samples, with 23,157/65,536 fractional remainder.
 * When the fractional accumulator reaches 65,536, an extra sample (739 total)
 * is generated. Across any N frames, total accumulated samples is exactly
 * (N * 44100 * 70224) / 4194304 with zero drift.
 */
#define GB_AUDIO_SAMPLE_BASE 738
#define GB_AUDIO_FRAC_NUM 23157u
#define GB_AUDIO_FRAC_DENOM 65536u
#define GB_AUDIO_MAX_SAMPLES_PER_FRAME 740

typedef struct {
    double time_accumulator;
    double frame_duration;
    uint64_t ticks_total;
    double max_frame_skip_sec; /* Clamp threshold for stalls/drags (e.g. 0.2s) */
    bool paused;
    uint32_t audio_frac_acc;
    uint64_t audio_samples_total;
} GbScheduler;

static inline void gb_scheduler_init(GbScheduler *s) {
    s->time_accumulator = 0.0;
    s->frame_duration = GB_FRAME_DURATION_SEC;
    s->ticks_total = 0;
    s->max_frame_skip_sec = 0.200; /* Max 200 ms accumulation per update */
    s->paused = false;
    s->audio_frac_acc = 0;
    s->audio_samples_total = 0;
}

static inline void gb_scheduler_reset(GbScheduler *s) {
    s->time_accumulator = 0.0;
    s->ticks_total = 0;
    s->audio_frac_acc = 0;
    s->audio_samples_total = 0;
}

static inline void gb_scheduler_set_paused(GbScheduler *s, bool paused) {
    s->paused = paused;
    if (paused) {
        /* Discard any partial accumulator when pausing so unpause does not burst */
        s->time_accumulator = 0.0;
    }
}

/*
 * Advances the monotonic scheduler by delta time dt (in seconds).
 * Returns the number of discrete simulation ticks that must be stepped.
 * Decouples game logic from window presentation and eliminates message-rate dependence.
 */
static inline int gb_scheduler_advance(GbScheduler *s, double dt) {
    if (dt < 0.0) dt = 0.0;
    if (dt > s->max_frame_skip_sec) dt = s->max_frame_skip_sec;

    if (s->paused) {
        return 0;
    }

    s->time_accumulator += dt;
    int ticks = 0;
    while (s->time_accumulator >= s->frame_duration && ticks < 10) {
        s->time_accumulator -= s->frame_duration;
        ticks++;
        s->ticks_total++;
    }

    /* Clamp remaining accumulator if still overflowing */
    if (s->time_accumulator >= s->frame_duration) {
        s->time_accumulator = 0.0;
    }

    return ticks;
}

/*
 * Computes exact fractional sample count for the current DMG frame.
 */
static inline int gb_scheduler_get_frame_audio_samples(GbScheduler *s) {
    s->audio_frac_acc += GB_AUDIO_FRAC_NUM;
    int count = GB_AUDIO_SAMPLE_BASE;
    if (s->audio_frac_acc >= GB_AUDIO_FRAC_DENOM) {
        s->audio_frac_acc -= GB_AUDIO_FRAC_DENOM;
        count++;
    }
    s->audio_samples_total += (uint64_t)count;
    return count;
}

/*
 * Theoretical expected total samples across N frames for validation.
 */
static inline uint64_t gb_scheduler_expected_audio_samples_total(uint64_t frames, uint32_t sample_rate) {
    return (frames * (uint64_t)sample_rate * (uint64_t)GB_CYCLES_PER_FRAME) / (uint64_t)GB_CPU_CLOCK_HZ;
}

/*
 * Converts Alleyway HUD bonus timer units (where 1 unit = 32 VBlanks) to simulation ticks.
 */
static inline uint64_t gb_scheduler_bonus_units_to_ticks(uint16_t bonus_units) {
    return (uint64_t)bonus_units * 32u;
}

/*
 * Converts Alleyway HUD bonus timer units to real-world seconds based on authentic DMG frame timing.
 */
static inline double gb_scheduler_bonus_units_to_seconds(uint16_t bonus_units) {
    return (double)gb_scheduler_bonus_units_to_ticks(bonus_units) * GB_FRAME_DURATION_SEC;
}

#endif /* GB_SCHEDULER_H */
