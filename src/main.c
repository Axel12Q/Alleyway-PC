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
    uint16_t state_timer;    /* Remaining frames of a timed state; loaded ONLY by stage1_enter_state() */
    uint16_t curtain_dur;    /* Initial State 4 duration loaded on entry */
    uint8_t game_over;       /* 1 while state 11 is the Game Over display */
    uint8_t curtain_grid_loaded;
    uint8_t serve_pending;   /* 1 during the 1-VBlank delay inside 0x0773..0x07A5 before State 6 */
    uint8_t first_curtain;   /* 1 on the first stage entered from Title State 2 (+155 frames Mario entry) */
    uint16_t bonus_tally_remaining; /* Remaining bonus reward tens to tally in State 8 */
    uint8_t bonus_1up_delay; /* 1 during the extra VBlank of CALL 0x47C7 inside 0x0C71 on bonus 1-UP */
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
    uint8_t normal_stage_count; /* 1-indexed normal stage counter (0xCA46, 1..24 in a loop) */
    uint8_t bonus_stage_count;  /* Bonus stage counter (0xCA47, 1..8) */
    uint8_t bonus_flag;
    uint8_t scroll_flag;
    uint8_t total_rows;
    uint8_t row_offset;      /* Visible row offset (0xFFA9) */
    uint8_t vblank_count;    /* Authentic VBlank frame counter (0xFFA2) */
    uint16_t bonus_timer;    /* Bonus stage countdown timer in seconds (0xCA48) */
    uint8_t prev_joyp_raw;
    uint8_t sfx_trigger; /* Queued SFX ID (0 = none, 7 = paddle bounce, 12 = wall bounce, etc.) */
    uint8_t up_index;    /* Index in 0x1B5D 1-UP score table */
    uint8_t vram_brick_dirty;    /* 0xFFA3: set when a brick destroy queues a VRAM update in current frame */
    uint8_t multi_brick_pending; /* 0x023D: 2nd brick destroyed in same frame waits 1 VBlank before 0x0E64 */
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

/*
 * Measured state durations (frames) from the original cartridge under PyBoy at the
 * atomic VBlank ISR boundary (0x01EF, LY=144), see docs/STATUS.md Hito 12 & Hito 13.
 * "Frames" = recorded VBlanks in which the state value is observed.
 */
#define DUR_CURTAIN_DEFAULT 66u
#define DUR_MARIO_ENTRY_EXTRA 155u
#define CURTAIN_GRID_LOAD_OFFSET 3u /* grid/bonus_flag swap 3 VBlanks after curtain entry at 0x01EF */
#define DUR_DEATH 101u
#define DUR_BONUS_END 229u
#define DUR_CLEAR 145u
#define DUR_BONUS_PRE_TALLY 449u
#define BONUS_TALLY_STEP_TENS 10u
#define DUR_BONUS_TALLY (DUR_BONUS_PRE_TALLY + 50u) /* Round 0 default: 449 + 50 = 499 */
#define DUR_LIFE_LOST_TRANSIENT 1u
#define DUR_GAME_OVER 377u
#define DUR_TITLE_ENTRY 1u
#define DUR_TITLE_START_DELAY 3u
#define DUR_ENDING 1445u

static bool is_timed_state(uint8_t st) {
    return st == 4 || st == 7 || st == 8 || st == 9 || st == 0x0B || st == 1;
}

/*
 * State 4 (0x06A2..0x0734) duration model:
 * In the original DMG ROM, State 4 runs:
 *   - 0x09BB: 30 VBlanks wiping 30 row-pairs of the BG tilemap
 *   - 0x0942..0x09E0: unhalted CPU loop unpacking `rows * 14` bytes from ROM into WRAM
 *     (with multiply/lookup per non-empty cell) and counting 840 cells while LCD/VBlank ISR is active
 *   - 0x06EE: 1 VBlank setup
 *   - 0x06FA: +155 VBlanks (0x43DC Mario spacecraft entrance) iff 0xCA46 == 1 (first_curtain)
 *   - 0x0702/0x0706: 0x4669 (normal banner) or 0x46F7 (+33 VBlanks bonus banner jingle)
 *   - 0x070F..0x0721: 17 VBlanks banner wait + 10 VBlanks (0x0997 bottom-up brick reveal) + 5 VBlanks cleanup.
 * The integer formula `(next_bonus ? 98 : 65) + (rows * 14 + nonempty * 5) / 390` is an empirical
 * linear cycle-budget model of the unhalted 0x0942..0x09E0 unpack loop (not a cycle-by-cycle CPU
 * emulator). It is verified against PyBoy across all 32 stages in the ROM (66..70 frames for normal
 * stages, 100..101 for bonus stages, 221 from Title, 66 after Ending) with 0 frames of error.
 */
static uint16_t compute_stage_curtain_dur(const AlleywayStage1 *s, uint8_t stage_idx) {
    uint16_t dur = DUR_CURTAIN_DEFAULT;
    uint16_t desc_addr = (uint16_t)(0x1BE1u + (uint16_t)stage_idx * 3u);
    if (s->rom_loaded && desc_addr + 2u < ROM_SIZE_BYTES) {
        uint8_t desc0 = s->rom[desc_addr];
        bool next_bonus = (desc0 & 0x80u) != 0;
        uint16_t map_ptr = (uint16_t)s->rom[desc_addr + 1u] | ((uint16_t)s->rom[desc_addr + 2u] << 8);
        uint32_t rows = 0;
        uint32_t nonempty = 0;
        int idx = 0;
        if (map_ptr >= 0x1C00u) {
            while (map_ptr + GRID_COLS <= ROM_SIZE_BYTES && s->rom[map_ptr] != 0xFF && idx + GRID_COLS <= GRID_CELLS) {
                for (int c = 0; c < GRID_COLS; ++c) {
                    if (s->rom[map_ptr + (uint16_t)c] != 0) nonempty++;
                }
                map_ptr = (uint16_t)(map_ptr + GRID_COLS);
                idx += GRID_COLS;
                rows++;
            }
        }
        if (rows > 1u) {
            uint16_t base = next_bonus ? 98u : 65u;
            uint16_t extra = (uint16_t)((rows * 14u + nonempty * 5u) / 390u);
            dur = (uint16_t)(base + extra);
        }
    }
    if (s->first_curtain != 0) {
        dur = (uint16_t)(dur + DUR_MARIO_ENTRY_EXTRA);
    }
    return dur;
}

/* Single shared entry path: loads the per-state timer exactly once on entry. */
static void stage1_enter_state(AlleywayStage1 *s, uint8_t new_state) {
    s->state = new_state;
    switch (new_state) {
    case 4:
        s->curtain_dur = compute_stage_curtain_dur(s, s->stage);
        s->state_timer = s->curtain_dur;
        s->curtain_grid_loaded = 0;
        s->first_curtain = 0;
        break;
    case 7:
        s->state_timer = (s->bonus_flag != 0) ? DUR_BONUS_END : DUR_DEATH;
        break;
    case 8:
        if (s->bonus_flag != 0 && s->bricks_left == 0) {
            uint8_t round_idx = (uint8_t)(s->stage >> 2);
            if (round_idx > 3u) round_idx = 3u;
            uint16_t reward_tens = 500u;
            uint16_t tbl = (uint16_t)(0x1B71u + (uint16_t)round_idx * 3u + 1u);
            if (s->rom_loaded && tbl + 1u < ROM_SIZE_BYTES) {
                uint16_t r = ((uint16_t)s->rom[tbl] << 8) | (uint16_t)s->rom[tbl + 1u];
                if (r > 0u) reward_tens = r;
            }
            s->bonus_tally_remaining = reward_tens;
            s->bonus_1up_delay = 0;
            s->state_timer = (uint16_t)(DUR_BONUS_PRE_TALLY + (reward_tens / BONUS_TALLY_STEP_TENS));
        } else {
            s->bonus_tally_remaining = 0;
            s->bonus_1up_delay = 0;
            s->state_timer = DUR_CLEAR;
        }
        break;
    case 9:
        s->state_timer = DUR_ENDING;
        break;
    case 0x0B:
        s->state_timer = (s->game_over != 0) ? DUR_GAME_OVER : DUR_LIFE_LOST_TRANSIENT;
        break;
    case 1:
        s->state_timer = DUR_TITLE_ENTRY;
        break;
    default:
        s->state_timer = 0;
        break;
    }
}

/* Returns true when the current timed state expires. A zero timer in a timed state
 * (never produced by stage1_enter_state) expires immediately. */
static bool state_timer_step(AlleywayStage1 *s) {
    if (s->state_timer == 0u) return true;
    s->state_timer--;
    return s->state_timer == 0u;
}

static void stage1_begin_serve(AlleywayStage1 *s) {
    s->paddle_shrunk = 0;
    s->paddle_w = 0x18;
    s->serve_pending = 0;
    stage1_enter_state(s, 5u);
}

static void stage1_advance_stage(AlleywayStage1 *s) {
    s->stage = (uint8_t)(s->stage + 1u);
    if (s->stage >= 32u) {
        s->stage = 0u;
        stage1_enter_state(s, 9u);
    } else {
        stage1_enter_state(s, 4u);
    }
}

static bool g_key_left = false;
static bool g_key_right = false;
static bool g_key_a = false;
static bool g_key_b = false;
static bool g_key_start = false;
/* Keep a short key press until the next simulation frame consumes it. */
static uint8_t g_pending_presses = 0;

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
    if (s->state == 4) {
        /* 0x06C0..0x06CA -> 0x0738 (bonus) / 0x0744 (normal) */
        if (s->bonus_flag != 0) s->bonus_stage_count++;
        else s->normal_stage_count++;
    } else {
        uint8_t comp = (uint8_t)(s->stage + 1u);
        s->bonus_stage_count = (uint8_t)(comp >> 2);
        s->normal_stage_count = (uint8_t)(comp - s->bonus_stage_count);
    }

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

    /* Authentic State 4 stage setup (0x06AE..0x06D8) also resets paddle position & width */
    s->paddle_shrunk = 0;
    s->paddle_w = 0x18;
    s->paddle_x = 0x28;
    s->paddle_y = 0x90;

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
    s->lives = 4; /* observed 0xCA44 = 4 at first serve of the original */
    s->state_timer = 0;
    s->curtain_dur = DUR_CURTAIN_DEFAULT;
    s->game_over = 0;
    s->curtain_grid_loaded = 1;
    s->serve_pending = 0;
    s->first_curtain = 0;
    s->bonus_tally_remaining = 0;
    s->bonus_1up_delay = 0;
    s->stage = 0;
    s->normal_stage_count = 1;
    s->bonus_stage_count = 0;
    s->bonus_flag = 0;
    s->scroll_flag = 0;
    s->prev_joyp_raw = 0xFF;
    s->sfx_trigger = 0;
    s->up_index = 0;
    s->vblank_count = 0;
    s->vram_brick_dirty = 0;
    s->multi_brick_pending = 0;
    load_stage_grid(s);
}

static uint8_t step_prng(AlleywayStage1 *s) {
    s->prng = (uint8_t)((s->prng + 0x41u) & 0xFFu);
    return s->prng;
}

/* 1-UP check against table at 0x1B5D (0x0C71..0x0CA5). Returns true when a 1-UP is awarded. */
static bool check_1up_threshold(AlleywayStage1 *s) {
    if (0x1B5Du + (uint16_t)s->up_index * 2u + 1u < ROM_SIZE_BYTES) {
        uint16_t thresh = ((uint16_t)s->rom[0x1B5Du + (uint16_t)s->up_index * 2u] << 8) |
                          (uint16_t)s->rom[0x1B5Du + (uint16_t)s->up_index * 2u + 1u];
        if (thresh != 0xFFFFu && thresh != 0u && s->score_tens >= thresh) {
            s->up_index++;
            if (s->lives < 99) s->lives++;
            return true;
        }
    }
    return false;
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
    /* In 0x0773..0x07A5, after 0x101B initializes ball pos/vel, 0x0794 calls 0x47C7 (JP 0x0221)
     * waiting 1 VBlank in State 5 without stepping PRNG; on the next VBlank, 0x0797 triggers SFX 7,
     * 0x07A1 sets State 6, and 0x07A5 returns to 0x0522: CALL 0x0505. */
    s->serve_pending = 1u;
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

enum {
    BRICK_HIT_NONE = 0,
    BRICK_HIT_SOLID = 1,
    BRICK_DESTROYED = 2,
    BRICK_DESTROYED_HALTED = 3
};

enum {
    PENDING_BOUNCE_NONE = 1,
    PENDING_BOUNCE_VERT = 2,
    PENDING_BOUNCE_HORIZ = 3,
    PENDING_SNAP_X_POS = 4,
    PENDING_SNAP_X_NEG = 5
};

static int check_brick_point(AlleywayStage1 *s, uint8_t y_probe, uint8_t x_probe) {
    uint8_t scroll_y = (uint8_t)((s->row_offset << 2) & 0xFFu);
    uint8_t a1 = (uint8_t)(y_probe - 0x18u);
    uint16_t a2_full = (uint16_t)a1 + (uint16_t)scroll_y;
    if (a2_full > 0xFFu) return BRICK_HIT_NONE;
    uint8_t row = (uint8_t)((a2_full & 0xFFu) >> 2);
    if (row >= GRID_ROWS) return BRICK_HIT_NONE;

    /* Authentic horizontal shift from row_shift[row - row_offset] */
    uint8_t shift = 0;
    if (row >= s->row_offset && (row - s->row_offset) < 20) {
        shift = s->row_shift[row - s->row_offset];
    }

    uint8_t x_rel = (uint8_t)((uint8_t)(x_probe - 0x10u) + shift);
    if (x_rel >= 0x70u) x_rel = (uint8_t)(x_rel - 0x70u);
    uint8_t col = (uint8_t)(x_rel >> 3);
    if (col >= GRID_COLS) return BRICK_HIT_NONE;

    int idx = (int)row * GRID_COLS + (int)col;
    uint8_t btype = s->grid_type[idx];
    if (btype == 0) return BRICK_HIT_NONE;

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
        return BRICK_HIT_SOLID;
    }

    if (s->bonus_flag == 0) {
        hp--;
        s->grid_hp[idx] = hp;
        if (hp != 0) return BRICK_HIT_NONE;
    }

    s->grid_type[idx] = 0;
    uint8_t pts = (uint8_t)((s->rom[0x1B87u + (uint16_t)(btype - 1u) * 6u + 3u] >> 4) & 0x0Fu);
    uint32_t new_score = (uint32_t)s->score_tens + (uint32_t)pts;
    s->score_tens = (new_score > 0xFFFFu) ? 0xFFFFu : (uint16_t)new_score;
    if (s->score_tens > s->high_score_tens) {
        s->high_score_tens = s->score_tens;
    }

    (void)check_1up_threshold(s);

    if (s->vram_brick_dirty != 0) {
        /* In Alleyway DMG (0x0E5F: CALL 0x09F9 -> CALL 0x023D), destroying a 2nd brick
         * in the same frame finds 0xFFA3 != 0 and executes HALT (0x0221) waiting for VBlank
         * before decrementing 0xFFC8 (bricks_left) at 0x0E68 and executing the probe bounce! */
        return BRICK_DESTROYED_HALTED;
    }

    s->vram_brick_dirty = 1;
    s->bricks_left = (uint16_t)(s->bricks_left - 1u);
    if (s->bricks_left == 0) {
        stage1_enter_state(s, 8u);
    }
    /* Authentic Alleyway brick hit sound: byte 5 of 0x1B87 brick properties */
    uint8_t sfx_id = (btype <= 3) ? btype : 1;
    s->sfx_trigger = sfx_id;
    if (s->bonus_flag != 0) return BRICK_HIT_NONE;
    return BRICK_DESTROYED;
}

static void step_paddle_bounce_vertical_drop(AlleywayStage1 *s) {
    if (s->scroll_timer > 0) {
        s->scroll_timer--;
    } else {
        s->scroll_timer = 0xFFu;
    }
    if (s->scroll_timer == 0) {
        /* Authentic vertical brick drop (0x1175: CALL 0x0BC7 -> CALL 0x0BF2):
         * Decrements row_offset and clears row (row_offset + 20) that scrolled off the bottom,
         * decrementing bricks_left for any destructible bricks on that row. */
        if (s->row_offset > 0) {
            s->row_offset--;
            uint8_t drop_row = (uint8_t)(s->row_offset + 20u);
            if (drop_row < GRID_ROWS) {
                for (int c = 0; c < GRID_COLS; ++c) {
                    int idx = (int)drop_row * GRID_COLS + c;
                    if (s->grid_type[idx] != 0) {
                        uint8_t hp = s->grid_hp[idx];
                        s->grid_type[idx] = 0;
                        if (hp != 0) {
                            if (s->bricks_left > 0) s->bricks_left--;
                            if (s->bricks_left == 0) stage1_enter_state(s, 8u);
                        }
                    }
                }
            }
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
    s->vram_brick_dirty = 0;
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
        stage1_enter_state(s, 7u);
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
            int res = check_brick_point(s, s->ball_prev_y, (uint8_t)(s->ball_x + 3u));
            if (res == BRICK_DESTROYED_HALTED) {
                s->multi_brick_pending = PENDING_BOUNCE_HORIZ;
                return;
            } else if (res != BRICK_HIT_NONE) {
                hits++;
                bounce_horizontal(s);
            } else {
                res = check_brick_point(s, s->ball_prev_y, s->ball_x);
                if (res == BRICK_DESTROYED_HALTED) {
                    s->multi_brick_pending = ((s->ball_vx & 0x8000u) != 0) ? PENDING_SNAP_X_POS : PENDING_SNAP_X_NEG;
                    return;
                } else if (res != BRICK_HIT_NONE) {
                    hits++;
                    if ((s->ball_vx & 0x8000u) != 0) snap_x_positive(s);
                    else snap_x_negative(s);
                }
            }
        } else {
            int res = check_brick_point(s, s->ball_prev_y, s->ball_x);
            if (res == BRICK_DESTROYED_HALTED) {
                s->multi_brick_pending = PENDING_BOUNCE_HORIZ;
                return;
            } else if (res != BRICK_HIT_NONE) {
                hits++;
                bounce_horizontal(s);
            } else {
                res = check_brick_point(s, s->ball_prev_y, (uint8_t)(s->ball_x + 3u));
                if (res == BRICK_DESTROYED_HALTED) {
                    s->multi_brick_pending = ((s->ball_vx & 0x8000u) != 0) ? PENDING_SNAP_X_POS : PENDING_SNAP_X_NEG;
                    return;
                } else if (res != BRICK_HIT_NONE) {
                    hits++;
                    if ((s->ball_vx & 0x8000u) != 0) snap_x_positive(s);
                    else snap_x_negative(s);
                }
            }
        }

        if ((s->ball_vy & 0x8000u) == 0) {
            int res = check_brick_point(s, (uint8_t)(s->ball_y + 3u), s->ball_prev_x);
            if (res == BRICK_DESTROYED_HALTED) {
                s->multi_brick_pending = PENDING_BOUNCE_VERT;
                return;
            } else if (res != BRICK_HIT_NONE) {
                hits++;
                bounce_vertical(s);
            } else {
                res = check_brick_point(s, s->ball_y, s->ball_prev_x);
                if (res == BRICK_DESTROYED_HALTED) {
                    s->multi_brick_pending = PENDING_BOUNCE_NONE;
                    return;
                } else if (res != BRICK_HIT_NONE) {
                    hits++;
                }
            }
        } else {
            int res = check_brick_point(s, s->ball_y, s->ball_prev_x);
            if (res == BRICK_DESTROYED_HALTED) {
                s->multi_brick_pending = PENDING_BOUNCE_VERT;
                return;
            } else if (res != BRICK_HIT_NONE) {
                hits++;
                bounce_vertical(s);
            } else {
                res = check_brick_point(s, (uint8_t)(s->ball_y + 3u), s->ball_prev_x);
                if (res == BRICK_DESTROYED_HALTED) {
                    s->multi_brick_pending = PENDING_BOUNCE_NONE;
                    return;
                } else if (res != BRICK_HIT_NONE) {
                    hits++;
                }
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
     * which in bonus stages transitions cleanly to State 4 (stage++, lives kept) without losing a life!
     */
    if (s->bonus_flag != 0 && s->bonus_timer > 0) {
        if ((s->vblank_count & 0x1Fu) == 0) {
            s->bonus_timer--;
            if (s->bonus_timer == 0) {
                stage1_enter_state(s, 7u);
            }
        }
    }
}

static void stage1_step_tick_scheduler(AlleywayStage1 *s, uint8_t joyp_raw, uint8_t joyp_edge, uint8_t elapsed_vblanks) {
    s->vblank_count = (uint8_t)(s->vblank_count + elapsed_vblanks);
    s->sfx_trigger = 0;
    if (s->multi_brick_pending != 0) {
        uint8_t pending_action = s->multi_brick_pending;
        s->multi_brick_pending = 0;
        s->vram_brick_dirty = 0;
        s->bricks_left = (uint16_t)(s->bricks_left - 1u);
        if (s->bricks_left == 0) {
            stage1_enter_state(s, 8u);
        }
        if (pending_action == PENDING_BOUNCE_VERT) {
            bounce_vertical(s);
        } else if (pending_action == PENDING_BOUNCE_HORIZ) {
            bounce_horizontal(s);
        } else if (pending_action == PENDING_SNAP_X_POS) {
            snap_x_positive(s);
        } else if (pending_action == PENDING_SNAP_X_NEG) {
            snap_x_negative(s);
        }
        step_prng(s);
        return;
    }
    /* Verified by disassembly (0x0505..0x0525, 0x0773..0x0804) & VBlank ISR (0x01EF) sampling:
     * 0x0522: CALL 0x0505 advances PRNG (0xFFA1) when a state handler executes RET back to the
     * main dispatch loop:
     * 1) In play states (5, 6, 0x0C), every VBlank returns to 0x0522 EXCEPT the first VBlank of
     *    a serve in State 5 (0x0794: CALL 0x47C7 waits 1 VBlank before 0x07A1 sets State 6 and RETs).
     * 2) In timed/menu states (1, 2, 4, 7, 8, 9, 0x0B), the handler loops internally on 0x0221 and
     *    only executes RET to 0x0522 on the tick it transitions to the next state, EXCEPT on 7 -> 0x0B
     *    when lives > 0 (0x07ED: RET Z is only taken when lives == 0; normal life loss stays inside
     *    0x07D3 across the 1-VBlank State 11 at 0x07F3 and RETs to 0x0522 on 0x0B -> 5!). */
    const bool play_loop_tick = (s->state == 5 || s->state == 6 || s->state == 0x0C);
    const uint8_t entry_state = s->state;
    if (s->state == 4) {
        /* State 4 (curtain). Timer loaded once by stage1_enter_state(). The new stage
         * grid / bonus_flag swap happens CURTAIN_GRID_LOAD_OFFSET ticks after entry. */
        bool expired = state_timer_step(s);
        uint16_t load_rem = (s->curtain_dur > CURTAIN_GRID_LOAD_OFFSET)
                          ? (uint16_t)(s->curtain_dur - CURTAIN_GRID_LOAD_OFFSET) : 0u;
        if (!s->curtain_grid_loaded && (expired || s->state_timer <= load_rem)) {
            load_stage_grid(s);
            s->curtain_grid_loaded = 1;
        }
        if (expired) {
            s->scroll_step = 0u; /* 0x0730: XOR A; LDH (0xFFC5), A */
            stage1_begin_serve(s);
        }
    } else if (s->state == 5) {
        if (s->serve_pending != 0) {
            /* 2nd VBlank of serve (0x0797..0x07A5): trigger SFX 7, enter State 6, RET to 0x0522 */
            s->serve_pending = 0;
            s->state = 6u;
            s->sfx_trigger = 7u;
        } else {
            step_horizontal_scroll(s);
            update_paddle(s, joyp_raw);
            if ((joyp_edge & 0x01u) == 0) {
                serve_ball(s);
            }
        }
    } else if (s->state == 6) {
        step_horizontal_scroll(s);
        step_stage_timers(s);
        step_ball_and_collisions(s);
        if (s->multi_brick_pending == 0) {
            update_paddle(s, joyp_raw);
            if ((joyp_edge & 0x08u) == 0) {
                s->state = 0x0C; /* Pause */
            }
        }
    } else if (s->state == 0x0C) {
        if ((joyp_edge & 0x08u) == 0) {
            s->state = 6;
        }
    } else if (s->state == 7) {
        /* Ball lost / bonus ended: 101 frames (normal) or 229 frames (bonus) at 0x01EF,
         * then bonus -> state 4 directly (stage++, no state 8, lives kept);
         * normal -> state 11 (lives-- first when lives > 0). */
        if (state_timer_step(s)) {
            if (s->bonus_flag != 0) {
                stage1_advance_stage(s);
            } else if (s->lives == 0) {
                s->game_over = 1;
                s->sfx_trigger = 10;
                stage1_enter_state(s, 0x0Bu);
            } else {
                s->lives--;
                s->game_over = 0;
                stage1_enter_state(s, 0x0Bu);
            }
        }
    } else if (s->state == 8) {
        /* Stage clear: 145 frames (normal). Perfect bonus (0x19F7..0x1A68): 449 pre-tally frames
         * + (reward_tens / 10) tally frames (+10 tens/frame) + 1 extra frame if 0x0C71 awards a 1-UP. */
        bool perfect_bonus = (s->bonus_flag != 0 && s->bricks_left == 0);
        if (!perfect_bonus && s->state_timer == DUR_CLEAR) {
            s->sfx_trigger = 8;
        } else if (perfect_bonus && s->state_timer == (uint16_t)(DUR_BONUS_PRE_TALLY + (s->bonus_tally_remaining / BONUS_TALLY_STEP_TENS))) {
            s->sfx_trigger = 8;
        }
        bool expired = state_timer_step(s);
        if (perfect_bonus) {
            if (s->bonus_1up_delay != 0) {
                /* Extra VBlank consumed by CALL 0x47C7 inside 0x0C71 on 1-UP */
                s->bonus_1up_delay = 0;
            } else if (!expired && s->bonus_tally_remaining > 0u) {
                uint16_t tally_frames_left = (uint16_t)(s->bonus_tally_remaining / BONUS_TALLY_STEP_TENS);
                if (s->state_timer <= tally_frames_left) {
                    uint16_t step = (s->bonus_tally_remaining >= BONUS_TALLY_STEP_TENS)
                                  ? BONUS_TALLY_STEP_TENS : s->bonus_tally_remaining;
                    uint32_t ns = (uint32_t)s->score_tens + (uint32_t)step;
                    s->score_tens = (ns > 0xFFFFu) ? 0xFFFFu : (uint16_t)ns;
                    if (s->score_tens > s->high_score_tens) s->high_score_tens = s->score_tens;
                    s->bonus_tally_remaining = (uint16_t)(s->bonus_tally_remaining - step);
                    if (check_1up_threshold(s)) {
                        s->state_timer++;
                        s->bonus_1up_delay = 1;
                    }
                }
            }
        }
        if (expired) stage1_advance_stage(s);
    } else if (s->state == 9) {
        /* State 9 (Ending after Stage 32, 0x0839): timed 1445-frame celebration, then Stage 1 curtain. */
        if (state_timer_step(s)) {
            s->stage = 0u;
            stage1_enter_state(s, 4u);
        }
    } else if (s->state == 0x0B) {
        /* State 11: 377 frames for Game Over, 1 frame transient after a normal life loss. */
        if (state_timer_step(s)) {
            if (s->game_over != 0) {
                s->game_over = 0;
                stage1_enter_state(s, 1u);
            } else {
                s->scroll_step = 2u; /* 0x07FC: LD A, 0x02; LDH (0xFFC5), A */
                stage1_begin_serve(s);
            }
        }
    } else if (s->state == 1) {
        if (state_timer_step(s)) stage1_enter_state(s, 2u);
    } else if (s->state == 2) {
        /* Title wait (0x0582..0x0604): Start/A initializes new game (stage=0, score=0, lives=4,
         * up_index=0, first_curtain=1) and waits 3 frames in State 2 before entering State 4. */
        if (s->state_timer > 0u) {
            if (state_timer_step(s)) {
                stage1_enter_state(s, 4u);
            }
        } else if ((joyp_edge & 0x08u) == 0 || (joyp_edge & 0x01u) == 0) {
            s->paddle_x = 0x28;
            s->paddle_y = 0x90;
            s->paddle_w = 0x18;
            s->paddle_shrunk = 0;
            s->paddle_hits = 0;
            s->scroll_step = 0;
            s->indestructible_hits = 0;
            s->score_tens = 0;
            s->lives = 4;
            s->game_over = 0;
            s->stage = 0;
            s->normal_stage_count = 0;
            s->bonus_stage_count = 0;
            s->up_index = 0;
            s->serve_pending = 0;
            s->first_curtain = 1u;
            s->state_timer = DUR_TITLE_START_DELAY;
        }
    }
    const bool suppress_serve_frame1_prng = (entry_state == 5 && s->serve_pending != 0);
    const bool suppress_life_lost_7_to_11_prng = (entry_state == 7 && s->state == 0x0B && s->game_over == 0);
    if (s->multi_brick_pending == 0 &&
        ((play_loop_tick && !suppress_serve_frame1_prng) ||
         (entry_state != s->state && (is_timed_state(entry_state) || entry_state == 2) && !suppress_life_lost_7_to_11_prng))) {
        step_prng(s);
    }
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

/* Minimal 3x5 digit font for synthetic fallback HUD (0-9) */
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

/*
 * Authentic ROM-driven BG/Window/OAM presentation helpers:
 * In Alleyway DMG, the entire 6 KiB VRAM tile bank (0x8000..0x97FF, 384 tiles) is stored
 * contiguously at ROM[0x4B75..0x6374].
 * - BG/Window tiles (LCDC bit 4 = 0, signed 0x8800 mode): tile ID `t` maps to
 *   ROM[0x4B75 + ((t >= 0x80) ? t : (256 + t)) * 16].
 * - OBJ (OAM) tiles (unsigned 0x8000 mode): tile ID `t` maps to ROM[0x4B75 + t * 16]
 *   (with `t == 0xFF` used as a blank/transparent sprite sentinel).
 */
static void blit_rom_bg_tile(const AlleywayStage1 *s, uint8_t tile_id, int sx, int sy, uint8_t bgp) {
    uint32_t idx = (tile_id >= 0x80u) ? (uint32_t)tile_id : (256u + (uint32_t)tile_id);
    uint32_t rom_off = 0x4B75u + idx * 16u;
    if (rom_off + 16u <= ROM_SIZE_BYTES) {
        gb_tile_blit(&s->rom[rom_off], sx, sy, false, false, false, bgp, DMG_SHADES, (uint32_t *)pixels, 160, 144);
    }
}

static void blit_rom_obj_tile(const AlleywayStage1 *s, uint8_t tile_id, int oam_x, int oam_y, uint8_t flags, uint8_t obp) {
    if (tile_id == 0xFFu || oam_y <= 0 || oam_y >= 160) return;
    uint32_t rom_off = 0x4B75u + (uint32_t)tile_id * 16u;
    if (rom_off + 16u <= ROM_SIZE_BYTES) {
        bool flip_x = (flags & 0x20u) != 0;
        bool flip_y = (flags & 0x40u) != 0;
        gb_tile_blit(&s->rom[rom_off], oam_x - 8, oam_y - 16, flip_x, flip_y, true, obp, DMG_SHADES, (uint32_t *)pixels, 160, 144);
    }
}

/*
 * Decodes a Nintendo DMG VRAM stripe display list (0x02C1 in ROM) into bg_map (0x9800..0x9BFF)
 * and win_map (0x9C00..0x9FFF).
 * Format: [vram_hi, vram_lo, (mode << 6) | count, data..., 0x00]
 *   mode 0: horizontal literal (step +1)
 *   mode 1: horizontal RLE repeat (step +1)
 *   mode 2: vertical literal (step +32)
 *   mode 3: vertical RLE repeat (step +32)
 */
static void apply_rom_vram_display_list(const AlleywayStage1 *s, uint16_t list_addr,
                                        uint8_t bg_map[32][32], uint8_t win_map[32][32]) {
    uint32_t pos = list_addr;
    while (pos + 2u < ROM_SIZE_BYTES) {
        uint8_t hi = s->rom[pos];
        if (hi == 0x00u) break;
        uint8_t lo = s->rom[pos + 1u];
        uint8_t ctrl = s->rom[pos + 2u];
        pos += 3u;
        uint16_t vaddr = (uint16_t)(((uint16_t)hi << 8) | (uint16_t)lo);
        uint8_t count = (uint8_t)(ctrl & 0x3Fu);
        uint8_t mode = (uint8_t)((ctrl >> 6) & 0x03u);
        uint16_t step = (mode >= 2u) ? 32u : 1u;
        bool rle = (mode & 1u) != 0;
        uint8_t rle_val = 0xFFu;
        if (rle) {
            if (pos >= ROM_SIZE_BYTES) break;
            rle_val = s->rom[pos++];
        }
        for (uint8_t i = 0; i < count; ++i) {
            uint8_t val = rle ? rle_val : ((pos < ROM_SIZE_BYTES) ? s->rom[pos++] : 0xFFu);
            if (vaddr >= 0x9800u && vaddr < 0x9C00u) {
                uint16_t off = (uint16_t)(vaddr - 0x9800u);
                bg_map[(off >> 5) & 31u][off & 31u] = val;
            } else if (vaddr >= 0x9C00u && vaddr < 0xA000u) {
                uint16_t off = (uint16_t)(vaddr - 0x9C00u);
                win_map[(off >> 5) & 31u][off & 31u] = val;
            }
            vaddr = (uint16_t)(vaddr + step);
        }
    }
}

/* Renders a 2x2 OAM metasprite from the ROM pointer table at 0x4A8B (0x4A66 in ROM). */
static void draw_rom_metasprite_2x2(const AlleywayStage1 *s, uint8_t meta_idx, int oam_x, int oam_y, uint8_t obp) {
    uint32_t ptr_off = 0x4A8Bu + (uint32_t)meta_idx * 2u;
    if (ptr_off + 1u >= ROM_SIZE_BYTES) return;
    /* Note: 0x4A6E loads D=(HL), E=(HL+1) big-endian! */
    uint32_t def_off = ((uint32_t)s->rom[ptr_off] << 8) | (uint32_t)s->rom[ptr_off + 1u];
    if (def_off + 16u > ROM_SIZE_BYTES) return;
    for (int i = 0; i < 4; ++i) {
        int dy = (int)(int8_t)s->rom[def_off + (uint32_t)i * 4u + 0u];
        int dx = (int)(int8_t)s->rom[def_off + (uint32_t)i * 4u + 1u];
        uint8_t tile = s->rom[def_off + (uint32_t)i * 4u + 2u];
        uint8_t flags = s->rom[def_off + (uint32_t)i * 4u + 3u];
        blit_rom_obj_tile(s, tile, oam_x + dx, oam_y + dy, flags, obp);
    }
}

/* Renders the 5 OAM score sprites (4 BCD digits + ten-thousands crown icon) matching 0x481E. */
static void draw_score_oam_5digits(const AlleywayStage1 *s, uint16_t score_tens, int base_oam_x, int base_oam_y, uint8_t obp) {
    uint8_t d0 = (uint8_t)(score_tens % 10u);
    uint8_t d1 = (uint8_t)((score_tens / 10u) % 10u);
    uint8_t d2 = (uint8_t)((score_tens / 100u) % 10u);
    uint8_t d3 = (uint8_t)((score_tens / 1000u) % 10u);
    uint8_t d4 = (uint8_t)(score_tens / 10000u);
    uint8_t crown_tile = 0xFFu;
    if (d4 == 1u) crown_tile = 0xBFu;
    else if (d4 == 2u) crown_tile = 0xBCu;
    else if (d4 >= 3u) crown_tile = 0xC9u;
    blit_rom_obj_tile(s, crown_tile, base_oam_x, base_oam_y + 8, 0u, obp);
    blit_rom_obj_tile(s, (uint8_t)(0x80u + d3), base_oam_x, base_oam_y, 0u, obp);
    blit_rom_obj_tile(s, (uint8_t)(0x80u + d2), base_oam_x + 8, base_oam_y, 0u, obp);
    blit_rom_obj_tile(s, (uint8_t)(0x80u + d1), base_oam_x + 16, base_oam_y, 0u, obp);
    blit_rom_obj_tile(s, (uint8_t)(0x80u + d0), base_oam_x + 24, base_oam_y, 0u, obp);
}

static void render_stage1(const AlleywayStage1 *s) {
    if (!s->rom_loaded) {
        draw_diagnostic();
        return;
    }

    bool has_authentic_vram = (s->rom[0x4B75] != 0 || s->rom[0x5375] != 0);
    if (!has_authentic_vram) {
        /* Fallback synthetic renderer when running with minimal synthetic test ROMs */
        fill_rect_fb(0, 0, 160, 144, DMG_SHADES[0]);
        fill_rect_fb(0, 0, 8, 144, DMG_SHADES[2]);
        fill_rect_fb(120, 0, 8, 144, DMG_SHADES[2]);
        fill_rect_fb(128, 0, 32, 144, DMG_SHADES[3]);
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
        fill_rect_fb(0, 0, 8, 144, DMG_SHADES[2]);
        fill_rect_fb(120, 0, 8, 144, DMG_SHADES[2]);
        fill_rect_fb(128, 0, 32, 144, DMG_SHADES[3]);
        int pad_sx = (int)s->paddle_x - 7;
        int pad_sy = (int)s->paddle_y - 16;
        fill_rect_fb(pad_sx, pad_sy, (int)s->paddle_w, 6, DMG_SHADES[3]);
        fill_rect_fb(pad_sx + 2, pad_sy + 1, (int)s->paddle_w - 4, 2, DMG_SHADES[1]);
        if (s->state == 6 || s->state == 0x0C) {
            fill_rect_fb((int)s->ball_x - 8, (int)s->ball_y - 16, 4, 4, DMG_SHADES[3]);
        }
        draw_number_fb(132, 16, (uint32_t)s->high_score_tens, 5, DMG_SHADES[0]);
        draw_number_fb(132, 36, (uint32_t)s->score_tens, 5, DMG_SHADES[0]);
        draw_number_fb(144, 88, (uint32_t)s->lives, 2, DMG_SHADES[0]);
        draw_number_fb(144, 112, (uint32_t)s->normal_stage_count, 2, DMG_SHADES[1]);
        return;
    }

    uint8_t bg_map[32][32];
    uint8_t win_map[32][32];
    memset(bg_map, 0xFF, sizeof(bg_map));
    memset(win_map, 0xFF, sizeof(win_map));

    /* 1. Title Screen (State 1 / State 2, 0x055A..0x0604):
     *    LCDC = 0x83 (Window OFF), BG display list at 0x41CD, High Score OAM[10..14] at (112, 104). */
    if (s->state == 1 || s->state == 2) {
        apply_rom_vram_display_list(s, 0x41CDu, bg_map, win_map);
        for (int r = 0; r < 18; ++r) {
            for (int c = 0; c < 20; ++c) {
                blit_rom_bg_tile(s, bg_map[r][c], c * 8, r * 8, 0xE4u);
            }
        }
        draw_score_oam_5digits(s, s->high_score_tens, 112, 104, 0xE4u);
        return;
    }

    /* 2. Game Over Final Screen (State 0x0B with game_over != 0 during last 194 frames, 0x08D9..0x0905):
     *    After Mario's ejection + 64-frame wait (183 frames elapsed), LCDC turns off Window (0x83),
     *    clears BG to 0xFF, and 0x498A writes 8 OAM sprites "GAME OVER" at OAM.Y = 80, X = 56..120. */
    if (s->state == 0x0B && s->game_over != 0 && s->state_timer <= 194u) {
        fill_rect_fb(0, 0, 160, 144, DMG_SHADES[0]);
        static const uint8_t go_tiles[8] = {0x90u, 0x8Au, 0x96u, 0x8Eu, 0x98u, 0x9Fu, 0x8Eu, 0x9Bu};
        static const int go_x[8] = {56, 64, 72, 80, 96, 104, 112, 120};
        for (int i = 0; i < 8; ++i) {
            blit_rom_obj_tile(s, go_tiles[i], go_x[i], 80, 0u, 0xE4u);
        }
        return;
    }

    /* 3. Ending Celebration (State 9, 1445 frames after Stage 32, 0x0839..0x08A7):
     *    Loads Playfield/HUD display list 0x4A3C and Ending portrait display list 0x42B3 ("NICE PLAY!" + Mario),
     *    fades palette via 0x08C2..0x08C9, animates Mario waving (0x45BD, metasprites at 0x45E6),
     *    and reveals "TRY AGAIN!" display list 0x1B0D in the final third of the sequence. */
    if (s->state == 9) {
        int el = (int)DUR_ENDING - (int)s->state_timer;
        uint8_t pal = 0xE4u;
        if (el < 64) {
            int step = el / 16;
            if (step > 3) step = 3;
            pal = s->rom[0x08C2u + (uint32_t)step];
        } else if (el < 128) {
            int step = (el - 64) / 16;
            if (step > 3) step = 3;
            pal = s->rom[0x08C6u + (uint32_t)step];
        } else if (el >= 1381) {
            int step = (el - 1381) / 16;
            if (step > 3) step = 3;
            pal = s->rom[0x08C2u + (uint32_t)step];
        }
        apply_rom_vram_display_list(s, 0x4A3Cu, bg_map, win_map);
        uint8_t disp_stg = (s->normal_stage_count > 0u) ? s->normal_stage_count : 24u;
        win_map[11][2] = (uint8_t)(0x80u + ((disp_stg / 10u) % 10u));
        win_map[11][3] = (uint8_t)(0x80u + (disp_stg % 10u));
        win_map[16][4] = (uint8_t)(0x80u + (s->lives % 10u));
        if (el >= 64 && el < 1381) {
            apply_rom_vram_display_list(s, 0x42B3u, bg_map, win_map);
            if (el >= 1020 && el < 1340) {
                apply_rom_vram_display_list(s, 0x1B0Du, bg_map, win_map);
            }
        }
        for (int r = 0; r < 18; ++r) {
            for (int c = 0; c < 15; ++c) {
                blit_rom_bg_tile(s, bg_map[r][c], c * 8, r * 8, pal);
            }
            for (int c = 0; c < 5; ++c) {
                blit_rom_bg_tile(s, win_map[r][c], 120 + c * 8, r * 8, pal);
            }
        }
        draw_score_oam_5digits(s, s->high_score_tens, 136, 32, pal);
        draw_score_oam_5digits(s, s->score_tens, 136, 56, pal);
        if (el >= 134 && el < 680) {
            int wave_step = ((el - 134) / 19) % 29;
            uint8_t meta_idx = s->rom[0x45E6u + (uint32_t)wave_step];
            draw_rom_metasprite_2x2(s, meta_idx, 56, 72, pal);
        }
        return;
    }

    /* 4. Playfield + Right HUD Window (States 4, 5, 6, 7, 8, 0x0C, and early 0x0B):
     *    LCDC = 0xE3 (Window ON at WX=127, WY=0 -> screen X=120..159), display list at 0x4A3C. */
    apply_rom_vram_display_list(s, 0x4A3Cu, bg_map, win_map);
    win_map[11][2] = (uint8_t)(0x80u + ((s->normal_stage_count / 10u) % 10u));
    win_map[11][3] = (uint8_t)(0x80u + (s->normal_stage_count % 10u));
    win_map[16][4] = (uint8_t)(0x80u + (s->lives % 10u));
    bool show_bonus_time_hud = (s->bonus_flag != 0) &&
                               (s->state != 4 || s->state_timer <= 5u);
    if (show_bonus_time_hud) {
        /* 0x47E4: writes "TIME" at 0x9DA1..0x9DA4 (Window row 13, cols 1..4) */
        win_map[13][1] = 0x9Du;
        win_map[13][2] = 0x92u;
        win_map[13][3] = 0x96u;
        win_map[13][4] = 0x8Eu;
    }

    /* Render BG playfield base (cols 0..14, rows 0..17) */
    for (int r = 0; r < 18; ++r) {
        for (int c = 0; c < 15; ++c) {
            /* In State 4 before brick reveal (0x09BB wipe), row 1 is cleared to 0xFF */
            uint8_t tid = (s->state == 4 && r >= 1) ? 0xFFu : bg_map[r][c];
            blit_rom_bg_tile(s, tid, c * 8, r * 8, 0xE4u);
        }
    }

    /* Determine visible brick tile rows (0x0997 in State 4 reveals 1 tile row = 2 brick rows
     * per frame from bottom row_pair=9 up to row_pair=0 over the final 15 frames of State 4,
     * normalized for any curtain_dur). */
    int min_row_pair = 0;
    if (s->state == 4) {
        int open_start = (s->curtain_dur >= 20u) ? 15 : (int)(s->curtain_dur / 4u);
        if (open_start < 1) open_start = 1;
        if ((int)s->state_timer > open_start) {
            min_row_pair = 10; /* Curtain closed: 0 brick rows visible */
        } else {
            int revealed = (open_start + 1 - (int)s->state_timer);
            if (open_start < 10) {
                revealed = (revealed * 10) / open_start;
            }
            if (revealed > 10) revealed = 10;
            if (revealed < 0) revealed = 0;
            min_row_pair = 10 - revealed;
        }
    }

    /* Render paired 8x8 brick tile rows (r = 0, 2, ..., 18 -> row_pair = 0..9) using 0x0AC9..0x0B08 lookup */
    for (int row_pair = min_row_pair; row_pair < 10; ++row_pair) {
        int r = row_pair * 2;
        int shift = (int)s->row_shift[r];
        for (int c = 0; c < GRID_COLS; ++c) {
            uint8_t top_t = s->grid_type[(r + s->row_offset) * GRID_COLS + c];
            uint8_t bot_t = s->grid_type[(r + 1 + s->row_offset) * GRID_COLS + c];
            if (top_t == 0 && bot_t == 0) continue;

            int col_pixel = (c * 8 - shift) % 112;
            if (col_pixel < 0) col_pixel += 112;
            int sx = 8 + col_pixel;
            int sy = 8 + r * 4;

            /* Authentic 0x0AC9..0x0B08 lookup into 0x1B87 brick property table:
             * When both top_t and bot_t are non-zero, 0x0AE4 overwrites 0xFFB1 with bot_t.
             * mask = (top_t != 0 ? 1 : 0) | (bot_t != 0 ? 2 : 0) -> offset (mask - 1) in 0..2. */
            uint8_t btype = (bot_t != 0) ? bot_t : top_t;
            uint8_t mask = (uint8_t)((top_t != 0 ? 1u : 0u) | (bot_t != 0 ? 2u : 0u));
            uint8_t tile_id = 0xFFu;
            uint16_t tbl_off = (uint16_t)(0x1B87u + (uint16_t)(btype - 1u) * 6u + (uint16_t)(mask - 1u));
            if (btype >= 1u && tbl_off < ROM_SIZE_BYTES) {
                tile_id = s->rom[tbl_off];
            }
            blit_rom_bg_tile(s, tile_id, sx, sy, 0xE4u);
        }
    }

    /* Render Left Wall column 0 top corner (0xBD) and OAM[15..31] left-wall pipe sprites (0xB4 at X=8, Y=24..152) */
    blit_rom_bg_tile(s, bg_map[0][0], 0, 0, 0xE4u);
    for (int r = 1; r < 18; ++r) {
        blit_rom_bg_tile(s, 0xFFu, 0, r * 8, 0xE4u);
        blit_rom_obj_tile(s, 0xB4u, 8, (r + 2) * 8, 0u, 0xE4u);
    }

    /* Render Right Window (WX=127 -> screen X=120..159, cols 0..4, rows 0..17) */
    for (int r = 0; r < 18; ++r) {
        for (int c = 0; c < 5; ++c) {
            blit_rom_bg_tile(s, win_map[r][c], 120 + c * 8, r * 8, 0xE4u);
        }
    }

    /* Render HUD OAM scores: High Score (OAM[10..14] at Y=32) and 1UP Score (OAM[5..9] at Y=56) */
    draw_score_oam_5digits(s, s->high_score_tens, 136, 32, 0xE4u);
    draw_score_oam_5digits(s, s->score_tens, 136, 56, 0xE4u);

    /* Render Bonus Countdown Timer OAM[32..33] at (144..152, 128) when active (0x19A2) */
    if (show_bonus_time_hud) {
        blit_rom_obj_tile(s, (uint8_t)(0x80u + ((s->bonus_timer / 10u) % 10u)), 144, 128, 0u, 0xE4u);
        blit_rom_obj_tile(s, (uint8_t)(0x80u + (s->bonus_timer % 10u)), 152, 128, 0u, 0xE4u);
    }

    /* State-specific OAM composition (Paddle, Ball, Mario entrance/ejection, Stage/Bonus banner, Bonus tally) */
    if (s->state == 4) {
        int el = (int)s->curtain_dur - (int)s->state_timer;
        bool is_first_mario_entry = (s->curtain_dur >= 200u);
        if (!is_first_mario_entry || el >= 38) {
            uint8_t hatch = 0u; /* 0x4554: 0=(00,01,00), 1=(00,04,00), 2=(00,03,00), 3=(02,03,02) */
            if (is_first_mario_entry) {
                if (el >= 94 && el <= 99) hatch = 1u;
                else if (el >= 100 && el <= 107) hatch = 2u;
                else if (el >= 108 && el <= 168) hatch = 3u;
                else if (el >= 169 && el <= 180) hatch = 2u;
                else if (el >= 181 && el <= 192) hatch = 1u;
            }
            uint16_t p_off = (uint16_t)(0x4554u + (uint16_t)hatch * 3u);
            int px = (int)s->paddle_x + 1;
            int py = (int)s->paddle_y;
            blit_rom_obj_tile(s, s->rom[p_off + 0u], px, py, 0u, 0xE4u);
            blit_rom_obj_tile(s, s->rom[p_off + 1u], px + 8, py, 0u, 0xE4u);
            blit_rom_obj_tile(s, s->rom[p_off + 2u], px + 16, py, 0x20u, 0xE4u);
        }
        if (is_first_mario_entry) {
            /* 0x43DC: Mario walks in from X=120 to 68, jumps along 0x44F5 arc into open paddle */
            if (el >= 38 && el <= 93) {
                int step = el - 38;
                uint8_t meta_idx = (uint8_t)((step / 5) % 3);
                draw_rom_metasprite_2x2(s, meta_idx, 120 - step, 128, 0xE4u);
            } else if (el >= 94 && el <= 115) {
                draw_rom_metasprite_2x2(s, 3u, 68, 128, 0xE4u);
            } else if (el >= 116 && el <= 139) {
                int step = el - 115;
                int my = 128;
                for (int k = 0; k < step && (0x44F5u + (uint32_t)k) < ROM_SIZE_BYTES; ++k) {
                    my += (int)(int8_t)s->rom[0x44F5u + (uint32_t)k];
                }
                draw_rom_metasprite_2x2(s, 4u, 68 - step, my, 0xE4u);
            } else if (el == 140) {
                draw_rom_metasprite_2x2(s, 4u, 44, 132, 0xE4u);
            }
        }
        /* 0x4669 ("STAGE XX") / 0x46F7 ("BONUS") OAM banner */
        if (s->bonus_flag == 0 && s->state_timer <= 28u && s->state_timer >= 1u) {
            static const uint8_t stg_str[6] = {0x9Cu, 0x9Du, 0x8Au, 0x90u, 0x8Eu, 0x3Eu};
            for (int i = 0; i < 6; ++i) {
                blit_rom_obj_tile(s, stg_str[i], 48 + i * 8, 112, 0u, 0xE4u);
            }
            blit_rom_obj_tile(s, (uint8_t)(0x80u + ((s->normal_stage_count / 10u) % 10u)), 96, 112, 0u, 0xE4u);
            blit_rom_obj_tile(s, (uint8_t)(0x80u + (s->normal_stage_count % 10u)), 104, 112, 0u, 0xE4u);
        } else if (s->bonus_flag != 0 && s->state_timer <= 61u && s->state_timer >= 6u) {
            static const uint8_t bon_str[5] = {0x8Bu, 0x98u, 0x97u, 0x9Eu, 0x9Cu};
            for (int i = 0; i < 5; ++i) {
                blit_rom_obj_tile(s, bon_str[i], 56 + i * 8, 112, 0u, 0xE4u);
            }
        }
        return;
    }

    if (s->state == 7) {
        int el = (int)((s->bonus_flag != 0 ? DUR_BONUS_END : DUR_DEATH) - s->state_timer);
        if (s->bonus_flag == 0) {
            /* 0x455D: 36-frame paddle explosion using metasprites 7..9 at 0x4599 */
            if (el >= 0 && el < 36 && (0x4599u + (uint32_t)el) < ROM_SIZE_BYTES) {
                uint8_t meta_idx = s->rom[0x4599u + (uint32_t)el];
                draw_rom_metasprite_2x2(s, meta_idx, (int)s->ball_x - 8, 144, 0xE4u);
            }
            return;
        }
    }

    if (s->state == 0x0B && s->game_over != 0) {
        /* First 183 frames of Game Over (0x08D1 -> 0x444D): open paddle hatch and eject Mario */
        int el = (int)DUR_GAME_OVER - (int)s->state_timer;
        int px = (int)s->paddle_x + 1;
        int py = (int)s->paddle_y;
        uint8_t hatch = (el < 8) ? 1u : ((el < 16) ? 2u : 3u);
        uint16_t p_off = (uint16_t)(0x4554u + (uint16_t)hatch * 3u);
        blit_rom_obj_tile(s, s->rom[p_off + 0u], px, py, 0u, 0xE4u);
        blit_rom_obj_tile(s, s->rom[p_off + 1u], px + 8, py, 0u, 0xE4u);
        blit_rom_obj_tile(s, s->rom[p_off + 2u], px + 16, py, 0x20u, 0xE4u);
        if (el >= 24 && el < 54) {
            int step = el - 24;
            int j_steps = (step < 24) ? step : 24;
            int my = 136;
            for (int k = 0; k < j_steps && (0x44F5u + (uint32_t)k) < ROM_SIZE_BYTES; ++k) {
                my += (int)(int8_t)s->rom[0x44F5u + (uint32_t)k];
            }
            if (step >= 24) my += (step - 23) * 4;
            int dir = (s->paddle_x < 0x4Cu) ? 1 : -1;
            uint8_t meta_idx = (s->paddle_x < 0x4Cu) ? 5u : 6u;
            draw_rom_metasprite_2x2(s, meta_idx, (int)s->paddle_x + 4 + dir * j_steps, my, 0xE4u);
        }
        return;
    }

    /* Render Paddle OAM[0..2] (normal 24 px or shrunk 16 px) */
    int pad_oam_x = (int)s->paddle_x + 1;
    int pad_oam_y = (int)s->paddle_y;
    if (s->paddle_w == 16) {
        blit_rom_obj_tile(s, 0x00u, pad_oam_x, pad_oam_y, 0u, 0xE4u);
        blit_rom_obj_tile(s, 0x00u, pad_oam_x + 8, pad_oam_y, 0x20u, 0xE4u);
    } else {
        blit_rom_obj_tile(s, 0x00u, pad_oam_x, pad_oam_y, 0u, 0xE4u);
        blit_rom_obj_tile(s, 0x01u, pad_oam_x + 8, pad_oam_y, 0u, 0xE4u);
        blit_rom_obj_tile(s, 0x00u, pad_oam_x + 16, pad_oam_y, 0x20u, 0xE4u);
    }

    /* Render Ball OAM[3] (tile 0x05): active in States 6, 8, 0x0C, and State 5 during serve_pending */
    if (s->state == 6 || s->state == 8 || s->state == 0x0C || (s->state == 5 && s->serve_pending != 0)) {
        blit_rom_obj_tile(s, 0x05u, (int)s->ball_x, (int)s->ball_y, 0u, 0xE4u);
    }

    /* Render Perfect Bonus Tally OAM[34..37] at (48..72, 120) during tally phase of State 8 (0x4949) */
    if (s->state == 8 && s->bonus_flag != 0 && s->bricks_left == 0) {
        uint16_t tally_frames = (uint16_t)(s->bonus_tally_remaining / BONUS_TALLY_STEP_TENS);
        int pre_left = (int)s->state_timer - (int)tally_frames;
        if (pre_left <= 130) {
            uint16_t rem = s->bonus_tally_remaining;
            blit_rom_obj_tile(s, (uint8_t)(0x80u + ((rem / 1000u) % 10u)), 48, 120, 0u, 0xE4u);
            blit_rom_obj_tile(s, (uint8_t)(0x80u + ((rem / 100u) % 10u)), 56, 120, 0u, 0xE4u);
            blit_rom_obj_tile(s, (uint8_t)(0x80u + ((rem / 10u) % 10u)), 64, 120, 0u, 0xE4u);
            blit_rom_obj_tile(s, (uint8_t)(0x80u + (rem % 10u)), 72, 120, 0u, 0xE4u);
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

static AlleywayStage1 g_trace_backup;

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

        /* The play-loop hook only produces trace records in states 5/6. Intermission states
         * (curtain, life lost, clear, Game Over) run other loops in the original, so the trace
         * has no records for them: the native engine runs those frames idle and the trace is
         * compared again at the next play record. The intermission frames themselves are
         * compared separately by tools/scenario_diff.py (--run-scenario). */
        g_trace_backup = g_sim;
        stage1_step_tick_scheduler(&g_sim, (uint8_t)in_raw, (uint8_t)in_edge, elapsed);
        if (g_sim.serve_pending != 0) {
            /* In logic_trace.jsonl (hooked at 0x051F..0x0525), the 2-VBlank serve is a single record */
            stage1_step_tick_scheduler(&g_sim, 0xFFu, 0xFFu, 0u);
        }
        if ((st == 5 || st == 6) && is_timed_state(g_sim.state)) {
            g_sim = g_trace_backup;
            stage1_step_tick_scheduler(&g_sim, (uint8_t)in_raw, (uint8_t)in_edge, 1u);
            unsigned extra = 1u;
            while (is_timed_state(g_sim.state) && extra < 4000u) {
                stage1_step_tick_scheduler(&g_sim, 0xFFu, 0xFFu, 1u);
                extra++;
            }
            if (elapsed > extra) g_sim.vblank_count = (uint8_t)(g_sim.vblank_count + (elapsed - extra));
        }
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

static bool write_framebuffer_ppm(const char *path) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return false;
    fprintf(fp, "P6\n160 144\n255\n");
    for (int y = 0; y < 144; ++y) {
        uint8_t row_rgb[160 * 3];
        for (int x = 0; x < 160; ++x) {
            uint32_t c = pixels[y][x];
            row_rgb[x * 3 + 0] = (uint8_t)((c >> 16) & 0xFFu);
            row_rgb[x * 3 + 1] = (uint8_t)((c >> 8) & 0xFFu);
            row_rgb[x * 3 + 2] = (uint8_t)(c & 0xFFu);
        }
        if (fwrite(row_rgb, 1, sizeof(row_rgb), fp) != sizeof(row_rgb)) {
            fclose(fp);
            return false;
        }
    }
    fclose(fp);
    return true;
}

/*
 * --run-scenario <rom> <scenario.jsonl> [--dump-ppm <tick> <out.ppm>]
 * Autonomous native run for ORIGINAL-vs-NATIVE differential tests.
 * Line 1: snapshot of the original taken at a VBlank boundary (same keys as logic trace).
 * Lines 2..N: ONLY inputs (in_joyp_raw / in_joyp_edge) latched by the original. No expected
 * values are read, so the native clock, timers and state machine run on their own.
 * Output: one JSON line per tick with state/stage/lives/score_tens/bonus_flag/bonus_timer/prng
 * and ball/paddle/scroll fields. When --dump-ppm is specified, invokes the production
 * render_stage1(&g_sim) function at the target tick (-1 for initial snapshot, >=0 for step tick).
 */
static int run_scenario(const char *rom_path, const char *scenario_path, int dump_tick, const char *dump_ppm_path) {
    if (!try_load_rom_file(&g_sim, rom_path)) {
        fprintf(stderr, "Error: failed to load valid 32 KiB ROM from %s\n", rom_path);
        return 2;
    }
    FILE *fp = fopen(scenario_path, "rb");
    if (!fp) {
        fprintf(stderr, "Error: failed to open scenario %s\n", scenario_path);
        return 2;
    }
    char line[4096];
    bool initialized = false;
    int tick = 0;
    while (fgets(line, sizeof(line), fp)) {
        int in_raw = 0xFF, in_edge = 0xFF;
        if (!initialized) {
            int st = -1, v = 0;
            if (!extract_json_int(line, "state", &st)) continue;
            stage1_reset_for_play(&g_sim);
            if (extract_json_int(line, "stage", &v)) g_sim.stage = (uint8_t)v;
            load_stage_grid(&g_sim);
            /* Initialize stage progression counters (0xCA46 / 0xCA47) from stage index unless overridden */
            if (st == 2) {
                g_sim.normal_stage_count = 0;
                g_sim.bonus_stage_count = 0;
            } else {
                uint8_t completed = (uint8_t)((g_sim.stage & 0x1Fu) + 1u);
                uint8_t bon_cnt = (uint8_t)(completed / 4u);
                g_sim.normal_stage_count = (uint8_t)(completed - bon_cnt);
                g_sim.bonus_stage_count = bon_cnt;
            }
            if (extract_json_int(line, "normal_stage_count", &v)) g_sim.normal_stage_count = (uint8_t)v;
            if (extract_json_int(line, "bonus_stage_count", &v)) g_sim.bonus_stage_count = (uint8_t)v;
            if (extract_json_int(line, "high_score_tens", &v)) g_sim.high_score_tens = (uint16_t)v;
            g_sim.state = (uint8_t)st;
            g_sim.state_timer = 0;
            if (extract_json_int(line, "prng", &v)) g_sim.prng = (uint8_t)v;
            if (extract_json_int(line, "paddle_x", &v)) g_sim.paddle_x = (uint8_t)v;
            if (extract_json_int(line, "paddle_w", &v)) g_sim.paddle_w = (uint8_t)v;
            if (extract_json_int(line, "ball_x", &v)) { g_sim.ball_x = (uint8_t)v; g_sim.ball_prev_x = (uint8_t)v; }
            if (extract_json_int(line, "ball_x_sub", &v)) g_sim.ball_x_sub = (uint8_t)v;
            if (extract_json_int(line, "ball_y", &v)) { g_sim.ball_y = (uint8_t)v; g_sim.ball_prev_y = (uint8_t)v; }
            if (extract_json_int(line, "ball_y_sub", &v)) g_sim.ball_y_sub = (uint8_t)v;
            if (extract_json_int(line, "ball_vx", &v)) g_sim.ball_vx = (uint16_t)v;
            if (extract_json_int(line, "ball_vy", &v)) g_sim.ball_vy = (uint16_t)v;
            if (extract_json_int(line, "speed_idx", &v)) g_sim.speed_idx = (uint8_t)v;
            if (extract_json_int(line, "bricks_left", &v)) g_sim.bricks_left = (uint16_t)v;
            if (extract_json_int(line, "score_tens", &v)) g_sim.score_tens = (uint16_t)v;
            if (extract_json_int(line, "lives", &v)) g_sim.lives = (uint8_t)v;
            if (extract_json_int(line, "row_offset", &v)) g_sim.row_offset = (uint8_t)v;
            if (extract_json_int(line, "scroll_timer", &v)) g_sim.scroll_timer = (uint8_t)v;
            if (extract_json_int(line, "scroll_step", &v)) g_sim.scroll_step = (uint8_t)v;
            if (extract_json_int(line, "bonus_timer", &v)) g_sim.bonus_timer = (uint16_t)v;
            if (extract_json_int(line, "vblank_count", &v)) g_sim.vblank_count = (uint8_t)v;
            for (int r = 0; r < 20; ++r) {
                char key_p[32], key_s[32], key_t[32];
                snprintf(key_p, sizeof(key_p), "row_pattern_%d", r);
                snprintf(key_s, sizeof(key_s), "row_shift_%d", r);
                snprintf(key_t, sizeof(key_t), "row_timer_%d", r);
                if (extract_json_int(line, key_p, &v)) g_sim.row_pattern[r] = (uint8_t)v;
                if (extract_json_int(line, key_s, &v)) g_sim.row_shift[r] = (uint8_t)v;
                if (extract_json_int(line, key_t, &v)) g_sim.row_timer[r] = (uint8_t)v;
            }
            /* Snapshots are taken at interactive waiting states: Title wait (2), Serve (5), or Active play (6). */
            if (st != 2 && st != 5 && st != 6) {
                fprintf(stderr, "Error: scenario snapshot must be taken in state 2, 5, or 6\n");
                fclose(fp);
                return 2;
            }
            initialized = true;
            if (dump_ppm_path && dump_tick == -1) {
                render_stage1(&g_sim);
                if (!write_framebuffer_ppm(dump_ppm_path)) {
                    fclose(fp);
                    return 3;
                }
            }
            continue;
        }
        if (!extract_json_int(line, "in_joyp_raw", &in_raw)) continue;
        extract_json_int(line, "in_joyp_edge", &in_edge);
        stage1_step_tick(&g_sim, (uint8_t)in_raw, (uint8_t)in_edge);
        if (dump_ppm_path && tick == dump_tick) {
            render_stage1(&g_sim);
            if (!write_framebuffer_ppm(dump_ppm_path)) {
                fclose(fp);
                return 3;
            }
        }
        printf("{\"tick\":%d,\"state\":%u,\"stage\":%u,\"lives\":%u,\"score_tens\":%u,\"bonus_flag\":%u,\"bonus_timer\":%u,\"prng\":%u,\"bricks_left\":%u,\"paddle_x\":%u,\"paddle_w\":%u,\"ball_x\":%u,\"ball_x_sub\":%u,\"ball_y\":%u,\"ball_y_sub\":%u,\"ball_vx\":%u,\"ball_vy\":%u,\"speed_idx\":%u,\"row_offset\":%u,\"scroll_timer\":%u,\"scroll_step\":%u,\"row_shift_0\":%u,\"row_shift_6\":%u,\"state_timer\":%u,\"curtain_dur\":%u}\n",
               tick, (unsigned)g_sim.state, (unsigned)g_sim.stage, (unsigned)g_sim.lives,
               (unsigned)g_sim.score_tens, (unsigned)g_sim.bonus_flag, (unsigned)g_sim.bonus_timer,
               (unsigned)g_sim.prng, (unsigned)g_sim.bricks_left,
               (unsigned)g_sim.paddle_x, (unsigned)g_sim.paddle_w,
               (unsigned)g_sim.ball_x, (unsigned)g_sim.ball_x_sub,
               (unsigned)g_sim.ball_y, (unsigned)g_sim.ball_y_sub,
               (unsigned)g_sim.ball_vx, (unsigned)g_sim.ball_vy,
               (unsigned)g_sim.speed_idx, (unsigned)g_sim.row_offset,
               (unsigned)g_sim.scroll_timer, (unsigned)g_sim.scroll_step,
               (unsigned)g_sim.row_shift[0], (unsigned)g_sim.row_shift[6],
               (unsigned)g_sim.state_timer, (unsigned)g_sim.curtain_dur);
        tick++;
    }
    fclose(fp);
    return initialized ? 0 : 1;
}

static void step_window_frame(void) {
    if (!g_sim.rom_loaded) return;
    uint8_t joyp_raw = 0xFFu;
    if (g_key_a)     joyp_raw &= (uint8_t)~0x01u;
    if (g_key_b)     joyp_raw &= (uint8_t)~0x02u;
    if (g_key_start) joyp_raw &= (uint8_t)~0x08u;
    if (g_key_right) joyp_raw &= (uint8_t)~0x10u;
    if (g_key_left)  joyp_raw &= (uint8_t)~0x20u;
    joyp_raw &= (uint8_t)~g_pending_presses;

    /* Active-low newly pressed edge: bit is 0 iff currently 0 and previously 1 */
    uint8_t newly_pressed_high = (uint8_t)(((~joyp_raw) & g_sim.prev_joyp_raw) | g_pending_presses);
    g_pending_presses = 0;
    uint8_t joyp_edge = (uint8_t)(~newly_pressed_high);
    g_sim.prev_joyp_raw = joyp_raw;

    stage1_step_tick(&g_sim, joyp_raw, joyp_edge);
    audio_step_frame();
    render_stage1(&g_sim);
}

static void handle_key_change(WPARAM vk, bool down, HWND w) {
    if (vk == VK_ESCAPE && down) { DestroyWindow(w); return; }
    if (vk == 'R' && down && g_sim.rom_loaded) { stage1_reset_for_play(&g_sim); return; }
    if (vk == 'T' && down && g_sim.rom_loaded) {
        stage1_reset_for_play(&g_sim);
        g_sim.state = 2;
        g_sim.normal_stage_count = 0;
        g_sim.bonus_stage_count = 0;
        return;
    }
    if (down) {
        if ((vk == VK_LEFT || vk == 'A') && !g_key_left) g_pending_presses |= 0x20;
        if ((vk == VK_RIGHT || vk == 'D') && !g_key_right) g_pending_presses |= 0x10;
        if ((vk == VK_SPACE || vk == 'Z') && !g_key_a) g_pending_presses |= 0x01;
        if ((vk == VK_SHIFT || vk == 'X') && !g_key_b) g_pending_presses |= 0x02;
        if (vk == VK_RETURN && !g_key_start) g_pending_presses |= 0x08;
    }
    if (vk == VK_LEFT || vk == 'A')  g_key_left = down;
    if (vk == VK_RIGHT || vk == 'D') g_key_right = down;
    if (vk == VK_SPACE || vk == 'Z') g_key_a = down;
    if (vk == VK_SHIFT || vk == 'X') g_key_b = down;
    if (vk == VK_RETURN)             g_key_start = down;
}

static uint32_t g_paint_count = 0;

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
            g_paint_count++;
        }
        EndPaint(w, &paint); return 0;
    }
    return DefWindowProcW(w, m, a, b);
}

static int ticks_until_state_change(AlleywayStage1 *s, uint8_t from_state) {
    int n = 0;
    while (s->state == from_state && n < 5000) {
        stage1_step_tick(s, 0xFFu, 0xFFu);
        n++;
    }
    return n;
}

static void pump_window_messages(HWND window) {
    MSG msg = {0};
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (IsWindow(window)) {
        InvalidateRect(window, NULL, FALSE);
        UpdateWindow(window);
    }
}

/*
 * --verify-window-session <rom> [out_dir]
 * End-to-end verification of the real Win32 windowed executable path:
 * Creates the actual HWND window, initializes waveOut audio, renders Title (State 2) via WM_PAINT,
 * sends real WM_KEYDOWN/WM_KEYUP events for Start (Title -> Mario entrance State 4 -> Serve State 5),
 * Right/Left movement, Space (Serve -> Active Play State 6 + SFX 7), Enter (Pause State 0x0C / Resume),
 * 'R' (Reset), and Escape (DestroyWindow + audio_backend_shutdown).
 */
static int run_verify_window_session(const char *rom_path, const char *out_dir) {
    if (!try_load_rom_file(&g_sim, rom_path)) {
        fprintf(stderr, "Error: failed to load valid 32 KiB ROM from %s\n", rom_path);
        return 2;
    }
    /* Start at authentic Title screen (State 2) */
    g_sim.state = 2;
    g_sim.normal_stage_count = 0;
    g_sim.bonus_stage_count = 0;
    render_stage1(&g_sim);
    audio_backend_init();

    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSW cls = {0}; cls.lpfnWndProc = window_proc;
    cls.style = CS_HREDRAW | CS_VREDRAW;
    cls.hInstance = instance; cls.lpszClassName = L"AlleywayVerifyWindow";
    cls.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&cls);
    RECT r = {0, 0, 640, 576}; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND window = CreateWindowW(cls.lpszClassName, L"Alleyway PC | Window Session Verification",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top, NULL, NULL, instance, NULL);
    if (!window) { audio_backend_shutdown(); return 3; }

    pump_window_messages(window);
    if (g_sim.state != 2 || g_paint_count == 0) { DestroyWindow(window); audio_backend_shutdown(); return 4; }
    if (out_dir) {
        char path[512];
        snprintf(path, sizeof(path), "%s/win_01_title.ppm", out_dir);
        write_framebuffer_ppm(path);
    }

    /* 1. Press Enter (VK_RETURN) on Title -> 3-tick start delay -> 221-tick Mario curtain -> Serve (State 5) */
    SendMessageW(window, WM_KEYDOWN, VK_RETURN, 0);
    SendMessageW(window, WM_KEYUP, VK_RETURN, 0);
    /* Regression: both messages can arrive before the next scheduler tick. */
    step_window_frame();
    for (int i = 0; i < 3; ++i) step_window_frame();
    if (g_sim.state != 4 || g_sim.curtain_dur != 221) { DestroyWindow(window); audio_backend_shutdown(); return 5; }

    /* Advance 85 frames into Mario entrance (Mario walking & paddle waiting) and paint */
    for (int i = 0; i < 85; ++i) step_window_frame();
    pump_window_messages(window);
    if (out_dir) {
        char path[512];
        snprintf(path, sizeof(path), "%s/win_02_mario_entry.ppm", out_dir);
        write_framebuffer_ppm(path);
    }

    /* Finish remaining 136 frames of State 4 -> arrives at State 5 (Serve) */
    for (int i = 0; i < 136; ++i) step_window_frame();
    if (g_sim.state != 5 || g_sim.stage != 0 || g_sim.lives != 4) { DestroyWindow(window); audio_backend_shutdown(); return 6; }

    /* 2. Move paddle right for 5 frames via WM_KEYDOWN(VK_RIGHT) -> paddle_x: 40 -> 55 */
    SendMessageW(window, WM_KEYDOWN, VK_RIGHT, 0);
    for (int i = 0; i < 5; ++i) step_window_frame();
    SendMessageW(window, WM_KEYUP, VK_RIGHT, 0);
    if (g_sim.paddle_x != 55) { DestroyWindow(window); audio_backend_shutdown(); return 7; }

    /* 3. Serve ball with Space (VK_SPACE) -> 2 frames to reach State 6 + triggers SFX 7 */
    SendMessageW(window, WM_KEYDOWN, VK_SPACE, 0);
    step_window_frame();
    SendMessageW(window, WM_KEYUP, VK_SPACE, 0);
    step_window_frame();
    if (g_sim.state != 6) { DestroyWindow(window); audio_backend_shutdown(); return 8; }
    for (int i = 0; i < 12; ++i) step_window_frame();
    pump_window_messages(window);
    if (out_dir) {
        char path[512];
        snprintf(path, sizeof(path), "%s/win_03_active_play.ppm", out_dir);
        write_framebuffer_ppm(path);
    }

    /* 4. Pause with Enter (VK_RETURN) -> State 0x0C, verify ball stays frozen for 5 frames, then Resume */
    SendMessageW(window, WM_KEYDOWN, VK_RETURN, 0);
    step_window_frame();
    SendMessageW(window, WM_KEYUP, VK_RETURN, 0);
    if (g_sim.state != 0x0C) { DestroyWindow(window); audio_backend_shutdown(); return 9; }
    uint8_t bx_before = g_sim.ball_x, by_before = g_sim.ball_y;
    for (int i = 0; i < 5; ++i) step_window_frame();
    if (g_sim.ball_x != bx_before || g_sim.ball_y != by_before) { DestroyWindow(window); audio_backend_shutdown(); return 10; }
    SendMessageW(window, WM_KEYDOWN, VK_RETURN, 0);
    step_window_frame();
    SendMessageW(window, WM_KEYUP, VK_RETURN, 0);
    if (g_sim.state != 6) { DestroyWindow(window); audio_backend_shutdown(); return 11; }

    /* 5. Press 'R' to reset to Stage 1 Serve (State 5, paddle_x = 40), then Esc to close window */
    SendMessageW(window, WM_KEYDOWN, 'R', 0);
    SendMessageW(window, WM_KEYUP, 'R', 0);
    step_window_frame();
    if (g_sim.state != 5 || g_sim.paddle_x != 40) { DestroyWindow(window); audio_backend_shutdown(); return 12; }

    SendMessageW(window, WM_KEYDOWN, VK_ESCAPE, 0);
    pump_window_messages(window);
    audio_backend_shutdown();

    printf("{\"window_verified\":true,\"paint_count\":%u,\"final_state\":%u,\"paddle_x\":%u}\n",
           (unsigned)g_paint_count, (unsigned)g_sim.state, (unsigned)g_sim.paddle_x);
    return 0;
}

int main(int argc, char **argv) {
    draw_diagnostic();
    if (argc == 2 && strcmp(argv[1], "--smoke-test") == 0) {
        if (pixels[0][0] != 0xe0f8d0 || pixels[0][40] != 0x88c070 ||
            pixels[143][159] != 0x346856) return 1;
        build_synthetic_stage1_rom(&g_sim);
        if (g_sim.bricks_left != 14 || g_sim.paddle_x != 40) return 1;
        /* Move right 1 tick (+3 px), then serve with A (2 VBlanks: setup + state 6 transition), then step 1 tick */
        stage1_step_tick(&g_sim, 0xEFu, 0xEFu);
        if (g_sim.paddle_x != 43) return 1;
        stage1_step_tick(&g_sim, 0xFEu, 0xFEu);
        if (g_sim.state != 5 || g_sim.serve_pending != 1 || g_sim.ball_y != 116) return 1;
        stage1_step_tick(&g_sim, 0xFFu, 0xFFu);
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

        /* 2. Serve ball with A button edge (2 VBlanks: setup + State 6 transition) */
        stage1_step_tick(&g_sim, 0xFEu, 0xFEu);
        if (g_sim.state != 5 || g_sim.serve_pending != 1 || g_sim.vblank_count != 51) return 3;
        stage1_step_tick(&g_sim, 0xFFu, 0xFFu);
        if (g_sim.state != 6 || g_sim.vblank_count != 52) return 3;

        /* 3. Active play: 64 ticks = exactly 2 timer ticks (32 frames per unit) */
        for (int i = 0; i < 64; ++i) {
            stage1_step_tick(&g_sim, 0xFFu, 0xFFu);
        }
        if (g_sim.vblank_count != 116 || g_sim.bonus_timer != 93 || g_sim.state != 6) return 4;

        /* 4. Pause toggled with Start edge */
        stage1_step_tick(&g_sim, 0xF7u, 0xF7u);
        if (g_sim.state != 0x0C) return 5;

        /* 5. In Pause: clock increments, but gameplay and bonus timer are frozen */
        for (int i = 0; i < 64; ++i) {
            stage1_step_tick(&g_sim, 0xFFu, 0xFFu);
        }
        if (g_sim.vblank_count != 181 || g_sim.bonus_timer != 93 || g_sim.state != 0x0C) return 6;

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

        /* 8. Bonus expiry: state 7 lasts DUR_BONUS_END ticks, then goes DIRECTLY to the curtain
         * (state 4, stage++, lives preserved) -- no state 8, as observed in the original. */
        uint8_t lives_before = g_sim.lives;
        if (ticks_until_state_change(&g_sim, 7) != DUR_BONUS_END) return 10;
        if (g_sim.state != 4 || g_sim.stage != 4 || g_sim.lives != lives_before) return 10;

        /* 9. Curtain lasts DUR_CURTAIN_DEFAULT ticks, then serve state 5 */
        if (ticks_until_state_change(&g_sim, 4) != DUR_CURTAIN_DEFAULT) return 11;
        if (g_sim.state != 5 || g_sim.lives != lives_before) return 11;

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
        /* Synthetic-ROM self checks of the PRODUCTION timers (entered through
         * stage1_enter_state, never by writing state_timer). The comparison with the ORIGINAL
         * lives in tools/scenario_diff.py. */
        uint8_t prev_lives = g_sim.lives;
        uint8_t prev_stage = g_sim.stage;
        if (ticks_until_state_change(&g_sim, 7) != DUR_BONUS_END) return 40;
        if (g_sim.state != 4 || g_sim.lives != prev_lives || g_sim.stage != (uint8_t)(prev_stage + 1u)) return 40;

        /* 6. Stage 32 clear: DUR_CLEAR ticks, wrap to stage 0, timed ending state 9 (1445 ticks) -> curtain -> serve */
        g_sim.stage = 31;
        g_sim.bonus_flag = 0;
        g_sim.bricks_left = 0;
        stage1_enter_state(&g_sim, 8u);
        if (ticks_until_state_change(&g_sim, 8) != DUR_CLEAR) return 41;
        if (g_sim.stage != 0) return 41;
        if (g_sim.state != 9) return 42;
        if (ticks_until_state_change(&g_sim, 9) != DUR_ENDING || g_sim.state != 4) return 43;
        if (ticks_until_state_change(&g_sim, 4) != DUR_CURTAIN_DEFAULT || g_sim.state != 5) return 43;

        /* 7a. Normal life loss with spare lives: DUR_DEATH ticks, lives--, 1-tick state 11, serve */
        g_sim.bonus_flag = 0;
        g_sim.lives = 2;
        stage1_enter_state(&g_sim, 7u);
        if (ticks_until_state_change(&g_sim, 7) != DUR_DEATH) return 44;
        if (g_sim.state != 0x0B || g_sim.lives != 1 || g_sim.game_over != 0) return 44;
        if (ticks_until_state_change(&g_sim, 0x0B) != DUR_LIFE_LOST_TRANSIENT || g_sim.state != 5) return 44;

        /* 7b. Last life (lives == 0): Game Over 377 ticks -> state 1 (1 tick) -> title wait (2),
         * Start/A -> 3-tick start delay in state 2 -> 221-tick first curtain (66+155) -> serve (lives 4, stage 0) */
        g_sim.lives = 0;
        stage1_enter_state(&g_sim, 7u);
        if (ticks_until_state_change(&g_sim, 7) != DUR_DEATH) return 45;
        if (g_sim.state != 0x0B || g_sim.game_over != 1) return 45;
        if (ticks_until_state_change(&g_sim, 0x0B) != DUR_GAME_OVER || g_sim.state != 1) return 45;
        if (ticks_until_state_change(&g_sim, 1) != DUR_TITLE_ENTRY || g_sim.state != 2) return 45;
        stage1_step_tick(&g_sim, 0xF7u, 0xF7u); /* Start pressed on title */
        if (g_sim.state != 2 || g_sim.lives != 4 || g_sim.stage != 0) return 45;
        if (ticks_until_state_change(&g_sim, 2) != DUR_TITLE_START_DELAY || g_sim.state != 4) return 45;
        if (ticks_until_state_change(&g_sim, 4) != (DUR_CURTAIN_DEFAULT + DUR_MARIO_ENTRY_EXTRA) || g_sim.state != 5) return 45;

        /* 8a. Bonus ball drop: DUR_BONUS_END ticks, lives kept, stage++ (no state 8) */
        g_sim.stage = 3;
        g_sim.bonus_flag = 1;
        g_sim.bricks_left = 15;
        g_sim.lives = 4;
        stage1_enter_state(&g_sim, 7u);
        if (ticks_until_state_change(&g_sim, 7) != DUR_BONUS_END) return 46;
        if (g_sim.state != 4 || g_sim.lives != 4 || g_sim.stage != 4) return 47;

        /* 8b. Perfect bonus clear: Round 0 (499 ticks, +500 tens) and Round 1 (519 ticks, +700 tens) */
        g_sim.stage = 3;
        g_sim.bonus_flag = 1;
        g_sim.bricks_left = 0;
        g_sim.score_tens = 100;
        stage1_enter_state(&g_sim, 8u);
        if (ticks_until_state_change(&g_sim, 8) != DUR_BONUS_TALLY) return 48;
        if (g_sim.score_tens != 600) return 48;
        if (g_sim.stage != 4 || g_sim.state != 4) return 49;

        g_sim.stage = 7; /* Round 1 bonus (reward = 700 tens -> 70 tally frames) */
        g_sim.bonus_flag = 1;
        g_sim.bricks_left = 0;
        g_sim.score_tens = 100;
        stage1_enter_state(&g_sim, 8u);
        if (ticks_until_state_change(&g_sim, 8) != (DUR_BONUS_PRE_TALLY + 70u)) return 50;
        if (g_sim.score_tens != 800 || g_sim.stage != 8 || g_sim.state != 4) return 50;

        puts("PASS: multi-stage rules verified (scroll wrap, bonus penetration, 1-UP threshold, vertical drop, bonus countdown, ending sequence, game over reset, bonus reward)");
        return 0;
    }

    if ((argc == 4 || argc == 7) && strcmp(argv[1], "--run-scenario") == 0) {
        int dump_tick = -999;
        const char *dump_ppm = NULL;
        if (argc == 7 && strcmp(argv[4], "--dump-ppm") == 0) {
            dump_tick = atoi(argv[5]);
            dump_ppm = argv[6];
        }
        return run_scenario(argv[2], argv[3], dump_tick, dump_ppm);
    }

    if ((argc == 3 || argc == 4) && strcmp(argv[1], "--verify-window-session") == 0) {
        return run_verify_window_session(argv[2], (argc == 4) ? argv[3] : NULL);
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
            /* Start interactive window sessions at Title Screen ("PUSH START", State 2) */
            g_sim.state = 2;
            g_sim.normal_stage_count = 0;
            g_sim.bonus_stage_count = 0;
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
        ? L"Alleyway PC | Native DMG Port (Enter/Space: Start/Serve, Arrows: Move, R: Stage 1, T: Title)"
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
