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
 * - NR12 (0xFF12): Volume envelope & DAC
 *     Bit 7-4: Initial envelope volume (0..15)
 *     Bit 3:   Envelope direction (0 = decrease, 1 = increase)
 *     Bit 2-0: Envelope step period (0..7, in 64 Hz units)
 *     Note: If bits 7-3 are 0 (initial volume 0 and decay), DAC is OFF!
 * - NR13 (0xFF13): Frequency low 8 bits
 * - NR14 (0xFF14): Frequency high 3 bits & trigger
 *     Bit 7:   Trigger (1 = restart sound)
 *     Bit 6:   Length enable
 *     Bit 2-0: Frequency high 3 bits (bits 8..10)
 *
 * Timing model:
 *   APU base clock = 4,194,304 Hz (T-cycles).
 *   Timer period for 1 duty step = (2048 - raw_freq) * 4 T-cycles.
 *   Frame Sequencer steps at 512 Hz:
 *     Step 0, 4: Clock Length (256 Hz)
 *     Step 2, 6: Clock Length & Sweep (128 Hz)
 *     Step 7:    Clock Envelope (64 Hz)
 *
 * Cycle accumulator:
 *   Advance CPU T-cycles per audio sample = 4194304 / sample_rate (with exact 64-bit fractional remainder).
 */

#define GB_CPU_CLOCK_HZ 4194304ULL

typedef struct {
    /* Hardware registers */
    uint8_t nr10;
    uint8_t nr11;
    uint8_t nr12;
    uint8_t nr13;
    uint8_t nr14;

    /* Channel state */
    bool active;
    bool dac_enabled;
    uint16_t raw_frequency;   /* 11-bit frequency value (0..2047) */
    uint8_t volume;           /* Current volume (0..15) */
    uint8_t duty_pattern;     /* Current 8-bit duty bitmask */
    uint8_t duty_step;        /* 0..7 duty cycle position */

    /* Frequency timer in CPU T-cycles */
    int32_t timer_cycles;
    int32_t timer_period_cycles;

    /* Frame Sequencer in CPU T-cycles (512 Hz = 8192 T-cycles per step) */
    int32_t frame_seq_cycles;
    uint8_t frame_seq_step;   /* 0..7 */

    /* Length counter */
    uint8_t length_counter;   /* 0..64 */
    bool length_enabled;

    /* Envelope tracker */
    uint8_t envelope_period;  /* 0..7 */
    uint8_t envelope_timer;
    bool envelope_add;

    /* Sweep tracker */
    uint8_t sweep_period;     /* 0..7 */
    uint8_t sweep_timer;
    uint8_t sweep_shift;
    bool sweep_negate;
    bool sweep_enabled;
    uint16_t shadow_frequency;

    /* Timing / sample rate conversion */
    uint32_t sample_rate;
    uint64_t cycles_per_sample_fp; /* 32.32 fixed-point T-cycles per sample */
    uint64_t cycle_frac_acc;       /* accumulator */
} GbApuChannel1;

static inline void gb_apu_ch1_init(GbApuChannel1 *ch, uint32_t sample_rate) {
    ch->sample_rate = (sample_rate > 0) ? sample_rate : 44100u;
    ch->cycles_per_sample_fp = (GB_CPU_CLOCK_HZ << 32) / (uint64_t)ch->sample_rate;
    ch->cycle_frac_acc = 0;

    ch->nr10 = 0;
    ch->nr11 = 0;
    ch->nr12 = 0;
    ch->nr13 = 0;
    ch->nr14 = 0;

    ch->active = false;
    ch->dac_enabled = false;
    ch->raw_frequency = 0;
    ch->shadow_frequency = 0;
    ch->volume = 0;
    ch->duty_pattern = 0x01;
    ch->duty_step = 0;

    ch->timer_cycles = 0;
    ch->timer_period_cycles = 2048 * 4;

    ch->frame_seq_cycles = 8192;
    ch->frame_seq_step = 0;

    ch->length_counter = 0;
    ch->length_enabled = false;

    ch->envelope_period = 0;
    ch->envelope_timer = 0;
    ch->envelope_add = false;

    ch->sweep_period = 0;
    ch->sweep_timer = 0;
    ch->sweep_shift = 0;
    ch->sweep_negate = false;
    ch->sweep_enabled = false;
}

static inline void gb_apu_ch1_trigger(GbApuChannel1 *ch) {
    /* DAC check: if bits 7-3 of NR12 are 0, DAC is off and channel cannot be enabled */
    ch->dac_enabled = ((ch->nr12 & 0xF8u) != 0);
    if (!ch->dac_enabled) {
        ch->active = false;
        return;
    }
    ch->active = true;

    ch->raw_frequency = (uint16_t)(((uint16_t)(ch->nr14 & 0x07u) << 8) | (uint16_t)ch->nr13);
    ch->shadow_frequency = ch->raw_frequency;
    ch->timer_period_cycles = (int32_t)((2048 - (int)ch->raw_frequency) * 4);
    ch->timer_cycles = ch->timer_period_cycles;

    /* Length counter: if 0, load with 64 */
    if (ch->length_counter == 0) {
        ch->length_counter = 64;
    }
    ch->length_enabled = (ch->nr14 & 0x40u) != 0;

    /* Volume Envelope */
    ch->volume = (uint8_t)((ch->nr12 >> 4) & 0x0Fu);
    ch->envelope_add = (ch->nr12 & 0x08u) != 0;
    ch->envelope_period = (uint8_t)(ch->nr12 & 0x07u);
    ch->envelope_timer = ch->envelope_period;

    /* Sweep */
    ch->sweep_period = (uint8_t)((ch->nr10 >> 4) & 0x07u);
    ch->sweep_timer = (ch->sweep_period > 0) ? ch->sweep_period : 8;
    ch->sweep_shift = (uint8_t)(ch->nr10 & 0x07u);
    ch->sweep_negate = (ch->nr10 & 0x08u) != 0;
    ch->sweep_enabled = (ch->sweep_period > 0) || (ch->sweep_shift > 0);

    /* Sweep overflow check on trigger if shift > 0 */
    if (ch->sweep_shift > 0) {
        uint16_t delta = (uint16_t)(ch->shadow_frequency >> ch->sweep_shift);
        int next_freq = ch->sweep_negate ? ((int)ch->shadow_frequency - (int)delta) : ((int)ch->shadow_frequency + (int)delta);
        if (next_freq > 2047) {
            ch->active = false;
        }
    }

    /* Duty cycle */
    uint8_t duty_code = (uint8_t)((ch->nr11 >> 6) & 0x03u);
    static const uint8_t DUTY_PATTERNS[4] = {0x01, 0x81, 0x87, 0x7E}; /* 12.5%, 25%, 50%, 75% */
    ch->duty_pattern = DUTY_PATTERNS[duty_code];
}

static inline void gb_apu_ch1_step_sweep(GbApuChannel1 *ch) {
    if (!ch->sweep_enabled || ch->sweep_period == 0) return;

    if (ch->sweep_timer > 0) {
        ch->sweep_timer--;
    }
    if (ch->sweep_timer == 0) {
        ch->sweep_timer = (ch->sweep_period > 0) ? ch->sweep_period : 8;
        if (ch->sweep_shift > 0) {
            uint16_t delta = (uint16_t)(ch->shadow_frequency >> ch->sweep_shift);
            int new_freq = ch->sweep_negate ? ((int)ch->shadow_frequency - (int)delta) : ((int)ch->shadow_frequency + (int)delta);
            if (new_freq > 2047) {
                ch->active = false;
                return;
            } else if (new_freq >= 0) {
                ch->shadow_frequency = (uint16_t)new_freq;
                ch->raw_frequency = (uint16_t)new_freq;
                ch->timer_period_cycles = (int32_t)((2048 - new_freq) * 4);

                /* Perform second overflow check */
                uint16_t delta2 = (uint16_t)(ch->shadow_frequency >> ch->sweep_shift);
                int check2 = ch->sweep_negate ? ((int)ch->shadow_frequency - (int)delta2) : ((int)ch->shadow_frequency + (int)delta2);
                if (check2 > 2047) {
                    ch->active = false;
                }
            }
        }
    }
}

static inline void gb_apu_ch1_step_length(GbApuChannel1 *ch) {
    if (ch->length_enabled && ch->length_counter > 0) {
        ch->length_counter--;
        if (ch->length_counter == 0) {
            ch->active = false;
        }
    }
}

static inline void gb_apu_ch1_step_envelope(GbApuChannel1 *ch) {
    if (ch->envelope_period == 0) return;

    if (ch->envelope_timer > 0) {
        ch->envelope_timer--;
    }
    if (ch->envelope_timer == 0) {
        ch->envelope_timer = ch->envelope_period;
        if (ch->envelope_add) {
            if (ch->volume < 15) ch->volume++;
        } else {
            if (ch->volume > 0) ch->volume--;
        }
    }
}

/*
 * Advances the APU state by a given number of CPU T-cycles.
 */
static inline void gb_apu_ch1_advance_cycles(GbApuChannel1 *ch, int32_t cycles) {
    while (cycles > 0) {
        int32_t step = cycles;
        if (ch->active && step > ch->timer_cycles) {
            step = ch->timer_cycles;
        }
        if (step > ch->frame_seq_cycles) {
            step = ch->frame_seq_cycles;
        }
        if (step <= 0) step = 1;

        cycles -= step;

        /* Advance frequency timer */
        if (ch->active) {
            ch->timer_cycles -= step;
            if (ch->timer_cycles <= 0) {
                ch->timer_cycles += ch->timer_period_cycles;
                ch->duty_step = (uint8_t)((ch->duty_step + 1u) & 0x07u);
            }
        }

        /* Advance 512 Hz Frame Sequencer */
        ch->frame_seq_cycles -= step;
        if (ch->frame_seq_cycles <= 0) {
            ch->frame_seq_cycles += 8192; /* 4194304 / 512 = 8192 */
            uint8_t s = ch->frame_seq_step;
            ch->frame_seq_step = (uint8_t)((ch->frame_seq_step + 1u) & 0x07u);

            /* Length clock: steps 0, 2, 4, 6 (256 Hz) */
            if ((s & 1u) == 0) {
                gb_apu_ch1_step_length(ch);
            }
            /* Sweep clock: steps 2, 6 (128 Hz) */
            if (s == 2 || s == 6) {
                gb_apu_ch1_step_sweep(ch);
            }
            /* Envelope clock: step 7 (64 Hz) */
            if (s == 7) {
                gb_apu_ch1_step_envelope(ch);
            }
        }
    }
}

/*
 * Writes the 5 registers of Channel 1 and handles length / trigger semantics.
 */
static inline void gb_apu_ch1_write_reg(GbApuChannel1 *ch, uint8_t reg, uint8_t value) {
    switch (reg) {
    case 0x10: ch->nr10=value; break;
    case 0x11:
        ch->nr11=value;
        ch->length_counter=(uint8_t)(64u-(value&63u));
        break;
    case 0x12:
        ch->nr12=value;
        ch->dac_enabled=(value&0xF8u)!=0;
        if (!ch->dac_enabled) ch->active=false;
        break;
    case 0x13:
        ch->nr13=value;
        ch->raw_frequency=(uint16_t)((ch->raw_frequency&0x700u)|value);
        ch->timer_period_cycles=(2048-(int)ch->raw_frequency)*4;
        break;
    case 0x14:
        ch->nr14=value;
        ch->raw_frequency=(uint16_t)(((value&7u)<<8)|ch->nr13);
        ch->timer_period_cycles=(2048-(int)ch->raw_frequency)*4;
        ch->length_enabled=(value&0x40u)!=0;
        if (value&0x80u) gb_apu_ch1_trigger(ch);
        break;
    default: break;
    }
}

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

    /* Length counter loaded from low 6 bits of NR11 */
    uint8_t len = (uint8_t)(nr11 & 0x3Fu);
    ch->length_counter = (uint8_t)(64u - len);

    if (nr14 & 0x80u) {
        gb_apu_ch1_trigger(ch);
    }
}

/*
 * Generates N mono 16-bit PCM audio samples using cycle accumulator.
 */
static inline void gb_apu_ch1_render(GbApuChannel1 *ch, int16_t *buffer, int num_samples) {
    for (int i = 0; i < num_samples; ++i) {
        ch->cycle_frac_acc += ch->cycles_per_sample_fp;
        int32_t cycles_to_advance = (int32_t)(ch->cycle_frac_acc >> 32);
        ch->cycle_frac_acc &= 0xFFFFFFFFULL;

        gb_apu_ch1_advance_cycles(ch, cycles_to_advance);

        if (!ch->active || !ch->dac_enabled || ch->volume == 0) {
            buffer[i] = 0;
        } else {
            bool high = (ch->duty_pattern & (1u << ch->duty_step)) != 0;
            int sample_val = high ? ((int)ch->volume * 1000) : (-((int)ch->volume * 1000));
            buffer[i] = (int16_t)sample_val;
        }
    }
}

#endif /* GB_APU_H */
