#ifndef ALLEYWAY_SFX_H
#define ALLEYWAY_SFX_H

/* Title-specific channel-1 sequencer reconstructed from 0x69FB..0x6F9C.
 * Tables are read from the user's ROM; no sound samples/table contents are bundled.
 * One call is one sound-driver VBlank, not one PCM sample. */
#include "gb_apu.h"
#include <stddef.h>

typedef struct {
    uint8_t active, clock, remaining, lo_up, lo_down, hi_up, hi_down;
    bool protected_effect;
} AlleywaySfx;

static inline void alleyway_sfx_stop(AlleywaySfx *s, GbApuChannel1 *ch) {
    s->active = s->clock = s->remaining = 0;
    gb_apu_ch1_write_reg(ch, 0x12, 0);
}

static inline void alleyway_sfx_table(GbApuChannel1 *ch, const uint8_t *rom, size_t size, size_t at) {
    if (!rom || at + 5 > size) return;
    for (uint8_t i = 0; i < 5; i++) gb_apu_ch1_write_reg(ch, (uint8_t)(0x10 + i), rom[at+i]);
}

static inline void alleyway_sfx_step(AlleywaySfx *s, GbApuChannel1 *ch,
                                     const uint8_t *rom, size_t size, uint8_t request) {
    /* Effect 1 has priority over all incoming primary-channel requests. */
    if (s->active == 1) request = 0;
    if (request >= 1 && request <= 12) {
        if ((request >= 2 && request <= 6 && s->protected_effect) ||
            (request == 12 && s->protected_effect)) return;
        s->active = request;
        if (request <= 8) {
            size_t at = 0;
            switch (request) {
            case 1: s->remaining=7; at=0x6FDE; break;
            case 2: s->remaining=5; at=0x6FBB; break;
            case 3: s->remaining=5; at=0x6FCA; break;
            case 4: s->remaining=4; at=0x6FAC; break;
            case 5: s->remaining=5; at=0x6FFC; break;
            case 6: s->remaining=5; at=0x700B; break;
            case 7: s->remaining=4; at=0x701A; break;
            case 8: s->remaining=5; at=0x7029; break;
            }
            alleyway_sfx_table(ch,rom,size,at);
        } else if (request == 9) {
            s->lo_up=0x63; s->lo_down=0x0A; s->hi_up=0x87; s->clock=0xFF;
        } else if (request == 10) {
            s->lo_up=0x0B; s->lo_down=0xAC; s->hi_up=0x86; s->hi_down=0x87; s->clock=0xFF;
        } else if (request == 11) {
            s->lo_down=0xA5; s->hi_down=0x87; s->protected_effect=true;
        } else {
            s->lo_up=0xFF; s->lo_down=0x0A; s->hi_up=0x85; s->clock=0xFF;
        }
        return;
    }
    uint8_t id=s->active;
    if (!id) return;
    if (id <= 8) {
        uint8_t period = id==1 ? 7 : id==3 ? 3 : id==8 ? 2 : 5;
        s->clock++;
        if (s->clock != period) return;
        s->clock=0;
        s->remaining--;
        size_t at=0;
        if (id==1 && s->remaining>=2 && s->remaining<=6) {
            at=0x6FDE + (size_t)(7-s->remaining)*5;
            if (s->remaining==2) s->protected_effect=false;
        } else if (id>=2 && id<=6 && s->remaining>=2 && s->remaining<=4) {
            size_t base=id==2 ? 0x6FBB : id==3 ? 0x6FCA : id==4 ? 0x6FAC : id==5 ? 0x6FFC : 0x700B;
            /* Effect 3 advances immediately; 2, 5 and 6 repeat their first table. */
            at=base+(size_t)((id==3 ? 5 : 4)-s->remaining)*5;
        } else if (id==7 && s->remaining>=2 && s->remaining<=3) {
            at=0x701A+(size_t)(4-s->remaining)*5;
        } else if (id==8 && s->remaining>=2 && s->remaining<=4) {
            at=0x7029+(size_t)(5-s->remaining)*5;
        }
        if (at) alleyway_sfx_table(ch,rom,size,at);
        else alleyway_sfx_stop(s,ch);
        return;
    }
    /* Software sweeps: bounded inner loops in the original driver. */
    bool first = id!=11 && s->clock!=0;
    uint8_t loops = id==9 ? (first?5:4) : id==10 ? (first?9:4) : id==11 ? 8 : 40;
    gb_apu_ch1_write_reg(ch,0x10,0);
    gb_apu_ch1_write_reg(ch,0x11,0xBF);
    gb_apu_ch1_write_reg(ch,0x12,(id==10 || id==11) ? 0x90 : 0x40);
    for (uint8_t i=0;i<loops;i++) {
        uint8_t next;
        if (id==11) {
            next=(uint8_t)(s->lo_down-1);
            if (next==6) { s->protected_effect=false; alleyway_sfx_stop(s,ch); return; }
            s->lo_down=next;
        } else if (first) {
            next=(uint8_t)(s->lo_up+(id==12 ? -1 : 1));
            if (next==(id==9 ? 0x63 : id==10 ? 0x89 : 0x10)) { s->clock=0; return; }
            s->lo_up=next;
        } else {
            next=(uint8_t)(s->lo_down+(id==12 ? 1 : -1));
            if (next==(id==9 ? 0x10 : id==10 ? 0x1E : 0x63)) { alleyway_sfx_stop(s,ch); return; }
            s->lo_down=next;
        }
    }
    gb_apu_ch1_write_reg(ch,0x13,first ? s->lo_up : s->lo_down);
    gb_apu_ch1_write_reg(ch,0x14,(id==11 || (id==10 && !first)) ? s->hi_down : s->hi_up);
}
#endif
