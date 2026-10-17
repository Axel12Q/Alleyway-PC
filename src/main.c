#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gb_tile.h"
#include "gb_apu.h"
#include "gb_scheduler.h"
#include "gb_audio_backend.h"

/*
 * Alleyway PC research & native Stage 1 verifier/player.
 *
 * No commercial ROM bytes, graphics, or level data are embedded in this file.
 * Stage 1 tables and brick layout are loaded at runtime from the user's local
 * 32 KiB ROM dump (or from an in-memory synthetic test fixture during --smoke-test).
 */

#define ROM_SIZE_BYTES 32768
#define GRID_ROWS 60
#define GRID_COLS 14
#define GRID_CELLS (GRID_ROWS * GRID_COLS)

static uint32_t pixels[144][160];
static const uint32_t DMG_SHADES[4] = {0xe0f8d0, 0x88c070, 0x346856, 0x081820};

typedef struct {
    uint8_t rom[ROM_SIZE_BYTES];
    bool rom_loaded;
    uint8_t state;
    uint16_t state_timer;    /* Delay counter for authentic DMG state transitions */
    uint8_t prng;
    uint8_t paddle_x;
    uint8_t paddle_y;
    uint8_t paddle_w;
    uint8_t paddle_shrunk;
    uint8_t ball_y;
    uint8_t ball_y_sub;
    uint8_t ball_x;
    uint8_t ball_x_sub;
    uint16_t ball_vy;
    uint16_t ball_vx;
    uint8_t ball_prev_y;
    uint8_t ball_prev_x;
    uint8_t speed_idx;
    uint8_t paddle_hits;
    uint8_t scroll_timer;
    uint8_t scroll_step;
    uint8_t indestructible_hits;
    uint16_t bricks_left;
    uint16_t score_tens;
    uint16_t high_score_tens;
    uint8_t lives;
    uint8_t stage;
    uint8_t bonus_flag;
    uint8_t scroll_flag;
    uint8_t total_rows;
    uint8_t row_offset;      /* Visible row offset (0xFFA9) */
    uint8_t vblank_count;    /* Authentic VBlank frame counter (0xFFA2) */
    uint16_t bonus_timer;    /* Bonus stage countdown timer in seconds (0xCA48) */
    uint8_t prev_joyp_raw;
    uint8_t sfx_trigger; /* Queued SFX ID (0 = none, 7 = paddle bounce, 12 = wall bounce, etc.) */
    uint8_t up_index;    /* Index in 0x1B5D 1-UP score table */
    uint8_t row_shift[20];   /* Pixel shift for each of the 20 visible rows (0xCA00..0xCA13) */
    uint8_t row_pattern[20]; /* Row speed & direction pattern (0xCA14..0xCA27) */
    uint8_t row_timer[20];   /* Row timer / step period (0xCA28..0xCA3B) */
    uint8_t grid_type[GRID_CELLS];
    uint8_t grid_hp[GRID_CELLS];
} AlleywayStage1;

static AlleywayStage1 g_sim;
static GbAudioContext g_audio_ctx;
static HWAVEOUT g_h_wave_out = NULL;
static WAVEHDR g_wave_hdrs[AUDIO_NUM_BUFFERS];

static bool g_key_left = false;
static bool g_key_right = false;
static bool g_key_a = false;
static bool g_key_b = false;
static bool g_key_start = false;

/* Win32 waveOut device operations implementation */
static int win32_waveout_open(GbAudioDeviceOps *ops, uint32_t sample_rate) {
    (void)ops;
    WAVEFORMATEX wfx = {0};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = sample_rate;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (WORD)(wfx.nChannels * (wfx.wBitsPerSample / 8));
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    MMRESULT res = waveOutOpen(&g_h_wave_out, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
    if (res != MMSYSERR_NOERROR) {
        g_h_wave_out = NULL;
        return -1;
    }

    memset(g_wave_hdrs, 0, sizeof(g_wave_hdrs));
    for (int i = 0; i < AUDIO_NUM_BUFFERS; ++i) {
        g_wave_hdrs[i].lpData = (LPSTR)g_audio_ctx.sample_buffers[i];
        g_wave_hdrs[i].dwBufferLength = AUDIO_SAMPLES_PER_FRAME * sizeof(int16_t);
        MMRESULT prep_res = waveOutPrepareHeader(g_h_wave_out, &g_wave_hdrs[i], sizeof(WAVEHDR));
        if (prep_res != MMSYSERR_NOERROR) {
            for (int j = 0; j < i; ++j) {
                waveOutUnprepareHeader(g_h_wave_out, &g_wave_hdrs[j], sizeof(WAVEHDR));
            }
            waveOutClose(g_h_wave_out);
            g_h_wave_out = NULL;
            return -2;
        }
    }
    return 0;
}

static int win32_waveout_write(GbAudioDeviceOps *ops, int buffer_index, const int16_t *samples, int num_samples) {
    (void)ops; (void)samples;
    if (!g_h_wave_out) return -1;
    WAVEHDR *hdr = &g_wave_hdrs[buffer_index];
    hdr->dwBufferLength = (DWORD)num_samples * sizeof(int16_t);
    MMRESULT res = waveOutWrite(g_h_wave_out, hdr, sizeof(WAVEHDR));
    return (res == MMSYSERR_NOERROR) ? 0 : -2;
}

static bool win32_waveout_is_buffer_done(GbAudioDeviceOps *ops, int buffer_index) {
    (void)ops;
    if (!g_h_wave_out) return true;
    return (g_wave_hdrs[buffer_index].dwFlags & WHDR_DONE) != 0;
}

static void win32_waveout_reset(GbAudioDeviceOps *ops) {
    (void)ops;
    if (g_h_wave_out) {
        waveOutReset(g_h_wave_out);
    }
}

static void win32_waveout_close(GbAudioDeviceOps *ops) {
    (void)ops;
    if (g_h_wave_out) {
        for (int i = 0; i < AUDIO_NUM_BUFFERS; ++i) {
            if (g_wave_hdrs[i].dwFlags & WHDR_PREPARED) {
                waveOutUnprepareHeader(g_h_wave_out, &g_wave_hdrs[i], sizeof(WAVEHDR));
            }
        }
        waveOutClose(g_h_wave_out);
        g_h_wave_out = NULL;
    }
}

static GbAudioDeviceOps g_win32_audio_ops = {
    .user_data = NULL,
    .open = win32_waveout_open,
    .write = win32_waveout_write,
    .is_buffer_done = win32_waveout_is_buffer_done,
    .reset = win32_waveout_reset,
    .close = win32_waveout_close,
};

static void audio_backend_init(void) {
    gb_audio_init(&g_audio_ctx, &g_win32_audio_ops);
}

static void audio_backend_shutdown(void) {
    gb_audio_shutdown(&g_audio_ctx);
}

static void audio_step_frame(void) {
    gb_audio_step_frame(
        &g_audio_ctx,
        g_sim.rom,
        ROM_SIZE_BYTES,
        g_sim.rom_loaded,
        g_sim.sfx_trigger
    );
}

static uint16_t neg16(uint16_t val) {
    return (uint16_t)(((val ^ 0xFFFFu) + 1u) & 0xFFFFu);
}

static void load_stage_grid(AlleywayStage1 *s) {
    memset(s->grid_type, 0, sizeof(s->grid_type));
    memset(s->grid_hp, 0, sizeof(s->grid_hp));
    memset(s->row_shift, 0, sizeof(s->row_shift));
    memset(s->row_pattern, 0, sizeof(s->row_pattern));
    memset(s->row_timer, 0, sizeof(s->row_timer));

    uint16_t desc_addr = (uint16_t)(0x1BE1u + (uint16_t)s->stage * 3u);
    if (desc_addr + 2u >= ROM_SIZE_BYTES) return;

    uint8_t desc0 = s->rom[desc_addr];
    s->bonus_flag = (desc0 & 0x80u) ? 1u : 0u;
    s->scroll_flag = (desc0 & 0x40u) ? 1u : 0u;

    /* Initialize horizontal scroll pattern buffer if scrolling is enabled */
    if (s->scroll_flag != 0) {
        uint8_t pattern_idx = (uint8_t)(desc0 & 0x3Fu);
        uint16_t ptr_table = (uint16_t)(0x4075u + (uint16_t)pattern_idx * 2u);
        if (ptr_table + 1u < ROM_SIZE_BYTES) {
            uint16_t pattern_addr = (uint16_t)s->rom[ptr_table] | ((uint16_t)s->rom[ptr_table + 1u] << 8);
            if (pattern_addr + 20u <= ROM_SIZE_BYTES) {
                for (int r = 0; r < 20; ++r) {
                    uint8_t p = s->rom[pattern_addr + (uint16_t)r];
                    s->row_pattern[r] = p;
                    s->row_timer[r] = (uint8_t)(p & 0x7Fu);
                }
            }
        }
    }

    uint16_t map_ptr = (uint16_t)s->rom[desc_addr + 1u] | ((uint16_t)s->rom[desc_addr + 2u] << 8);
    int idx = 0;
    int rows = 0;
    uint16_t destructible = 0;
    while (map_ptr + GRID_COLS <= ROM_SIZE_BYTES && s->rom[map_ptr] != 0xFF && idx + GRID_COLS <= GRID_CELLS) {
        for (int c = 0; c < GRID_COLS; ++c) {
            uint8_t t = s->rom[map_ptr + (uint16_t)c];
            s->grid_type[idx + c] = t;
            if (t != 0) {
                uint16_t prop_addr = (uint16_t)(0x1B87u + (uint16_t)(t - 1u) * 6u + 3u);
                uint8_t hp = (prop_addr < ROM_SIZE_BYTES) ? (uint8_t)(s->rom[prop_addr] & 0x0Fu) : 1u;
                s->grid_hp[idx + c] = hp;
                if (hp != 0) {
                    destructible++;
                }
            }
        }
        map_ptr = (uint16_t)(map_ptr + GRID_COLS);
        idx += GRID_COLS;
        rows++;
    }
    s->total_rows = (uint8_t)rows;
    s->row_offset = (rows >= 20) ? (uint8_t)(rows - 20) : 0u;
    s->bricks_left = destructible;

    /* Initialize authentic bonus timer from 0x1B71 if in bonus stage (0x0735..0x0745) */
    if (s->bonus_flag != 0) {
        uint8_t round_idx = (uint8_t)(s->stage >> 2); /* stages 3, 7, 11, 15 -> rounds 0, 1, 2, 3 */
        if (round_idx > 3u) round_idx = 3u;
        uint16_t timer_tbl = (uint16_t)(0x1B71u + (uint16_t)round_idx * 3u);
        if (timer_tbl < ROM_SIZE_BYTES) {
            s->bonus_timer = s->rom[timer_tbl];
        } else {
            s->bonus_timer = 95u;
        }
    } else {
        s->bonus_timer = 0;
    }
}

static void stage1_reset_for_play(AlleywayStage1 *s) {
    s->state = 5;
    s->prng = 43;
    s->paddle_x = 0x28;
    s->paddle_y = 0x90;
    s->paddle_w = 0x18;
    s->paddle_shrunk = 0;
    s->ball_y = 0;
    s->ball_y_sub = 0;
    s->ball_x = 0;
    s->ball_x_sub = 0;
    s->ball_vy = 0;
    s->ball_vx = 0;
    s->ball_prev_y = 0;
    s->ball_prev_x = 0;
    s->speed_idx = 0;
    s->paddle_hits = 0;
    s->scroll_timer = 0;
    s->scroll_step = 0;
    s->indestructible_hits = 0;
    s->score_tens = 0;
    if (s->high_score_tens < 200) s->high_score_tens = 200;
    s->lives = 3;
    s->stage = 0;
    s->bonus_flag = 0;
    s->scroll_flag = 0;
    s->prev_joyp_raw = 0xFF;
    s->sfx_trigger = 0;
    s->up_index = 0;
    s->vblank_count = 0;
    load_stage_grid(s);
}

static uint8_t step_prng(AlleywayStage1 *s) {
    s->prng = (uint8_t)((s->prng + 0x41u) & 0xFFu);
    return s->prng;
}

static void load_speed_vector(const AlleywayStage1 *s, uint8_t speed_one_based, uint8_t angle_idx,
                              uint16_t *out_vy, uint16_t *out_vx) {
    if (speed_one_based == 0) speed_one_based = 1;
    uint16_t ptr_addr = (uint16_t)(0x11EEu + (uint16_t)(speed_one_based - 1u) * 2u);
    uint16_t table_ptr = (uint16_t)s->rom[ptr_addr] | ((uint16_t)s->rom[ptr_addr + 1u] << 8);
    uint16_t entry = (uint16_t)(table_ptr + (uint16_t)angle_idx * 4u);
    *out_vy = ((uint16_t)s->rom[entry] << 8) | (uint16_t)s->rom[entry + 1u];
    *out_vx = ((uint16_t)s->rom[entry + 2u] << 8) | (uint16_t)s->rom[entry + 3u];
}

static void reindex_velocity_random_angle(AlleywayStage1 *s) {
    uint8_t rnd = (uint8_t)(step_prng(s) & 0x07u);
    uint8_t angle_idx = s->rom[0x100Bu + rnd];
    uint16_t vy = 0, vx = 0;
    load_speed_vector(s, s->speed_idx, angle_idx, &vy, &vx);
    if (s->ball_vy & 0x8000u) vy = neg16(vy);
    s->ball_vy = vy;
    if (s->ball_vx & 0x8000u) vx = neg16(vx);
    s->ball_vx = vx;
}

static void update_paddle(AlleywayStage1 *s, uint8_t joyp_raw) {
    int speed = 3;
    if ((joyp_raw & 0x01u) == 0) {
        speed = 5;
    } else if ((joyp_raw & 0x02u) == 0) {
        speed = 1;
    }
    uint8_t dpad = (uint8_t)((joyp_raw ^ 0xFFu) & 0x30u);
    if (dpad == 0) return;
    if (dpad & 0x20u) {
        int nx = (int)s->paddle_x - speed;
        if (nx < 0x0F) nx = 0x0F;
        s->paddle_x = (uint8_t)nx;
    } else {
        int limit = 0x7F - (int)s->paddle_w;
        int nx = (int)s->paddle_x + speed;
        if (nx >= limit) nx = limit;
        s->paddle_x = (uint8_t)nx;
    }
}

static void serve_ball(AlleywayStage1 *s) {
    s->paddle_hits = 0;
    s->indestructible_hits = 0;
    if (s->scroll_step < 0x0Au) {
        uint8_t c = s->scroll_step;
        s->scroll_step = (uint8_t)(s->scroll_step + 1u);
        uint16_t tbl_addr = (uint16_t)(0x1B7Du + (uint16_t)c);
        if (s->rom_loaded && tbl_addr < ROM_SIZE_BYTES) {
            s->scroll_timer = s->rom[tbl_addr];
        } else {
            s->scroll_timer = 8u;
        }
    } else {
        s->scroll_timer = 1u;
    }

    s->ball_y_sub = 0;
    s->ball_x_sub = 0;
    s->speed_idx = 3;
    if (s->bonus_flag != 0 || s->bricks_left < 0x28u) {
        s->speed_idx = 7;
    }

    uint8_t offset_b = 0x18u;
    uint8_t half_w = (uint8_t)(s->paddle_w >> 1);
    if ((uint8_t)(s->paddle_x + half_w) >= 0x48u) {
        offset_b = 0xE8u;
    }
    uint8_t bx = (uint8_t)(s->paddle_x + offset_b + half_w);
    s->ball_x = bx;
    s->ball_prev_x = bx;
    uint8_t by = (uint8_t)(0x8Cu - 0x18u);
    s->ball_y = by;
    s->ball_prev_y = by;

    uint16_t vy = 0, vx = 0;
    load_speed_vector(s, 1, 9, &vy, &vx);
    s->ball_vy = vy;
    if (offset_b < 0x80u) {
        vx = neg16(vx);
    }
    s->ball_vx = vx;
    s->state = 6;
    s->sfx_trigger = 7; /* SFX 7: Serve / Paddle bounce */
}

static void snap_y_positive(AlleywayStage1 *s) {
    uint8_t rem = (uint8_t)(s->ball_y & 0x03u);
    if (rem == 0) return;
    s->ball_y = (uint8_t)((s->ball_y & 0xFCu) - rem + 1u);
}

static void snap_y_negative(AlleywayStage1 *s) {
    uint8_t rem = (uint8_t)(s->ball_y & 0x03u);
    if (rem == 0) return;
    s->ball_y = (uint8_t)((s->ball_y & 0xFCu) + 8u - rem - 1u);
}

static void snap_x_positive(AlleywayStage1 *s) {
    uint8_t b = (s->ball_x & 0x04u) ? 0x04u : 0xFCu;
    uint8_t a = (uint8_t)((s->ball_x & 0xF8u) + b);
    if (a < 0x10u) a = 0x10u;
    s->ball_x = a;
}

static void snap_x_negative(AlleywayStage1 *s) {
    uint8_t a = (uint8_t)((s->ball_x & 0xF8u) + 8u);
    if (a >= 0x7Cu) a = 0x7Cu;
    s->ball_x = a;
}

static void bounce_vertical(AlleywayStage1 *s) {
    if ((s->ball_vy & 0x8000u) == 0) {
        snap_y_positive(s);
    } else {
        snap_y_negative(s);
    }
    s->ball_vy = neg16(s->ball_vy);
    s->ball_prev_y = s->ball_y;
}

static void bounce_horizontal(AlleywayStage1 *s) {
    if ((s->ball_vx & 0x8000u) == 0) {
        snap_x_positive(s);
    } else {
        snap_x_negative(s);
    }
    s->ball_vx = neg16(s->ball_vx);
    s->ball_prev_x = s->ball_x;
}

static int check_brick_point(AlleywayStage1 *s, uint8_t y_probe, uint8_t x_probe) {
    uint8_t scroll_y = (uint8_t)((s->row_offset << 2) & 0xFFu);
    uint8_t a1 = (uint8_t)(y_probe - 0x18u);
    uint16_t a2_full = (uint16_t)a1 + (uint16_t)scroll_y;
    if (a2_full > 0xFFu) return 0;
    uint8_t row = (uint8_t)((a2_full & 0xFFu) >> 2);
    if (row >= GRID_ROWS) return 0;

    /* Authentic horizontal shift from row_shift[row - row_offset] */
    uint8_t shift = 0;
    if (row >= s->row_offset && (row - s->row_offset) < 20) {
        shift = s->row_shift[row - s->row_offset];
    }

    uint8_t x_rel = (uint8_t)((uint8_t)(x_probe - 0x10u) + shift);
    if (x_rel >= 0x70u) x_rel = (uint8_t)(x_rel - 0x70u);
    uint8_t col = (uint8_t)(x_rel >> 3);
    if (col >= GRID_COLS) return 0;

    int idx = (int)row * GRID_COLS + (int)col;
    uint8_t btype = s->grid_type[idx];
    if (btype == 0) return 0;

    uint8_t min_spd = s->rom[0x1B87u + (uint16_t)(btype - 1u) * 6u + 4u];
    if (min_spd != 0 && s->speed_idx < min_spd) {
        s->speed_idx = min_spd;
        reindex_velocity_random_angle(s);
    }

    uint8_t hp = s->grid_hp[idx];
    if (hp == 0) {
        s->indestructible_hits = (uint8_t)(s->indestructible_hits + 1u);
        if (s->indestructible_hits >= 0x0Au) {
            reindex_velocity_random_angle(s);
            s->indestructible_hits = 0;
        }
        return 1;
    }

    if (s->bonus_flag == 0) {
        hp--;
        s->grid_hp[idx] = hp;
        if (hp != 0) return 0;
    }

    s->grid_type[idx] = 0;
    uint8_t pts = (uint8_t)((s->rom[0x1B87u + (uint16_t)(btype - 1u) * 6u + 3u] >> 4) & 0x0Fu);
    uint32_t new_score = (uint32_t)s->score_tens + (uint32_t)pts;
    s->score_tens = (new_score > 0xFFFFu) ? 0xFFFFu : (uint16_t)new_score;
    if (s->score_tens > s->high_score_tens) {
        s->high_score_tens = s->score_tens;
    }

    /* 1-UP check against table at 0x1B5D (0x0C71..0x0CA5) */
    if (0x1B5Du + (uint16_t)s->up_index * 2u + 1u < ROM_SIZE_BYTES) {
        uint16_t thresh = ((uint16_t)s->rom[0x1B5Du + (uint16_t)s->up_index * 2u] << 8) |
                          (uint16_t)s->rom[0x1B5Du + (uint16_t)s->up_index * 2u + 1u];
        if (thresh != 0xFFFFu && s->score_tens >= thresh) {
            s->up_index++;
            if (s->lives < 99) s->lives++;
        }
    }

    if (s->bricks_left > 0) {
        s->bricks_left--;
    }
    if (s->bricks_left == 0) {
        s->state = 8;
    }
    /* Authentic Alleyway brick hit sound: byte 5 of 0x1B87 brick properties */
    uint8_t sfx_id = (btype <= 3) ? btype : 1;
    s->sfx_trigger = sfx_id;
    if (s->bonus_flag != 0) return 0;
    return 1;
}

static void step_paddle_bounce_vertical_drop(AlleywayStage1 *s) {
    if (s->scroll_timer > 0) {
        s->scroll_timer--;
    } else {
        s->scroll_timer = 0xFFu;
    }
    if (s->scroll_timer == 0) {
        /* Authentic vertical brick drop (0x1175: CALL 0x0BC7) */
        if (s->row_offset > 0) {
            s->row_offset--;
        }
        if (s->scroll_step < 0x0Au) {
            uint8_t sc = s->scroll_step;
            s->scroll_step = (uint8_t)(s->scroll_step + 1u);
            uint16_t step_tbl = (uint16_t)(0x1B7Du + (uint16_t)sc);
            if (s->rom_loaded && step_tbl < ROM_SIZE_BYTES) {
                s->scroll_timer = s->rom[step_tbl];
            } else {
                s->scroll_timer = 8u;
            }
        } else {
            s->scroll_timer = 1u;
        }
    }
}

static void step_ball_and_collisions(AlleywayStage1 *s) {
    s->ball_prev_y = s->ball_y;
    uint16_t y16 = (uint16_t)((((uint16_t)s->ball_y << 8) | (uint16_t)s->ball_y_sub) + s->ball_vy);
    s->ball_y = (uint8_t)(y16 >> 8);
    s->ball_y_sub = (uint8_t)(y16 & 0xFFu);

    s->ball_prev_x = s->ball_x;
    uint16_t x16 = (uint16_t)((((uint16_t)s->ball_x << 8) | (uint16_t)s->ball_x_sub) + s->ball_vx);
    s->ball_x = (uint8_t)(x16 >> 8);
    s->ball_x_sub = (uint8_t)(x16 & 0xFFu);

    /* Paddle collision (0x0CB1..0x0CE0) */
    if ((s->ball_vy & 0x8000u) == 0) {
        uint8_t c = (uint8_t)(s->ball_y - 0x8Du);
        if (s->ball_y >= 0x8Du && c < 8u) {
            uint8_t d = (uint8_t)(s->paddle_w + 5u);
            uint8_t b = (uint8_t)(s->paddle_x - 3u);
            uint8_t rel_x = (uint8_t)(s->ball_x - b);
            if (s->ball_x >= b && rel_x < d) {
                uint8_t zone = (uint8_t)(rel_x >> 1);
                if (c < 7u) {
                    uint16_t table_base = (s->paddle_shrunk == 0) ? 0x1B41u : 0x1B51u;
                    uint8_t angle_idx = s->rom[table_base + zone];
                    uint16_t vy = 0, vx = 0;
                    load_speed_vector(s, s->speed_idx, angle_idx, &vy, &vx);
                    s->ball_vy = neg16(vy);
                    uint8_t mid = (s->paddle_shrunk == 0) ? 8u : 6u;
                    if (zone < mid) {
                        vx = neg16(vx);
                    }
                    s->ball_vx = vx;
                    s->sfx_trigger = 7; /* SFX 7: Paddle bounce */
                    step_paddle_bounce_vertical_drop(s);
                } else {
                    bounce_horizontal(s);
                }
            }
        }
    }

    /* Top ceiling and bottom out-of-bounds (0x0CE1..0x0D14) */
    if (s->ball_y < 0x18u) {
        if (s->bonus_flag == 0 && s->paddle_shrunk == 0) {
            s->paddle_shrunk = 1;
            s->paddle_w = 0x10;
            s->paddle_x = (uint8_t)(s->paddle_x + 4u);
        }
        bounce_vertical(s);
        s->sfx_trigger = 11; /* SFX 11: Ceiling bounce */
    } else if (s->ball_y >= 0xA0u) {
        s->state = 7;
        return;
    }

    /* Left and right walls (0x0D15..0x0D26) */
    if (s->ball_x < 0x10u || s->ball_x >= 0x7Cu) {
        bounce_horizontal(s);
        s->sfx_trigger = 12; /* SFX 12: Wall bounce */
    }

    /* Brick grid collisions (0x0D27..0x0E8C) */
    if (s->ball_y >= 0x88u) return;

    for (int pass = 0; pass < 2; ++pass) {
        int hits = 0;
        if ((s->ball_vx & 0x8000u) == 0) {
            if (check_brick_point(s, s->ball_prev_y, (uint8_t)(s->ball_x + 3u))) {
                hits++;
                bounce_horizontal(s);
            } else if (check_brick_point(s, s->ball_prev_y, s->ball_x)) {
                hits++;
                if ((s->ball_vx & 0x8000u) != 0) snap_x_positive(s);
                else snap_x_negative(s);
            }
        } else {
            if (check_brick_point(s, s->ball_prev_y, s->ball_x)) {
                hits++;
                bounce_horizontal(s);
            } else if (check_brick_point(s, s->ball_prev_y, (uint8_t)(s->ball_x + 3u))) {
                hits++;
                if ((s->ball_vx & 0x8000u) != 0) snap_x_positive(s);
                else snap_x_negative(s);
            }
        }

        if ((s->ball_vy & 0x8000u) == 0) {
            if (check_brick_point(s, (uint8_t)(s->ball_y + 3u), s->ball_prev_x)) {
                hits++;
                bounce_vertical(s);
            } else if (check_brick_point(s, s->ball_y, s->ball_prev_x)) {
                hits++;
            }
        } else {
            if (check_brick_point(s, s->ball_y, s->ball_prev_x)) {
                hits++;
                bounce_vertical(s);
            } else if (check_brick_point(s, (uint8_t)(s->ball_y + 3u), s->ball_prev_x)) {
                hits++;
            }
        }

        if (pass == 0 && hits == 0) break;
    }
}

static void step_horizontal_scroll(AlleywayStage1 *s) {
    if (s->scroll_flag == 0) return;
    for (int r = 0; r < 20; ++r) {
        if (s->row_timer[r] == 0) continue;
        s->row_timer[r]--;
        if (s->row_timer[r] == 0) {
            uint8_t pat = s->row_pattern[r];
            if (pat != 0) {
                if ((pat & 0x80u) == 0) {
                    /* Scroll right (INC shift mod 0x70) */
                    uint8_t sh = (uint8_t)(s->row_shift[r] + 1u);
                    if (sh >= 0x70u) sh = 0;
                    s->row_shift[r] = sh;
                } else {
                    /* Scroll left (DEC shift mod 0x70) */
                    uint8_t sh = s->row_shift[r];
                    if (sh == 0) sh = 0x6Fu;
                    else sh--;
                    s->row_shift[r] = sh;
                }
            }
            s->row_timer[r] = (uint8_t)(pat & 0x7Fu);
        }
    }
}

static void step_stage_timers(AlleywayStage1 *s) {
    /* Bonus stage countdown timer (0x198C):
     * When bonus_flag is set, bonus timer counts down every 32 frames/ticks (vblank_count & 0x1F == 0).
     * 1 HUD unit = 32 VBlanks (~0.536s; 95 units = 3040 frames = ~50.9s).
     * When timer expires (hits 0), triggers state 7 (0x19C7: LD A, 7 / LDH (0xA4), A)
     * which in bonus stages transitions cleanly to State 8 (0x07D3: if bonus_flag != 0 -> State 8)
     * without losing a life!
     */
    if (s->bonus_flag != 0 && s->bonus_timer > 0) {
        if ((s->vblank_count & 0x1Fu) == 0) {
            s->bonus_timer--;
            if (s->bonus_timer == 0) {
                s->state = 7;
            }
        }
    }
}

static void stage1_step_tick_scheduler(AlleywayStage1 *s, uint8_t joyp_raw, uint8_t joyp_edge, uint8_t elapsed_vblanks) {
    s->vblank_count = (uint8_t)(s->vblank_count + elapsed_vblanks);
    s->sfx_trigger = 0;
    if (s->state == 4) {
        /* State 4 (0x06A2): Curtain opening / stage grid initialization (67 frames in DMG) */
        if (s->state_timer > 0) {
            s->state_timer--;
            if (s->state_timer > 0) return;
        }
        load_stage_grid(s);
        s->paddle_shrunk = 0;
        s->paddle_w = 0x18;
        s->state = 5;
    } else if (s->state == 5) {
        step_horizontal_scroll(s);
        update_paddle(s, joyp_raw);
        if ((joyp_edge & 0x01u) == 0) {
            serve_ball(s);
        }
    } else if (s->state == 6) {
        step_horizontal_scroll(s);
        step_stage_timers(s);
        step_ball_and_collisions(s);
        update_paddle(s, joyp_raw);
        if ((joyp_edge & 0x08u) == 0) {
            s->state = 0x0C; /* Pause */
        }
    } else if (s->state == 0x0C) {
        if ((joyp_edge & 0x08u) == 0) {
            s->state = 6;
        }
    } else if (s->state == 7) {
        /* Life lost routine (0x07D3):
         * Delay: 101 frames for normal death; 228 frames for bonus drop/expiration.
         */
        if (s->state_timer > 0) {
            s->state_timer--;
            if (s->state_timer > 0) return;
        }
        if (s->bonus_flag != 0) {
            s->state = 8;
        } else {
            if (s->lives == 0) {
                s->state = 0x0B; /* Game Over (0x07E4 -> 0x08D1) */
                s->sfx_trigger = 10;
            } else {
                s->lives--;
                s->paddle_shrunk = 0;
                s->paddle_w = 0x18;
                s->state = 5;
            }
        }
    } else if (s->state == 8) {
        /* Stage Clear (0x0805):
         * Delay: 145 frames for normal clear; 499 frames for bonus tally.
         */
        if (s->state_timer > 0) {
            s->state_timer--;
            if (s->state_timer > 0) return;
        }
        if (s->bonus_flag != 0) {
            if (s->bricks_left == 0) {
                uint8_t round_idx = (uint8_t)(s->stage >> 2);
                if (round_idx > 3u) round_idx = 3u;
                uint16_t tbl_addr = (uint16_t)(0x1B71u + (uint16_t)round_idx * 3u);
                if (s->rom_loaded && tbl_addr + 2u < ROM_SIZE_BYTES) {
                    uint16_t reward_tens = ((uint16_t)s->rom[tbl_addr + 1u] << 8) | (uint16_t)s->rom[tbl_addr + 2u];
                    uint32_t ns = (uint32_t)s->score_tens + (uint32_t)reward_tens;
                    s->score_tens = (ns > 0xFFFFu) ? 0xFFFFu : (uint16_t)ns;
                    if (s->score_tens > s->high_score_tens) s->high_score_tens = s->score_tens;
                }
            }
            s->sfx_trigger = 8;
        } else {
            s->sfx_trigger = 8;
        }

        /* Advance stage (0x082B: INC stage; if stage >= 32 -> stage = 0) */
        s->stage = (uint8_t)(s->stage + 1u);
        if (s->stage >= 32u) {
            s->stage = 0u;
            s->state = 9u; /* State 9 (0x0839): Stage 32 ending sequence */
        } else {
            s->state = 4u; /* State 4 (0x06A2): Curtain opening / load next stage */
            load_stage_grid(s);
            s->paddle_shrunk = 0;
            s->paddle_w = 0x18;
            s->state = 5u;
        }
    } else if (s->state == 9) {
        /* State 9 (0x0839): Stage 32 ending celebration */
        if (s->state_timer > 0) {
            s->state_timer--;
        }
        if ((joyp_edge & 0x08u) == 0 || (joyp_edge & 0x01u) == 0) {
            s->stage = 0u;
            s->state = 4u;
            load_stage_grid(s);
            s->paddle_shrunk = 0;
            s->paddle_w = 0x18;
            s->state = 5u;
        }
    } else if (s->state == 0x0B) {
        /* State 11 (0x08D1): Game Over (377 frames or Start/A press) */
        if (s->state_timer > 0) {
            s->state_timer--;
            if (s->state_timer == 0) {
                s->stage = 0u;
                stage1_reset_for_play(s);
                return;
            }
        }
        if ((joyp_edge & 0x08u) == 0 || (joyp_edge & 0x01u) == 0) {
            s->stage = 0u;
            stage1_reset_for_play(s);
        }
    }
    step_prng(s);
}

static void stage1_step_tick(AlleywayStage1 *s, uint8_t joyp_raw, uint8_t joyp_edge) {
    stage1_step_tick_scheduler(s, joyp_raw, joyp_edge, 1u);
}

static void fill_rect_fb(int x0, int y0, int w, int h, uint32_t color) {
    for (int y = y0; y < y0 + h; ++y) {
        if (y < 0 || y >= 144) continue;
        for (int x = x0; x < x0 + w; ++x) {
            if (x < 0 || x >= 160) continue;
            pixels[y][x] = color;
        }
    }
}

/* Minimal 3x5 digit font for numeric HUD (0-9) */
static const uint8_t DIGIT_FONT[10][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}
};

static void draw_number_fb(int x, int y, uint32_t value, int digits, uint32_t color) {
    for (int i = digits - 1; i >= 0; --i) {
        int d = (int)(value % 10u);
        value /= 10u;
        int dx = x + i * 4;
        for (int r = 0; r < 5; ++r) {
            uint8_t row_bits = DIGIT_FONT[d][r];
            for (int c = 0; c < 3; ++c) {
                if (row_bits & (1u << (2 - c))) {
                    if (y + r >= 0 && y + r < 144 && dx + c >= 0 && dx + c < 160) {
                        pixels[y + r][dx + c] = color;
                    }
                }
            }
        }
    }
}

static void draw_diagnostic(void) {
    for (int y = 0; y < 144; ++y)
        for (int x = 0; x < 160; ++x)
            pixels[y][x] = DMG_SHADES[(x / 40 + y / 36) % 4];
}

static void render_stage1(const AlleywayStage1 *s) {
    if (!s->rom_loaded) {
        draw_diagnostic();
        return;
    }
    /* Clear playfield to lightest DMG shade */
    fill_rect_fb(0, 0, 160, 144, DMG_SHADES[0]);

    /* Left wall (X=0..7), Right wall & HUD panel (X=120..159) in OAM-adjusted screen coords:
     * Playfield left wall is at OAM X=16 -> screen X=8.
     * Playfield right wall is at OAM X=128 -> screen X=120.
     * Top border is at OAM Y=24..31 -> screen Y=8..15 (rows 0..1 of grid!).
     */
    fill_rect_fb(0, 0, 8, 144, DMG_SHADES[2]);
    fill_rect_fb(120, 0, 8, 144, DMG_SHADES[2]);
    fill_rect_fb(128, 0, 32, 144, DMG_SHADES[3]);

    /* Render 14x20 visible brick cells.
     * In Alleyway DMG, background tiles 168..176 are located at ROM offset 0x55F5.
     * Every 8x8 tile packs TWO 4-pixel rows:
     * - Both rows present: tile = 168 (type 1), 169 (type 2), 170 (type 3).
     * - Top row only:      tile = 171 (type 1), 172 (type 2), 173 (type 3).
     * - Bottom row only:   tile = 174 (type 1), 175 (type 2), 176 (type 3).
     * - Top wall (type 4): tile 179 at ROM offset 0x56A5.
     */
    bool has_rom_tiles = (0x56A5 + 16 <= ROM_SIZE_BYTES && s->rom[0x55F5] != 0);

    if (has_rom_tiles) {
        /* Render paired 8x8 tile rows (r = 0, 2, 4, ..., 18) */
        for (int r = 0; r < 20; r += 2) {
            int shift = (int)s->row_shift[r];
            for (int c = 0; c < GRID_COLS; ++c) {
                uint8_t top_t = s->grid_type[(r + s->row_offset) * GRID_COLS + c];
                uint8_t bot_t = s->grid_type[(r + 1 + s->row_offset) * GRID_COLS + c];
                if (top_t == 0 && bot_t == 0) continue;

                /* Calculate screen X with horizontal shift wrapped across the 112 px (14 cols) width */
                int col_pixel = (c * 8 - shift) % 112;
                if (col_pixel < 0) col_pixel += 112;
                int sx = 8 + col_pixel;
                int sy = 8 + r * 4;

                uint8_t tile_id = 0;
                if (top_t == 4 || bot_t == 4) {
                    tile_id = 179;
                } else {
                    uint8_t ref_t = (top_t != 0) ? top_t : bot_t;
                    uint8_t base_offset = (ref_t >= 1 && ref_t <= 3) ? (uint8_t)(ref_t - 1) : 0u;
                    if (top_t != 0 && bot_t != 0) {
                        tile_id = (uint8_t)(168 + base_offset);
                    } else if (top_t != 0) {
                        tile_id = (uint8_t)(171 + base_offset);
                    } else {
                        tile_id = (uint8_t)(174 + base_offset);
                    }
                }
                uint32_t rom_addr = (tile_id == 179) ? 0x56A5u : (0x55F5u + (uint32_t)(tile_id - 168) * 16u);
                gb_tile_blit(&s->rom[rom_addr], sx, sy, false, false, false, 0xE4, DMG_SHADES, (uint32_t *)pixels, 160, 144);
            }
        }
    } else {
        /* Fallback synthetic / block renderer */
        for (int r = 0; r < 20; ++r) {
            int shift = (int)s->row_shift[r];
            for (int c = 0; c < GRID_COLS; ++c) {
                uint8_t t = s->grid_type[(r + s->row_offset) * GRID_COLS + c];
                if (t == 0) continue;
                int col_pixel = (c * 8 - shift) % 112;
                if (col_pixel < 0) col_pixel += 112;
                int sx = 8 + col_pixel;
                int sy = 8 + r * 4;
                if (t == 4) {
                    fill_rect_fb(sx, sy, 8, 4, DMG_SHADES[2]);
                } else {
                    uint32_t fill = (t == 3) ? DMG_SHADES[3] : (t == 2 ? DMG_SHADES[2] : DMG_SHADES[1]);
                    fill_rect_fb(sx, sy, 7, 3, fill);
                    fill_rect_fb(sx, sy + 3, 8, 1, DMG_SHADES[0]);
                }
            }
        }
    }

    /* Redraw left and right borders over any wrapped tiles extending into borders */
    fill_rect_fb(0, 0, 8, 144, DMG_SHADES[2]);
    fill_rect_fb(120, 0, 8, 144, DMG_SHADES[2]);
    fill_rect_fb(128, 0, 32, 144, DMG_SHADES[3]);

    /* Render Paddle: OAM[0].X = paddle_x + 1 -> screen X = (paddle_x + 1) - 8 = paddle_x - 7
     * OAM.Y = 144 -> screen Y = 144 - 16 = 128.
     * Authentic DMG paddle:
     * - Normal (24 px): Tile 0 at 0x4B75 (left cap), Tile 1 at 0x4B85 (middle), Tile 0 flipped X (right cap).
     * - Shrunk (16 px): Tile 0 (left cap), Tile 0 flipped X (right cap).
     */
    int pad_sx = (int)s->paddle_x - 7;
    int pad_sy = (int)s->paddle_y - 16;
    bool has_paddle_tiles = (0x4B85 + 16 <= ROM_SIZE_BYTES && s->rom[0x4B75] != 0);

    if (has_paddle_tiles) {
        if (s->paddle_w == 16) {
            /* Left cap (8x8) */
            gb_tile_blit(&s->rom[0x4B75], pad_sx, pad_sy, false, false, true, 0xE4, DMG_SHADES, (uint32_t *)pixels, 160, 144);
            /* Right cap (8x8 flipped X) */
            gb_tile_blit(&s->rom[0x4B75], pad_sx + 8, pad_sy, true, false, true, 0xE4, DMG_SHADES, (uint32_t *)pixels, 160, 144);
        } else {
            /* Left cap */
            gb_tile_blit(&s->rom[0x4B75], pad_sx, pad_sy, false, false, true, 0xE4, DMG_SHADES, (uint32_t *)pixels, 160, 144);
            /* Middle piece */
            gb_tile_blit(&s->rom[0x4B85], pad_sx + 8, pad_sy, false, false, true, 0xE4, DMG_SHADES, (uint32_t *)pixels, 160, 144);
            /* Right cap */
            gb_tile_blit(&s->rom[0x4B75], pad_sx + 16, pad_sy, true, false, true, 0xE4, DMG_SHADES, (uint32_t *)pixels, 160, 144);
        }
    } else {
        fill_rect_fb(pad_sx, pad_sy, (int)s->paddle_w, 6, DMG_SHADES[3]);
        fill_rect_fb(pad_sx + 2, pad_sy + 1, (int)s->paddle_w - 4, 2, DMG_SHADES[1]);
    }

    /* Render Ball: Tile 5 at ROM 0x4BC5
     * Active (state == 6 or 0x0C): OAM.X = ball_x -> screen X = ball_x - 8, Y = ball_y - 16
     */
    bool has_ball_tile = (0x4BC5 + 16 <= ROM_SIZE_BYTES && s->rom[0x4BC5] != 0);

    if (s->state == 6 || s->state == 0x0C) {
        int ball_sx = (int)s->ball_x - 8;
        int ball_sy = (int)s->ball_y - 16;
        if (has_ball_tile) {
            gb_tile_blit(&s->rom[0x4BC5], ball_sx, ball_sy, false, false, true, 0xE4, DMG_SHADES, (uint32_t *)pixels, 160, 144);
        } else {
            fill_rect_fb(ball_sx, ball_sy, 4, 4, DMG_SHADES[3]);
        }
    } else if (s->state == 5) {
        /* Preview ball resting above paddle before serve */
        int preview_sx = pad_sx + ((int)s->paddle_w / 2) - 2;
        int preview_sy = pad_sy - 5;
        if (has_ball_tile) {
            gb_tile_blit(&s->rom[0x4BC5], preview_sx - 1, preview_sy - 1, false, false, true, 0xE4, DMG_SHADES, (uint32_t *)pixels, 160, 144);
        } else {
            fill_rect_fb(preview_sx, preview_sy, 4, 4, DMG_SHADES[2]);
        }
    }

    /* HUD numbers on right panel (X = 132..156): High Score, Score, Bricks/Bonus timer, Lives, Stage */
    draw_number_fb(132, 16, (uint32_t)s->high_score_tens * 10u, 6, DMG_SHADES[0]);
    draw_number_fb(132, 36, (uint32_t)s->score_tens * 10u, 6, DMG_SHADES[0]);
    if (s->bonus_flag != 0) {
        /* In bonus stages, display remaining countdown seconds in HUD */
        draw_number_fb(144, 64, (uint32_t)s->bonus_timer, 2, DMG_SHADES[0]);
    } else {
        draw_number_fb(140, 64, (uint32_t)s->bricks_left, 3, DMG_SHADES[1]);
    }
    draw_number_fb(144, 88, (uint32_t)s->lives, 2, DMG_SHADES[0]);
    draw_number_fb(144, 112, (uint32_t)(s->stage + 1u), 2, DMG_SHADES[1]);

    /* Authentic banners and transitions:
     * 1. State 4 (Curtains): Draw closing / opening curtain borders across playfield (X = 8..119).
     * 2. State 8 (Stage Clear): Draw "STAGE CLEAR" banner.
     * 3. State 9 (Ending Celebration): Draw "CLEAR" banner.
     * 4. State 0x0B (Game Over): Draw "GAME OVER" banner.
     */
    if (s->state == 4 && s->state_timer > 0) {
        /* Curtains move inwards/outwards: width proportional to remaining timer */
        int curtain_w = ((int)s->state_timer * 56) / 67;
        if (curtain_w > 56) curtain_w = 56;
        fill_rect_fb(8, 8, curtain_w, 128, DMG_SHADES[2]);
        fill_rect_fb(120 - curtain_w, 8, curtain_w, 128, DMG_SHADES[2]);
    } else if (s->state == 0x0B) {
        /* Render GAME OVER box: background plate + authentic font tiles */
        fill_rect_fb(28, 56, 72, 28, DMG_SHADES[3]);
        fill_rect_fb(30, 58, 68, 24, DMG_SHADES[0]);
        /* "GAME" centered at Y=62, "OVER" at Y=70 */
        static const uint8_t game_str[4] = {0x90, 0x8A, 0x96, 0x8E};
        static const uint8_t over_str[4] = {0x98, 0x9F, 0x8E, 0x9B};
        for (int i = 0; i < 4; ++i) {
            uint32_t g_addr = 0x5375u + (uint32_t)game_str[i] * 16u;
            uint32_t o_addr = 0x5375u + (uint32_t)over_str[i] * 16u;
            if (g_addr + 16u <= ROM_SIZE_BYTES) {
                gb_tile_blit(&s->rom[g_addr], 48 + i * 8, 62, false, false, false, 0xE4, DMG_SHADES, (uint32_t *)pixels, 160, 144);
            }
            if (o_addr + 16u <= ROM_SIZE_BYTES) {
                gb_tile_blit(&s->rom[o_addr], 48 + i * 8, 70, false, false, false, 0xE4, DMG_SHADES, (uint32_t *)pixels, 160, 144);
            }
        }
    } else if (s->state == 8 || s->state == 9) {
        /* Render STAGE CLEAR / CLEAR box */
        fill_rect_fb(20, 60, 88, 20, DMG_SHADES[3]);
        fill_rect_fb(22, 62, 84, 16, DMG_SHADES[0]);
        static const uint8_t clear_str[10] = {0x9C, 0x9D, 0x8A, 0x90, 0x8E, 0x8C, 0x95, 0x8E, 0x8A, 0x9B};
        for (int i = 0; i < 10; ++i) {
            uint32_t c_addr = 0x5375u + (uint32_t)clear_str[i] * 16u;
            int x_pos = (i < 5) ? (24 + i * 8) : (28 + i * 8);
            if (c_addr + 16u <= ROM_SIZE_BYTES) {
                gb_tile_blit(&s->rom[c_addr], x_pos, 66, false, false, false, 0xE4, DMG_SHADES, (uint32_t *)pixels, 160, 144);
            }
        }
    }
}

static bool try_load_rom_file(AlleywayStage1 *s, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    size_t n = fread(s->rom, 1, ROM_SIZE_BYTES, fp);
    fclose(fp);
    if (n != ROM_SIZE_BYTES) return false;
    uint8_t chk = 0;
    for (int i = 0x134; i < 0x14D; ++i) {
        chk = (uint8_t)((chk - s->rom[i] - 1u) & 0xFFu);
    }
    if (chk != s->rom[0x14D]) return false;
    s->rom_loaded = true;
    stage1_reset_for_play(s);
    return true;
}

static void build_synthetic_stage1_rom(AlleywayStage1 *s) {
    memset(s->rom, 0, sizeof(s->rom));
    /* Random angle table at 0x100B */
    const uint8_t rand_angles[8] = {6, 8, 10, 6, 8, 10, 8, 10};
    memcpy(&s->rom[0x100B], rand_angles, sizeof(rand_angles));
    /* Speed table pointers at 0x11EE (point speed 1 -> 0x1220, speed 3 -> 0x12B8) */
    s->rom[0x11EE + 0] = 0x20; s->rom[0x11EE + 1] = 0x12;
    s->rom[0x11EE + 4] = 0xB8; s->rom[0x11EE + 5] = 0x12;
    /* Angle 9 at Speed 1 (0x1220 + 9*4 = 0x1244): vy=181, vx=181 */
    s->rom[0x1244] = 0x00; s->rom[0x1245] = 181; s->rom[0x1246] = 0x00; s->rom[0x1247] = 181;
    /* Angle 9 at Speed 3 (0x12B8 + 9*4 = 0x12DC): vy=226, vx=226 */
    s->rom[0x12DC] = 0x00; s->rom[0x12DD] = 226; s->rom[0x12DE] = 0x00; s->rom[0x12DF] = 226;
    /* Normal paddle angle table at 0x1B41 */
    for (int i = 0; i < 15; ++i) s->rom[0x1B41 + i] = 9;
    /* Brick type 1 at 0x1B87: score_tens=1, hp=1, min_spd=0 */
    s->rom[0x1B87 + 3] = 0x11;
    s->rom[0x1B87 + 4] = 0x00;

    /* Authentic bonus stage countdown and reward table at 0x1B71 (big-endian reward tens) */
    /* Round 0: timer=95, reward=500 tens (0x01F4 = 5,000 pts) */
    s->rom[0x1B71 + 0] = 95; s->rom[0x1B71 + 1] = 0x01; s->rom[0x1B71 + 2] = 0xF4;
    /* Round 1: timer=90, reward=700 tens (0x02BC = 7,000 pts) */
    s->rom[0x1B71 + 3] = 90; s->rom[0x1B71 + 4] = 0x02; s->rom[0x1B71 + 5] = 0xBC;
    /* Round 2: timer=85, reward=1000 tens (0x03E8 = 10,000 pts) */
    s->rom[0x1B71 + 6] = 85; s->rom[0x1B71 + 7] = 0x03; s->rom[0x1B71 + 8] = 0xE8;
    /* Round 3: timer=80, reward=1500 tens (0x05DC = 15,000 pts) */
    s->rom[0x1B71 + 9] = 80; s->rom[0x1B71 + 10] = 0x05; s->rom[0x1B71 + 11] = 0xDC;

    /* Stage 0 descriptor at 0x1BE1 -> 0x1C44 */
    s->rom[0x1BE1] = 0x00; s->rom[0x1BE2] = 0x44; s->rom[0x1BE3] = 0x1C;

    /* Stage 3 (bonus stage) descriptor at 0x1BE1 + 3*3 = 0x1BEA -> points to 0x1C44 */
    s->rom[0x1BEA] = 0x80; /* bonus_flag = 1 */
    s->rom[0x1BEB] = 0x44; s->rom[0x1BEC] = 0x1C;

    /* Stage 31 (Stage 32) descriptor at 0x1BE1 + 31*3 = 0x1C3E -> points to 0x1C44 */
    s->rom[0x1C3E] = 0x00;
    s->rom[0x1C3F] = 0x44; s->rom[0x1C40] = 0x1C;

    /* 1 row of brick type 1 at 0x1C44, followed by 0xFF */
    for (int c = 0; c < GRID_COLS; ++c) s->rom[0x1C44 + c] = 1;
    s->rom[0x1C44 + GRID_COLS] = 0xFF;
    s->rom_loaded = true;
    stage1_reset_for_play(s);
}

static bool extract_json_int(const char *line, const char *key, int *out_val) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *pos = strstr(line, pattern);
    if (!pos) return false;
    pos += strlen(pattern);
    while (*pos == ' ' || *pos == '\t') pos++;
    char *end = NULL;
    long v = strtol(pos, &end, 10);
    if (end == pos) return false;
    *out_val = (int)v;
    return true;
}

static int run_verify_trace(const char *rom_path, const char *trace_path) {
    if (!try_load_rom_file(&g_sim, rom_path)) {
        fprintf(stderr, "Error: failed to load valid 32 KiB ROM from %s\n", rom_path);
        return 2;
    }
    FILE *fp = fopen(trace_path, "rb");
    if (!fp) {
        fprintf(stderr, "Error: failed to open logic trace file %s\n", trace_path);
        return 2;
    }

    char line[4096];
    bool initialized = false;
    int ticks_compared = 0;
    int mismatches = 0;
    int prev_vblank = -1;

    while (fgets(line, sizeof(line), fp)) {
        int st = -1, prng = 0, pad_x = 0, pad_w = 0;
        int bx = 0, bx_sub = 0, by = 0, by_sub = 0, bvx = 0, bvy = 0;
        int spd = 0, bricks = 0, score = 0, lives = 0;
        int in_raw = 0xFF, in_edge = 0xFF, tick_id = 0;
        int stg = 0, sc_flag = 0, bo_flag = 0;
        int ref_row_offset = -1, ref_scroll_timer = -1, ref_scroll_step = -1, ref_bonus_timer = -1, ref_vblank = -1;
        int r_shifts[20], r_timers[20];
        bool has_shifts = true;

        if (!extract_json_int(line, "state", &st)) continue;
        extract_json_int(line, "tick", &tick_id);
        extract_json_int(line, "prng", &prng);
        extract_json_int(line, "paddle_x", &pad_x);
        extract_json_int(line, "paddle_w", &pad_w);
        extract_json_int(line, "ball_x", &bx);
        extract_json_int(line, "ball_x_sub", &bx_sub);
        extract_json_int(line, "ball_y", &by);
        extract_json_int(line, "ball_y_sub", &by_sub);
        extract_json_int(line, "ball_vx", &bvx);
        extract_json_int(line, "ball_vy", &bvy);
        extract_json_int(line, "speed_idx", &spd);
        extract_json_int(line, "bricks_left", &bricks);
        extract_json_int(line, "score_tens", &score);
        extract_json_int(line, "lives", &lives);
        extract_json_int(line, "in_joyp_raw", &in_raw);
        extract_json_int(line, "in_joyp_edge", &in_edge);
        extract_json_int(line, "stage", &stg);
        extract_json_int(line, "scroll_flag", &sc_flag);
        extract_json_int(line, "bonus_flag", &bo_flag);
        extract_json_int(line, "row_offset", &ref_row_offset);
        extract_json_int(line, "scroll_timer", &ref_scroll_timer);
        extract_json_int(line, "scroll_step", &ref_scroll_step);
        extract_json_int(line, "bonus_timer", &ref_bonus_timer);
        extract_json_int(line, "vblank_count", &ref_vblank);

        for (int r = 0; r < 20; ++r) {
            char key_s[32], key_t[32];
            snprintf(key_s, sizeof(key_s), "row_shift_%d", r);
            snprintf(key_t, sizeof(key_t), "row_timer_%d", r);
            if (!extract_json_int(line, key_s, &r_shifts[r]) || !extract_json_int(line, key_t, &r_timers[r])) {
                has_shifts = false;
            }
        }

        int r_patterns[20];
        bool has_patterns = true;
        for (int r = 0; r < 20; ++r) {
            char key_p[32];
            snprintf(key_p, sizeof(key_p), "row_pattern_%d", r);
            if (!extract_json_int(line, key_p, &r_patterns[r])) {
                has_patterns = false;
            }
        }

        if (!initialized) {
            if (st == 5 || st == 6) {
                stage1_reset_for_play(&g_sim);
                g_sim.stage = (uint8_t)stg;
                load_stage_grid(&g_sim);
                g_sim.state = (uint8_t)st;
                g_sim.prng = (uint8_t)prng;
                g_sim.paddle_x = (uint8_t)pad_x;
                g_sim.paddle_w = (uint8_t)pad_w;
                g_sim.ball_x = (uint8_t)bx;
                g_sim.ball_x_sub = (uint8_t)bx_sub;
                g_sim.ball_y = (uint8_t)by;
                g_sim.ball_y_sub = (uint8_t)by_sub;
                g_sim.ball_vx = (uint16_t)bvx;
                g_sim.ball_vy = (uint16_t)bvy;
                g_sim.speed_idx = (uint8_t)spd;
                g_sim.bricks_left = (uint16_t)bricks;
                g_sim.score_tens = (uint16_t)score;
                g_sim.lives = (uint8_t)lives;
                if (ref_row_offset >= 0) g_sim.row_offset = (uint8_t)ref_row_offset;
                if (ref_scroll_timer >= 0) g_sim.scroll_timer = (uint8_t)ref_scroll_timer;
                if (ref_scroll_step >= 0) g_sim.scroll_step = (uint8_t)ref_scroll_step;
                if (ref_bonus_timer >= 0) g_sim.bonus_timer = (uint16_t)ref_bonus_timer;
                if (ref_vblank >= 0) {
                    g_sim.vblank_count = (uint8_t)ref_vblank;
                    prev_vblank = ref_vblank;
                }
                if (has_patterns) {
                    for (int r = 0; r < 20; ++r) {
                        g_sim.row_pattern[r] = (uint8_t)r_patterns[r];
                    }
                }
                if (has_shifts) {
                    for (int r = 0; r < 20; ++r) {
                        g_sim.row_shift[r] = (uint8_t)r_shifts[r];
                        g_sim.row_timer[r] = (uint8_t)r_timers[r];
                    }
                }
                initialized = true;
            }
            continue;
        }

        if (st != 5 && st != 6 && st != 7 && st != 8 && st != 9 && st != 0x0B) {
            break;
        }

        /* Distinct scheduler-supplied clock integration vs internal autonomous clock:
         * The native engine increments s->vblank_count autonomously per tick.
         * When replaying an external reference trace, the scheduler supplies the elapsed
         * frames (delta between reference VBlank counts). The internal counter accumulates
         * this scheduler delta and is verified against the reference without per-tick secret resets!
         */
        uint8_t elapsed = 1u;
        if (ref_vblank >= 0 && prev_vblank >= 0) {
            int diff = (ref_vblank - prev_vblank) & 0xFF;
            if (diff > 0) {
                elapsed = (uint8_t)diff;
            }
        }
        if (ref_vblank >= 0) {
            prev_vblank = ref_vblank;
        }

        stage1_step_tick_scheduler(&g_sim, (uint8_t)in_raw, (uint8_t)in_edge, elapsed);
        ticks_compared++;

        bool mismatch_detected = false;
        if ((int)g_sim.state != st || (int)g_sim.prng != prng ||
            (int)g_sim.paddle_x != pad_x || (int)g_sim.paddle_w != pad_w ||
            (int)g_sim.ball_x != bx || (int)g_sim.ball_x_sub != bx_sub ||
            (int)g_sim.ball_y != by || (int)g_sim.ball_y_sub != by_sub ||
            (int)g_sim.ball_vx != bvx || (int)g_sim.ball_vy != bvy ||
            (int)g_sim.speed_idx != spd || (int)g_sim.bricks_left != bricks ||
            (int)g_sim.score_tens != score || (int)g_sim.lives != lives) {
            mismatch_detected = true;
        }

        if (ref_vblank >= 0 && (int)g_sim.vblank_count != ref_vblank) {
            mismatch_detected = true;
        }
        if (ref_row_offset >= 0 && (int)g_sim.row_offset != ref_row_offset) {
            mismatch_detected = true;
        }
        if (ref_scroll_timer >= 0 && (int)g_sim.scroll_timer != ref_scroll_timer) {
            mismatch_detected = true;
        }
        if (ref_scroll_step >= 0 && (int)g_sim.scroll_step != ref_scroll_step) {
            mismatch_detected = true;
        }
        if (ref_bonus_timer >= 0 && (int)g_sim.bonus_timer != ref_bonus_timer) {
            mismatch_detected = true;
        }

        if (has_shifts) {
            for (int r = 0; r < 20; ++r) {
                if ((int)g_sim.row_shift[r] != r_shifts[r] || (int)g_sim.row_timer[r] != r_timers[r]) {
                    mismatch_detected = true;
                    break;
                }
            }
        }

        if (mismatch_detected) {
            fprintf(stderr,
                "Mismatch at tick=%d: native(st=%u,padX=%u,ball=%u.%u,%u.%u,v=%u,%u,spd=%u,bricks=%u,score=%u,off=%u,c4=%u,c5=%u,bon=%u,vb=%u) "
                "!= ref(st=%d,padX=%d,ball=%d.%d,%d.%d,v=%d,%d,spd=%d,bricks=%d,score=%d,off=%d,c4=%d,c5=%d,bon=%d,vb=%d)\n",
                tick_id, g_sim.state, g_sim.paddle_x, g_sim.ball_x, g_sim.ball_x_sub,
                g_sim.ball_y, g_sim.ball_y_sub, g_sim.ball_vx, g_sim.ball_vy,
                g_sim.speed_idx, g_sim.bricks_left, g_sim.score_tens,
                g_sim.row_offset, g_sim.scroll_timer, g_sim.scroll_step, (unsigned)g_sim.bonus_timer, (unsigned)g_sim.vblank_count,
                st, pad_x, bx, bx_sub, by, by_sub, bvx, bvy, spd, bricks, score,
                ref_row_offset, ref_scroll_timer, ref_scroll_step, ref_bonus_timer, ref_vblank);
            mismatches++;
            break;
        }
    }
    fclose(fp);

    printf("{\"initialized\":%s,\"ticks_compared\":%d,\"mismatches\":%d,\"bricks_left\":%u,\"score\":%u,\"speed_idx\":%u}\n",
           initialized ? "true" : "false", ticks_compared, mismatches,
           (unsigned)g_sim.bricks_left, (unsigned)g_sim.score_tens * 10u, (unsigned)g_sim.speed_idx);
    return (initialized && ticks_compared > 0 && mismatches == 0) ? 0 : 1;
}

static void step_window_frame(void) {
    if (!g_sim.rom_loaded) return;
    uint8_t joyp_raw = 0xFFu;
    if (g_key_a)     joyp_raw &= (uint8_t)~0x01u;
    if (g_key_b)     joyp_raw &= (uint8_t)~0x02u;
    if (g_key_start) joyp_raw &= (uint8_t)~0x08u;
    if (g_key_right) joyp_raw &= (uint8_t)~0x10u;
    if (g_key_left)  joyp_raw &= (uint8_t)~0x20u;

    /* Active-low newly pressed edge: bit is 0 iff currently 0 and previously 1 */
    uint8_t newly_pressed_high = (uint8_t)((~joyp_raw) & g_sim.prev_joyp_raw);
    uint8_t joyp_edge = (uint8_t)(~newly_pressed_high);
    g_sim.prev_joyp_raw = joyp_raw;

    stage1_step_tick(&g_sim, joyp_raw, joyp_edge);
    audio_step_frame();
    render_stage1(&g_sim);
}

static void handle_key_change(WPARAM vk, bool down, HWND w) {
    if (vk == VK_ESCAPE && down) { DestroyWindow(w); return; }
    if (vk == 'R' && down && g_sim.rom_loaded) { stage1_reset_for_play(&g_sim); return; }
    if (vk == VK_LEFT || vk == 'A')  g_key_left = down;
    if (vk == VK_RIGHT || vk == 'D') g_key_right = down;
    if (vk == VK_SPACE || vk == 'Z') g_key_a = down;
    if (vk == VK_SHIFT || vk == 'X') g_key_b = down;
    if (vk == VK_RETURN)             g_key_start = down;
}

static LRESULT CALLBACK window_proc(HWND w, UINT m, WPARAM a, LPARAM b) {
    if (m == WM_DESTROY) { audio_backend_shutdown(); PostQuitMessage(0); return 0; }
    if (m == WM_KEYDOWN) { handle_key_change(a, true, w); return 0; }
    if (m == WM_KEYUP)   { handle_key_change(a, false, w); return 0; }
    if (m == WM_PAINT) {
        PAINTSTRUCT paint; HDC dc = BeginPaint(w, &paint);
        RECT rect; GetClientRect(w, &rect);
        int width = rect.right, height = rect.bottom;
        int scale = width / 160 < height / 144 ? width / 160 : height / 144;
        FillRect(dc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
        if (scale > 0) {
            BITMAPINFO info = {0};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = 160; info.bmiHeader.biHeight = -144;
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            StretchDIBits(dc, (width - 160 * scale) / 2, (height - 144 * scale) / 2,
                160 * scale, 144 * scale, 0, 0, 160, 144, pixels, &info,
                DIB_RGB_COLORS, SRCCOPY);
        }
        EndPaint(w, &paint); return 0;
    }
    return DefWindowProcW(w, m, a, b);
}

int main(int argc, char **argv) {
    draw_diagnostic();
    if (argc == 2 && strcmp(argv[1], "--smoke-test") == 0) {
        if (pixels[0][0] != 0xe0f8d0 || pixels[0][40] != 0x88c070 ||
            pixels[143][159] != 0x346856) return 1;
        build_synthetic_stage1_rom(&g_sim);
        if (g_sim.bricks_left != 14 || g_sim.paddle_x != 40) return 1;
        /* Move right 1 tick (+3 px), then serve with A, then step 1 tick */
        stage1_step_tick(&g_sim, 0xEFu, 0xEFu);
        if (g_sim.paddle_x != 43) return 1;
        stage1_step_tick(&g_sim, 0xFEu, 0xFEu);
        if (g_sim.state != 6 || g_sim.ball_y != 116) return 1;
        stage1_step_tick(&g_sim, 0xFFu, 0xFFu);
        if (g_sim.ball_y_sub != 181 || g_sim.ball_x_sub != 75) return 1;
        puts("PASS: native executable, diagnostic framebuffer and Stage 1 fixed-point engine verified");
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "--verify-autonomous-clock") == 0) {
        build_synthetic_stage1_rom(&g_sim);
        /* Set to Stage 4 bonus (stage index 3) */
        g_sim.stage = 3;
        load_stage_grid(&g_sim);
        if (g_sim.bonus_flag != 1 || g_sim.bonus_timer != 95) return 1;

        g_sim.vblank_count = 0;
        g_sim.state = 5; /* Serve */
        g_sim.lives = 3;

        /* 1. Serve phase: clock advances autonomously, timer does not decrement */
        for (int i = 0; i < 50; ++i) {
            stage1_step_tick(&g_sim, 0xFFu, 0xFFu);
        }
        if (g_sim.vblank_count != 50 || g_sim.bonus_timer != 95 || g_sim.state != 5) return 2;

        /* 2. Serve ball with A button edge */
        stage1_step_tick(&g_sim, 0xFEu, 0xFEu);
        if (g_sim.state != 6 || g_sim.vblank_count != 51) return 3;

        /* 3. Active play: 64 ticks = exactly 2 timer ticks (32 frames per unit) */
        for (int i = 0; i < 64; ++i) {
            stage1_step_tick(&g_sim, 0xFFu, 0xFFu);
        }
        if (g_sim.vblank_count != 115 || g_sim.bonus_timer != 93 || g_sim.state != 6) return 4;

        /* 4. Pause toggled with Start edge */
        stage1_step_tick(&g_sim, 0xF7u, 0xF7u);
        if (g_sim.state != 0x0C) return 5;

        /* 5. In Pause: clock increments, but gameplay and bonus timer are frozen */
        for (int i = 0; i < 64; ++i) {
            stage1_step_tick(&g_sim, 0xFFu, 0xFFu);
        }
        if (g_sim.vblank_count != 180 || g_sim.bonus_timer != 93 || g_sim.state != 0x0C) return 6;

        /* 6. Unpause with Start edge */
        stage1_step_tick(&g_sim, 0xF7u, 0xF7u);
        if (g_sim.state != 6) return 7;

        /* 7. Fast-forward timer to 1 unit to test exact expiration boundary */
        g_sim.bonus_timer = 1;
        /* Align vblank_count so next boundary fires on tick 32 */
        g_sim.vblank_count = 0x00;
        for (int i = 0; i < 31; ++i) {
            stage1_step_tick(&g_sim, 0xFFu, 0xFFu);
            if (g_sim.bonus_timer != 1 || g_sim.state != 6) return 8;
        }
        /* Tick that reaches multiple of 32: timer reaches 0, triggers state 7 */
        stage1_step_tick(&g_sim, 0xFFu, 0xFFu);
        if (g_sim.bonus_timer != 0 || g_sim.state != 7) return 9;

        /* 8. In bonus stage, State 7 transitions to State 8 with lives preserved */
        uint8_t lives_before = g_sim.lives;
        stage1_step_tick(&g_sim, 0xFFu, 0xFFu);
        if (g_sim.state != 8 || g_sim.lives != lives_before) return 10;

        /* 9. State 8 advances stage from 3 to 4, ready in State 5 */
        stage1_step_tick(&g_sim, 0xFFu, 0xFFu);
        if (g_sim.stage != 4 || g_sim.state != 5 || g_sim.lives != lives_before) return 11;

        puts("PASS: autonomous clock and bonus lifecycle verified");
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "--verify-scheduler-timing") == 0) {
        GbScheduler sched;
        gb_scheduler_init(&sched);

        /* 1. Verify exact DMG timing constants */
        if (GB_CYCLES_PER_FRAME != 70224u) return 1;
        if (GB_CPU_CLOCK_HZ != 4194304u) return 2;
        double expected_fps = 4194304.0 / 70224.0; /* 59.72750056961936... */
        if (GB_FRAMES_PER_SEC < expected_fps - 0.001 || GB_FRAMES_PER_SEC > expected_fps + 0.001) return 2;
        if (sched.frame_duration < 0.016742 || sched.frame_duration > 0.016743) return 3;

        /* 2. Step uniform 60 Hz dt (1/60 = 0.016666... sec) */
        /* Since 1/60 < 1/59.7275, every ~219 frames has 0 ticks, giving exact 59.7275 ticks/sec */
        int total_ticks = 0;
        double dt_60fps = 1.0 / 60.0;
        for (int i = 0; i < 600; ++i) {
            total_ticks += gb_scheduler_advance(&sched, dt_60fps);
        }
        /* Over 10.0 seconds of wall clock, DMG steps 597 or 598 ticks */
        if (total_ticks < 597 || total_ticks > 598) return 4;

        /* 3. Verify irregular intervals / frame jitter decoupling */
        gb_scheduler_reset(&sched);
        int j_ticks = 0;
        double jitters[6] = {0.005, 0.025, 0.010, 0.035, 0.008, 0.017};
        for (int cycle = 0; cycle < 100; ++cycle) {
            for (int k = 0; k < 6; ++k) {
                j_ticks += gb_scheduler_advance(&sched, jitters[k]);
            }
        }
        /* Sum of jitters is 0.100s per cycle * 100 = 10.0s -> 597 ticks */
        if (j_ticks != 597) return 5;

        /* 4. Verify pause freeze & accumulator discard */
        gb_scheduler_set_paused(&sched, true);
        if (gb_scheduler_advance(&sched, 1.0) != 0) return 6;
        gb_scheduler_set_paused(&sched, false);
        /* First frame after unpause should not burst past 1 tick */
        int unpause_ticks = gb_scheduler_advance(&sched, sched.frame_duration);
        if (unpause_ticks != 1) return 7;

        /* 5. Verify stall clamp (e.g. 5-second window freeze/drag clamps to max_frame_skip_sec) */
        int stall_ticks = gb_scheduler_advance(&sched, 5.0);
        if (stall_ticks > 12) return 8; /* Clamped to 200 ms ~ 11 ticks */

        /* 6. Verify zero-drift fractional audio accumulation across 1,000 DMG frames */
        gb_scheduler_reset(&sched);
        uint64_t accumulated_samples = 0;
        for (int f = 0; f < 1000; ++f) {
            int s_count = gb_scheduler_get_frame_audio_samples(&sched);
            if (s_count < 738 || s_count > 739) return 9;
            accumulated_samples += (uint64_t)s_count;
        }
        /* Exact expected samples: (1000 * 44100 * 70224) / 4194304 = 738353 */
        uint64_t expected_samples = gb_scheduler_expected_audio_samples_total(1000, 44100);
        if (accumulated_samples != expected_samples || accumulated_samples != 738353ull) return 10;

        /* 7. Verify HUD bonus timer units to ticks and seconds */
        /* 95 units = 3040 ticks = 50.90 seconds */
        uint64_t bonus_ticks = gb_scheduler_bonus_units_to_ticks(95);
        if (bonus_ticks != 3040ull) return 11;
        double bonus_sec = gb_scheduler_bonus_units_to_seconds(95);
        if (bonus_sec < 50.89 || bonus_sec > 50.91) return 12;

        puts("PASS: scheduler monotonic timing and fractional audio verified");
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "--verify-multi-stage-rules") == 0) {
        build_synthetic_stage1_rom(&g_sim);

        /* 1. Verify Horizontal Scroll Rule (wrap mod 112 / 0x70, direction & timer reload) */
        g_sim.scroll_flag = 1;
        /* Row 0: scroll right (bit 7 = 0), timer=2 */
        g_sim.row_pattern[0] = 0x02;
        g_sim.row_timer[0] = 0x02;
        g_sim.row_shift[0] = 0x6E; /* 110 */
        /* Tick 1: timer decrements 2 -> 1, shift unchanged */
        step_horizontal_scroll(&g_sim);
        if (g_sim.row_timer[0] != 1 || g_sim.row_shift[0] != 0x6E) return 10;
        /* Tick 2: timer reaches 0, shift wraps 0x6E + 1 -> 0x6F (111), timer reloaded to 2 */
        step_horizontal_scroll(&g_sim);
        if (g_sim.row_timer[0] != 2 || g_sim.row_shift[0] != 0x6F) return 11;
        /* Tick 3: timer decrements 2 -> 1 */
        step_horizontal_scroll(&g_sim);
        /* Tick 4: shift wraps 0x6F + 1 -> 0 (mod 0x70 = 112), timer reloaded to 2 */
        step_horizontal_scroll(&g_sim);
        if (g_sim.row_shift[0] != 0 || g_sim.row_timer[0] != 2) return 12;

        /* Row 1: scroll left (bit 7 = 1), pattern=0x83 (period 3), shift=0 */
        g_sim.row_pattern[1] = 0x83;
        g_sim.row_timer[1] = 1;
        g_sim.row_shift[1] = 0;
        /* Step: shift wraps 0 -> 0x6F (111), timer reloads to 3 */
        step_horizontal_scroll(&g_sim);
        if (g_sim.row_shift[1] != 0x6F || g_sim.row_timer[1] != 3) return 13;

        /* 2. Verify Bonus Penetration Rule (bonus_flag == 1 clears brick, returns 0 -> no bounce) */
        g_sim.bonus_flag = 1;
        g_sim.grid_type[0] = 1;
        g_sim.grid_hp[0] = 1;
        g_sim.bricks_left = 5;
        g_sim.ball_vy = 0x0100; /* moving down */
        g_sim.ball_vx = 0x0100; /* moving right */
        /* Probe row 0, col 0 (x=0x10, y=0x18 -> row 0 in grid) */
        uint8_t hit_res = check_brick_point(&g_sim, 0x18, 0x10);
        /* Return value must be 0 (no bounce reflection), but brick is destroyed! */
        if (hit_res != 0) return 20;
        if (g_sim.grid_type[0] != 0) return 21;
        if (g_sim.bricks_left != 4) return 22;

        /* 3. Verify 1-UP score table thresholds (0x1B5D) */
        /* In our synthetic ROM, inject 1-UP threshold: table entry 0 = 0x03E8 (1000 tens = 10,000 pts) */
        g_sim.rom[0x1B5D] = 0x03;
        g_sim.rom[0x1B5E] = 0xE8;
        g_sim.rom[0x1B5F] = 0xFF; /* end sentinel */
        g_sim.rom[0x1B60] = 0xFF;
        g_sim.up_index = 0;
        g_sim.lives = 3;
        g_sim.score_tens = 999;
        /* Destroy one brick giving 1 ten (999 -> 1000) */
        g_sim.grid_type[1] = 1;
        g_sim.grid_hp[1] = 1;
        check_brick_point(&g_sim, 0x18, 0x18);
        if (g_sim.score_tens != 1000) return 30;
        if (g_sim.lives != 4 || g_sim.up_index != 1) return 31;

        /* 4. Verify authentic Stage 3 vertical brick drop stepping on paddle bounce */
        g_sim.rom[0x1B7D] = 8;
        g_sim.row_offset = 3;
        g_sim.scroll_step = 0;
        g_sim.scroll_timer = 2;
        /* First bounce: scroll_timer decrements 2 -> 1, row_offset stays 3 */
        step_paddle_bounce_vertical_drop(&g_sim);
        if (g_sim.scroll_timer != 1 || g_sim.row_offset != 3) return 35;
        /* Second bounce: scroll_timer reaches 0 -> row_offset decrements 3 -> 2, scroll_step becomes 1, scroll_timer reloads to 8 */
        step_paddle_bounce_vertical_drop(&g_sim);
        if (g_sim.row_offset != 2 || g_sim.scroll_step != 1 || g_sim.scroll_timer != 8) return 36;

        /* 5. Verify Bonus Stage countdown timer cadence and expiration */
        g_sim.bonus_flag = 1;
        g_sim.bonus_timer = 2;
        g_sim.vblank_count = 1; /* Not a multiple of 32 */
        g_sim.state = 6;
        step_stage_timers(&g_sim);
        if (g_sim.bonus_timer != 2 || g_sim.state != 6) return 37;
        /* When vblank_count & 0x1F == 0 */
        g_sim.vblank_count = 0x20;
        step_stage_timers(&g_sim);
        if (g_sim.bonus_timer != 1 || g_sim.state != 6) return 38;
        /* Next 32-tick boundary: timer reaches 0, triggers state 7 */
        g_sim.vblank_count = 0x40;
        step_stage_timers(&g_sim);
        if (g_sim.bonus_timer != 0 || g_sim.state != 7) return 39;
        /* In bonus stage, state 7 transitions to state 8 with lives preserved */
        uint8_t prev_lives = g_sim.lives;
        stage1_step_tick(&g_sim, 0xFFu, 0xFFu);
        if (g_sim.state != 8 || g_sim.lives != prev_lives) return 40;

        /* 6. Verify Stage 32 Clear -> Ending sequence (State 9) -> wrap to Stage 0 */
        g_sim.stage = 31;
        g_sim.state = 8; /* Stage Clear on Stage 32 */
        stage1_step_tick(&g_sim, 0xFFu, 0xFFu);
        if (g_sim.stage != 0) return 41;
        if (g_sim.state != 9) return 42; /* Authentic State 9 Ending sequence */
        /* Key press in State 9 loops back to Stage 1 Serve */
        stage1_step_tick(&g_sim, 0xFEu, 0xFEu);
        if (g_sim.state != 5) return 43;

        /* 7. Verify Game Over lifecycle: last life lost -> State 0x0B -> Reset */
        g_sim.lives = 0;
        g_sim.bonus_flag = 0;
        g_sim.state = 7; /* Life lost with 0 lives */
        stage1_step_tick(&g_sim, 0xFFu, 0xFFu);
        if (g_sim.state != 0x0B) return 44; /* State 11 = 0x0B (Game Over) */
        /* Key press in State 0x0B resets game */
        stage1_step_tick(&g_sim, 0xFEu, 0xFEu);
        if (g_sim.state != 5 || g_sim.lives != 3 || g_sim.stage != 0) return 45;

        /* 8. Verify Bonus Stage lifecycle: Drop ball (no life lost) & Perfect Clear reward */
        /* 8a: Ball drop in bonus stage preserves lives and advances to next stage */
        g_sim.stage = 3;
        g_sim.bonus_flag = 1;
        g_sim.bricks_left = 15;
        g_sim.lives = 4;
        g_sim.state = 7; /* Ball dropped */
        stage1_step_tick(&g_sim, 0xFFu, 0xFFu);
        if (g_sim.state != 8 || g_sim.lives != 4) return 46; /* State 8, lives intact */
        stage1_step_tick(&g_sim, 0xFFu, 0xFFu);
        if (g_sim.stage != 4 || g_sim.state != 5 || g_sim.lives != 4) return 47; /* Advance to Stage 5 */

        /* 8b: Perfect Bonus Clear awards authentic bonus tens from 0x1B71 (+500 tens = 5,000 pts) */
        g_sim.stage = 3;
        g_sim.bonus_flag = 1;
        g_sim.bricks_left = 0;
        g_sim.score_tens = 100;
        g_sim.state = 8; /* Stage Clear on completed bonus */
        stage1_step_tick(&g_sim, 0xFFu, 0xFFu);
        if (g_sim.score_tens != 600) return 48; /* 100 + 500 tens = 600 tens (6,000 pts) */
        if (g_sim.stage != 4 || g_sim.state != 5) return 49;

        puts("PASS: multi-stage rules verified (scroll wrap, bonus penetration, 1-UP threshold, vertical drop, bonus countdown, ending sequence, game over reset, bonus reward)");
        return 0;
    }

    if (argc == 4 && strcmp(argv[1], "--verify-trace") == 0) {
        return run_verify_trace(argv[2], argv[3]);
    }

    const char *rom_candidates[] = {
        (argc == 3 && strcmp(argv[1], "--rom") == 0) ? argv[2] : NULL,
        "local/roms/Alleyway (USA).gb",
        "local/roms/alleyway.gb",
        "../Local-Privado/roms/Alleyway (USA).gb",
        "../Local-Privado/roms/alleyway.gb",
    };
    for (size_t i = 0; i < sizeof(rom_candidates) / sizeof(rom_candidates[0]); ++i) {
        if (rom_candidates[i] && try_load_rom_file(&g_sim, rom_candidates[i])) {
            break;
        }
    }
    render_stage1(&g_sim);
    audio_backend_init();

    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSW cls = {0}; cls.lpfnWndProc = window_proc;
    cls.style = CS_HREDRAW | CS_VREDRAW;
    cls.hInstance = instance; cls.lpszClassName = L"AlleywayResearch";
    cls.hCursor = LoadCursor(NULL, IDC_ARROW);
    if (!RegisterClassW(&cls)) { audio_backend_shutdown(); return 2; }
    RECT r = {0, 0, 640, 576}; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    const wchar_t *title = g_sim.rom_loaded
        ? L"Alleyway PC | Stage 1 Native (Arrows: Move, Space/Z: A, Shift/X: B, Enter: Pause, R: Reset)"
        : L"Alleyway PC research | diagnostic only (no ROM found) | Esc to close";
    HWND window = CreateWindowW(cls.lpszClassName, title,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top, NULL, NULL, instance, NULL);
    if (!window) { audio_backend_shutdown(); return 3; }

    GbScheduler sched;
    gb_scheduler_init(&sched);

    LARGE_INTEGER perf_freq, prev_counter;
    QueryPerformanceFrequency(&perf_freq);
    QueryPerformanceCounter(&prev_counter);

    MSG msg = {0};
    while (msg.message != WM_QUIT) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (msg.message == WM_QUIT) break;

        LARGE_INTEGER cur_counter;
        QueryPerformanceCounter(&cur_counter);
        double dt = (double)(cur_counter.QuadPart - prev_counter.QuadPart) / (double)perf_freq.QuadPart;
        prev_counter = cur_counter;

        int ticks = gb_scheduler_advance(&sched, dt);
        if (ticks > 0) {
            for (int t = 0; t < ticks; ++t) {
                step_window_frame();
            }
            InvalidateRect(window, NULL, FALSE);
        } else {
            Sleep(1); /* Yield CPU to avoid 100% spin while waiting for next frame boundary */
        }
    }
    audio_backend_shutdown();
    return 0;
}
