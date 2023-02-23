#include "colors.h"

const float ca = .299f;
const float cb = .114f;
const float cc = 1.0/(1.0 - cb)/2.0;
const float cd = 1.0/(1.0 - ca)/2.0;
const float ct = 1.0 - ca - cb;
const float r2y[3][3] = {{ca, ct, cb}, {-cc*ca, -cc*ct, cc*(1-cb)}, {cd*(1-ca), -cd*ct, -cb*cd}};
const float y2r[3][3] = {{1, 0, 1/cd}, {1, -cb/cc/ct, -ca/cd/ct}, {1, 1/cc, 0}};


YUV_pixel yuv_colors[16];

