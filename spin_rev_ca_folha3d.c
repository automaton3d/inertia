#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* =========================================================
 * spin_rev_ca_folha3d.c
 * Autômato celular com dupla grade por folha (corrente/rascunho).
 * Cada cacique/bolha é uma folha 3D (L x L x L) com fronteiras
 * periódicas; as folhas se sobrepõem num espaço 3D comum e
 * interagem por convolução do campo world. Visualização 3D
 * projetada (scatter) no estilo da interface contínua.
 *
 * Headless: gcc -DHEADLESS -O2 -std=c11 -o ca_f3d spin_rev_ca_folha3d.c -lm
 * SDL3:     cl /Fe:ca_f3d.exe spin_rev_ca_folha3d.c SDL3.lib
 * ========================================================= */

#ifndef HEADLESS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#endif

/* capacidades maximas */
#define N_K 2
#define N_S 20
#define N_P 1                /* numero de pares P; cada par ocupa 2 folhas */
#define N_FOLHAS (N_K + N_S + 2 * N_P)

static int L = 191;              /* tamanho da grade; redefinido em init() */
static int cenario = 3;          /* 1 = 2K+20S+1P; 2 = 1K+1P (L=701); 3 = 1K+8S+1P (particula movel) */
static int n_k = 0, n_s = 0, n_p = 0, n_folhas = 0;

#define MAX_SHELL_R (L/2)
#define MIN_R2      0
#define MAX_R2      (MAX_SHELL_R * MAX_SHELL_R)
#define BAND        (L / 8)
#define STEP_R2     (L / 8)
#define SHELL_AMP   6000

#define S_STEP_R2   (L / 12)
#define S_BAND      (L / 12)
#define S_SHELL_AMP 3000
#define SHELL_MARGIN 2     /* folgas em celulas para detecao geometrica de cascas ocas */

/* fase = tempo do pulso em r^2. A casca expande em r^2 ate contato;
   quando as folhas se tocam, a fonte K reemite do raio minimo. */

enum Vista { VISTA_XY, VISTA_XZ, VISTA_YZ, VISTA_ISO };

/* grades alocadas dinamicamente em init() */
static short  *folha = NULL;       /* [n_folhas][L][L][L] : amplitude da folha */
static int    *world = NULL;       /* [L][L][L] : campo convolucao */
static signed char *owner = NULL;  /* [L][L][L] : kind dominante */
static short  *dom_mag = NULL;    /* [L][L][L] : amplitude dominante */

#define VOL ((size_t)(L) * (L) * (L))

static inline size_t idx3(int x, int y, int z) { return ((size_t)(x) * L + (y)) * L + (z); }
static inline size_t idx4(int b, int x, int y, int z) { return (size_t)(b) * VOL + idx3(x, y, z); }

#define FOLHA(b,x,y,z) (folha[idx4((b),(x),(y),(z))])
#define WORLD(x,y,z)   (world[idx3((x),(y),(z))])
#define OWNER(x,y,z)   (owner[idx3((x),(y),(z))])
#define DOM_MAG(x,y,z) (dom_mag[idx3((x),(y),(z))])

typedef struct {
    int kind;          /* 0=K, 1=S (solto), 2=D (delegado capturado), 3=P (constituinte de par/foton) */
    int parent;        /* para D: cacique a que pertence */
    int pair;          /* indice do parceiro no par P; -1 se solto */
    int q;             /* carga: K e S sempre 1; par P tem q=0 e q=1 */
    int wx, wy, wz;
    int sgn;
    int sx, sy, sz;    /* vetor spin (imutavel) */
    int mx, my, mz;    /* direcao M (uso principal: P) */
    int spin_target;   /* +1=S para fora, -1=S para dentro (D) */
    int ix, iy, iz;    /* impulso acumulado (momentum) */
    int phase;
    unsigned int max_r2;
    unsigned int step_r2;
    int band;
    int amp;
} Source;

static Source srcs[N_FOLHAS];

#ifndef HEADLESS
static enum Vista vista_atual = VISTA_ISO;
static float zoom = 1.0f;
static float pan_x = 0.0f, pan_y = 0.0f, pan_z = 0.0f;
static int window_width = 800;
static int window_height = 800;
static int mostrar_vetores = 0;
#endif

static int mostrar_somente_caciques = 0;  /* tecla K: mostra K + P */

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

static int abss(int x) { return x < 0 ? -x : x; }
static int shells_intersect(int a, int b);  /* definida depois de move_sources */

/* cor por tipo de bolha: K=avermelhado, S=azulado, D=amarelo;
   mag (0..255) controla o quao acesa/pura e a cor. */
static void color_for_kind(int kind, int mag,
                           unsigned char *r, unsigned char *g, unsigned char *b) {
    if (mag > 255) mag = 255;
    int dim = 255 - mag;
    if (kind == 0) {       /* K */
        *r = 255; *g = (unsigned char)dim; *b = (unsigned char)dim;
    } else if (kind == 1) { /* S */
        *r = (unsigned char)dim; *g = (unsigned char)dim; *b = 255;
    } else if (kind == 2) { /* D */
        *r = 255; *g = 255; *b = (unsigned char)dim;
    } else {                 /* P: ciano puro */
        *r = 0;
        *g = 255;
        *b = 255;
    }
}

#ifndef HEADLESS
static uint32_t argb_for_kind(int kind, int mag) {
    unsigned char r, g, b;
    color_for_kind(kind, mag, &r, &g, &b);
    return (0xFFu << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}
#endif

static unsigned int isqrt(unsigned int x) {
    if (x == 0) return 0;
    unsigned int r = 1;
    while ((unsigned long long)(r + 1) * (r + 1) <= x) r++;
    return r;
}

static unsigned int pulse_r2(int tick, unsigned int step_r2, unsigned int max_r2) {
    (void)step_r2;
    /* O raio cresce uma célula por frame (passo de luz).
       target = r^2, com r = tick, ate max_r2. */
    unsigned long long r = (unsigned)tick;
    unsigned long long r2 = r * r;
    if (r2 > max_r2) r2 = max_r2;
    return (unsigned int)r2;
}

static void local_to_world(int b, int lx, int ly, int lz,
                           int *wx, int *wy, int *wz) {
    int mid = L / 2;
    *wx = wrap(srcs[b].wx + (lx - mid));
    *wy = wrap(srcs[b].wy + (ly - mid));
    *wz = wrap(srcs[b].wz + (lz - mid));
}

static void init(void) {
    if (cenario == 1) {
        L = 191;
        n_k = N_K; n_s = N_S; n_p = N_P;
    } else if (cenario == 2) {
        L = 701;
        n_k = 1; n_s = 0; n_p = N_P;
    } else {
        /* Cenario 3: partícula móvel (1 K + nuvem de S + 1 par P) */
        L = 191;
        n_k = 1; n_s = N_S; n_p = N_P;
    }
    n_folhas = n_k + n_s + 2 * n_p;

    free(folha); free(world); free(owner); free(dom_mag);
    folha   = (short *)calloc((size_t)n_folhas * VOL, sizeof(short));
    world   = (int *)calloc((size_t)VOL, sizeof(int));
    owner   = (signed char *)calloc((size_t)VOL, sizeof(signed char));
    dom_mag = (short *)calloc((size_t)VOL, sizeof(short));

    int mid = L / 2;
    int sep = L / 4;

    for (int b = 0; b < n_folhas; b++) {
        srcs[b].kind = 1; srcs[b].parent = -1; srcs[b].pair = -1; srcs[b].q = 1;
        srcs[b].phase = 0; srcs[b].spin_target = 0;
        srcs[b].ix = 0; srcs[b].iy = 0; srcs[b].iz = 0;
    }

    /* K0 */
    srcs[0].kind = 0; srcs[0].parent = -1; srcs[0].q = 1;
    srcs[0].sgn = +1;
    srcs[0].sx = +1; srcs[0].sy = 0; srcs[0].sz = 0;
    srcs[0].mx = +1; srcs[0].my = 0; srcs[0].mz = 0;
    srcs[0].max_r2 = MAX_R2; srcs[0].step_r2 = STEP_R2; srcs[0].band = BAND; srcs[0].amp = SHELL_AMP;

    if (cenario == 1) {
        srcs[0].wx = mid - sep; srcs[0].wy = mid; srcs[0].wz = mid;

        /* K1 */
        srcs[1].kind = 0; srcs[1].parent = -1; srcs[1].q = 1;
        srcs[1].wx = mid + sep; srcs[1].wy = mid; srcs[1].wz = mid; srcs[1].sgn = -1;
        srcs[1].sx = -1; srcs[1].sy = 0; srcs[1].sz = 0;
        srcs[1].mx = -1; srcs[1].my = 0; srcs[1].mz = 0;
        srcs[1].max_r2 = MAX_R2; srcs[1].step_r2 = STEP_R2; srcs[1].band = BAND; srcs[1].amp = SHELL_AMP;
    } else if (cenario == 2) {
        /* Cenario 2: K mais proximo do P, com pequeno desvio em y/z;
           raios iniciais pequenos e diferentes. */
        srcs[0].wx = mid + sep / 4; srcs[0].wy = mid + 3; srcs[0].wz = mid + 2;
        srcs[0].phase = 3;
    } else {
        /* Cenario 3: K no centro com nuvem de S; P vem da esquerda em +x */
        srcs[0].wx = mid; srcs[0].wy = mid; srcs[0].wz = mid;
        srcs[0].phase = 3;
    }

    if (cenario == 1 || cenario == 3) {
        /* S: posicoes aleatorias dentro de uma regiao elipsoidal proxima a cada K,
           orientada com o eixo maior apontando para o outro K. */
        int per_K = n_s / n_k;
        int a = sep / 2;          /* semi-eixo x (para o outro K) */
        int bb = sep / 4;         /* semi-eixos y e z */
        long a2 = (long)a * a;
        long b2 = (long)bb * bb;
        long limit = a2 * b2 * b2;
        long bc = b2 * b2;
        long ac = a2 * b2;
        long ab = a2 * b2;        /* ac == ab pois b==c */
        srand(42);
        for (int k = 0; k < n_k; k++) {
            int dir = (k == 0) ? +1 : -1; /* K0: +x, K1: -x */
            for (int s = 0; s < per_K; s++) {
                int b = n_k + k * per_K + s;
                srcs[b].kind = 1; srcs[b].parent = -1; srcs[b].q = 1;
                int rx, dy, dz;
                int attempts = 0;
                do {
                    rx = rand() % (a + 1);         /* 0 .. a */
                    dy = (rand() % (2 * bb + 1)) - bb;
                    dz = (rand() % (2 * bb + 1)) - bb;
                    attempts++;
                } while (attempts < 200 &&
                         ((long)rx * rx * bc + (long)dy * dy * ac + (long)dz * dz * ab > limit ||
                          (long)rx * rx + (long)dy * dy + (long)dz * dz < 9));
                int dx = dir * rx;
                srcs[b].wx = wrap(srcs[k].wx + dx);
                srcs[b].wy = wrap(srcs[k].wy + dy);
                srcs[b].wz = wrap(srcs[k].wz + dz);
                srcs[b].sgn = (rand() % 2) ? +1 : -1;
                int axis = rand() % 3;
                int sdir = (rand() % 2) ? +1 : -1;
                srcs[b].sx = (axis == 0) ? sdir : 0;
                srcs[b].sy = (axis == 1) ? sdir : 0;
                srcs[b].sz = (axis == 2) ? sdir : 0;
                srcs[b].mx = srcs[b].sx;
                srcs[b].my = srcs[b].sy;
                srcs[b].mz = srcs[b].sz;
                srcs[b].max_r2 = MAX_R2; srcs[b].step_r2 = S_STEP_R2; srcs[b].band = S_BAND; srcs[b].amp = S_SHELL_AMP;
            }
        }
    }

    /* P: par/foton solto formado por dois S superpostos (q=1 e q=0) com M igual. */
    for (int ip = 0; ip < n_p; ip++) {
        int p = n_k + n_s + 2 * ip;
        int p2 = p + 1;
        srcs[p].kind = 3; srcs[p].parent = -1; srcs[p].q = 1;
        srcs[p2].kind = 3; srcs[p2].parent = -1; srcs[p2].q = 0;
        srcs[p].pair = p2; srcs[p2].pair = p;
        srcs[p].sgn = +1; srcs[p2].sgn = -1;  /* cargas opostas: campos cancelam */
        if (cenario == 1) {
            int paxis = rand() % 3;
            int pdir = (rand() % 2) ? +1 : -1;
            srcs[p].mx = (paxis == 0) ? pdir : 0;
            srcs[p].my = (paxis == 1) ? pdir : 0;
            srcs[p].mz = (paxis == 2) ? pdir : 0;
        } else {
            srcs[p].mx = +1; srcs[p].my = 0; srcs[p].mz = 0;
        }
        srcs[p2].mx = srcs[p].mx; srcs[p2].my = srcs[p].my; srcs[p2].mz = srcs[p].mz;
        srcs[p].sx = srcs[p].mx; srcs[p].sy = srcs[p].my; srcs[p].sz = srcs[p].mz;
        srcs[p2].sx = srcs[p].mx; srcs[p2].sy = srcs[p].my; srcs[p2].sz = srcs[p].mz;
        srcs[p].phase = 0; srcs[p2].phase = 0;
        srcs[p].max_r2 = MAX_R2; srcs[p].step_r2 = S_STEP_R2; srcs[p].band = S_BAND; srcs[p].amp = S_SHELL_AMP;
        srcs[p2].max_r2 = MAX_R2; srcs[p2].step_r2 = S_STEP_R2; srcs[p2].band = S_BAND; srcs[p2].amp = S_SHELL_AMP;

        if (cenario == 1) {
            srcs[p].wx = mid; srcs[p].wy = mid; srcs[p].wz = mid;
        } else if (cenario == 2) {
            srcs[p].wx = mid; srcs[p].wy = mid; srcs[p].wz = mid;
        } else {
            srcs[p].wx = wrap(mid - 3 * sep / 4); srcs[p].wy = mid; srcs[p].wz = mid;
        }
        srcs[p2].wx = srcs[p].wx; srcs[p2].wy = srcs[p].wy; srcs[p2].wz = srcs[p].wz;
    }
}

static void build_world(void) {
    memset(world, 0, (size_t)VOL * sizeof(int));
    memset(owner, -1, (size_t)VOL * sizeof(signed char));
    memset(dom_mag, 0, (size_t)VOL * sizeof(short));
    for (int b = 0; b < n_folhas; b++) {
        for (int x = 0; x < L; x++)
        for (int y = 0; y < L; y++)
        for (int z = 0; z < L; z++) {
            int wx, wy, wz;
            local_to_world(b, x, y, z, &wx, &wy, &wz);
            short u = FOLHA(b,x,y,z);
            WORLD(wx,wy,wz) += u;
            if (u == 0) continue;
            short au = u < 0 ? -u : u;
            if (OWNER(wx,wy,wz) == (signed char)-1 || au > DOM_MAG(wx,wy,wz)) {
                OWNER(wx,wy,wz) = (signed char)srcs[b].kind;
                DOM_MAG(wx,wy,wz) = au;
            }
        }
    }
}

static int sign_of(int v) { return (v > 0) - (v < 0); }

static void exchange_impulse(int a, int b);
static int has_contact(int b);

static void clear_leaf(int b) {
    memset(folha + (size_t)b * VOL, 0, (size_t)VOL * sizeof(short));
}

static void move_sources(void) {
    int cap_by[N_FOLHAS];
    for (int i = 0; i < n_folhas; i++) cap_by[i] = -1;

    /* K interagem com K e capturam S. S/D livres nao se movem sozinhos. */
    for (int b = 0; b < n_folhas; b++) {
        if (srcs[b].kind != 0) continue;
        long long fx = 0, fy = 0, fz = 0;
        int overlap_k = 0;

        /* K x K: cascas de caciques se intersectam -> colapso */
        for (int other = 0; other < n_k; other++) {
            if (other == b) continue;
            if (!shells_intersect(b, other)) continue;
            overlap_k = 1;
            fx += delta_pbc(srcs[b].wx, srcs[other].wx);
            fy += delta_pbc(srcs[b].wy, srcs[other].wy);
            fz += delta_pbc(srcs[b].wz, srcs[other].wz);
        }

        /* K x S: deteccao geometrica das cascas ocas com margem (inteiro) */
        if (!overlap_k) {
            unsigned int rK2 = pulse_r2(srcs[b].phase, srcs[b].step_r2, srcs[b].max_r2);
            int rK = isqrt(rK2);
            for (int c = 0; c < n_folhas; c++) {
                if (srcs[c].kind != 1 || cap_by[c] != -1) continue;
                unsigned int rS2 = pulse_r2(srcs[c].phase, srcs[c].step_r2, srcs[c].max_r2);
                int rS = isqrt(rS2);
                int dx = delta_pbc(srcs[b].wx, srcs[c].wx);
                int dy = delta_pbc(srcs[b].wy, srcs[c].wy);
                int dz = delta_pbc(srcs[b].wz, srcs[c].wz);
                unsigned int d2 = (unsigned int)dx*dx + (unsigned int)dy*dy + (unsigned int)dz*dz;
                int d = isqrt(d2);
                int sum = rK + rS + SHELL_MARGIN;
                int diff = abss(rK - rS) - SHELL_MARGIN;
                if (diff < 0) diff = 0;
                if (d >= diff && (unsigned int)d <= (unsigned int)sum &&
                    (unsigned long long)d2 >= (unsigned long long)diff*diff &&
                    (unsigned long long)d2 <= (unsigned long long)sum*sum) {
                    cap_by[c] = b;
                }
            }
        }

        if (overlap_k) {
            srcs[b].wx = wrap(srcs[b].wx + sign_of((int)fx));
            srcs[b].wy = wrap(srcs[b].wy + sign_of((int)fy));
            srcs[b].wz = wrap(srcs[b].wz + sign_of((int)fz));
            srcs[b].phase = 0;
            clear_leaf(b);
            /* KxK tem prioridade sobre KxS */
            for (int c = 0; c < n_folhas; c++) if (cap_by[c] == b) cap_by[c] = -1;
        }
    }

    /* K x S: S capturado vira D (delegado) e da um passo para dentro do K.
       spin_target: +1 se S aponta para fora de K, -1 se aponta para dentro. */
    for (int c = 0; c < n_folhas; c++) {
        if (srcs[c].kind != 1 || cap_by[c] == -1) continue;
        int b = cap_by[c];
        int dx = delta_pbc(srcs[b].wx, srcs[c].wx);
        int dy = delta_pbc(srcs[b].wy, srcs[c].wy);
        int dz = delta_pbc(srcs[b].wz, srcs[c].wz);
        long long dot = (long long)srcs[c].sx * dx
                      + (long long)srcs[c].sy * dy
                      + (long long)srcs[c].sz * dz;
        srcs[c].spin_target = (dot < 0) ? 1 : -1;
        srcs[c].wx = wrap(srcs[c].wx + sign_of(dx));
        srcs[c].wy = wrap(srcs[c].wy + sign_of(dy));
        srcs[c].wz = wrap(srcs[c].wz + sign_of(dz));
        srcs[c].kind = 2;
        srcs[c].parent = b;
        srcs[c].phase = 0;
        clear_leaf(c);
    }
}

static int radius_of(int b) {
    return isqrt(pulse_r2(srcs[b].phase, srcs[b].step_r2, srcs[b].max_r2));
}

static int shells_intersect(int a, int b) {
    int rA = radius_of(a);
    int rB = radius_of(b);
    int dx = delta_pbc(srcs[a].wx, srcs[b].wx);
    int dy = delta_pbc(srcs[a].wy, srcs[b].wy);
    int dz = delta_pbc(srcs[a].wz, srcs[b].wz);
    unsigned int d2 = (unsigned int)dx*dx + (unsigned int)dy*dy + (unsigned int)dz*dz;
    int d = isqrt(d2);
    int sum = rA + rB + SHELL_MARGIN;
    int diff = abss(rA - rB) - SHELL_MARGIN;
    if (diff < 0) diff = 0;
    return (d >= diff && (unsigned int)d <= (unsigned int)sum &&
            (unsigned long long)d2 >= (unsigned long long)diff*diff &&
            (unsigned long long)d2 <= (unsigned long long)sum*sum);
}

static void pair_singletons(void) {
    /* S x S: soltos interagem entre si. Sinais opostos formam par (viram D
       um do outro); sinais iguais se repelem. */
    for (int i = 0; i < n_folhas; i++) {
        if (srcs[i].kind != 1) continue;
        for (int j = i + 1; j < n_folhas; j++) {
            if (srcs[j].kind != 1) continue;
            if (!shells_intersect(i, j)) continue;

            if (srcs[i].sgn == srcs[j].sgn) {
                /* mesmo sinal: repelem */
                int dx = delta_pbc(srcs[j].wx, srcs[i].wx);
                int dy = delta_pbc(srcs[j].wy, srcs[i].wy);
                int dz = delta_pbc(srcs[j].wz, srcs[i].wz);
                srcs[i].wx = wrap(srcs[i].wx - sign_of(dx));
                srcs[i].wy = wrap(srcs[i].wy - sign_of(dy));
                srcs[i].wz = wrap(srcs[i].wz - sign_of(dz));
                srcs[j].wx = wrap(srcs[j].wx + sign_of(dx));
                srcs[j].wy = wrap(srcs[j].wy + sign_of(dy));
                srcs[j].wz = wrap(srcs[j].wz + sign_of(dz));
                srcs[i].phase = 0; srcs[j].phase = 0;
                clear_leaf(i); clear_leaf(j);
            } else {
                /* sinais opostos: viram par (D um do outro) */
                srcs[i].kind = 2; srcs[i].parent = j;
                srcs[j].kind = 2; srcs[j].parent = i;
                srcs[i].phase = 0; srcs[j].phase = 0;
                clear_leaf(i); clear_leaf(j);
            }
            break; /* i ja processado neste passo */
        }
    }
}

static void interact_delegates(void) {
    /* D x D: mesma tribo troca raio com o pai; tribos diferentes repelem. */
    for (int i = 0; i < n_folhas; i++) {
        if (srcs[i].kind != 2) continue;
        for (int j = i + 1; j < n_folhas; j++) {
            if (srcs[j].kind != 2) continue;
            if (!shells_intersect(i, j)) continue;

            if (srcs[i].sgn == srcs[j].sgn) {
                /* troca radial: maior raio entra, menor sai */
                int pi = srcs[i].parent, pj = srcs[j].parent;
                if (pi < 0 || pj < 0) continue;
                int rxi = delta_pbc(srcs[i].wx, srcs[pi].wx);
                int ryi = delta_pbc(srcs[i].wy, srcs[pi].wy);
                int rzi = delta_pbc(srcs[i].wz, srcs[pi].wz);
                int rxj = delta_pbc(srcs[j].wx, srcs[pj].wx);
                int ryj = delta_pbc(srcs[j].wy, srcs[pj].wy);
                int rzj = delta_pbc(srcs[j].wz, srcs[pj].wz);
                int ri2 = rxi*rxi + ryi*ryi + rzi*rzi;
                int rj2 = rxj*rxj + ryj*ryj + rzj*rzj;
                if (ri2 == 0 || rj2 == 0) continue;

                /* D x D mesma tribo: contagio do spin_target (efeito manada).
                   O D mais externo impoe o seu spin_target ao outro. */
                if (pi == pj &&
                    srcs[i].spin_target != 0 && srcs[j].spin_target != 0 &&
                    srcs[i].spin_target != srcs[j].spin_target) {
                    int i_outer = (ri2 > rj2) ||
                                  ((ri2 == rj2) &&
                                   ((srcs[i].wx > srcs[j].wx) ||
                                    ((srcs[i].wx == srcs[j].wx) && (srcs[i].wy > srcs[j].wy)) ||
                                    ((srcs[i].wx == srcs[j].wx) && (srcs[i].wy == srcs[j].wy) && (srcs[i].wz > srcs[j].wz))));
                    if (i_outer) srcs[j].spin_target = srcs[i].spin_target;
                    else         srcs[i].spin_target = srcs[j].spin_target;
                }

                if (ri2 > rj2) {
                    rxi -= sign_of(rxi);
                    ryi -= sign_of(ryi);
                    rzi -= sign_of(rzi);
                    rxj += sign_of(rxj);
                    ryj += sign_of(ryj);
                    rzj += sign_of(rzj);
                } else if (ri2 < rj2) {
                    rxi += sign_of(rxi);
                    ryi += sign_of(ryi);
                    rzi += sign_of(rzi);
                    rxj -= sign_of(rxj);
                    ryj -= sign_of(ryj);
                    rzj -= sign_of(rzj);
                }
                srcs[i].wx = wrap(srcs[pi].wx + rxi);
                srcs[i].wy = wrap(srcs[pi].wy + ryi);
                srcs[i].wz = wrap(srcs[pi].wz + rzi);
                srcs[j].wx = wrap(srcs[pj].wx + rxj);
                srcs[j].wy = wrap(srcs[pj].wy + ryj);
                srcs[j].wz = wrap(srcs[pj].wz + rzj);
                srcs[i].phase = 0; srcs[j].phase = 0;
                clear_leaf(i); clear_leaf(j);
                exchange_impulse(i, j);
            } else {
                /* tribos opostas: repelem */
                int dx = delta_pbc(srcs[j].wx, srcs[i].wx);
                int dy = delta_pbc(srcs[j].wy, srcs[i].wy);
                int dz = delta_pbc(srcs[j].wz, srcs[i].wz);
                srcs[i].wx = wrap(srcs[i].wx - sign_of(dx));
                srcs[i].wy = wrap(srcs[i].wy - sign_of(dy));
                srcs[i].wz = wrap(srcs[i].wz - sign_of(dz));
                srcs[j].wx = wrap(srcs[j].wx + sign_of(dx));
                srcs[j].wy = wrap(srcs[j].wy + sign_of(dy));
                srcs[j].wz = wrap(srcs[j].wz + sign_of(dz));
                srcs[i].phase = 0; srcs[j].phase = 0;
                clear_leaf(i); clear_leaf(j);
                exchange_impulse(i, j);
            }
            break;
        }
    }
}

static void update_leaves(void) {
    for (int b = 0; b < n_folhas; b++) {
        int sgn = srcs[b].sgn;
        unsigned int target = pulse_r2(srcs[b].phase, srcs[b].step_r2, srcs[b].max_r2);
        int mid = L / 2;
        int band = srcs[b].band;
        int amp = srcs[b].amp;

        memset(folha + (size_t)b * VOL, 0, (size_t)VOL * sizeof(short));
        for (int x = 0; x < L; x++)
        for (int y = 0; y < L; y++)
        for (int z = 0; z < L; z++) {
            int dx = delta_pbc(x, mid);
            int dy = delta_pbc(y, mid);
            int dz = delta_pbc(z, mid);
            int r2 = dx*dx + dy*dy + dz*dz;
            int dr2 = abss((int)r2 - (int)target);

            int u_new = 0;
            if (dr2 <= band) {
                int a = (amp * (band + 1 - dr2)) / (band + 1);
                u_new = sgn * a;
            }
            FOLHA(b,x,y,z) = (short)u_new;
        }
    }
}

static void orbit_delegates(void) {
    /* D orbita o K pai. spin_target=+1 alinha S para fora (max S.r);
       spin_target=-1 alinha S para dentro (max -S.r). spin_target=0
       assume +1 como padrao. */
    for (int c = 0; c < n_folhas; c++) {
        if (srcs[c].kind != 2) continue;
        int p = srcs[c].parent;
        if (p < 0 || p >= N_FOLHAS) continue;

        int rx = delta_pbc(srcs[c].wx, srcs[p].wx);
        int ry = delta_pbc(srcs[c].wy, srcs[p].wy);
        int rz = delta_pbc(srcs[c].wz, srcs[p].wz);
        int r2 = rx*rx + ry*ry + rz*rz;
        if (r2 == 0) continue;

        int sp = srcs[c].spin_target;
        if (sp == 0) sp = 1;
        int sx = sp * srcs[c].sx;
        int sy = sp * srcs[c].sy;
        int sz = sp * srcs[c].sz;
        long long dot = (long long)sx*rx + (long long)sy*ry + (long long)sz*rz;
        /* T = S*r2 - (S.r)*r : componente tangente (nao normalizada) */
        long long Tx = (long long)sx * r2 - dot * rx;
        long long Ty = (long long)sy * r2 - dot * ry;
        long long Tz = (long long)sz * r2 - dot * rz;

        long long best_score = 0;
        long long best_abs = (long long)r2 + 1;
        int best_dx = 0, best_dy = 0, best_dz = 0;

        for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++)
        for (int dz = -1; dz <= 1; dz++) {
            int nx = rx + dx, ny = ry + dy, nz = rz + dz;
            int n2 = nx*nx + ny*ny + nz*nz;
            long long diff2 = (long long)n2 - r2;
            if (diff2 < 0) diff2 = -diff2;
            long long score = Tx*dx + Ty*dy + Tz*dz;
            if (score <= 0) continue;
            if (diff2 < best_abs || (diff2 == best_abs && score > best_score)) {
                best_abs = diff2;
                best_score = score;
                best_dx = dx; best_dy = dy; best_dz = dz;
            }
        }

        rx += best_dx; ry += best_dy; rz += best_dz;
        srcs[c].wx = wrap(srcs[p].wx + rx);
        srcs[c].wy = wrap(srcs[p].wy + ry);
        srcs[c].wz = wrap(srcs[p].wz + rz);
    }
}

static void move_photon(void) {
    /* Par P: dois constituintes (q=1 e q=0) com mesmo centro, raio e M.
       Processa apenas o lider (pair > b); o parceiro e copiado.
       O contato e detectado na ponta (centro + r*M); alvo reemite na
       superficie e recebe um impulso unitario na direcao M. */
    for (int b = 0; b < n_folhas; b++) {
        if (srcs[b].kind != 3) continue;
        int p2 = srcs[b].pair;
        /* processa apenas o lider de cada par (menor indice) */
        if (p2 < 0 || p2 <= b) continue;

        int rP = isqrt(pulse_r2(srcs[b].phase, srcs[b].step_r2, srcs[b].max_r2));
        int mx = srcs[b].mx, my = srcs[b].my, mz = srcs[b].mz;
        int tx = wrap(srcs[b].wx + rP * mx);
        int ty = wrap(srcs[b].wy + rP * my);
        int tz = wrap(srcs[b].wz + rP * mz);

        int hit = -1;
        for (int c = 0; c < n_folhas; c++) {
            if (c == b || c == p2) continue;
            if (srcs[c].kind != 0 && srcs[c].kind != 1 && srcs[c].kind != 2) continue;
            int rH = isqrt(pulse_r2(srcs[c].phase, srcs[c].step_r2, srcs[c].max_r2));
            int dx = delta_pbc(tx, srcs[c].wx);
            int dy = delta_pbc(ty, srcs[c].wy);
            int dz = delta_pbc(tz, srcs[c].wz);
            unsigned int d2 = (unsigned int)dx*dx + (unsigned int)dy*dy + (unsigned int)dz*dz;
            int sum = rH + SHELL_MARGIN;
            if ((unsigned long long)d2 <= (unsigned long long)sum * sum) {
                hit = c; break;
            }
        }
        if (hit < 0) continue;

        int rH = isqrt(pulse_r2(srcs[hit].phase, srcs[hit].step_r2, srcs[hit].max_r2));

        /* alvo reemite na superficie na direcao M e recebe impulso */
        srcs[hit].wx = wrap(srcs[hit].wx + rH * mx);
        srcs[hit].wy = wrap(srcs[hit].wy + rH * my);
        srcs[hit].wz = wrap(srcs[hit].wz + rH * mz);
        srcs[hit].phase = 0;
        srcs[hit].ix += mx;
        srcs[hit].iy += my;
        srcs[hit].iz += mz;
        clear_leaf(hit);

        /* par P reemite na ponta (ponto de contato) */
        srcs[b].wx = tx; srcs[b].wy = ty; srcs[b].wz = tz;
        srcs[b].phase = 0;
        srcs[p2].wx = tx; srcs[p2].wy = ty; srcs[p2].wz = tz;
        srcs[p2].phase = 0;
        clear_leaf(b);
        clear_leaf(p2);
    }
}

static void exchange_impulse(int a, int b) {
    /* divide o impulso de forma conservativa (media inteira truncada,
       a sobra fica para o segundo); sem ponto flutuante. */
    int sx = srcs[a].ix + srcs[b].ix;
    int sy = srcs[a].iy + srcs[b].iy;
    int sz = srcs[a].iz + srcs[b].iz;
    srcs[a].ix = sx / 2;
    srcs[a].iy = sy / 2;
    srcs[a].iz = sz / 2;
    srcs[b].ix = sx - srcs[a].ix;
    srcs[b].iy = sy - srcs[a].iy;
    srcs[b].iz = sz - srcs[a].iz;
}

static void transfer_impulse_kd(void) {
    /* K absorve/averagia o impulso dos D da mesma tribo em contato. */
    for (int k = 0; k < n_folhas; k++) {
        if (srcs[k].kind != 0) continue;
        for (int d = 0; d < n_folhas; d++) {
            if (srcs[d].kind != 2 || srcs[d].parent != k) continue;
            if (!shells_intersect(k, d)) continue;
            exchange_impulse(k, d);
        }
    }
}

static int has_contact(int b) {
    for (int c = 0; c < n_folhas; c++) {
        if (c == b) continue;
        if (shells_intersect(b, c)) return 1;
    }
    return 0;
}

static void propagate_impulse(void) {
    /* Troca impulso entre D em contato (mesma nuvem) e K-D.
       Uma fonte com impulso so se desloca se estiver em contato com outra
       bolha; o impulso e consumido passo a passo. */
    for (int i = 0; i < n_folhas; i++) {
        if (srcs[i].kind != 2) continue;
        for (int j = i + 1; j < n_folhas; j++) {
            if (srcs[j].kind != 2) continue;
            if (!shells_intersect(i, j)) continue;
            exchange_impulse(i, j);
        }
    }
    transfer_impulse_kd();

    for (int b = 0; b < n_folhas; b++) {
        if (srcs[b].ix == 0 && srcs[b].iy == 0 && srcs[b].iz == 0) continue;
        if (!has_contact(b)) continue;  /* so move em interacao */
        int dx = sign_of(srcs[b].ix);
        int dy = sign_of(srcs[b].iy);
        int dz = sign_of(srcs[b].iz);
        srcs[b].wx = wrap(srcs[b].wx + dx);
        srcs[b].wy = wrap(srcs[b].wy + dy);
        srcs[b].wz = wrap(srcs[b].wz + dz);
        srcs[b].ix -= dx;
        srcs[b].iy -= dy;
        srcs[b].iz -= dz;
        if (srcs[b].pair >= 0) {
            int p2 = srcs[b].pair;
            srcs[p2].wx = srcs[b].wx;
            srcs[p2].wy = srcs[b].wy;
            srcs[p2].wz = srcs[b].wz;
        }
        clear_leaf(b);
    }
}

static void step(void) {
    build_world();
    move_sources();
    pair_singletons();
    interact_delegates();
    orbit_delegates();
    move_photon();
    propagate_impulse();
    update_leaves();
    for (int b = 0; b < n_folhas; b++) srcs[b].phase++;
}

#ifndef HEADLESS

static void project(float x, float y, float z, float *px, float *py, float *pd) {
    float scale = (window_width < window_height ? window_width : window_height) / (1.4f * L);
    scale *= zoom;

    x -= L / 2.0f;
    y -= L / 2.0f;
    z -= L / 2.0f;
    x += pan_x; y += pan_y; z += pan_z;

    switch (vista_atual) {
        case VISTA_XY:
            *px = (window_width/2.0f) + x*scale;
            *py = (window_height/2.0f) - y*scale;
            *pd = z;
            break;
        case VISTA_XZ:
            *px = (window_width/2.0f) + x*scale;
            *py = (window_height/2.0f) - z*scale;
            *pd = y;
            break;
        case VISTA_YZ:
            *px = (window_width/2.0f) + y*scale;
            *py = (window_height/2.0f) - z*scale;
            *pd = x;
            break;
        case VISTA_ISO:
            *px = (window_width/2.0f) + (x - y)*scale;
            *py = (window_height/2.0f) - ((x + y)/2.0f - z)*scale;
            *pd = x + y - z;
            break;
    }
}

typedef struct {
    int x, y, z, w;
} Ponto3D;

static int cmp_ponto(const void *a, const void *b) {
    const Ponto3D *pa = (const Ponto3D *)a;
    const Ponto3D *pb = (const Ponto3D *)b;
    return pb->w - pa->w; /* longe primeiro */
}

#ifndef HEADLESS
static void draw_line_pixels(uint32_t *pixels, int w, int h,
                             float x0, float y0, float x1, float y1, uint32_t c) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 0.001f) return;
    int steps = (int)(len * 1.5f);
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / steps;
        int ix = (int)(x0 + dx*t + 0.5f);
        int iy = (int)(y0 + dy*t + 0.5f);
        if (ix >= 0 && ix < w && iy >= 0 && iy < h)
            pixels[iy*w + ix] = c;
    }
}

static void draw_axes_pixels(uint32_t *pixels, int w, int h) {
    float px0, py0, pd0;
    project(0, 0, 0, &px0, &py0, &pd0);
    float px1, py1, pd1;
    project(L/2, 0, 0, &px1, &py1, &pd1);
    draw_line_pixels(pixels, w, h, px0, py0, px1, py1, 0xFFFF0000);
    project(0, L/2, 0, &px1, &py1, &pd1);
    draw_line_pixels(pixels, w, h, px0, py0, px1, py1, 0xFF00FF00);
    project(0, 0, L/2, &px1, &py1, &pd1);
    draw_line_pixels(pixels, w, h, px0, py0, px1, py1, 0xFF0000FF);
}

static void draw_arrows_pixels(uint32_t *pixels, int w, int h) {
    for (int b = 0; b < n_folhas; b++) {
        if (mostrar_somente_caciques && srcs[b].kind != 0 && srcs[b].kind != 3) continue;
        float px0, py0, pd0;
        project((float)srcs[b].wx, (float)srcs[b].wy, (float)srcs[b].wz, &px0, &py0, &pd0);
        float px1, py1, pd1;
        project((float)srcs[b].wx + srcs[b].sx*4.0f,
                (float)srcs[b].wy + srcs[b].sy*4.0f,
                (float)srcs[b].wz + srcs[b].sz*4.0f,
                &px1, &py1, &pd1);
        draw_line_pixels(pixels, w, h, px0, py0, px1, py1, argb_for_kind(srcs[b].kind, 220));
    }
}
#endif

static void render_to_pixels(uint32_t *pixels, int w, int h) {
    memset(pixels, 0, w * h * sizeof(uint32_t));

    if (!mostrar_vetores) {
        int npts = 0;
        for (int x = 0; x < L; x++)
        for (int y = 0; y < L; y++)
        for (int z = 0; z < L; z++) {
            if (DOM_MAG(x,y,z) >= 80) npts++;
        }

        Ponto3D *pts = (Ponto3D *)malloc((size_t)npts * sizeof(Ponto3D));
        if (!pts) return;
        npts = 0;

        for (int x = 0; x < L; x++)
        for (int y = 0; y < L; y++)
        for (int z = 0; z < L; z++) {
            if (DOM_MAG(x,y,z) < 80) continue;
            float px, py, pd;
            project((float)x, (float)y, (float)z, &px, &py, &pd);
            pts[npts].x = x;
            pts[npts].y = y;
            pts[npts].z = z;
            pts[npts].w = (int)pd;
            npts++;
        }

        if (npts > 0)
            qsort(pts, npts, sizeof(Ponto3D), cmp_ponto);

        for (int i = 0; i < npts; i++) {
            int x = pts[i].x, y = pts[i].y, z = pts[i].z;
            if (OWNER(x,y,z) < 0 || DOM_MAG(x,y,z) < 80) continue;
            if (mostrar_somente_caciques && OWNER(x,y,z) != 0 && OWNER(x,y,z) != 3) continue;
            float px, py, pd;
            project((float)x, (float)y, (float)z, &px, &py, &pd);

            int mag = DOM_MAG(x,y,z) >> 5;
            if (mag > 255) mag = 255;
            unsigned char r, g, bb;
            color_for_kind(OWNER(x,y,z), mag, &r, &g, &bb);

            int ix = (int)(px + 0.5f);
            int iy = (int)(py + 0.5f);
            if (ix >= 0 && ix < w && iy >= 0 && iy < h) {
                uint32_t c = (0xFF << 24) | (r << 16) | (g << 8) | bb;
                pixels[iy*w + ix] = c;
            }
        }
        free(pts);
    }

    /* fontes e vetores (visiveis em ambos os modos) */
    for (int b = 0; b < n_folhas; b++) {
        if (mostrar_somente_caciques && srcs[b].kind != 0 && srcs[b].kind != 3) continue;
        float px0, py0, pd0;
        project((float)srcs[b].wx, (float)srcs[b].wy, (float)srcs[b].wz, &px0, &py0, &pd0);
        int ix = (int)(px0 + 0.5f), iy = (int)(py0 + 0.5f);
        if (ix >= 0 && ix < w && iy >= 0 && iy < h)
            pixels[iy*w + ix] = argb_for_kind(srcs[b].kind, 180);
    }

    draw_arrows_pixels(pixels, w, h);
    draw_axes_pixels(pixels, w, h);
}

#ifndef HEADLESS
static void draw_label(SDL_Renderer *ren, float x, float y, const char *txt,
                       Uint8 r, Uint8 g, Uint8 b, float scale) {
    SDL_SetRenderDrawColor(ren, r, g, b, 255);
    SDL_SetRenderScale(ren, scale, scale);
    SDL_RenderDebugText(ren, x / scale, y / scale, txt);
    SDL_SetRenderScale(ren, 1.0f, 1.0f);
}
#endif

int main(int argc, char *argv[]) {
    if (argc > 1) cenario = atoi(argv[1]);
    init();
    SDL_Init(SDL_INIT_VIDEO);
    {
        SDL_DisplayID id = SDL_GetPrimaryDisplay();
        const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(id);
        int wh = mode ? mode->h - 80 : 720;
        if (wh < 600) wh = 600;
        window_width = wh;
        window_height = wh;
    }
    SDL_Window *win = SDL_CreateWindow("spin_rev_ca_folha3d", window_width, window_height, 0);
    SDL_Renderer *ren = SDL_CreateRenderer(win, NULL);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, window_width, window_height);
    uint32_t *pixels = (uint32_t *)malloc(window_width * window_height * sizeof(uint32_t));

    int rodando = 1;
    int frame = 0;
    while (rodando) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) rodando = 0;
            if (e.type == SDL_EVENT_KEY_DOWN) {
                switch (e.key.key) {
                    case SDLK_ESCAPE: rodando = 0; break;
                    case SDLK_1: vista_atual = VISTA_XY; break;
                    case SDLK_2: vista_atual = VISTA_XZ; break;
                    case SDLK_3: vista_atual = VISTA_YZ; break;
                    case SDLK_4: vista_atual = VISTA_ISO; break;
                    case SDLK_X: vista_atual = VISTA_YZ; break;
                    case SDLK_Y: vista_atual = VISTA_XZ; break;
                    case SDLK_Z: vista_atual = VISTA_XY; break;
                    case SDLK_I: vista_atual = VISTA_ISO; break;
                    case SDLK_V: mostrar_vetores = !mostrar_vetores; break;
                    case SDLK_K: mostrar_somente_caciques = !mostrar_somente_caciques; break;
                    case SDLK_EQUALS: zoom *= 1.1f; break;
                    case SDLK_MINUS: zoom /= 1.1f; break;
                    case SDLK_UP: pan_y += 1.0f; break;
                    case SDLK_DOWN: pan_y -= 1.0f; break;
                    case SDLK_LEFT: pan_x -= 1.0f; break;
                    case SDLK_RIGHT: pan_x += 1.0f; break;
                    case SDLK_PAGEUP: pan_z += 1.0f; break;
                    case SDLK_PAGEDOWN: pan_z -= 1.0f; break;
                }
            }
            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                if (e.wheel.y > 0.0f) zoom *= 1.1f;
                else if (e.wheel.y < 0.0f) zoom /= 1.1f;
            }
        }

        step();
        frame++;
        render_to_pixels(pixels, window_width, window_height);

        SDL_UpdateTexture(tex, NULL, pixels, window_width * sizeof(uint32_t));
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderTexture(ren, tex, NULL, NULL);

        /* labels UP/DOWN somente em torno dos centros dos K */
        for (int b = 0; b < n_folhas; b++) {
            if (srcs[b].kind != 0) continue;
            float px, py, pd;
            project((float)srcs[b].wx, (float)srcs[b].wy, (float)srcs[b].wz, &px, &py, &pd);
            const char *label = (srcs[b].sgn > 0) ? "UP" : "DOWN";
            Uint8 lr = (srcs[b].sgn > 0) ? 0 : 255;
            Uint8 lg = (srcs[b].sgn > 0) ? 255 : 50;
            Uint8 lb = (srcs[b].sgn > 0) ? 100 : 50;
            draw_label(ren, px - 20.0f, py - 30.0f, label, lr, lg, lb, 1.5f);
        }

        /* labels dos eixos */
        float px, py, pd;
        project((float)(L/2 + 2), 0.0f, 0.0f, &px, &py, &pd);
        draw_label(ren, px, py, "x", 255, 0, 0, 1.5f);
        project(0.0f, (float)(L/2 + 2), 0.0f, &px, &py, &pd);
        draw_label(ren, px, py, "y", 0, 255, 0, 1.5f);
        project(0.0f, 0.0f, (float)(L/2 + 2), &px, &py, &pd);
        draw_label(ren, px, py, "z", 0, 0, 255, 1.5f);

        /* info e ajuda de teclas */
        char info[128];
        const char *vname = (vista_atual == VISTA_XY) ? "XY" :
                            (vista_atual == VISTA_XZ) ? "XZ" :
                            (vista_atual == VISTA_YZ) ? "YZ" : "ISO";
        const char *mode = mostrar_somente_caciques ? "[K+P]" :
                             mostrar_vetores ? "[Vetores]" : "[Bolhas]";
        snprintf(info, sizeof(info), "t=%d view:%s zoom:%.2f %s",
                 frame, vname, zoom, mode);
        draw_label(ren, 10.0f, 10.0f, info, 200, 200, 200, 1.2f);
        draw_label(ren, 10.0f, 30.0f, "XYZI=view  V=vetores  K=K+P  +/- ou roda=zoom  setas=pan  PgUp/Dn=z  ESC=sair", 180, 180, 180, 1.0f);

        SDL_RenderPresent(ren);
        SDL_Delay(30);
    }

    free(pixels);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

#else

static float pan_x_h = 0.0f, pan_y_h = 0.0f, pan_z_h = 0.0f;

static void project_h(float x, float y, float z, float *px, float *py, float *pd) {
    float scale = 600.0f / (1.4f * L);
    x -= L / 2.0f;
    y -= L / 2.0f;
    z -= L / 2.0f;
    x += pan_x_h; y += pan_y_h; z += pan_z_h;

    *px = 400.0f + (x - y)*scale;
    *py = 400.0f - ((x + y)/2.0f - z)*scale;
    *pd = x + y - z;
}

typedef struct {
    int x, y, z, w;
} Ponto3D_h;

static int cmp_ponto_h(const void *a, const void *b) {
    const Ponto3D_h *pa = (const Ponto3D_h *)a;
    const Ponto3D_h *pb = (const Ponto3D_h *)b;
    return pb->w - pa->w;
}

static void set_pixel(unsigned char *rgb, int w, int h, int x, int y,
                      unsigned char r, unsigned char g, unsigned char b) {
    if (x < 0 || x >= w || y < 0 || y >= h) return;
    int i = 3 * (y * w + x);
    rgb[i] = r; rgb[i+1] = g; rgb[i+2] = b;
}

static void draw_line(unsigned char *rgb, int w, int h,
                      float x0, float y0, float x1, float y1,
                      unsigned char r, unsigned char g, unsigned char b) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 0.001f) return;
    int steps = (int)(len * 1.5f);
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / steps;
        int ix = (int)(x0 + dx*t + 0.5f);
        int iy = (int)(y0 + dy*t + 0.5f);
        set_pixel(rgb, w, h, ix, iy, r, g, b);
    }
}

static void draw_axes(unsigned char *rgb, int w, int h) {
    float px0, py0, pd0;
    project_h(0, 0, 0, &px0, &py0, &pd0);
    float px1, py1, pd1;
    project_h(L/2, 0, 0, &px1, &py1, &pd1);
    draw_line(rgb, w, h, px0, py0, px1, py1, 255, 0, 0);
    project_h(0, L/2, 0, &px1, &py1, &pd1);
    draw_line(rgb, w, h, px0, py0, px1, py1, 0, 255, 0);
    project_h(0, 0, L/2, &px1, &py1, &pd1);
    draw_line(rgb, w, h, px0, py0, px1, py1, 0, 0, 255);
}

static void write_ppm(const char *outdir, int t) {
    int W = 800, H = 800;
    unsigned char *rgb = (unsigned char *)calloc(3 * W * H, 1);
    if (!rgb) return;

    int npts = 0;
    for (int x = 0; x < L; x++)
    for (int y = 0; y < L; y++)
    for (int z = 0; z < L; z++) {
        if (DOM_MAG(x,y,z) >= 80) npts++;
    }

    Ponto3D_h *pts = (Ponto3D_h *)malloc((size_t)npts * sizeof(Ponto3D_h));
    if (!pts) { free(rgb); return; }
    npts = 0;

    for (int x = 0; x < L; x++)
    for (int y = 0; y < L; y++)
    for (int z = 0; z < L; z++) {
        if (DOM_MAG(x,y,z) < 80) continue;
        pts[npts].x = x; pts[npts].y = y; pts[npts].z = z;
        pts[npts].w = x + y - z;
        npts++;
    }

    if (npts > 0)
        qsort(pts, npts, sizeof(Ponto3D_h), cmp_ponto_h);

    for (int i = 0; i < npts; i++) {
        int x = pts[i].x, y = pts[i].y, z = pts[i].z;
        if (OWNER(x,y,z) < 0 || DOM_MAG(x,y,z) < 80) continue;
        if (mostrar_somente_caciques && OWNER(x,y,z) != 0 && OWNER(x,y,z) != 3) continue;
        float px, py, pd;
        project_h((float)x, (float)y, (float)z, &px, &py, &pd);

        int mag = DOM_MAG(x,y,z) >> 5;
        if (mag > 255) mag = 255;
        unsigned char r, g, b;
        color_for_kind(OWNER(x,y,z), mag, &r, &g, &b);

        int ix = (int)(px + 0.5f);
        int iy = (int)(py + 0.5f);
        set_pixel(rgb, W, H, ix, iy, r, g, b);
    }

    /* fontes e setas coloridas por tipo (1 pixel) */
    for (int b = 0; b < n_folhas; b++) {
        if (mostrar_somente_caciques && srcs[b].kind != 0 && srcs[b].kind != 3) continue;
        float px0, py0, pd0;
        project_h((float)srcs[b].wx, (float)srcs[b].wy, (float)srcs[b].wz, &px0, &py0, &pd0);
        unsigned char mr, mg, mb;
        color_for_kind(srcs[b].kind, 180, &mr, &mg, &mb);
        set_pixel(rgb, W, H, (int)px0, (int)py0, mr, mg, mb);

        float px1, py1, pd1;
        float sx = (float)srcs[b].sx * 4.0f;
        float sy = (float)srcs[b].sy * 4.0f;
        float sz = (float)srcs[b].sz * 4.0f;
        project_h((float)srcs[b].wx + sx, (float)srcs[b].wy + sy, (float)srcs[b].wz + sz,
                  &px1, &py1, &pd1);
        color_for_kind(srcs[b].kind, 220, &mr, &mg, &mb);
        draw_line(rgb, W, H, px0, py0, px1, py1, mr, mg, mb);
    }

    draw_axes(rgb, W, H);

    char path[256];
    if (strcmp(outdir, ".") == 0)
        snprintf(path, sizeof(path), "ca_f3d_frame_%05d.ppm", t);
    else
        snprintf(path, sizeof(path), "%s/ca_f3d_frame_%05d.ppm", outdir, t);

    fprintf(stderr, "[write_ppm] t=%d npts=%d path=%s\n", t, npts, path);
    fflush(stderr);

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[write_ppm] fopen FAILED: %s\n", path);
        fflush(stderr);
    } else {
        fprintf(f, "P6\n%d %d\n255\n", W, H);
        fwrite(rgb, 1, 3 * W * H, f);
        fclose(f);
        fprintf(stderr, "[write_ppm] wrote %s\n", path);
        fflush(stderr);
    }
    free(pts);
    free(rgb);
}

int main(int argc, char *argv[]) {
    int frames = 200;
    int save_every = 20;
    const char *outdir = ".";
    if (argc > 1) frames = atoi(argv[1]);
    if (argc > 2) save_every = atoi(argv[2]);
    if (argc > 3) outdir = argv[3];
    if (argc > 4) cenario = atoi(argv[4]);

    init();
    for (int t = 0; t < frames; t++) {
        step();
        if ((t % save_every) == 0 || t == frames - 1) {
            int maxp=0,maxn=0;
            int cntp=0,cntn=0;
            for(int x=0;x<L;x++)for(int y=0;y<L;y++)for(int z=0;z<L;z++){
                int u=WORLD(x,y,z);
                if(u>0){cntp++; if(u>maxp)maxp=u;}
                else if(u<0){cntn++; if(-u>maxn)maxn=-u;}
            }
            fprintf(stderr,"t=%3d pos=(%2d,%2d,%2d) neg=(%2d,%2d,%2d) maxp=%6d cntp=%6d maxn=%6d cntn=%6d\n",
                t,srcs[0].wx,srcs[0].wy,srcs[0].wz,srcs[1].wx,srcs[1].wy,srcs[1].wz,
                maxp,cntp,maxn,cntn);
            fflush(stderr);
            write_ppm(outdir, t);
        }
    }
    printf("%d frames, PPM em %s/ca_f3d_frame_*.ppm\n", frames, outdir);
    return 0;
}

#endif
