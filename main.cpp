#include <SDL2/SDL.h>

#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

const unsigned maxx = 32;
const unsigned maxy = 24;
#define USE_HQ4  1
struct VRAM {
  uint8_t pix[maxx * maxy * 8];//6144
  uint8_t attr[maxx * maxy];//768
  uint32_t *sinc_fb;
  uint32_t *sinc4_fb;
  VRAM() {
    sinc_fb = new uint32_t[256*192];
    sinc4_fb = new uint32_t[256*192*4*4];
  }
} vram;

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

uint8_t colors[8][3] = {
  {0, 0, 0}, //black
  {0, 0, 1}, //blue
  {1, 0, 0}, //red
  {1, 0, 1}, //magenta
  {0, 1, 0}, //green
  {0, 1, 1}, //cyan
  {1, 1, 0}, //yellow
  {1, 1, 1}, //grey
};

static uint32_t yuv_colors[16];
static uint32_t rgb_colors[16];

static uint32_t rgb2yuv(const uint8_t r, const uint8_t g, const uint8_t b) {
    unsigned y = round(4.784*r + 9.392*g + 1.824*b);
    int u = round(-0.67494*r - 1.32505*g + 2.0*b)+512;
    int v = round(2.0*r - 1.67476*g - 0.32526*b)+512;
    return (y & 0xfff) << 20 | (u & 0x3ff) << 10 | (v & 0x3ff);
}

static void yuv2rgb(const uint32_t yuv, int &r, int &g, int &b) {
  unsigned y = ((yuv >> 20) & 0xfff);
  int u = ((yuv >> 10) & 0x3ff) - 512;
  int v = (yuv & 0x3ff) - 512;
  r = round(6.25e-2 * y             + 0.3505 * v);
  g = round(6.25e-2 * y - 8.604e-2 * u - 0.17853 * v);
  b = round(6.25e-2 * y + 0.443 * u);
}

SDL_Renderer *renderer;

void initVideo()
{
  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window *window = SDL_CreateWindow("ZX Spectrum x4 - SDL", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, maxx*8*4, maxy*8*4, 0);
  renderer = SDL_CreateRenderer(window, -1, 0);
  uint8_t mult = 192;
  for (unsigned i = 0; i < 2; ++i) {
    for (unsigned j = 0; j < 8; ++j) {
      uint8_t r = mult * colors[j][0];
      uint8_t g = mult * colors[j][1];
      uint8_t b = mult * colors[j][2];
      rgb_colors[i * 8 + j] = r << 16 | g << 8 | b;
      yuv_colors[i * 8 + j] = rgb2yuv(r, g, b);
    }
    mult = 255;
  }
}

extern void hqx_filter(const uint32_t *src32, uint32_t *dst32, const int width, const int height);

uint32_t get_sinclair_color(unsigned x, unsigned y) {
    unsigned yb = y >> 3, xb = x >> 3, xbit = x & 0b111, ybit = y &0b111;
    uint8_t attr = vram.attr[yb * maxx + xb];
    uint8_t fg = 0b111 & attr;
    uint8_t bg = 0b111 & (attr >> 3);
    uint8_t bright = (attr >> 6) & 1;
    unsigned off0 = (yb & 0b11000 | ybit) << 8 | (yb & 0b111) << 5 | xb;
    uint8_t pix8 = vram.pix[off0];
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
                vram.sinc_fb[y * 256 + x] = yuv_colors[get_sinclair_color(x, y)];
            else {
                uint8_t ci = get_sinclair_color(x, y);
                SDL_SetRenderDrawColor(renderer, rgb_colors[ci] >> 16, rgb_colors[ci] >> 8, rgb_colors[ci], 255);
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
        hqx_filter(vram.sinc_fb, vram.sinc4_fb, 256, 192);
        for (int y = 0; y < 768; ++y) {
            for (int x = 0; x < 1024; ++x) {
                int r, g, b;
                yuv2rgb(vram.sinc4_fb[y*1024+x], r, g, b);
                SDL_SetRenderDrawColor(renderer, r, g, b, 255);
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
  int len = sizeof(vram.pix) + sizeof(vram.attr);
  int x = read(fd, &vram, len);
  if (x != len)
    exit(-1);
  close(fd);
  initVideo();
#if 1
  do {
    draw();
    SDL_Delay(200);
  } while (handleInput() >= 0);
#endif
  return 0;
}
