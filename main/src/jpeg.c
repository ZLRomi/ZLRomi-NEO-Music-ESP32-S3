#include "jpeg.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_COMP   3
#define MAX_HUFF   8
#define MARKER_END 0xD9

static const int ZZ[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

typedef struct {
    uint8_t counts[17];
    uint8_t symbols[256];
    int     nsym;
    int     code[256];
    int     length[256];
} huff_t;

typedef struct {
    int id, h, v, tq, td, ta;
    int pred;
    uint8_t *plane;
    int pw, ph;
} comp_t;

typedef struct {
    const uint8_t *p;
    const uint8_t *end;
    int bitbuf, bitcnt;
} reader_t;

typedef struct {
    int w, h, nc;
    comp_t comp[MAX_COMP];
    uint16_t quant[4][64];
    huff_t   huff[MAX_HUFF];
    int max_h, max_v;
    int restart_interval;
    uint8_t *rgb;      /* 全尺寸 RGB565 */
} jpeg_t;

static double s_cos[8][8];
static double s_cc[8];
static bool   s_tables_ready;

static void init_tables(void)
{
    if (s_tables_ready) return;
    for (int x = 0; x < 8; x++) {
        for (int u = 0; u < 8; u++) {
            s_cos[x][u] = cos((2.0 * x + 1.0) * u * 3.14159265358979323846 / 16.0);
        }
    }
    for (int u = 0; u < 8; u++) s_cc[u] = (u == 0) ? 0.7071067811865476 : 1.0;
    s_tables_ready = true;
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static void build_huff(huff_t *h)
{
    int code = 0, k = 0;
    for (int l = 1; l <= 16; l++) {
        int n = h->counts[l];
        for (int i = 0; i < n; i++) {
            if (k < 256) {
                h->code[k] = code;
                h->length[k] = l;
            }
            code++;
            k++;
        }
        code <<= 1;
    }
    h->nsym = k;
}

static int next_byte(reader_t *r)
{
    if (r->p >= r->end) return -1;
    int b = *r->p++;
    if (b == 0xFF) {
        if (r->p >= r->end) return -1;
        int n = *r->p;
        if (n == 0x00) { r->p++; return 0xFF; }
        if (n >= 0xD0 && n <= 0xD7) { r->p++; return next_byte(r); }
        return -1;                       /* 遇到其它 marker，熵数据结束 */
    }
    return b;
}

static int get_bit(reader_t *r)
{
    if (r->bitcnt == 0) {
        int b = next_byte(r);
        if (b < 0) return -1;
        r->bitbuf = b;
        r->bitcnt = 8;
    }
    r->bitcnt--;
    return (r->bitbuf >> r->bitcnt) & 1;
}

static int get_bits(reader_t *r, int n)
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        int b = get_bit(r);
        if (b < 0) return -32768;
        v = (v << 1) | b;
    }
    return v;
}

static int huff_symbol(reader_t *r, huff_t *h)
{
    int code = 0;
    for (int l = 1; l <= 16; l++) {
        int b = get_bit(r);
        if (b < 0) return -1;
        code = (code << 1) | b;
        for (int i = 0; i < h->nsym; i++) {
            if (h->length[i] == l && h->code[i] == code) return h->symbols[i];
        }
    }
    return -1;
}

static int extend(int v, int t)
{
    if (t == 0) return 0;
    int threshold = 1 << (t - 1);
    return (v < threshold) ? (v - (1 << t) + 1) : v;
}

static void idct_block(const int coef[64], uint8_t out[64])
{
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            double s = 0.0;
            for (int v = 0; v < 8; v++) {
                double cv = s_cc[v] * s_cos[y][v];
                for (int u = 0; u < 8; u++) {
                    s += coef[v * 8 + u] * s_cc[u] * s_cos[x][u] * cv;
                }
            }
            int px = (int)(s * 0.25 + 128.0);
            if (px < 0) px = 0;
            if (px > 255) px = 255;
            out[y * 8 + x] = (uint8_t)px;
        }
    }
}

static bool decode_block(reader_t *r, jpeg_t *j, comp_t *c, int bx, int by)
{
    int coef[64] = { 0 };
    huff_t *dc = &j->huff[c->td];
    huff_t *ac = &j->huff[4 + c->ta];

    int t = huff_symbol(r, dc);
    if (t < 0 || t > 15) return false;
    int diff = get_bits(r, t);
    if (diff == -32768) return false;
    c->pred += extend(diff, t);
    coef[0] = c->pred * (int)j->quant[c->tq][0];

    for (int k = 1; k < 64; ) {
        int rs = huff_symbol(r, ac);
        if (rs < 0) return false;
        int run = rs >> 4, size = rs & 15;
        if (size == 0) {
            if (run == 15) { k += 16; continue; }
            break;                              /* EOB */
        }
        k += run;
        if (k > 63) break;
        int v = get_bits(r, size);
        if (v == -32768) return false;
        coef[ZZ[k]] = extend(v, size) * (int)j->quant[c->tq][ZZ[k]];
        k++;
    }

    uint8_t pixels[64];
    idct_block(coef, pixels);
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int px = bx + x, py = by + y;
            if (px < c->pw && py < c->ph) {
                c->plane[(size_t)py * c->pw + px] = pixels[y * 8 + x];
            }
        }
    }
    return true;
}

static bool parse_segments(jpeg_t *j, const uint8_t *data, size_t len, reader_t *entropy)
{
    const uint8_t *p = data + 2;              /* 跳过 SOI */
    const uint8_t *end = data + len;
    bool got_sof = false, got_sos = false;

    while (p + 4 <= end) {
        if (p[0] != 0xFF) { p++; continue; }
        int m = p[1];
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { p += 2; continue; }
        if (m == MARKER_END) return false;
        int seglen = be16(p + 2);
        if (seglen < 2 || p + 2 + seglen > end) return false;
        const uint8_t *s = p + 4;
        int n = seglen - 2;

        if (m == 0xDB) {                       /* DQT */
            const uint8_t *q = s;
            while (q + 1 <= s + n) {
                int pq = q[0] >> 4, id = q[0] & 15;
                q++;
                int step = pq ? 2 : 1;
                if (id >= 4) return false;
                for (int i = 0; i < 64; i++) {
                    if (pq) {
                        if (q + 2 > s + n) return false;
                        j->quant[id][i] = be16(q);
                        q += 2;
                    } else {
                        if (q + 1 > s + n) return false;
                        j->quant[id][i] = *q++;
                    }
                }
                (void)step;
            }
        } else if (m == 0xC0 || m == 0xC1) {  /* SOF0 / SOF1 baseline */
            if (n < 6) return false;
            j->h = be16(s + 1);
            j->w = be16(s + 3);
            j->nc = s[5];
            if (j->nc < 1 || j->nc > MAX_COMP || j->w < 1 || j->h < 1) return false;
            if (n < 6 + j->nc * 3) return false;
            j->max_h = j->max_v = 1;
            for (int i = 0; i < j->nc; i++) {
                const uint8_t *c = s + 6 + i * 3;
                j->comp[i].id = c[0];
                j->comp[i].h  = c[1] >> 4;
                j->comp[i].v  = c[1] & 15;
                j->comp[i].tq = c[2];
                if (j->comp[i].h < 1 || j->comp[i].v < 1) return false;
                if (j->comp[i].h > j->max_h) j->max_h = j->comp[i].h;
                if (j->comp[i].v > j->max_v) j->max_v = j->comp[i].v;
                if (j->comp[i].tq > 3) return false;
            }
            got_sof = true;
        } else if (m == 0xC4) {                /* DHT */
            const uint8_t *q = s;
            while (q + 17 <= s + n) {
                int tc = q[0] >> 4, th = q[0] & 3;
                if (tc > 1 || th > 3) return false;
                huff_t *h = &j->huff[tc * 4 + th];
                int total = 0;
                for (int i = 1; i <= 16; i++) {
                    h->counts[i] = q[i];
                    total += q[i];
                }
                q += 17;
                if (total > 256 || q + total > s + n) return false;
                for (int i = 0; i < total; i++) h->symbols[i] = *q++;
                build_huff(h);
            }
        } else if (m == 0xDD) {                /* DRI */
            if (n >= 2) j->restart_interval = be16(s);
        } else if (m == 0xDA) {                /* SOS */
            if (!got_sof || n < 1) return false;
            int ns = s[0];
            if (ns != j->nc || n < 1 + ns * 2) return false;
            for (int i = 0; i < ns; i++) {
                int id = s[1 + i * 2];
                int t  = s[2 + i * 2];
                for (int c = 0; c < j->nc; c++) {
                    if (j->comp[c].id == id) {
                        j->comp[c].td = t >> 4;
                        j->comp[c].ta = t & 15;
                    }
                }
            }
            entropy->p = p + 2 + seglen;
            entropy->end = end;
            entropy->bitbuf = 0;
            entropy->bitcnt = 0;
            got_sos = true;
            break;
        }
        p += 2 + seglen;
    }
    return got_sof && got_sos;
}

static inline uint8_t plane_at(const comp_t *c, int x, int y, int max_h, int max_v)
{
    int px = x * c->h / max_h;
    int py = y * c->v / max_v;
    if (px >= c->pw) px = c->pw - 1;
    if (py >= c->ph) py = c->ph - 1;
    return c->plane[(size_t)py * c->pw + px];
}

static inline uint8_t clamp_u8(int v)
{
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (uint8_t)v;
}

bool jpeg_decode_rgb565(const uint8_t *data, size_t len,
                        uint16_t *out, int out_w, int out_h)
{
    if (!data || !out || len < 16 || out_w < 1 || out_h < 1) return false;
    if (data[0] != 0xFF || data[1] != 0xD8) return false;
    init_tables();

    jpeg_t *j = calloc(1, sizeof(jpeg_t));
    if (!j) return false;
    j->restart_interval = 0;

    reader_t r = { 0 };
    bool ok = parse_segments(j, data, len, &r);
    if (!ok) goto done;

    int mcu_w = 8 * j->max_h, mcu_h = 8 * j->max_v;
    int cols = (j->w + mcu_w - 1) / mcu_w;
    int rows = (j->h + mcu_h - 1) / mcu_h;

    for (int c = 0; c < j->nc; c++) {
        j->comp[c].pw = cols * j->comp[c].h * 8;
        j->comp[c].ph = rows * j->comp[c].v * 8;
        j->comp[c].plane = calloc((size_t)j->comp[c].pw * j->comp[c].ph, 1);
        if (!j->comp[c].plane) { ok = false; goto done; }
        j->comp[c].pred = 0;
    }

    int mcus = 0;
    for (int my = 0; my < rows && ok; my++) {
        for (int mx = 0; mx < cols && ok; mx++) {
            if (j->restart_interval > 0 && mcus > 0 && mcus % j->restart_interval == 0) {
                r.bitcnt = 0;
                if (r.p + 1 < r.end && r.p[0] == 0xFF &&
                    r.p[1] >= 0xD0 && r.p[1] <= 0xD7) {
                    r.p += 2;
                }
                for (int c = 0; c < j->nc; c++) j->comp[c].pred = 0;
            }
            for (int c = 0; c < j->nc && ok; c++) {
                comp_t *cc = &j->comp[c];
                for (int by = 0; by < cc->v && ok; by++) {
                    for (int bx = 0; bx < cc->h && ok; bx++) {
                        ok = decode_block(&r, j, cc,
                                          mx * cc->h * 8 + bx * 8,
                                          my * cc->v * 8 + by * 8);
                    }
                }
            }
            mcus++;
        }
    }
    if (!ok) goto done;

    /* YCbCr -> 全尺寸 RGB565，再最近邻缩到目标尺寸 */
    comp_t *y  = &j->comp[0];
    comp_t *cb = j->nc > 1 ? &j->comp[1] : y;
    comp_t *cr = j->nc > 2 ? &j->comp[2] : cb;

    for (int oy = 0; oy < out_h; oy++) {
        int sy = oy * j->h / out_h;
        for (int ox = 0; ox < out_w; ox++) {
            int sx = ox * j->w / out_w;
            int yy = plane_at(y, sx, sy, j->max_h, j->max_v);
            int uu = plane_at(cb, sx, sy, j->max_h, j->max_v) - 128;
            int vv = plane_at(cr, sx, sy, j->max_h, j->max_v) - 128;
            int rr = clamp_u8(yy + ((359 * vv) >> 8));
            int gg = clamp_u8(yy - ((88 * uu + 183 * vv) >> 8));
            int bb = clamp_u8(yy + ((454 * uu) >> 8));
            out[(size_t)oy * out_w + ox] =
                (uint16_t)(((rr & 0xF8) << 8) | ((gg & 0xFC) << 3) | (bb >> 3));
        }
    }

done:
    for (int c = 0; c < j->nc; c++) free(j->comp[c].plane);
    free(j);
    return ok;
}
