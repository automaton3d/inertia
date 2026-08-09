#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* =========================================================
 * spin_rev_ca_pulsating.c
 * AC 3D estilo pulsating.c, mas com N fontes (folhas) de
 * sinais opostos, fronteiras periodicas e interacao por
 * convolucao (superposicao do campo world + pressao que move
 * as fontes).
 *
 * Compilar headless: gcc -DHEADLESS -O2 -std=c11 -o ca_puls spin_rev_ca_pulsating.c -lm
 * Visual SDL3:      cl /Fe:ca_puls.exe spin_rev_ca_pulsating.c SDL3.lib
 * ========================================================= */

#ifndef HEADLESS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#endif

#define L 101
#define N 2
#define BAND 12

#define MIN_R2 25
#define MAX_R2 ((unsigned int)((L/2)*(L/2)*0.92))
#define STEP_R2 7

#define PRESSURE_THRESHOLD 30

typedef struct {
    int x, y, z;
    int sgn;
    int t;
} Source;

static int world[L][L][L];
static Source srcs[N];

static int wrap(int v) {
    v %= L;
    if (v < 0) v += L;
    return v;
}

static int delta_pbc(int a, int b) {
    int d = a - b;
    if (d > L/2) d -= L;
    else if (d < -L/2) d += L;
    return d;
}

static unsigned int pulse_from_time(int t) {
    unsigned int span = MAX_R2 - MIN_R2;
    unsigned int period = 2 * span;
    unsigned int phase = ((unsigned)t * STEP_R2) % period;
    if (phase < span) return MIN_R2 + phase;
    else return MAX_R2 - (phase - span);
}

static void init(void) {
    memset(world, 0, sizeof(world));
    int mid = L / 2;
    srcs[0].x = mid - 15; srcs[0].y = mid; srcs[0].z = mid; srcs[0].sgn = +1; srcs[0].t = 0;
    srcs[1].x = mid + 15; srcs[1].y = mid; srcs[1].z = mid; srcs[1].sgn = -1; srcs[1].t = 0;
}

static void build_world(void) {
    memset(world, 0, sizeof(world));
    for (int i = 0; i < N; i++) {
        unsigned int target = pulse_from_time(srcs[i].t);
        int sx = srcs[i].x, sy = srcs[i].y, sz = srcs[i].z;
        int sgn = srcs[i].sgn;
        for (int x = 0; x < L; x++)
        for (int y = 0; y < L; y++)
        for (int z = 0; z < L; z++) {
            int dx = delta_pbc(x, sx);
            int dy = delta_pbc(y, sy);
            int dz = delta_pbc(z, sz);
            int r2 = dx*dx + dy*dy + dz*dz;
            int dr = (int)r2 - (int)target;
            if (dr < 0) dr = -dr;
            if (dr <= BAND) {
                world[x][y][z] += sgn * (BAND + 1 - dr);
            }
        }
    }
}

static int sign_of(int v) { return (v > 0) - (v < 0); }

static void move_sources(void) {
    for (int i = 0; i < N; i++) {
        unsigned int target = pulse_from_time(srcs[i].t);
        int sx = srcs[i].x, sy = srcs[i].y, sz = srcs[i].z;
        int sgn = srcs[i].sgn;
        long long fx = 0, fy = 0, fz = 0;
        int pressure = 0;

        for (int x = 0; x < L; x++)
        for (int y = 0; y < L; y++)
        for (int z = 0; z < L; z++) {
            int dx = delta_pbc(x, sx);
            int dy = delta_pbc(y, sy);
            int dz = delta_pbc(z, sz);
            int r2 = dx*dx + dy*dy + dz*dz;
            int dr = (int)r2 - (int)target;
            if (dr < 0) dr = -dr;
            if (dr > BAND) continue;

            int contr = sgn * (BAND + 1 - dr);
            int ext = world[x][y][z] - contr;
            if (sgn * ext < 0) {
                fx -= dx;
                fy -= dy;
                fz -= dz;
                pressure++;
            }
        }

        if (pressure > PRESSURE_THRESHOLD) {
            srcs[i].x = wrap(srcs[i].x + sign_of((int)fx));
            srcs[i].y = wrap(srcs[i].y + sign_of((int)fy));
            srcs[i].z = wrap(srcs[i].z + sign_of((int)fz));
            srcs[i].t = 0;
        }
    }
}

static int clamp255(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

static void write_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", L, L);
    int z = L / 2;
    for (int y = 0; y < L; y++) {
        for (int x = 0; x < L; x++) {
            int w = world[x][y][z];
            unsigned char r = 0, g = 0, b = 0;
            if (w > 0) {
                r = (unsigned char)clamp255(w * 8);
            } else if (w < 0) {
                b = (unsigned char)clamp255(-w * 8);
            }
            for (int i = 0; i < N; i++) {
                if (x == srcs[i].x && y == srcs[i].y && z == srcs[i].z) {
                    r = g = b = 255;
                }
            }
            fwrite(&r, 1, 1, f); fwrite(&g, 1, 1, f); fwrite(&b, 1, 1, f);
        }
    }
    fclose(f);
}

#ifndef HEADLESS
static void render(SDL_Renderer *ren, SDL_Texture *tex, uint32_t *pixels) {
    int z = L / 2;
    for (int y = 0; y < L; y++)
    for (int x = 0; x < L; x++) {
        int w = world[x][y][z];
        uint32_t pix = 0xFF000000;
        if (w > 0) pix = 0xFFFF0000 | (clamp255(w*8) << 16);
        else if (w < 0) pix = 0xFF0000FF | (clamp255(-w*8));
        for (int i = 0; i < N; i++) {
            if (x == srcs[i].x && y == srcs[i].y && z == srcs[i].z) pix = 0xFFFFFFFF;
        }
        pixels[y*L + x] = pix;
    }
    SDL_UpdateTexture(tex, NULL, pixels, L * sizeof(uint32_t));
    SDL_RenderClear(ren);
    SDL_RenderTexture(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
}
#endif

#ifdef HEADLESS
int main(int argc, char *argv[]) {
    int frames = 200;
    int save_every = 10;
    if (argc > 1) frames = atoi(argv[1]);
    if (argc > 2) save_every = atoi(argv[2]);

    init();
    for (int tick = 0; tick < frames; tick++) {
        build_world();
        if ((tick % save_every) == 0 || tick == frames - 1) {
            char path[128];
            snprintf(path, sizeof(path), "/tmp/ca_puls_frame_%05d.ppm", tick);
            write_ppm(path);
        }
        move_sources();
        for (int i = 0; i < N; i++) srcs[i].t++;
    }
    printf("%d frames, PPM em /tmp/ca_puls_frame_*.ppm\n", frames);
    return 0;
}
#else
int main(int argc, char *argv[]) {
    init();
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *win = SDL_CreateWindow("spin_rev_ca_pulsating", 800, 800, 0);
    SDL_Renderer *ren = SDL_CreateRenderer(win, NULL);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STREAMING, L, L);
    uint32_t *pixels = (uint32_t *)malloc(L * L * sizeof(uint32_t));
    while (1) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) goto cleanup;
        }
        build_world();
        render(ren, tex, pixels);
        move_sources();
        for (int i = 0; i < N; i++) srcs[i].t++;
        SDL_Delay(30);
    }
cleanup:
    free(pixels);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
#endif
