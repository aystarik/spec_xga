#include <SDL2/SDL.h>

#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "colors.h"

const unsigned MAXX = 32;
const unsigned MAXY = 24;

namespace vram {
  uint8_t pix[MAXY*8][MAXX]; //6144
  uint8_t attr[MAXY][MAXX]; //768
}

uint8_t sinc_fb[192*256];
YUV_pixel sinc4_fb[768*1024];

static bool compare = false;

int handleInput()
{
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_QUIT:
      return -1;
    case SDL_KEYDOWN:
      if (event.key.keysym.sym == SDLK_c) {
        compare = !compare;
        return 0;
      }
      if (event.key.keysym.sym == SDLK_ESCAPE)
        return -2;
      break;
    default:
      break;
    }
  }
  return 0;
}

SDL_Renderer *renderer;

void initVideo()
{
  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window *window = SDL_CreateWindow("ZX Spectrum x4 - SDL", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, MAXX*8*4, MAXY*8*4, 0);
  renderer = SDL_CreateRenderer(window, -1, 0);
}

extern void hqx_filter(const uint8_t *src, YUV_pixel *dst, unsigned width, unsigned height);

uint8_t get_sinclair_color(unsigned x, unsigned y) {
    unsigned yb = y >> 3, xb = x >> 3, xbit = x & 0b111, ybit = y &0b111;
    uint8_t attr = vram::attr[yb][xb];
    uint8_t fg = 0b111 & attr;
    uint8_t bg = 0b111 & (attr >> 3);
    uint8_t bright = (attr >> 6) & 1;
    uint8_t pix8 = vram::pix[(yb & 0b11000 | ybit) << 3 | (yb & 0b111)][xb];
    return (bright << 3) | ((pix8 & (1 << (7 - xbit)))?fg:bg);
    //return yuv_colors[ci];
}

void calc_area() {
    uint32_t w[9];
    for (unsigned y = 0; y < 192; ++y) {
        unsigned yprev = (y > 0) ? y - 1 : y;
        unsigned ynext = (y < 192 - 1)? y + 1:y;
        for (unsigned x = 0; x < 256; ++x) {
            unsigned xprev = (x > 0) ? x - 1 : x;
            unsigned xnext = (x < 256 - 1) ? x + 1: x;
            w[0] = get_sinclair_color(xprev, yprev);
            w[1] = get_sinclair_color(x, yprev);
            w[2] = get_sinclair_color(xnext, yprev);
            w[3] = get_sinclair_color(xprev, y);
            w[4] = get_sinclair_color(x, y);
            w[5] = get_sinclair_color(xnext, y);
            w[6] = get_sinclair_color(xprev, ynext);
            w[7] = get_sinclair_color(x, ynext);
            w[8] = get_sinclair_color(xnext, ynext);
        }
    }
}
void drawScreen() {
    bool use_hq4 = false;
    if (compare)
        use_hq4 = true;
#if 1
    for (unsigned y = 0; y < 192; ++y) {
        for (unsigned x = 0; x < 256; ++x) {
            if (use_hq4)
                sinc_fb[y*256 + x] = get_sinclair_color(x, y);
            else {
                RGB_pixel rgb;
                uint8_t ci = get_sinclair_color(x, y);
                sinc2rgb(rgb, ci);
                SDL_SetRenderDrawColor(renderer, rgb.r, rgb.g, rgb.b, 255);
                int xo = x * 4;
                int yo = y * 4;
                for (unsigned i = 0; i < 4; ++i)
                    for (unsigned j = 0; j < 4; ++j)
                        SDL_RenderDrawPoint(renderer, xo + i, yo + j);
            }
        }
    }
#else
    for (unsigned y = 0; y < maxy; ++y) {
        for (unsigned x = 0; x < maxx; ++x) {
            uint8_t attr = vram.attr[y * maxx + x];
            uint8_t fg = 0b111 & attr;
            uint8_t bk = 0b111 & (attr >> 3);
            uint8_t bright = 1 & (attr >> 6);
            //attr >>= 1;
            //uint8_t flash = 1 & attr;
            int off0 = (y & 0b11000) << 8 | (y & 0b111) << 5 | x;
            for (unsigned iy = 0; iy < 8; ++iy) {
                uint8_t pix8 = vram.pix[off0 | iy << 8];
                for (unsigned ix = 0; ix < 8; ++ix, pix8 <<= 1) {
                    uint8_t pix = !!(0x80 & pix8);
                    unsigned ci = (bright << 3)|(pix ? fg : bk);
                    if (use_hq4) {
                        int xo = (x * 8 + ix);
                        int yo = (y * 8 + iy);
                        vram.sinc_fb[yo * 256 + xo] = yuv_colors[ci];
                    } else {
                        SDL_SetRenderDrawColor(renderer, rgb_colors[ci] >> 16, rgb_colors[ci] >> 8, rgb_colors[ci], 255);
                        int xo = (x * 8 + ix);
                        int yo = (y * 8 + iy);
                        uint8_t ci = get_sinclair_color(xo, yo);
                        for (unsigned i = 0; i < 4; ++i)
                            for (unsigned j = 0; j < 4; ++j)
                                SDL_RenderDrawPoint(renderer, xo*4 + i, yo*4 + j);
                    }
                }
            }
        }
    }
#endif
    if (use_hq4) {
        hqx_filter(sinc_fb, sinc4_fb, 256,192);
        for (int y = 0; y < 768; ++y) {
            for (int x = 0; x < 1024; ++x) {
                RGB_pixel rgb;
                yuv2rgb(rgb, sinc4_fb[y*1024+x]);
                SDL_SetRenderDrawColor(renderer, rgb.r, rgb.g, rgb.b, 255);
                SDL_RenderDrawPoint(renderer, x, y);
            }
        }
    }
}

void draw()
{
  SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
  SDL_RenderClear(renderer);
  drawScreen();
  SDL_RenderPresent(renderer);
}

int main(int argc, char *argv[])
{
  //top = new Vmy_computer;
  if (argc < 2)
    exit(-1);
  int fd = open(argv[1], O_RDONLY);
  if (fd == -1)
    exit(fd);
  int len = sizeof(vram::pix) + sizeof(vram::attr);
  int x = read(fd, &vram::pix, len);
  if (x != len)
    exit(-1);
  close(fd);
  color_init();
  initVideo();
#if 1
  do {
    draw();
    SDL_Delay(200);
  } while (handleInput() >= 0);
#endif
  return 0;
}
