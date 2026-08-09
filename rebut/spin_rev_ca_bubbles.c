#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* =========================================================
 * spin_rev_ca_bubbles.c
 * AC de frentes de onda 3D móveis ("folhas" esféricas).
 * Cada bolha é uma fonte pontual que emite uma folha 3D
 * pulsante em um toro 3D. A interação entre folhas usa
 * convolução local no campo de fontes: células do contorno
 * de uma folha contabilizam vizinhos de mesmo/oposto sinal
 * e movem/reemetem o centro.
 *
 * Compilar headless: gcc -DHEADLESS -O2 -std=c11 -o ca_bub spin_rev_ca_bubbles.c
 * Visual SDL3:      cl /Fe:ca_bub.exe spin_rev_ca_bubbles.c SDL3.lib
 * ========================================================= */

#ifndef HEADLESS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#endif

#define L 101
#define N_MAX 8

#define MIN_R2 9
#define MAX_R2 625
#define STEP_R2 11

typedef struct {
    int x, y, z;     /* posição da fonte (PBC) */
    int sgn;         /* +1 up (vermelho), -1 down (azul) */
    int t;           /* fase do pulso local */
    int parent;      /* -1 = cacique livre, >=0 = órbita em torno do parente (não usado ainda) */
} Source;

typedef struct {
    unsigned int r2; /* distância quadrada até a fonte mais próxima */
    int src;         /* índice da fonte mais próxima */
} Cell;

static Cell grid[L][L][L];
static Source srcs[N_MAX];
static int n_src = 2;
static int g_tick = 0;

static int wrap(int x) {
    x %= L;
    if (x < 0) x += L;
    return x;
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

static unsigned int pulse_radius(void) { return pulse_from_time(g_tick); }

static void init(void) {
    int mid = L / 2;
    g_tick = 0;
    n_src = 2;
    srcs[0].x = wrap(mid - 20);
    srcs[0].y = mid;
    srcs[0].z = mid;
    srcs[0].sgn = +1;
    srcs[0].t = 0;
    srcs[0].parent = -1;

    srcs[1].x = wrap(mid + 20);
    srcs[1].y = mid;
    srcs[1].z = mid;
    srcs[1].sgn = -1;
    srcs[1].t = 0;
    srcs[1].parent = -1;
}

static unsigned int dist2_pbc(int x, int y, int z, const Source *s) {
    int dx = delta_pbc(x, s->x);
    int dy = delta_pbc(y, s->y);
    int dz = delta_pbc(z, s->z);
    return (unsigned int)(dx*dx + dy*dy + dz*dz);
}

/* transformada de distância exata com múltiplas fontes (PBC) */
static void update_dist(void) {
    for (int x = 0; x < L; x++)
    for (int y = 0; y < L; y++)
    for (int z = 0; z < L; z++) {
        unsigned int best = 0xFFFFFFFFu;
        int best_src = -1;
        for (int i = 0; i < n_src; i++) {
            unsigned int r2 = dist2_pbc(x, y, z, &srcs[i]);
            if (r2 < best) { best = r2; best_src = i; }
        }
        grid[x][y][z].r2 = best;
        grid[x][y][z].src = best_src;
    }
}

/* convolução local nas folhas: detecta contatos e devolve força em dx,dy,dz */
static void convolve_source(int i, int *fx, int *fy, int *fz, int *threat, int *coh) {
    *fx = *fy = *fz = 0;
    *threat = *coh = 0;
    unsigned int thr = pulse_radius();
    int sx = srcs[i].x, sy = srcs[i].y, sz = srcs[i].z;
    int sgn = srcs[i].sgn;

    for (int x = 0; x < L; x++)
    for (int y = 0; y < L; y++)
    for (int z = 0; z < L; z++) {
        if (grid[x][y][z].src != i) continue;
        unsigned int r2 = grid[x][y][z].r2;
        if (r2 + 2 < thr || r2 > thr + 2) continue;  /* folha = casca fina */

        int dx = delta_pbc(x, sx);
        int dy = delta_pbc(y, sy);
        int dz = delta_pbc(z, sz);

        /* vizinhança 6-conectada */
        const int off[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (int k = 0; k < 6; k++) {
            int nx = wrap(x + off[k][0]);
            int ny = wrap(y + off[k][1]);
            int nz = wrap(z + off[k][2]);
            int js = grid[nx][ny][nz].src;
            if (js < 0 || js == i) continue;
            int jsgn = srcs[js].sgn;
            if (sgn * jsgn < 0) {
                *fx -= dx; *fy -= dy; *fz -= dz; /* ameaça: repel */
                (*threat)++;
            } else {
                *fx += dx; *fy += dy; *fz += dz; /* coesão: atrai */
                (*coh)++;
            }
        }
    }
}

static int sign_of(int v) { return (v > 0) - (v < 0); }

static void move_sources(void) {
    int any_threat = 0;
    int force[N_MAX][3];
    int threat[N_MAX], coh[N_MAX];

    for (int i = 0; i < n_src; i++) {
        convolve_source(i, &force[i][0], &force[i][1], &force[i][2], &threat[i], &coh[i]);
        if (threat[i] > 0) any_threat = 1;
    }

    for (int i = 0; i < n_src; i++) {
        int mx = sign_of(force[i][0]);
        int my = sign_of(force[i][1]);
        int mz = sign_of(force[i][2]);

        srcs[i].x = wrap(srcs[i].x + mx);
        srcs[i].y = wrap(srcs[i].y + my);
        srcs[i].z = wrap(srcs[i].z + mz);
    }

    /* colisão oposta: reemissão sincronizada de todas as fontes */
    if (any_threat) g_tick = 0;
}

static void write_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", L, L);
    int z = L / 2;
    for (int y = 0; y < L; y++) {
        for (int x = 0; x < L; x++) {
            unsigned char r = 0, g = 0, b = 0;
            Cell *c = &grid[x][y][z];
            if (c->src >= 0) {
                unsigned int thr = pulse_radius();
                if (c->r2 + 1 >= thr && c->r2 <= thr + 1) {
                    if (srcs[c->src].sgn > 0) r = 255;
                    else b = 255;
                } else if (c->r2 < thr) {
                    unsigned char shade = 20 + (unsigned char)((c->r2 * 40) / (thr + 1));
                    if (shade > 60) shade = 60;
                    r = g = b = shade;
                }
            }
            for (int i = 0; i < n_src; i++) {
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
        Cell *c = &grid[x][y][z];
        uint32_t pix = 0xFF000000;
        if (c->src >= 0) {
            unsigned int thr = pulse_radius();
            if (c->r2 + 1 >= thr && c->r2 <= thr + 1) {
                pix = (srcs[c->src].sgn > 0) ? 0xFFFF0000 : 0xFF0000FF;
            }
        }
        for (int i = 0; i < n_src; i++) {
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
    int frames = 300;
    int save_every = 20;
    if (argc > 1) frames = atoi(argv[1]);
    if (argc > 2) save_every = atoi(argv[2]);

    init();
    for (int t = 0; t < frames; t++) {
        update_dist();
        if ((t % save_every) == 0 || t == frames - 1) {
            char path[128];
            snprintf(path, sizeof(path), "/tmp/ca_bub_frame_%05d.ppm", t);
            write_ppm(path);
        }
        move_sources();
        g_tick++;
    }
    printf("%d frames, PPM em /tmp/ca_bub_frame_*.ppm\n", frames);
    return 0;
}
#else
int main(int argc, char *argv[]) {
    init();
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *win = SDL_CreateWindow("spin_rev_ca_bubbles", 800, 800, 0);
    SDL_Renderer *ren = SDL_CreateRenderer(win, NULL);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STREAMING, L, L);
    uint32_t *pixels = (uint32_t *)malloc(L * L * sizeof(uint32_t));
    while (1) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) goto cleanup;
        }
        update_dist();
        render(ren, tex, pixels);
        move_sources();
        g_tick++;
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
