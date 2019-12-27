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
    uint8_t pix[maxx * maxy * 8];
    uint8_t attr[maxx * maxy];
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

uint32_t yuv_colors[16];
uint32_t rgb_colors[16];

static uint32_t rgb2yuv(const int r, const int g, const int b) {
    uint8_t y = round(0.257*r + 0.504*g + 0.098*b) + 16;
    uint8_t u = round(-0.148*r - 0.291*g + 0.439*b) + 128;
    uint8_t v = round(0.439*r - 0.368*g - 0.071*b) + 128;
    uint32_t res =  y << 16 | u << 8 | v;
    return res;
}

static void yuv2rgb(const uint32_t yuv, uint8_t &r, uint8_t &g, uint8_t &b) {
    int y = ((yuv >> 16) & 0xff) - 16;
    int u = ((yuv >> 8) & 0xff) - 128;
    int v = (yuv & 0xff) - 128;
    r = (1.164 * y             + 1.596 * v);
    g = (1.164 * y - 0.392 * u - 0.813 * v);
    b = (1.164 * y + 2.017 * u);
}

SDL_Renderer *renderer;

void initVideo()
{
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *window = SDL_CreateWindow("ZX Spectrum x4 - SDL", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, maxx*8*4, maxy*8*4, 0);
    renderer = SDL_CreateRenderer(window, -1, 0);
    for (int i = 0; i < 2; ++i) {
        uint8_t mult = 192 + i * (255 - 192);
        for (int j = 0; j < 8; ++j) {
            uint8_t r = mult * colors[j][0];
            uint8_t g = mult * colors[j][1];
            uint8_t b = mult * colors[j][2];
            rgb_colors[i * 8 + j] = r << 16 | g << 8 | b;
            yuv_colors[i * 8 + j] = rgb2yuv(r, g, b);
        }
    }
}

extern void hqx_filter(const uint32_t *src32, uint32_t *dst32, const int width, const int height);

void drawScreen() {
    bool use_hq4 = false;
    if (compare)
        use_hq4 = true;
    for (unsigned y = 0; y < maxy; ++y) {
        for (unsigned x = 0; x < maxx; ++x) {
            uint8_t attr = vram.attr[y * maxx + x];
            uint8_t fg = 0b111 & attr;
            attr >>= 3;
            uint8_t bk = 0b111 & attr;
            attr >>= 3;
            uint8_t bright = 1 & attr;
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
                        int xo = (x * 8 + ix) * 4;
                        int yo = (y * 8 + iy) * 4;
                        for (unsigned i = 0; i < 4; ++i)
                            for (unsigned j = 0; j < 4; ++j)
                                SDL_RenderDrawPoint(renderer, xo + i, yo + j);
                    }
				}
			}
		}
	}
    if (use_hq4) {
        hqx_filter(vram.sinc_fb, vram.sinc4_fb, 256, 192);
        for (int y = 0; y < 768; ++y) {
            for (int x = 0; x < 1024; ++x) {
                uint8_t r, g, b;
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
	do {
		draw();
        SDL_Delay(200);
	} while (handleInput() >= 0);
	return 0;
}
