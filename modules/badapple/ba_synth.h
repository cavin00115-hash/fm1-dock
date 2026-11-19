/* ba_synth.h: the Bad Apple player's own little synth and sequencer (integers only, 44.1 kHz).
 *
 * 18 instruments: 10 two-operator FM parts (the MIDI's melodic tracks) and 8 drum voices (the MIDI's
 * drum kit, one per kit piece; the note number bends the pitch / colour within a piece). 24 voices,
 * the oldest stolen. Events come from the pack's music stream (badapple_pack.py):
 *   u32 count, then per event: varint delta_ms, u8 inst | vel_level << 5, u8 note, varint dur_ms
 * (vel_level 0..7; times already include the delay that lines the music up with the video). */
#ifndef BA_SYNTH_H
#define BA_SYNTH_H
#include <stdint.h>

#define BA_SR 44100u
#define BA_NVOICE 24u
#define BA_NINST 18u

enum { K_FM, K_KICK, K_NOISE, K_CLAP, K_TONE, K_SLAP };

typedef struct {
    uint8_t kind;
    uint8_t ratio;          /* FM: modulator / carrier, in halves (2 = 1:1) */
    uint16_t idx;           /* FM: index, 1/100 rad; TONE/SLAP: start pitch, % above the note */
    uint16_t idx_ms;        /* FM: the index's decay to idx_floor (0: constant); KICK/TONE/SLAP: pitch fall */
    uint8_t idx_floor;      /* FM: % of idx it decays to */
    uint16_t atk_ms, dec_ms;
    uint8_t sus;            /* % */
    uint16_t rel_ms;
    uint8_t level;          /* 0..127 */
    int8_t pan;             /* -64 .. 63 */
    int8_t transpose;       /* semitones */
    uint8_t tone;           /* NOISE / CLAP / SLAP: the filter (1 dark .. 6 bright) */
} ba_inst_t;

/* instrument order = the pack's: 0..9 the melodic tracks, 10..17 the drum kit */
static const ba_inst_t BA_INST[BA_NINST] = {
    /* kind  ratio idx  idx_ms fl  atk  dec  sus rel  lvl pan tr tone */
    {K_FM,    2,   80,     0, 100,  3, 200,  90,  60, 112,   0, 0, 0},  /* 0 sub bass: near sine */
    {K_FM,    2,  180,   300,  40,  2, 260,  45,  80,  70, -18, 0, 0},  /* 1 synth2 */
    {K_FM,    4,  220,   400,  60,  4, 300,  70, 120,  62,  20, 0, 0},  /* 2 synth1: square-ish lead, high */
    {K_FM,    2,  140,   600,  70, 18, 400,  85, 140,  96,   0, 0, 0},  /* 3 vocals: the main melody */
    {K_FM,    6,  300,   250,  35,  2, 350,  40, 100,  58, -26, 0, 0},  /* 4 guitar */
    {K_FM,    4,  260,    90,  20,  1, 140,  10,  70,  54,  28, 0, 0},  /* 5 arp: pluck */
    {K_FM,    2,  240,   160,  30,  2, 220,  50,  60,  88,   0, 0, 0},  /* 6 bass: plucked */
    {K_FM,    2,  320,   500,  60,  3, 300,  70,  70,  50,  14, 0, 0},  /* 7 bass2: saw-ish */
    {K_NOISE, 0,    0,     0,   0,  1, 300,   0, 200,  40, -30, 0, 3},  /* 8 perc1 */
    {K_NOISE, 0,    0,     0,   0,  1, 180,   0, 120,  40,  30, 0, 5},  /* 9 perc2 */
    {K_KICK,  0,    0,    40,   0,  0, 280,   0,  40, 127,   0, 0, 0},  /* 10 kick */
    {K_CLAP,  0,    0,     0,   0,  0, 160,   0,  60,  88,   4, 0, 3},  /* 11 clap */
    {K_NOISE, 0,    0,     0,   0,  0,  45,   0,  20,  50,  22, 0, 6},  /* 12 hi-hat closed */
    {K_NOISE, 0,    0,     0,   0,  0, 260,   0,  80,  46,  22, 0, 6},  /* 13 hi-hat open */
    {K_NOISE, 0,    0,     0,   0,  0, 1400,  0, 400,  52, -20, 0, 5},  /* 14 crash */
    {K_NOISE, 0,    0,     0,   0,  2,  50,   0,  20,  30, -36, 0, 6},  /* 15 shaker / cabasa */
    {K_TONE,  0,   35,    30,   0,  0, 170,   0,  60,  70,  30, 0, 0},  /* 16 conga / bongo */
    {K_SLAP,  0,  300,    25,   0,  0,  90,   0,  40,  56, -12, 12, 4}, /* 17 slap / scratch */
};

typedef struct {
    uint32_t atk, dk, rk, ik, pk;     /* attack step, decay / release / index / pitch coefficients (Q24) */
    int32_t sus, idx, floor_;         /* sustain (Q24), index (phase units per Q15), index floor (Q24) */
    int32_t gl, gr;                   /* pan gains, Q15 */
} ba_coef_t;

typedef struct {
    uint8_t on, inst, note, stage;    /* stage 0 attack, 1 decay, 2 release */
    uint32_t pc, pm, inc_c, inc_m;
    int32_t env, ienv, penv;          /* Q24 */
    int32_t gain;                     /* velocity x level, Q15 */
    int32_t lp, lp2;                  /* noise filters */
    uint32_t len, t, age;             /* gate length left, time since the start (samples) */
} ba_voice_t;

typedef struct {
    int16_t sine[1024];
    uint32_t inc[128];                /* phase step per note */
    ba_coef_t co[BA_NINST];
    ba_voice_t v[BA_NVOICE];
    uint32_t noise, age;
    /* the sequencer */
    const uint8_t *ev, *ev_end;
    uint32_t ev_left, pos, next_at, next_ms;
    uint8_t playing;
} ba_synth_t;

static uint32_t ba__coef(uint32_t ms)             /* exp(-1 / samples) in Q24, near enough */
{
    uint32_t n = ms * 441u / 10u;
    if (n < 1u) n = 1u;
    return (1u << 24) - (1u << 24) / n;
}

static void ba_synth_init(ba_synth_t *s)
{
    static const uint32_t SEMI[12] = {65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218,
                                      116772, 123715};
    int64_t c = 1073741824, sn = 0;               /* the sine by rotation, Q30 */
    const int64_t RC = 1073721611, RS = 6588356;   /* cos, sin of 2 pi / 1024 */
    uint32_t i;
    for (i = 0; i < 1024u; i++) {
        int64_t nc, ns;
        s->sine[i] = (int16_t)(sn >> 15 > 32767 ? 32767 : sn >> 15 < -32767 ? -32767 : sn >> 15);
        nc = (c * RC - sn * RS) >> 30;
        ns = (sn * RC + c * RS) >> 30;
        c = nc; sn = ns;
    }
    for (i = 0; i < 128u; i++) {                  /* A4 (69) = 440 Hz: 42852281 per sample */
        int32_t oct = ((int32_t)i - 57) / 12, semi = ((int32_t)i - 57) - oct * 12;   /* from A3 */
        uint64_t a3 = 21426140u;
        if (semi < 0) { semi += 12; oct--; }
        a3 = (a3 * SEMI[semi]) >> 16;
        s->inc[i] = (uint32_t)(oct >= 0 ? a3 << oct : a3 >> -oct);
    }
    for (i = 0; i < BA_NINST; i++) {
        const ba_inst_t *p = &BA_INST[i];
        ba_coef_t *k = &s->co[i];
        uint32_t an = p->atk_ms * 441u / 10u;
        k->atk = an ? (1u << 24) / an : (1u << 24);
        k->dk = ba__coef(p->dec_ms);
        k->rk = ba__coef(p->rel_ms);
        k->ik = p->idx_ms ? ba__coef(p->idx_ms) : (1u << 24);
        k->pk = ba__coef(p->idx_ms ? p->idx_ms : 30u);
        k->sus = (int32_t)((uint32_t)p->sus * ((1u << 24) / 100u));
        k->idx = (int32_t)((uint32_t)p->idx * 20861u / 100u);   /* 1 rad = 2^32 / 2 pi phase per Q15 unit */
        k->floor_ = (int32_t)((uint32_t)p->idx_floor * ((1u << 24) / 100u));
        k->gl = (64 - p->pan) * 256;
        k->gr = (64 + p->pan) * 256;
    }
    s->noise = 0x12345678u;
}

static inline int32_t ba__noise(ba_synth_t *s)
{
    uint32_t x = s->noise;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    s->noise = x;
    return (int32_t)(x >> 16) - 32768;
}

static void ba_note_on(ba_synth_t *s, uint32_t inst, uint32_t note, uint32_t vel, uint32_t dur_ms)
{
    const ba_inst_t *p = &BA_INST[inst];
    ba_voice_t *v = 0;
    uint32_t i, oldest = 0, best = 0;
    if (inst == 12u)                              /* a closed hi-hat chokes the open one */
        for (i = 0; i < BA_NVOICE; i++)
            if (s->v[i].on && s->v[i].inst == 13u) s->v[i].stage = 2;
    for (i = 0; i < BA_NVOICE && !v; i++)         /* a drum hit again takes its own voice */
        if (s->v[i].on && p->kind != K_FM && s->v[i].inst == inst && s->v[i].note == note) v = &s->v[i];
    for (i = 0; i < BA_NVOICE && !v; i++)
        if (!s->v[i].on) v = &s->v[i];
    if (!v) {
        for (i = 0; i < BA_NVOICE; i++)
            if (s->age - s->v[i].age >= best) { best = s->age - s->v[i].age; oldest = i; }
        v = &s->v[oldest];
    }
    note += (uint32_t)(int32_t)p->transpose;
    if (note > 127u) note = 127u;
    v->on = 1; v->inst = (uint8_t)inst; v->note = (uint8_t)note; v->stage = 0;
    v->pc = v->pm = 0;
    v->inc_c = s->inc[note];
    v->inc_m = p->kind == K_FM ? (uint32_t)(((uint64_t)v->inc_c * p->ratio) >> 1)
                               : v->inc_c / 100u * p->idx;   /* TONE / SLAP: the extra pitch at the start */
    v->env = p->atk_ms ? 0 : (1 << 24);
    if (!p->atk_ms) v->stage = 1;
    v->ienv = v->penv = 1 << 24;
    v->gain = (int32_t)(vel * p->level * 2u);
    v->lp = v->lp2 = 0;
    v->len = p->kind == K_FM ? dur_ms * 441u / 10u : 0;
    v->t = 0;
    v->age = s->age++;
}

/* one voice, n samples, added into acc (stereo, interleaved) */
static void ba__voice(ba_synth_t *s, ba_voice_t *v, int32_t *acc, uint32_t n)
{
    const ba_inst_t *p = &BA_INST[v->inst];
    const ba_coef_t *k = &s->co[v->inst];
    const int16_t *sn = s->sine;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t x, e;
        /* the envelope */
        if (v->stage == 0) {
            v->env += (int32_t)k->atk;
            if (v->env >= (1 << 24)) { v->env = 1 << 24; v->stage = 1; }
        } else if (v->stage == 1) {
            v->env = k->sus + (int32_t)(((int64_t)(v->env - k->sus) * k->dk) >> 24);
            if (p->kind != K_FM && v->env < (1 << 12)) { v->on = 0; return; }
        } else {
            v->env = (int32_t)(((int64_t)v->env * k->rk) >> 24);
            if (v->env < (1 << 12)) { v->on = 0; return; }
        }
        if (v->len && --v->len == 0) v->stage = 2;
        switch (p->kind) {
        case K_FM: {
            int32_t m = sn[v->pm >> 22], idx;
            v->ienv = k->floor_ + (int32_t)(((int64_t)(v->ienv - k->floor_) * k->ik) >> 24);
            idx = (int32_t)(((int64_t)k->idx * v->ienv) >> 24);
            x = sn[(v->pc + (uint32_t)(m * idx)) >> 22];
            v->pm += v->inc_m;
            v->pc += v->inc_c;
            break;
        }
        case K_KICK: {                            /* a sine falling from ~160 Hz to ~48 Hz, a click at the start */
            uint32_t lo = 4675000u, hi = 15580000u;   /* 48 Hz, 160 Hz */
            v->penv = (int32_t)(((int64_t)v->penv * k->pk) >> 24);
            x = sn[v->pc >> 22];
            v->pc += lo + (uint32_t)(((uint64_t)(hi - lo) * (uint32_t)v->penv) >> 24);
            if (v->t < 90u) x += ba__noise(s) >> 2;
            break;
        }
        case K_CLAP: {                            /* three quick bursts, then the tail */
            int32_t nz = ba__noise(s);
            v->lp += (nz - v->lp) >> 1;
            v->lp2 += (v->lp - v->lp2) >> 3;
            x = v->lp - v->lp2;
            if (v->t < 1323u) {                   /* 30 ms */
                uint32_t b = v->t % 441u;         /* a burst every 10 ms, 4 ms each */
                x = b < 176u ? x : x >> 3;
            }
            x <<= 1;
            break;
        }
        case K_NOISE: {                           /* high-passed noise; the note opens it a little */
            int32_t nz = ba__noise(s), sh = 7 - (int32_t)p->tone + ((int32_t)v->note > 75 ? -1 : 0);
            if (sh < 1) sh = 1;
            v->lp += (nz - v->lp) >> sh;
            x = nz - v->lp;
            break;
        }
        case K_TONE:                              /* a drum tone: the note's pitch, falling into it */
        case K_SLAP: {
            v->penv = (int32_t)(((int64_t)v->penv * k->pk) >> 24);
            x = sn[v->pc >> 22];
            v->pc += v->inc_c + (uint32_t)(((uint64_t)v->inc_m * (uint32_t)v->penv) >> 24);
            if (p->kind == K_SLAP) {
                int32_t nz = ba__noise(s);
                v->lp += (nz - v->lp) >> 2;
                x = (x >> 1) + (v->note & 1 ? v->lp >> 1 : (nz - v->lp) >> 1);
            }
            break;
        }
        default: x = 0;
        }
        v->t++;
        if (x > 32767) x = 32767; else if (x < -32767) x = -32767;
        e = (int32_t)(((int64_t)x * (v->env >> 9)) >> 15);   /* env Q24 -> Q15 */
        e = (e * v->gain) >> 15;
        acc[2 * i] += (e * k->gl) >> 15;
        acc[2 * i + 1] += (e * k->gr) >> 15;
    }
}

static uint32_t ba__varint(const uint8_t **pp, const uint8_t *end)
{
    uint32_t v = 0, sh = 0;
    while (*pp < end) {
        uint8_t c = *(*pp)++;
        v |= (uint32_t)(c & 0x7Fu) << sh;
        if (!(c & 0x80u)) break;
        sh += 7;
    }
    return v;
}

static void ba__next_time(ba_synth_t *s)
{
    if (!s->ev_left) { s->next_at = 0xFFFFFFFFu; return; }
    s->next_ms += ba__varint(&s->ev, s->ev_end);
    s->next_at = s->next_ms * 441u / 10u;
}

static void ba_play(ba_synth_t *s, const uint8_t *music, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < BA_NVOICE; i++) s->v[i].on = 0;
    s->pos = 0;
    s->next_ms = 0;
    s->ev = music + 4;
    s->ev_end = music + len;
    s->ev_left = len >= 4u ? (uint32_t)music[0] | (uint32_t)music[1] << 8 | (uint32_t)music[2] << 16 |
                                 (uint32_t)music[3] << 24 : 0;
    ba__next_time(s);
    s->playing = 1;
}

static void ba_stop(ba_synth_t *s)
{
    uint32_t i;
    s->playing = 0;
    for (i = 0; i < BA_NVOICE; i++) s->v[i].on = 0;
}

static int32_t ba_acc[2 * 256];

/* the audio interrupt: n stereo frames added into out (Q15); s->pos counts samples since the start */
static void ba_render(ba_synth_t *s, int32_t *out, uint32_t n)
{
    uint32_t done = 0, i;
    if (!s->playing) return;
    while (done < n) {
        uint32_t chunk = n - done;
        while (s->ev_left && s->next_at <= s->pos) {   /* the events due now */
            uint8_t a = s->ev < s->ev_end ? *s->ev++ : 0;
            uint8_t note = s->ev < s->ev_end ? *s->ev++ : 0;
            uint32_t dur = ba__varint(&s->ev, s->ev_end);
            uint32_t inst = a & 31u, vel = 24u + (uint32_t)(a >> 5) * 14u + 5u;   /* level 0..7 -> 29..127 */
            if (inst < BA_NINST) ba_note_on(s, inst, note, vel > 127u ? 127u : vel, dur);
            s->ev_left--;
            ba__next_time(s);
        }
        if (s->ev_left && s->next_at - s->pos < chunk) chunk = s->next_at - s->pos;
        if (chunk > 256u) chunk = 256u;
        for (i = 0; i < 2u * chunk; i++) ba_acc[i] = 0;
        for (i = 0; i < BA_NVOICE; i++)
            if (s->v[i].on) ba__voice(s, &s->v[i], ba_acc, chunk);
        for (i = 0; i < 2u * chunk; i++) out[2u * done + i] += (ba_acc[i] * 5) >> 4;   /* (headroom: the peaks of all 24 voices) */
        s->pos += chunk;
        done += chunk;
    }
}
#endif
