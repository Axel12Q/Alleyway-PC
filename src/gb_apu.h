#ifndef GB_APU_H
#define GB_APU_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Generic Game Boy APU Channel 1 (Pulse with frequency sweep & envelope) synthesiser.
 *
 * Game Boy Channel 1 registers:
 * - NR10 (0xFF10): Sweep register
 *     Bit 6-4: Sweep time (0..7, in 128 Hz units)
 *     Bit 3:   Sweep direction (0 = increase frequency, 1 = decrease frequency)
 *     Bit 2-0: Sweep shift (0..7)
 * - NR11 (0xFF11): Duty cycle & length
 *     Bit 7-6: Wave duty (00: 12.5%, 01: 25%, 10: 50%, 11: 75%)
 *     Bit 5-0: Sound length (64 - length)
 * - NR12 (0xFF12): Volume envelope
 *     Bit 7-4: Initial envelope volume (0..15)
 *     Bit 3:   Envelope direction (0 = decrease, 1 = increase)
 *     Bit 2-0: Envelope step period (0..7, in 64 Hz units)
 * - NR13 (0xFF13): Frequency low 8 bits
 * - NR14 (0xFF14): Frequency high 3 bits & trigger
 *     Bit 7:   Trigger (1 = restart sound)
 *     Bit 6:   Length enable
 *     Bit 2-0: Frequency high 3 bits (bits 8..10)
 *
 * Formula:
 *   Frequency (Hz) = 131072 / (2048 - raw_freq_11bit)
 */

typedef struct {
    /* Hardware registers */
    uint8_t nr10;
    uint8_t nr11;
    uint8_t nr12;
    uint8_t nr13;
    uint8_t nr14;

    /* Internal simulation state */
    bool active;
    uint16_t raw_frequency;   /* 11-bit frequency value (0..2047) */
    uint8_t volume;           /* Current volume (0..15) */
    uint8_t duty_pattern;     /* Current 8-bit duty bitmask */
    uint8_t duty_step;        /* 0..7 duty cycle position */

    /* Frequency timer */
    int timer_samples;
    int timer_period_samples;

    /* Envelope tracker */
    int envelope_timer_samples;
    int envelope_period_samples;
    bool envelope_add;

    /* Sweep tracker */
    int sweep_timer_samples;
    int sweep_period_samples;
    bool sweep_enabled;
    bool sweep_negate;
    uint8_t sweep_shift;

    uint32_t sample_rate;
} GbApuChannel1;

static inline void gb_apu_ch1_init(GbApuChannel1 *ch, uint32_t sample_rate) {
    ch->sample_rate = (sample_rate > 0) ? sample_rate : 44100u;
    ch->nr10 = 0;
    ch->nr11 = 0;
    ch->nr12 = 0;
    ch->nr13 = 0;
    ch->nr14 = 0;
    ch->active = false;
    ch->raw_frequency = 0;
    ch->volume = 0;
    ch->duty_pattern = 0x01; /* 12.5% */
    ch->duty_step = 0;
    ch->timer_samples = 0;
    ch->timer_period_samples = 0;
    ch->envelope_timer_samples = 0;
    ch->envelope_period_samples = 0;
    ch->envelope_add = false;
    ch->sweep_timer_samples = 0;
    ch->sweep_period_samples = 0;
    ch->sweep_enabled = false;
    ch->sweep_negate = false;
    ch->sweep_shift = 0;
}

static inline void gb_apu_ch1_trigger(GbApuChannel1 *ch) {
    ch->active = true;
    ch->raw_frequency = (uint16_t)(((uint16_t)(ch->nr14 & 0x07u) << 8) | (uint16_t)ch->nr13);
    ch->volume = (uint8_t)((ch->nr12 >> 4) & 0x0Fu);

    /* Duty cycle */
    uint8_t duty_code = (uint8_t)((ch->nr11 >> 6) & 0x03u);
    static const uint8_t DUTY_PATTERNS[4] = {0x01, 0x81, 0x87, 0x7E}; /* 12.5%, 25%, 50%, 75% */
    ch->duty_pattern = DUTY_PATTERNS[duty_code];
    ch->duty_step = 0;

    /* Frequency timer period in audio samples:
     * GB CPU clock = 4,194,304 Hz.
     * Channel 1 timer period = (2048 - raw_freq) * 4 CPU cycles.
     * In audio samples: period_samples = (2048 - raw_freq) * 4 * sample_rate / 4194304.
     */
    int period_cpu = (2048 - (int)ch->raw_frequency) * 4;
    ch->timer_period_samples = (int)((int64_t)period_cpu * (int64_t)ch->sample_rate / 4194304LL);
    if (ch->timer_period_samples < 1) ch->timer_period_samples = 1;
    ch->timer_samples = ch->timer_period_samples;

    /* Envelope setup (64 Hz = every sample_rate / 64 samples) */
    uint8_t env_period = (uint8_t)(ch->nr12 & 0x07u);
    ch->envelope_add = (ch->nr12 & 0x08u) != 0;
    if (env_period > 0) {
        ch->envelope_period_samples = (int)((int64_t)ch->sample_rate * (int64_t)env_period / 64LL);
        ch->envelope_timer_samples = ch->envelope_period_samples;
    } else {
        ch->envelope_period_samples = 0;
        ch->envelope_timer_samples = 0;
    }

    /* Sweep setup (128 Hz = every sample_rate / 128 samples) */
    uint8_t sweep_time = (uint8_t)((ch->nr10 >> 4) & 0x07u);
    ch->sweep_shift = (uint8_t)(ch->nr10 & 0x07u);
    ch->sweep_negate = (ch->nr10 & 0x08u) != 0;
    if (sweep_time > 0 && ch->sweep_shift > 0) {
        ch->sweep_enabled = true;
        ch->sweep_period_samples = (int)((int64_t)ch->sample_rate * (int64_t)sweep_time / 128LL);
        ch->sweep_timer_samples = ch->sweep_period_samples;
    } else {
        ch->sweep_enabled = false;
        ch->sweep_period_samples = 0;
        ch->sweep_timer_samples = 0;
    }
}

/*
 * Writes the 5 registers of Channel 1 and triggers playback.
 */
static inline void gb_apu_ch1_write_regs(
    GbApuChannel1 *ch,
    uint8_t nr10,
    uint8_t nr11,
    uint8_t nr12,
    uint8_t nr13,
    uint8_t nr14
) {
    ch->nr10 = nr10;
    ch->nr11 = nr11;
    ch->nr12 = nr12;
    ch->nr13 = nr13;
    ch->nr14 = nr14;
    gb_apu_ch1_trigger(ch);
}

/*
 * Generates N mono 16-bit PCM audio samples.
 */
static inline void gb_apu_ch1_render(GbApuChannel1 *ch, int16_t *buffer, int num_samples) {
    for (int i = 0; i < num_samples; ++i) {
        if (!ch->active || ch->volume == 0) {
            buffer[i] = 0;
            continue;
        }

        /* Step frequency timer */
        ch->timer_samples--;
        if (ch->timer_samples <= 0) {
            ch->timer_samples = ch->timer_period_samples;
            ch->duty_step = (uint8_t)((ch->duty_step + 1u) & 0x07u);
        }

        /* Step sweep timer */
        if (ch->sweep_enabled && ch->sweep_period_samples > 0) {
            ch->sweep_timer_samples--;
            if (ch->sweep_timer_samples <= 0) {
                ch->sweep_timer_samples = ch->sweep_period_samples;
                uint16_t delta = (uint16_t)(ch->raw_frequency >> ch->sweep_shift);
                int new_freq = ch->sweep_negate ? ((int)ch->raw_frequency - (int)delta) : ((int)ch->raw_frequency + (int)delta);
                if (new_freq > 2047 || new_freq < 0) {
                    ch->active = false;
                    buffer[i] = 0;
                    continue;
                }
                ch->raw_frequency = (uint16_t)new_freq;
                int period_cpu = (2048 - (int)ch->raw_frequency) * 4;
                ch->timer_period_samples = (int)((int64_t)period_cpu * (int64_t)ch->sample_rate / 4194304LL);
                if (ch->timer_period_samples < 1) ch->timer_period_samples = 1;
            }
        }

        /* Step envelope timer */
        if (ch->envelope_period_samples > 0) {
            ch->envelope_timer_samples--;
            if (ch->envelope_timer_samples <= 0) {
                ch->envelope_timer_samples = ch->envelope_period_samples;
                if (ch->envelope_add) {
                    if (ch->volume < 15) ch->volume++;
                } else {
                    if (ch->volume > 0) ch->volume--;
                    else ch->active = false;
                }
            }
        }

        /* Pulse wave output: high = +volume, low = -volume */
        bool high = (ch->duty_pattern & (1u << ch->duty_step)) != 0;
        int sample_val = high ? ((int)ch->volume * 1000) : (-((int)ch->volume * 1000));
        buffer[i] = (int16_t)sample_val;
    }
}

#endif /* GB_APU_H */
