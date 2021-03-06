/*
 * Copyright (c) 2014 Clément Bœsch
 *
 * This file is part of FFmpeg.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * @file
 * hqx magnification filters (hq2x, hq3x, hq4x)
 *
 * Originally designed by Maxim Stephin.
 *
 * @see http://en.wikipedia.org/wiki/Hqx
 * @see http://web.archive.org/web/20131114143602/http://www.hiend3d.com/hq3x.html
 * @see http://blog.pkh.me/p/19-butchering-hqx-scaling-filters.html
 */

#include <stdint.h>
#include <stdlib.h>

const uint32_t YMASK = 0xfff00000u;
const uint32_t UMASK = 0xffc00u;
const uint32_t VMASK = 0x3ffu;

static int yuv_diff(uint32_t c1, uint32_t c2)
{
    int y1 = (c1 >> 20) & 0xfff;
    int y2 = (c2 >> 20) & 0xfff;
    bool ydiff = std::abs(y1 - y2) > 768;
    int u1 = int((c1 >> 10) & 0x3ff);
    int u2 = int((c2 >> 10) & 0x3ff);
    bool udiff = std::abs(u1 - u2) > 28;
    int v1 = int(c1 & 0x3ff);
    int v2 = int(c2 & 0x3ff);
    bool vdiff = std::abs(v1 - v2) > 24;
    return ydiff || udiff || vdiff;
}

/* (c1*w1 + c2*w2) >> s */
static uint32_t interp_2px(uint32_t c1, unsigned w1, uint32_t c2, unsigned w2, unsigned s)
{
    uint32_t y1 = (c1 >> 20) & 0xfff;
    uint32_t y2 = (c2 >> 20) & 0xfff;
    uint32_t y = ((y1 * w1 + y2 * w2) << (20 - s)) & YMASK;
    int u1 = int((c1 >> 10) & 0x3ff) - 512;
    int u2 = int((c2 >> 10) & 0x3ff) - 512;
    uint32_t u = ((u1 * w1 + u2 * w2 + (512 << s)) << (10 - s)) & UMASK;
    int v1 = int(c1 & 0x3ff) - 512;
    int v2 = int(c2 & 0x3ff) - 512;
    uint32_t v = ((v1 * w1 + v2 * w2 + (512 << s)) >> s) & VMASK;
    return y | u | v;
}

/* (c1*w1 + c2*w2 + c3*w3) >> s */
static uint32_t interp_3px(uint32_t c1, int w1, uint32_t c2, int w2, uint32_t c3, int w3, int s)
{
    uint y1 = (c1 >> 20) & 0xfff;
    uint y2 = (c2 >> 20) & 0xfff;
    uint y3 = (c3 >> 20) & 0xfff;
    uint32_t y = ((y1 * w1 + y2 * w2 + y3 * w3) << (20 - s)) & YMASK;
    int u1 = int((c1 >> 10) & 0x3ff) - 512;
    int u2 = int((c2 >> 10) & 0x3ff) - 512;
    int u3 = int((c3 >> 10) & 0x3ff) - 512;
    uint32_t u = ((u1 * w1 + u2 * w2 + u3 * w3 + (512 << s)) << (10 - s)) & UMASK;
    int v1 = int(c1 & 0x3ff) - 512;
    int v2 = int(c2 & 0x3ff) - 512;
    int v3 = int(c3 & 0x3ff) - 512;
    uint32_t v = ((v1 * w1 + v2 * w2 + v3 * w3 + (512 << s)) >> s) & VMASK;
    return y | u | v;
}

/* m is the mask of diff with the center pixel that matters in the pattern, and
 * r is the expected result (bit set to 1 if there is difference with the
 * center, 0 otherwise) */
#define P(m, r) ((k_shuffled & (m)) == (r))

/* adjust 012345678 to 01235678: the mask doesn't contain the (null) diff
 * between the center/current pixel and itself */
#define DROP4(z) ((z) > 4 ? (z)-1 : (z))

/* shuffle the input mask: move bit n (4-adjusted) to position stored in p<n> */
#define SHF(x, rot, n) (((x) >> ((rot) ? 7-DROP4(n) : DROP4(n)) & 1) << DROP4(p##n))

/* used to check if there is YUV difference between 2 pixels */
#define WDIFF(c1, c2) yuv_diff((c1), (c2))

/* bootstrap template for every interpolation code. It defines the shuffled
 * masks and surrounding pixels. The rot flag is used to indicate if it's a
 * rotation; its basic effect is to shuffle k using p8..p0 instead of p0..p8 */
#define INTERP_BOOTSTRAP(rot)                                           \
    const int k_shuffled = SHF(k,rot,0) | SHF(k,rot,1) | SHF(k,rot,2)   \
                         | SHF(k,rot,3) |       0      | SHF(k,rot,5)   \
                         | SHF(k,rot,6) | SHF(k,rot,7) | SHF(k,rot,8);  \
                                                                        \
    const uint32_t w0 = w[p0], w1 = w[p1],                              \
                   w3 = w[p3], w4 = w[p4], w5 = w[p5],                  \
                               w7 = w[p7]

/* Assuming p0..p8 is mapped to pixels 0..8, this function interpolates the
 * top-left block of 2x2 pixels in the total of the 4x4 pixels (or 4 blocks) to
 * interpolates. The function is also used for the 3 other blocks of 2x2
 * pixels. */
static void hq4x_interp_2x2(uint32_t *dst, int dst_linesize,
                            int k, const uint32_t *w,
                                             int pos00, int pos01,
                                             int pos10, int pos11,
                                             int p0, int p1, int p2,
                                             int p3, int p4, int p5,
                                             int p6, int p7, int p8)
{
    INTERP_BOOTSTRAP(0);

    uint32_t *dst00 = &dst[dst_linesize*(pos00>>1) + (pos00&1)];
    uint32_t *dst01 = &dst[dst_linesize*(pos01>>1) + (pos01&1)];
    uint32_t *dst10 = &dst[dst_linesize*(pos10>>1) + (pos10&1)];
    uint32_t *dst11 = &dst[dst_linesize*(pos11>>1) + (pos11&1)];

    const int cond00 = (P(0xbf,0x37) || P(0xdb,0x13)) && WDIFF(w1, w5);
    const int cond01 = (P(0xdb,0x49) || P(0xef,0x6d)) && WDIFF(w7, w3);
    const int cond02 = (P(0x6f,0x2a) || P(0x5b,0x0a) || P(0xbf,0x3a) ||
                        P(0xdf,0x5a) || P(0x9f,0x8a) || P(0xcf,0x8a) ||
                        P(0xef,0x4e) || P(0x3f,0x0e) || P(0xfb,0x5a) ||
                        P(0xbb,0x8a) || P(0x7f,0x5a) || P(0xaf,0x8a) ||
                        P(0xeb,0x8a)) && WDIFF(w3, w1);
    const int cond03 = P(0xdb,0x49) || P(0xef,0x6d);
    const int cond04 = P(0xbf,0x37) || P(0xdb,0x13);
    const int cond05 = P(0x1b,0x03) || P(0x4f,0x43) || P(0x8b,0x83) ||
                       P(0x6b,0x43);
    const int cond06 = P(0x4b,0x09) || P(0x8b,0x89) || P(0x1f,0x19) ||
                       P(0x3b,0x19);
    const int cond07 = P(0x0b,0x08) || P(0xf9,0x68) || P(0xf3,0x62) ||
                       P(0x6d,0x6c) || P(0x67,0x66) || P(0x3d,0x3c) ||
                       P(0x37,0x36) || P(0xf9,0xf8) || P(0xdd,0xdc) ||
                       P(0xf3,0xf2) || P(0xd7,0xd6) || P(0xdd,0x1c) ||
                       P(0xd7,0x16) || P(0x0b,0x02);
    const int cond08 = (P(0x0f,0x0b) || P(0x2b,0x0b) || P(0xfe,0x4a) ||
                        P(0xfe,0x1a)) && WDIFF(w3, w1);
    const int cond09 = P(0x2f,0x2f);
    const int cond10 = P(0x0a,0x00);
    const int cond11 = P(0x0b,0x09);
    const int cond12 = P(0x7e,0x2a) || P(0xef,0xab);
    const int cond13 = P(0xbf,0x8f) || P(0x7e,0x0e);
    const int cond14 = P(0x4f,0x4b) || P(0x9f,0x1b) || P(0x2f,0x0b) ||
                       P(0xbe,0x0a) || P(0xee,0x0a) || P(0x7e,0x0a) ||
                       P(0xeb,0x4b) || P(0x3b,0x1b);
    const int cond15 = P(0x0b,0x03);

    if (cond00)
        *dst00 = interp_2px(w4, 5, w3, 3, 3);
    else if (cond01)
        *dst00 = interp_2px(w4, 5, w1, 3, 3);
    else if ((P(0x0b,0x0b) || P(0xfe,0x4a) || P(0xfe,0x1a)) && WDIFF(w3, w1))
        *dst00 = w4;
    else if (cond02)
        *dst00 = interp_2px(w4, 5, w0, 3, 3);
    else if (cond03)
        *dst00 = interp_2px(w4, 3, w3, 1, 2);
    else if (cond04)
        *dst00 = interp_2px(w4, 3, w1, 1, 2);
    else if (cond05)
        *dst00 = interp_2px(w4, 5, w3, 3, 3);
    else if (cond06)
        *dst00 = interp_2px(w4, 5, w1, 3, 3);
    else if (P(0x0f,0x0b) || P(0x5e,0x0a) || P(0x2b,0x0b) || P(0xbe,0x0a) ||
             P(0x7a,0x0a) || P(0xee,0x0a))
        *dst00 = interp_2px(w1, 1, w3, 1, 1);
    else if (cond07)
        *dst00 = interp_2px(w4, 5, w0, 3, 3);
    else
        *dst00 = interp_3px(w4, 2, w1, 1, w3, 1, 2);

    if (cond00)
        *dst01 = interp_2px(w4, 7, w3, 1, 3);
    else if (cond08)
        *dst01 = w4;
    else if (cond02)
        *dst01 = interp_2px(w4, 3, w0, 1, 2);
    else if (cond09)
        *dst01 = w4;
    else if (cond10)
        *dst01 = interp_3px(w4, 5, w1, 2, w3, 1, 3);
    else if (P(0x0b,0x08))
        *dst01 = interp_3px(w4, 5, w1, 2, w0, 1, 3);
    else if (cond11)
        *dst01 = interp_2px(w4, 5, w1, 3, 3);
    else if (cond04)
        *dst01 = interp_2px(w1, 3, w4, 1, 2);
    else if (cond12)
        *dst01 = interp_3px(w1, 2, w4, 1, w3, 1, 2);
    else if (cond13)
        *dst01 = interp_2px(w1, 5, w3, 3, 3);
    else if (cond05)
        *dst01 = interp_2px(w4, 7, w3, 1, 3);
    else if (P(0xf3,0x62) || P(0x67,0x66) || P(0x37,0x36) || P(0xf3,0xf2) ||
             P(0xd7,0xd6) || P(0xd7,0x16) || P(0x0b,0x02))
        *dst01 = interp_2px(w4, 3, w0, 1, 2);
    else if (cond14)
        *dst01 = interp_2px(w1, 1, w4, 1, 1);
    else
        *dst01 = interp_2px(w4, 3, w1, 1, 2);

    if (cond01)
        *dst10 = interp_2px(w4, 7, w1, 1, 3);
    else if (cond08)
        *dst10 = w4;
    else if (cond02)
        *dst10 = interp_2px(w4, 3, w0, 1, 2);
    else if (cond09)
        *dst10 = w4;
    else if (cond10)
        *dst10 = interp_3px(w4, 5, w3, 2, w1, 1, 3);
    else if (P(0x0b,0x02))
        *dst10 = interp_3px(w4, 5, w3, 2, w0, 1, 3);
    else if (cond15)
        *dst10 = interp_2px(w4, 5, w3, 3, 3);
    else if (cond03)
        *dst10 = interp_2px(w3, 3, w4, 1, 2);
    else if (cond13)
        *dst10 = interp_3px(w3, 2, w4, 1, w1, 1, 2);
    else if (cond12)
        *dst10 = interp_2px(w3, 5, w1, 3, 3);
    else if (cond06)
        *dst10 = interp_2px(w4, 7, w1, 1, 3);
    else if (P(0x0b,0x08) || P(0xf9,0x68) || P(0x6d,0x6c) || P(0x3d,0x3c) ||
             P(0xf9,0xf8) || P(0xdd,0xdc) || P(0xdd,0x1c))
        *dst10 = interp_2px(w4, 3, w0, 1, 2);
    else if (cond14)
        *dst10 = interp_2px(w3, 1, w4, 1, 1);
    else
        *dst10 = interp_2px(w4, 3, w3, 1, 2);

    if ((P(0x7f,0x2b) || P(0xef,0xab) || P(0xbf,0x8f) || P(0x7f,0x0f)) &&
         WDIFF(w3, w1))
        *dst11 = w4;
    else if (cond02)
        *dst11 = interp_2px(w4, 7, w0, 1, 3);
    else if (cond15)
        *dst11 = interp_2px(w4, 7, w3, 1, 3);
    else if (cond11)
        *dst11 = interp_2px(w4, 7, w1, 1, 3);
    else if (P(0x0a,0x00) || P(0x7e,0x2a) || P(0xef,0xab) || P(0xbf,0x8f) ||
             P(0x7e,0x0e))
        *dst11 = interp_3px(w4, 6, w3, 1, w1, 1, 3);
    else if (cond07)
        *dst11 = interp_2px(w4, 7, w0, 1, 3);
    else
        *dst11 = w4;
}

void hqx_filter(const uint32_t *src, uint32_t *dst, const int width, const int height)
{
    for (int y = 0; y < height; ++y) {
        const uint32_t *src32 = src;
        uint32_t *dst32 = dst;
        const int prevline = y > 0          ? -width : 0;
        const int nextline = y < height - 1 ?  width : 0;
        for (int x = 0; x < width; ++x) {
            const int prevcol = x > 0        ? -1 : 0;
            const int nextcol = x < width -1 ?  1 : 0;
            const uint32_t w[3*3] = {
                src32[prevcol + prevline], src32[prevline], src32[prevline + nextcol],
                src32[prevcol           ], src32[       0], src32[           nextcol],
                src32[prevcol + nextline], src32[nextline], src32[nextline + nextcol]
            };
            const uint32_t yuv1 = w[4];
            const int pattern = (w[4] != w[0] ? (yuv_diff(yuv1, w[0])) : 0)
                              | (w[4] != w[1] ? (yuv_diff(yuv1, w[1])) : 0) << 1
                              | (w[4] != w[2] ? (yuv_diff(yuv1, w[2])) : 0) << 2
                              | (w[4] != w[3] ? (yuv_diff(yuv1, w[3])) : 0) << 3
                              | (w[4] != w[5] ? (yuv_diff(yuv1, w[5])) : 0) << 4
                              | (w[4] != w[6] ? (yuv_diff(yuv1, w[6])) : 0) << 5
                              | (w[4] != w[7] ? (yuv_diff(yuv1, w[7])) : 0) << 6
                              | (w[4] != w[8] ? (yuv_diff(yuv1, w[8])) : 0) << 7;

            hq4x_interp_2x2(dst32,                     4 * width, pattern, w, 0,1,2,3, 0,1,2,3,4,5,6,7,8); // 00 01 10 11
            hq4x_interp_2x2(dst32 + 2,                 4 * width, pattern, w, 1,0,3,2, 2,1,0,5,4,3,8,7,6); // 02 03 12 13 (vert mirrored)
            hq4x_interp_2x2(dst32 + 2 * 4 * width,     4 * width, pattern, w, 2,3,0,1, 6,7,8,3,4,5,0,1,2); // 20 21 30 31 (horiz mirrored)
            hq4x_interp_2x2(dst32 + 2 * 4 * width + 2, 4 * width, pattern, w, 3,2,1,0, 8,7,6,5,4,3,2,1,0); // 22 23 32 33 (center mirrored)
            ++src32;
            dst32 += 4;
        }
        src += width;
        dst += width * 4 * 4;
    }
}
