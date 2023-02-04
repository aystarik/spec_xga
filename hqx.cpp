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

// 12Y + 10U + 10V = 32bit
const uint32_t YMASK = 0xfffu << 20;
const uint32_t UMASK = 0x3ffu << 10;
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
static uint32_t interp_2px(uint32_t c1, unsigned w1, uint32_t c2, unsigned w2)
{
    uint32_t y1 = (c1 >> 20) & 0xfff;
    uint32_t y2 = (c2 >> 20) & 0xfff;
    uint32_t y = ((y1 * w1 + y2 * w2) << (20 - 3)) & YMASK;
    int u1 = int((c1 >> 10) & 0x3ff) - 512;
    int u2 = int((c2 >> 10) & 0x3ff) - 512;
    uint32_t u = ((u1 * w1 + u2 * w2 + (512 << 3)) << (10 - 3)) & UMASK;
    int v1 = int(c1 & 0x3ff) - 512;
    int v2 = int(c2 & 0x3ff) - 512;
    uint32_t v = ((v1 * w1 + v2 * w2 + (512 << 3)) >> 3) & VMASK;
    return y | u | v;
}

/* (c1*w1 + c2*w2 + c3*w3) >> s */
static uint32_t interp_3px(uint32_t c1, int w1, uint32_t c2, int w2, uint32_t c3, int w3)
{
    uint y1 = (c1 >> 20) & 0xfff;
    uint y2 = (c2 >> 20) & 0xfff;
    uint y3 = (c3 >> 20) & 0xfff;
    uint32_t y = ((y1 * w1 + y2 * w2 + y3 * w3) << (20 - 3)) & YMASK;
    int u1 = int((c1 >> 10) & 0x3ff) - 512;
    int u2 = int((c2 >> 10) & 0x3ff) - 512;
    int u3 = int((c3 >> 10) & 0x3ff) - 512;
    uint32_t u = ((u1 * w1 + u2 * w2 + u3 * w3 + (512 << 3)) << (10 - 3)) & UMASK;
    int v1 = int(c1 & 0x3ff) - 512;
    int v2 = int(c2 & 0x3ff) - 512;
    int v3 = int(c3 & 0x3ff) - 512;
    uint32_t v = ((v1 * w1 + v2 * w2 + v3 * w3 + (512 << 3)) >> 3) & VMASK;
    return y | u | v;
}

/* m is the mask of diff with the center pixel that matters in the pattern, and
 * r is the expected result (bit set to 1 if there is difference with the
 * center, 0 otherwise) */
bool P(const uint8_t pattern, const uint8_t mask, const uint8_t result) {return (pattern & mask) == result;}

/* Assuming p0..p8 is mapped to pixels 0..8, this function interpolates the
 * top-left block of 2x2 pixels in the total of the 4x4 pixels (or 4 blocks) to
 * interpolates. The function is also used for the 3 other blocks of 2x2
 * pixels. */
static uint32_t hq4x_interp_2x2_00(const uint8_t pattern, const uint32_t *w)
{
    const bool diff15 = yuv_diff(w[1], w[5]);
    const bool diff73 = yuv_diff(w[7], w[3]);
    const bool diff31 = yuv_diff(w[3], w[1]);
    const bool cond00 = (P(pattern, 0b10111111,0b00110111) || P(pattern, 0b11011011,0b00010011)) && diff15;
    const bool cond01 = (P(pattern, 0b11011011,0b01001001) || P(pattern, 0b11101111,0b01101101)) && diff73;
    const bool cond02 = (P(pattern, 0b01101111,0b00101010) || P(pattern, 0b1011011,0b1010) || P(pattern, 0b10111111,0b111010) || P(pattern, 0b11011111,0b1011010) ||
                         P(pattern, 0b10011111,0b10001010) || P(pattern, 0b11001111,0b10001010) ||
                        P(pattern, 0b11101111,0b1001110) || P(pattern, 0b111111,0b1110) || P(pattern, 0b11111011,0b1011010) || P(pattern, 0b10111011,0b10001010) ||
                         P(pattern, 0b1111111,0b1011010) || P(pattern, 0b10101111,0b10001010) ||
                        P(pattern, 0b11101011,0b10001010)) && diff31;
    const bool cond03 = P(pattern, 0xdb,0x49) || P(pattern, 0xef,0x6d);
    const bool cond04 = P(pattern, 0xbf,0x37) || P(pattern, 0xdb,0x13);
    const bool cond05 = P(pattern, 0x1b,0x03) || P(pattern, 0x4f,0x43) || P(pattern, 0x8b,0x83) || P(pattern, 0x6b,0x43);
    const bool cond06 = P(pattern, 0x4b,0x09) || P(pattern, 0x8b,0x89) || P(pattern, 0x1f,0x19) || P(pattern, 0x3b,0x19);
    const bool cond07 = P(pattern, 0x0b,0x08) || P(pattern, 0xf9,0x68) || P(pattern, 0xf3,0x62) || P(pattern, 0x6d,0x6c) || P(pattern, 0x67,0x66) || P(pattern, 0x3d,0x3c) ||
                       P(pattern, 0x37,0x36) || P(pattern, 0xf9,0xf8) || P(pattern, 0xdd,0xdc) || P(pattern, 0xf3,0xf2) || P(pattern, 0xd7,0xd6) || P(pattern, 0xdd,0x1c) || P(pattern, 0xd7,0x16) || P(pattern, 0x0b,0x02);

    if (cond00)
        return interp_2px(w[4], 5, w[3], 3);
    if (cond01)
        return interp_2px(w[4], 5, w[1], 3);
    if ((P(pattern, 0x0b,0x0b) || P(pattern, 0xfe,0x4a) || P(pattern, 0xfe,0x1a)) && diff31)
        return w[4];
    if (cond02)
        return interp_2px(w[4], 5, w[0], 3);
    if (cond03)
        return interp_2px(w[4], 6, w[3], 2);
    if (cond04)
        return interp_2px(w[4], 6, w[1], 2);
    if (cond05)
        return interp_2px(w[4], 5, w[3], 3);
    if (cond06)
        return interp_2px(w[4], 5, w[1], 3);
    if (P(pattern, 0x0f,0x0b) || P(pattern, 0x5e,0x0a) || P(pattern, 0x2b,0x0b) || P(pattern, 0xbe,0x0a) || P(pattern, 0x7a,0x0a) || P(pattern, 0xee,0x0a))
        return interp_2px(w[1], 4, w[3], 4);
    if (cond07)
        return interp_2px(w[4], 5, w[0], 3);
    return interp_3px(w[4], 4, w[1], 2, w[3], 2);
}

static uint32_t hq4x_interp_2x2_01(const uint8_t pattern, const uint32_t *w)
{
    const bool diff15 = yuv_diff(w[1], w[5]);
    const bool diff31 = yuv_diff(w[3], w[1]);
    const bool cond00 = (P(pattern, 0b10111111,0b00110111) || P(pattern, 0b11011011,0b00010011)) && diff15;
    const bool cond02 = (P(pattern, 0b01101111,0b00101010) || P(pattern, 0b1011011,0b1010) || P(pattern, 0b10111111,0b111010) || P(pattern, 0b11011111,0b1011010) ||
                         P(pattern, 0b10011111,0b10001010) || P(pattern, 0b11001111,0b10001010) ||
                        P(pattern, 0b11101111,0b1001110) || P(pattern, 0b111111,0b1110) || P(pattern, 0b11111011,0b1011010) || P(pattern, 0b10111011,0b10001010) ||
                         P(pattern, 0b1111111,0b1011010) || P(pattern, 0b10101111,0b10001010) ||
                        P(pattern, 0b11101011,0b10001010)) && diff31;
    const bool cond04 = P(pattern, 0xbf,0x37) || P(pattern, 0xdb,0x13);
    const bool cond05 = P(pattern, 0x1b,0x03) || P(pattern, 0x4f,0x43) || P(pattern, 0x8b,0x83) || P(pattern, 0x6b,0x43);
    const bool cond08 = (P(pattern, 0x0f,0x0b) || P(pattern, 0x2b,0x0b) || P(pattern, 0xfe,0x4a) || P(pattern, 0xfe,0x1a)) && diff31;
    const bool cond09 = P(pattern, 0x2f,0x2f);
    const bool cond10 = P(pattern, 0x0a,0x00);
    const bool cond11 = P(pattern, 0x0b,0x09);
    const bool cond12 = P(pattern, 0x7e,0x2a) || P(pattern, 0xef,0xab);
    const bool cond13 = P(pattern, 0xbf,0x8f) || P(pattern, 0x7e,0x0e);
    const bool cond14 = P(pattern, 0x4f,0x4b) || P(pattern, 0x9f,0x1b) || P(pattern, 0x2f,0x0b) || P(pattern, 0xbe,0x0a) || P(pattern, 0xee,0x0a) ||
            P(pattern, 0x7e,0x0a) || P(pattern, 0xeb,0x4b) || P(pattern, 0x3b,0x1b);

    if (cond00)
        return interp_2px(w[4], 7, w[3], 1);
    if (cond08)
        return w[4];
    if (cond02)
        return interp_2px(w[4], 6, w[0], 2);
    if (cond09)
        return w[4];
    if (cond10)
        return interp_3px(w[4], 5, w[1], 2, w[3], 1);
    if (P(pattern, 0x0b,0x08))
        return interp_3px(w[4], 5, w[1], 2, w[0], 1);
    if (cond11)
        return interp_2px(w[4], 5, w[1], 3);
    if (cond04)
        return interp_2px(w[1], 6, w[4], 2);
    if (cond12)
        return interp_3px(w[1], 4, w[4], 2, w[3], 2);
    if (cond13)
        return interp_2px(w[1], 5, w[3], 3);
    if (cond05)
        return interp_2px(w[4], 7, w[3], 1);
    if (P(pattern, 0xf3,0x62) || P(pattern, 0x67,0x66) || P(pattern, 0x37,0x36) || P(pattern, 0xf3,0xf2) || P(pattern, 0xd7,0xd6) || P(pattern, 0xd7,0x16) || P(pattern, 0x0b,0x02))
        return interp_2px(w[4], 6, w[0], 2);
    if (cond14)
        return interp_2px(w[1], 4, w[4], 4);
    return interp_2px(w[4], 6, w[1], 2);
}

static uint32_t hq4x_interp_2x2_10(const uint8_t pattern, const uint32_t *w)
{
    const bool diff73 = yuv_diff(w[7], w[3]);
    const bool diff31 = yuv_diff(w[3], w[1]);
    const bool cond01 = (P(pattern, 0b11011011,0b01001001) || P(pattern, 0b11101111,0b01101101)) && diff73;
    const bool cond02 = (P(pattern, 0b01101111,0b00101010) || P(pattern, 0b1011011,0b1010) || P(pattern, 0b10111111,0b111010) || P(pattern, 0b11011111,0b1011010) ||
                         P(pattern, 0b10011111,0b10001010) || P(pattern, 0b11001111,0b10001010) ||
                        P(pattern, 0b11101111,0b1001110) || P(pattern, 0b111111,0b1110) || P(pattern, 0b11111011,0b1011010) || P(pattern, 0b10111011,0b10001010) ||
                         P(pattern, 0b1111111,0b1011010) || P(pattern, 0b10101111,0b10001010) ||
                        P(pattern, 0b11101011,0b10001010)) && diff31;
    const bool cond03 = P(pattern, 0xdb,0x49) || P(pattern, 0xef,0x6d);
    const bool cond06 = P(pattern, 0x4b,0x09) || P(pattern, 0x8b,0x89) || P(pattern, 0x1f,0x19) || P(pattern, 0x3b,0x19);
    const bool cond08 = (P(pattern, 0x0f,0x0b) || P(pattern, 0x2b,0x0b) || P(pattern, 0xfe,0x4a) || P(pattern, 0xfe,0x1a)) && diff31;
    const bool cond09 = P(pattern, 0x2f,0x2f);
    const bool cond10 = P(pattern, 0x0a,0x00);
    const bool cond12 = P(pattern, 0x7e,0x2a) || P(pattern, 0xef,0xab);
    const bool cond13 = P(pattern, 0xbf,0x8f) || P(pattern, 0x7e,0x0e);
    const bool cond14 = P(pattern, 0x4f,0x4b) || P(pattern, 0x9f,0x1b) || P(pattern, 0x2f,0x0b) || P(pattern, 0xbe,0x0a) || P(pattern, 0xee,0x0a) ||
            P(pattern, 0x7e,0x0a) || P(pattern, 0xeb,0x4b) || P(pattern, 0x3b,0x1b);
    const bool cond15 = P(pattern, 0x0b,0x03);

    if (cond01)
        return interp_2px(w[4], 7, w[1], 1);
    if (cond08)
        return w[4];
    if (cond02)
        return interp_2px(w[4], 6, w[0], 2);
    if (cond09)
        return w[4];
    if (cond10)
        return interp_3px(w[4], 5, w[3], 2, w[1], 1);
    if (P(pattern, 0x0b,0x02))
        return interp_3px(w[4], 5, w[3], 2, w[0], 1);
    if (cond15)
        return interp_2px(w[4], 5, w[3], 3);
    if (cond03)
        return interp_2px(w[3], 6, w[4], 2);
    if (cond13)
        return interp_3px(w[3], 4, w[4], 2, w[1], 2);
    if (cond12)
        return interp_2px(w[3], 5, w[1], 3);
    if (cond06)
        return interp_2px(w[4], 7, w[1], 1);
    if (P(pattern, 0x0b,0x08) || P(pattern, 0xf9,0x68) || P(pattern, 0x6d,0x6c) || P(pattern, 0x3d,0x3c) || P(pattern, 0xf9,0xf8) || P(pattern, 0xdd,0xdc) || P(pattern, 0xdd,0x1c))
        return interp_2px(w[4], 6, w[0], 2);
    if (cond14)
        return interp_2px(w[3], 4, w[4], 4);
    return interp_2px(w[4], 6, w[3], 2);
}

static uint32_t hq4x_interp_2x2_11(const uint8_t pattern, const uint32_t *w)
{
    bool diff31 = yuv_diff(w[3], w[1]);
    const bool cond02 = (P(pattern, 0b01101111,0b00101010) || P(pattern, 0b1011011,0b1010) || P(pattern, 0b10111111,0b111010) || P(pattern, 0b11011111,0b1011010) ||
                         P(pattern, 0b10011111,0b10001010) || P(pattern, 0b11001111,0b10001010) ||
                        P(pattern, 0b11101111,0b1001110) || P(pattern, 0b111111,0b1110) || P(pattern, 0b11111011,0b1011010) || P(pattern, 0b10111011,0b10001010) ||
                         P(pattern, 0b1111111,0b1011010) || P(pattern, 0b10101111,0b10001010) ||
                        P(pattern, 0b11101011,0b10001010)) && diff31;
    const bool cond07 = P(pattern, 0x0b,0x08) || P(pattern, 0xf9,0x68) || P(pattern, 0xf3,0x62) || P(pattern, 0x6d,0x6c) || P(pattern, 0x67,0x66) || P(pattern, 0x3d,0x3c) ||
                       P(pattern, 0x37,0x36) || P(pattern, 0xf9,0xf8) || P(pattern, 0xdd,0xdc) || P(pattern, 0xf3,0xf2) || P(pattern, 0xd7,0xd6) || P(pattern, 0xdd,0x1c) ||
            P(pattern, 0xd7,0x16) || P(pattern, 0x0b,0x02);
    const bool cond11 = P(pattern, 0x0b,0x09);
    const bool cond15 = P(pattern, 0x0b,0x03);


    if ((P(pattern, 0x7f,0x2b) || P(pattern, 0xef,0xab) || P(pattern, 0xbf,0x8f) || P(pattern, 0x7f,0x0f)) && diff31)
        return w[4];
    if (cond02)
        return interp_2px(w[4], 7, w[0], 1);
    if (cond15)
        return interp_2px(w[4], 7, w[3], 1);
    if (cond11)
        return interp_2px(w[4], 7, w[1], 1);
    if (P(pattern, 0x0a,0x00) || P(pattern, 0x7e,0x2a) || P(pattern, 0xef,0xab) || P(pattern, 0xbf,0x8f) || P(pattern, 0x7e,0x0e))
        return interp_3px(w[4], 6, w[3], 1, w[1], 1);
    if (cond07)
        return interp_2px(w[4], 7, w[0], 1);
    return w[4];
}

void hqx_filter(const uint32_t *src, uint32_t *dst, const int width, const int height)
{
    for (int y = 0; y < height; ++y) {
        const int prevline = y > 0          ? -width : 0;
        const int nextline = y < height - 1 ?  width : 0;
        for (int x = 0; x < width; ++x) {
            const int prevcol = x > 0        ? -1 : 0;
            const int nextcol = x < width -1 ?  1 : 0;
            const uint32_t w[3*3] = {
                src[prevcol + prevline], src[prevline], src[prevline + nextcol],
                src[prevcol           ], src[       0], src[           nextcol],
                src[prevcol + nextline], src[nextline], src[nextline + nextcol]
            };
            const bool diff0 = yuv_diff(w[4], w[0]);
            const bool diff1 = yuv_diff(w[4], w[1]);
            const bool diff2 = yuv_diff(w[4], w[2]);
            const bool diff3 = yuv_diff(w[4], w[3]);
            const bool diff5 = yuv_diff(w[4], w[5]);
            const bool diff6 = yuv_diff(w[4], w[6]);
            const bool diff7 = yuv_diff(w[4], w[7]);
            const bool diff8 = yuv_diff(w[4], w[8]);
            uint8_t pattern = diff0 | diff1 << 1| diff2 << 2| diff3 << 3| diff5 << 4| diff6 << 5| diff7 << 6| diff8 << 7;
            unsigned dst_linesize = 4 * width;
            dst[0] = hq4x_interp_2x2_00(pattern, w); // 00 01 10 11
            dst[1] = hq4x_interp_2x2_01(pattern, w); // 00 01 10 11
            dst[dst_linesize] = hq4x_interp_2x2_10(pattern, w); // 00 01 10 11
            dst[dst_linesize + 1] = hq4x_interp_2x2_11(pattern, w); // 00 01 10 11
            //2,1,0,5,3,8,7,6
            pattern = diff2 | diff1 << 1 | diff0 << 2 | diff5 << 3 | diff3 << 4 | diff8 << 5 | diff7 << 6 | diff6 << 7;

            const uint32_t W1[] = {w[2], w[1], 0, w[5], w[4], w[3], 0, w[7]};
            dst[3] = hq4x_interp_2x2_00(pattern, W1); // 02 03 12 13 (vert mirrored)
            dst[2] = hq4x_interp_2x2_01(pattern, W1); // 02 03 12 13 (vert mirrored)
            dst[dst_linesize + 3] = hq4x_interp_2x2_10(pattern, W1); // 02 03 12 13 (vert mirrored)
            dst[dst_linesize + 2] = hq4x_interp_2x2_11(pattern, W1); // 02 03 12 13 (vert mirrored)
            //6,7,8,3,5,0,1,2
            pattern = diff6 | diff7 << 1 | diff8 << 2 | diff3 << 3| diff5 << 4|diff0 << 5| diff1 << 6| diff2 << 7;
            const uint32_t W2[] = {w[6], w[7], 0, w[3], w[4], w[5], 0, w[1]};
            dst[3 * dst_linesize] = hq4x_interp_2x2_00(pattern, W2); // 20 21 30 31 (horiz mirrored)
            dst[3 * dst_linesize + 1] = hq4x_interp_2x2_01(pattern, W2); // 20 21 30 31 (horiz mirrored)
            dst[2 * dst_linesize] = hq4x_interp_2x2_10(pattern, W2); // 20 21 30 31 (horiz mirrored)
            dst[2 * dst_linesize + 1] = hq4x_interp_2x2_11(pattern, W2); // 20 21 30 31 (horiz mirrored)
            //8,7,6,5,3,2,1,0
            pattern = diff8 | diff7 << 1| diff6 << 2| diff5 << 3| diff3 << 4| diff2 << 5| diff1 << 6| diff0 << 7;
            const uint32_t W3[] = {w[8], w[7], 0, w[5], w[4], w[3], 0, w[1]};
            dst[3 * dst_linesize + 3] = hq4x_interp_2x2_00(pattern, W3); // 22 23 32 33 (center mirrored)
            dst[3 * dst_linesize + 2] = hq4x_interp_2x2_01(pattern, W3); // 22 23 32 33 (center mirrored)
            dst[2 * dst_linesize + 3] = hq4x_interp_2x2_10(pattern, W3); // 22 23 32 33 (center mirrored)
            dst[2 * dst_linesize + 2] = hq4x_interp_2x2_11(pattern, W3); // 22 23 32 33 (center mirrored)
            ++src;
            dst += 4;
        }
        dst += width * 4 * 3;
    }
}
