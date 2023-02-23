#pragma once

#include <stdint.h>
#include <stdio.h>
#include <cmath>

struct RGB_pixel {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct YUV_pixel {
    uint16_t y;
    int16_t u;
    int16_t v;
};

extern const float r2y[3][3];
extern const float y2r[3][3];

inline void rgb2yuv(YUV_pixel &yuv, const RGB_pixel &rgb) {
    yuv.y = r2y[0][0]*rgb.r + r2y[0][1] * rgb.g + r2y[0][2] * rgb.b + 0.5;
    yuv.u = r2y[1][0]*rgb.r + r2y[1][1] * rgb.g + r2y[1][2] * rgb.b + 0.5;
    yuv.v = r2y[2][0]*rgb.r + r2y[2][1] * rgb.g + r2y[2][2] * rgb.b + 0.5;
}

inline void yuv2rgb(RGB_pixel &rgb, const YUV_pixel &yuv) {
    rgb.r = y2r[0][0]*yuv.y + y2r[0][1] * yuv.u + y2r[0][2] * yuv.v + 0.5;
    rgb.g = y2r[1][0]*yuv.y + y2r[1][1] * yuv.u + y2r[1][2] * yuv.v + 0.5;
    rgb.b = y2r[2][0]*yuv.y + y2r[2][1] * yuv.u + y2r[2][2] * yuv.v + 0.5;
}

#if 0
bool yuv_diff(const YUV_pixel &c1, const YUV_pixel &c2)
{
    bool ydiff = std::abs(c1.y - c2.y) > 48;
    bool udiff = std::abs(c1.u - c2.u) > 7;
    bool vdiff = std::abs(c1.v - c2.v) > 6;
    return ydiff || udiff || vdiff;
}
#endif

inline bool diff(uint8_t i, uint8_t j) {
    return (i != j && (i != 0 || j != 8) && (i != 8 || j != 0));
}

inline void sinc2rgb(RGB_pixel &rgb, uint8_t i) {
    rgb.b = (i & 1) * (((i >> 3)&1) + 3) * 60;
    rgb.g = ((i >> 1) & 1) * (((i >> 3)&1) + 3) * 60;
    rgb.r = ((i >> 2) & 1) * (((i >> 3)&1) + 3) * 60;
}

inline void sinc2yuv(YUV_pixel &yuv, uint8_t i) {
    RGB_pixel rgb;
    sinc2rgb(rgb, i);
    rgb2yuv(yuv, rgb);
}

extern YUV_pixel yuv_colors[16];

inline void color_init() {
    for (unsigned i = 0; i < 16; ++i) {
        YUV_pixel yuv;
        sinc2yuv(yuv, i);
        yuv_colors[i] = yuv;
    }
}

#if 0
int main() {
    for (unsigned i = 0; i < 16; ++i) {
        RGB_pixel rgb, rgb2;
        YUV_pixel yuv;
        sinc2rgb(rgb, i);
        rgb2yuv(yuv, rgb);
        yuv_indexed[i] = yuv;
        yuv2rgb(rgb2, yuv);
        //printf("rgb(%d, %d, %d)->yuv(%d, %d, %d)->rgb(%d, %d, %d)\n", rgb.r, rgb.g, rgb.b, yuv.y, yuv.u, yuv.v, rgb2.r, rgb2.g, rgb2.b);
    }
#if 1
    for (unsigned i = 0; i < 16; ++i) {
        for (unsigned j = 0; j < 16; ++j) {
            if (i == j) continue;
            //if (!yuv_diff(yuv_indexed[i], yuv_indexed[j])) {
            //    printf("%d-%d are identical\n", i, j);
            //}
            if (!diff(i, j)) {
                printf("%d-%d are identical\n", i, j);
            }
        }
    }
#endif
    return 0;
}
#endif
