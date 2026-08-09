/* spin_rev_headless.c - headless probe for rule variation */
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <limits.h>

#define MAX_BOLHAS 256
#define L 20
#define R_FUGA 15.0f
#define R_MAX 0.5f
#define FASE_EXPANSAO_FRAMES 1

#define MAX_PONTOS 500

typedef struct { float dx, dy, dz; } PontoSuperficie;

typedef struct {
    float cx, cy, cz;
    float r;
    float mx, my, mz;
    float sx, sy, sz;
    PontoSuperficie pontos[MAX_PONTOS];
    int tipo;
    float dir_x, dir_y, dir_z;
    int timeout;
    int cacique_idx;
    unsigned int fase;
} Bolha;

typedef struct {
    int a, b;
    int info_alvo;
    int consome_timeout;
    int consome_timeout_b;
    int reemit_a;
    int reemit_b;
    int zera_raio_a;
    int zera_raio_b;
    float ax, ay, az;
    float bx, by, bz;
    float info_x, info_y, info_z;
    int prio;
} Interacao;

static unsigned int proc_hit[MAX_BOLHAS];
static unsigned int proc_generation = 1;
int contagem_cacique_cacique = 0;

/* Rule knobs (set from command line) */
int rule_kd = 0;   /* 0=tangent (vetor_fuga), 1=inward (toward K), 2=outward */
int rule_d1 = 0;   /* same for D x D, D1 */
int rule_d2 = 1;   /* same for D x D, D2 */
int rule_sd_convert = 1; /* S x D converts S to D */
float radial_fraction = 0.0f; /* 0..1 for mode 0: fraction of delta_r radially outward */

int cmp_interacao_prio(const void *p1, const void *p2);
void normalizar3D(float *x, float *y, float *z);
void inicializar_bolha(Bolha *b, float cx, float cy, float cz, float raio,
                       float mx, float my, float mz,
                       float ax, float ay, float az, int tipo);
void inicializar_pontos(Bolha *b);
static float delta_pbc(float a, float b, float period);
static void wrap_pos(float *x, float period);
void reemitir_bolha(Bolha *b, float cx, float cy, float cz, int zera_raio);
static void vetor_fuga(const Bolha *b, float dir_x, float dir_y, float dir_z,
                       float delta_r, float *ox, float *oy, float *oz);
void aplicar_interacao(const Interacao *h, Bolha *bolhas);
void cenario2(Bolha *b, int *num_bolhas);

void reemit_desloc(const Bolha *b, float dir_x, float dir_y, float dir_z,
                   float delta_r, int mode, float *ox, float *oy, float *oz) {
    if (mode == 0) {
        vetor_fuga(b, dir_x, dir_y, dir_z, delta_r, ox, oy, oz);
    } else {
        float s = (mode == 1) ? 1.0f : -1.0f;
        *ox = s * delta_r * dir_x;
        *oy = s * delta_r * dir_y;
        *oz = s * delta_r * dir_z;
    }
}

int cmp_interacao_prio(const void *p1, const void *p2) {
    const Interacao *i1 = (const Interacao *)p1;
    const Interacao *i2 = (const Interacao *)p2;
    if (i1->prio != i2->prio) return (i1->prio > i2->prio) - (i1->prio < i2->prio);
    if (i1->a != i2->a) return (i1->a > i2->a) - (i1->a < i2->a);
    return (i1->b > i2->b) - (i1->b < i2->b);
}

void normalizar3D(float *x, float *y, float *z) {
    float norm = sqrtf((*x)*(*x) + (*y)*(*y) + (*z)*(*z));
    if (norm > 0.0001f) { *x /= norm; *y /= norm; *z /= norm; }
    else { *x = 1.0f; *y = 0.0f; *z = 0.0f; }
}

void inicializar_pontos(Bolha *b) {
    for (int i = 0; i < MAX_PONTOS; i++) {
        float u = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
        float theta = ((float)rand() / RAND_MAX) * 2.0f * (float)M_PI;
        float phi = asinf(u);
        b->pontos[i].dx = cosf(phi) * cosf(theta);
        b->pontos[i].dy = cosf(phi) * sinf(theta);
        b->pontos[i].dz = sinf(phi);
    }
}

void inicializar_bolha(Bolha *b, float cx, float cy, float cz, float raio,
                       float mx, float my, float mz,
                       float ax, float ay, float az, int tipo) {
    b->cx = cx; b->cy = cy; b->cz = cz;
    wrap_pos(&b->cx, L); wrap_pos(&b->cy, L); wrap_pos(&b->cz, L);
    b->r = raio;
    b->tipo = tipo;

    b->mx = mx; b->my = my; b->mz = mz;
    normalizar3D(&b->mx, &b->my, &b->mz);

    b->sx = b->my * az - b->mz * ay;
    b->sy = b->mz * ax - b->mx * az;
    b->sz = b->mx * ay - b->my * ax;

    if (sqrtf(b->sx * b->sx + b->sy * b->sy + b->sz * b->sz) < 0.0001f) {
        b->sx = 0.0f; b->sy = b->mz; b->sz = -b->my;
    }

    normalizar3D(&b->sx, &b->sy, &b->sz);
    b->cacique_idx = -1;
    b->fase = (unsigned int)rand();
    inicializar_pontos(b);
}

static float delta_pbc(float a, float b, float period) {
    (void)period;
    return a - b;
}

static void wrap_pos(float *x, float period) {
    (void)x; (void)period;
    // periodicidade desativada para este cenario
}

void reemitir_bolha(Bolha *b, float cx, float cy, float cz, int zera_raio) {
    b->cx = cx; b->cy = cy; b->cz = cz;
    wrap_pos(&b->cx, L); wrap_pos(&b->cy, L); wrap_pos(&b->cz, L);
    if (zera_raio) b->r = 0.0f;
    b->fase = b->fase * 1103515245u + 12345u;
}

static void vetor_fuga(const Bolha *b, float dir_x, float dir_y, float dir_z,
                       float delta_r, float *ox, float *oy, float *oz) {
    // t1 = dir x S  (perpendicular a dir e a S)
    float t1x = dir_y * b->sz - dir_z * b->sy;
    float t1y = dir_z * b->sx - dir_x * b->sz;
    float t1z = dir_x * b->sy - dir_y * b->sx;

    // t2 = S - (S.dir)dir  (projecao de S no plano perpendicular a dir)
    float s_dot_dir = b->sx * dir_x + b->sy * dir_y + b->sz * dir_z;
    float t2x = b->sx - s_dot_dir * dir_x;
    float t2y = b->sy - s_dot_dir * dir_y;
    float t2z = b->sz - s_dot_dir * dir_z;

    float ux, uy, uz, vx, vy, vz;
    float n1 = sqrtf(t1x*t1x + t1y*t1y + t1z*t1z);
    float n2 = sqrtf(t2x*t2x + t2y*t2y + t2z*t2z);
    if (n1 < 1e-8f && n2 < 1e-8f) {
        float ax = fabsf(dir_x), ay = fabsf(dir_y), az = fabsf(dir_z);
        ux = 0.0f; uy = 0.0f; uz = 0.0f;
        if (ax < ay) { if (ax < az) ux = 1.0f; else uy = 1.0f; }
        else { if (ay < az) uz = 1.0f; else uy = 1.0f; }
        float dot = ux*dir_x + uy*dir_y + uz*dir_z;
        ux -= dot*dir_x; uy -= dot*dir_y; uz -= dot*dir_z;
        float len = sqrtf(ux*ux + uy*uy + uz*uz);
        if (len > 0.0001f) { ux /= len; uy /= len; uz /= len; }
        else { ux = 1.0f; uy = 0.0f; uz = 0.0f; }
        vx = dir_y * uz - dir_z * uy;
        vy = dir_z * ux - dir_x * uz;
        vz = dir_x * uy - dir_y * ux;
        float lenv = sqrtf(vx*vx + vy*vy + vz*vz);
        if (lenv > 0.0001f) { vx /= lenv; vy /= lenv; vz /= lenv; }
    } else {
        if (n1 > 0.0001f) { ux = t1x/n1; uy = t1y/n1; uz = t1z/n1; }
        else { ux = t2x/n2; uy = t2y/n2; uz = t2z/n2; }
        if (n2 > 0.0001f) { vx = t2x/n2; vy = t2y/n2; vz = t2z/n2; }
        else {
            vx = dir_y * uz - dir_z * uy;
            vy = dir_z * ux - dir_x * uz;
            vz = dir_x * uy - dir_y * ux;
            float lenv = sqrtf(vx*vx + vy*vy + vz*vz);
            if (lenv > 0.0001f) { vx /= lenv; vy /= lenv; vz /= lenv; }
        }
    }

    float angle = (float)(b->fase & 0xFFFFu) * (2.0f * (float)M_PI / 65536.0f);
    float ca = cosf(angle), sa = sinf(angle);
    float tx = ca * ux + sa * vx;
    float ty = ca * uy + sa * vy;
    float tz = ca * uz + sa * vz;
    float nt = sqrtf(tx*tx + ty*ty + tz*tz);
    if (nt > 0.0001f) { tx /= nt; ty /= nt; tz /= nt; }

    *ox = delta_r * tx;
    *oy = delta_r * ty;
    *oz = delta_r * tz;
}

int detectar_interacao(Bolha *a, Bolha *b, int idx_a, int idx_b,
                       Bolha *bolhas, int num_bolhas, Interacao *h,
                       float delta_r) {
    h->a = idx_a; h->b = idx_b;
    h->info_alvo = -1;
    h->consome_timeout = 0;
    h->consome_timeout_b = 0;
    h->reemit_a = 1;
    h->reemit_b = 1;
    h->zera_raio_a = 1;
    h->zera_raio_b = 1;
    h->prio = 99;

    if (b->tipo != 0) return 0;

    int a_cacique = (idx_a % 128 == 0);
    int b_cacique = (idx_b % 128 == 0);

    // cacique x cacique: detecta contato de superficie qualquer
    if (a_cacique && b_cacique) {
        float dx = delta_pbc(b->cx, a->cx, L);
        float dy = delta_pbc(b->cy, a->cy, L);
        float dz = delta_pbc(b->cz, a->cz, L);
        float d2 = dx*dx + dy*dy + dz*dz;
        float rsum = a->r + b->r + 0.001f;
        if (d2 > rsum * rsum) return 0;
        normalizar3D(&dx, &dy, &dz);
        contagem_cacique_cacique++;
        // cacique x cacique: ambos reemitem (raio zero) e se repelem 1 passo
        h->reemit_a = 1; h->reemit_b = 1;
        h->zera_raio_a = 1; h->zera_raio_b = 1;
        h->ax = a->cx - delta_r * dx;
        h->ay = a->cy - delta_r * dy;
        h->az = a->cz - delta_r * dz;
        h->bx = b->cx + delta_r * dx;
        h->by = b->cy + delta_r * dy;
        h->bz = b->cz + delta_r * dz;
        h->prio = 0;
        return 1;
    }

    // S x S: detecta contato de superficie; ambos reemitem (raio zero)
    // e se atraem um passo de luz cada um.
    if (!a_cacique && !b_cacique && a->tipo == 0 && b->tipo == 0 &&
        a->timeout == 0 && b->timeout == 0) {
        float dx = a->cx - b->cx;
        float dy = a->cy - b->cy;
        float dz = a->cz - b->cz;
        float d2 = dx*dx + dy*dy + dz*dz;
        float rsum = a->r + b->r + 0.001f;
        if (d2 > rsum * rsum) return 0;
        if (d2 < 0.0001f) { dx = 1.0f; dy = 0.0f; dz = 0.0f; }
        else { normalizar3D(&dx, &dy, &dz); }
        h->ax = a->cx - delta_r * dx;
        h->ay = a->cy - delta_r * dy;
        h->az = a->cz - delta_r * dz;
        h->bx = b->cx + delta_r * dx;
        h->by = b->cy + delta_r * dy;
        h->bz = b->cz + delta_r * dz;
        h->prio = 5;
        return 1;
    }

    // D x S / S x D: detecta contato de superficie; ambos reemitem (raio zero)
    // e se atraem um passo de luz cada um.
    int a_delegado = (!a_cacique && a->tipo == 0 && a->timeout > 0 && a->cacique_idx != -1);
    int b_delegado = (!b_cacique && b->tipo == 0 && b->timeout > 0 && b->cacique_idx != -1);
    int a_s = (!a_cacique && a->tipo == 0 && a->timeout == 0);
    int b_s = (!b_cacique && b->tipo == 0 && b->timeout == 0);
    if ((a_delegado && b_s) || (a_s && b_delegado)) {
        int a_is_del = a_delegado;
        Bolha *del = a_is_del ? a : b;
        Bolha *s   = a_is_del ? b : a;
        float dx = del->cx - s->cx;
        float dy = del->cy - s->cy;
        float dz = del->cz - s->cz;
        float d2 = dx*dx + dy*dy + dz*dz;
        float rsum = a->r + b->r + 0.001f;
        if (d2 > rsum * rsum) return 0;
        if (d2 < 0.0001f) { dx = 1.0f; dy = 0.0f; dz = 0.0f; }
        else { normalizar3D(&dx, &dy, &dz); }

        // Ambos se atraem 1 passo ao longo da linha centro-centro
        if (rule_sd_convert) {
            // o S vira delegado, herdando o cacique do delegado
            Bolha *c = &bolhas[del->cacique_idx];
            float cx = c->cx - s->cx;
            float cy = c->cy - s->cy;
            float cz = c->cz - s->cz;
            normalizar3D(&cx, &cy, &cz);
            h->info_x = cx; h->info_y = cy; h->info_z = cz;
            h->info_alvo = (a_is_del ? idx_b : idx_a);
        }

        if (a_is_del) {
            h->ax = del->cx - delta_r * dx;
            h->ay = del->cy - delta_r * dy;
            h->az = del->cz - delta_r * dz;
            h->bx = s->cx + delta_r * dx;
            h->by = s->cy + delta_r * dy;
            h->bz = s->cz + delta_r * dz;
        } else {
            h->ax = s->cx + delta_r * dx;
            h->ay = s->cy + delta_r * dy;
            h->az = s->cz + delta_r * dz;
            h->bx = del->cx - delta_r * dx;
            h->by = del->cy - delta_r * dy;
            h->bz = del->cz - delta_r * dz;
        }

        h->prio = 4;
        return 1;
    }

    if (a_cacique && !b_cacique) {
        // cacique x S / cacique x delegado: disparado por contato de superficie
        float dx = delta_pbc(a->cx, b->cx, L);
        float dy = delta_pbc(a->cy, b->cy, L);
        float dz = delta_pbc(a->cz, b->cz, L);
        float d2 = dx*dx + dy*dy + dz*dz;
        float rsum = a->r + b->r + 0.001f;
        if (d2 > rsum * rsum) return 0;
        float ux = dx, uy = dy, uz = dz;
        normalizar3D(&ux, &uy, &uz);

        // K nao se move e nao altera raio
        h->reemit_a = 0;
        h->zera_raio_a = 0;
        h->ax = a->cx; h->ay = a->cy; h->az = a->cz;

        // ponto de contato na superficie do K (lado de b)
        float pcx = a->cx - a->r * ux;
        float pcy = a->cy - a->r * uy;
        float pcz = a->cz - a->r * uz;
        float ox, oy, oz;
        reemit_desloc(b, ux, uy, uz, delta_r, rule_kd, &ox, &oy, &oz);

        // K x S: raio zerado (S vira D); K x D: mantem raio para D expandir
        h->reemit_b = 1;
        h->zera_raio_b = (b->timeout == 0) ? 1 : 0;
        h->bx = pcx + ox;
        h->by = pcy + oy;
        h->bz = pcz + oz;

        // dir do alvo aponta para o cacique
        h->info_x = ux; h->info_y = uy; h->info_z = uz;
        h->info_alvo = idx_b;

        if (a->timeout > 0) h->consome_timeout = 1;
        if (a->timeout > 0 && b->timeout > 0) h->consome_timeout_b = 1;
        h->prio = 1;
        return 1;
    }

    // suprime S -> cacique e delegado -> cacique (deixa P -> cacique e cacique -> cacique)
    if (b_cacique && !a_cacique && a->tipo != 1) return 0;

    // cacique nao reinterage com delegado de outro cacique
    if (a_cacique && b->timeout > 0 && b->cacique_idx != -1 && b->cacique_idx != idx_a) return 0;

    // delegados de caciques diferentes devem se repelir; da mesma tribo se aproximam
    int repelir = (a->timeout > 0 && b->timeout > 0 &&
                   a->cacique_idx != -1 && b->cacique_idx != -1 &&
                   a->cacique_idx != b->cacique_idx);
    int atrair = (a->timeout > 0 && b->timeout > 0 &&
                  a->cacique_idx != -1 && b->cacique_idx != -1 &&
                  a->cacique_idx == b->cacique_idx);

    // delegado x delegado: detecta por contato de superficie
    if (repelir || atrair) {
        float dx = a->cx - b->cx;
        float dy = a->cy - b->cy;
        float dz = a->cz - b->cz;
        float d2 = dx*dx + dy*dy + dz*dz;
        float rsum = a->r + b->r + 0.001f;
        if (d2 > rsum * rsum) return 0;
        if (d2 < 0.0001f) { dx = 1.0f; dy = 0.0f; dz = 0.0f; }
        else { normalizar3D(&dx, &dy, &dz); }

        if (repelir) {
            // D x D (cacique_idx distintos): ambos reemitem (raio zero)
            // e se repelem um passo de luz cada um
            h->ax = a->cx + delta_r * dx;
            h->ay = a->cy + delta_r * dy;
            h->az = a->cz + delta_r * dz;
            h->bx = b->cx - delta_r * dx;
            h->by = b->cy - delta_r * dy;
            h->bz = b->cz - delta_r * dz;
            h->prio = 2;
        } else {
            // D x D (mesma tribo): deslocamentos puramente tangenciais,
            // raio zerado; expansao radial so em r += delta_r
            h->zera_raio_a = 1;
            h->zera_raio_b = 1;

            // D1: um passo tangente a esfera do cacique (perpendicular a dir)
            float o1x, o1y, o1z;
            reemit_desloc(a, a->dir_x, a->dir_y, a->dir_z, delta_r, rule_d1, &o1x, &o1y, &o1z);
            h->ax = a->cx + o1x;
            h->ay = a->cy + o1y;
            h->az = a->cz + o1z;

            // D2: a partir do ponto de contato, desloca segundo rule_d2
            float pcbx = b->cx + b->r * dx;
            float pcby = b->cy + b->r * dy;
            float pcbz = b->cz + b->r * dz;
            float o2x, o2y, o2z;
            reemit_desloc(b, b->dir_x, b->dir_y, b->dir_z, delta_r, rule_d2, &o2x, &o2y, &o2z);
            h->bx = pcbx + o2x;
            h->by = pcby + o2y;
            h->bz = pcbz + o2z;
        }
        h->consome_timeout = 1;
        h->consome_timeout_b = 1;
        h->prio = (repelir ? 2 : 3);
        return 1;
    }

    float vx, vy, vz;
    // P usa M; demais usam S
    if (a->tipo == 1) {
        vx = a->mx; vy = a->my; vz = a->mz;
        normalizar3D(&vx, &vy, &vz);
    } else {
        vx = a->sx; vy = a->sy; vz = a->sz;
    }

    float tip_x = a->cx + a->r * vx;
    float tip_y = a->cy + a->r * vy;
    float tip_z = a->cz + a->r * vz;

    float dx = delta_pbc(tip_x, b->cx, L);
    float dy = delta_pbc(tip_y, b->cy, L);
    float dz = delta_pbc(tip_z, b->cz, L);
    if (dx*dx + dy*dy + dz*dz >= (b->r + 0.001f) * (b->r + 0.001f)) return 0;

    // P x S / P x D / P x K: P reemite no ponto de contato (tip);
    // o alvo (S, D ou K) reemite em C_alvo + r_alvo * M_P_unit.
    h->ax = tip_x;
    h->ay = tip_y;
    h->az = tip_z;
    h->bx = b->cx + b->r * vx;
    h->by = b->cy + b->r * vy;
    h->bz = b->cz + b->r * vz;

    if (a->timeout > 0) h->consome_timeout = 1;
    if (a->timeout > 0 && b->timeout > 0) h->consome_timeout_b = 1;

    h->prio = (b_cacique ? 1 : 6);
    return 1;
}

void aplicar_interacao(const Interacao *h, Bolha *bolhas) {
    if (h->reemit_a) reemitir_bolha(&bolhas[h->a], h->ax, h->ay, h->az, h->zera_raio_a);
    if (h->reemit_b) reemitir_bolha(&bolhas[h->b], h->bx, h->by, h->bz, h->zera_raio_b);

    if (h->info_alvo >= 0) {
        float ix = h->info_x, iy = h->info_y, iz = h->info_z;
        normalizar3D(&ix, &iy, &iz);
        Bolha *alvo = &bolhas[h->info_alvo];
        alvo->dir_x = ix;
        alvo->dir_y = iy;
        alvo->dir_z = iz;

        // Escolhe o lado que tem cacique (K ou D) para passar cacique_idx/timeout.
        int src = -1;
        if (h->a % 128 == 0 || (bolhas[h->a].timeout > 0 && bolhas[h->a].cacique_idx != -1)) {
            src = h->a;
        } else if (h->b % 128 == 0 || (bolhas[h->b].timeout > 0 && bolhas[h->b].cacique_idx != -1)) {
            src = h->b;
        }
        if (src != -1) {
            if (src % 128 == 0) {
                alvo->cacique_idx = src;
                alvo->timeout = INT_MAX;
            } else if (bolhas[src].timeout > 0 && bolhas[src].cacique_idx != -1) {
                alvo->cacique_idx = bolhas[src].cacique_idx;
                if (alvo->timeout == 0) {
                    alvo->timeout = (bolhas[src].timeout > 1) ? (bolhas[src].timeout - 1) : 0;
                }
            }
        }
    }

    if (h->consome_timeout) {
        Bolha *a = &bolhas[h->a];
        if (a->timeout > 0) {
            a->timeout--;
            if (a->timeout == 0) {
                a->dir_x = a->dir_y = a->dir_z = 0.0f;
            }
        }
    }
    if (h->consome_timeout_b) {
        Bolha *b = &bolhas[h->b];
        if (b->timeout > 0) {
            b->timeout--;
            if (b->timeout == 0) {
                b->dir_x = b->dir_y = b->dir_z = 0.0f;
            }
        }
    }

    // Delegados recalculam dir
    for (int lado = 0; lado < 2; lado++) {
        int idx = (lado == 0) ? h->a : h->b;
        Bolha *p = &bolhas[idx];
        if (p->cacique_idx != -1) {
            Bolha *c = &bolhas[p->cacique_idx];
            float dx = c->cx - p->cx;
            float dy = c->cy - p->cy;
            float dz = c->cz - p->cz;
            normalizar3D(&dx, &dy, &dz);
            p->dir_x = dx; p->dir_y = dy; p->dir_z = dz;
        }
    }
}

static void gerar_ms_fibonacci(int i, int n_pares,
                                float *mx, float *my, float *mz,
                                float *ax, float *ay, float *az) {
    float golden = (1.0f + sqrtf(5.0f)) / 2.0f;

    // M via esfera de Fibonacci (distribuicao uniforme deterministica)
    float y = 1.0f - (2.0f * i + 1.0f) / (float)n_pares;
    float theta = 2.0f * (float)M_PI * i / golden;
    float r_proj = sqrtf(1.0f - y * y);
    *mx = r_proj * cosf(theta);
    *my = r_proj * sinf(theta);
    *mz = y;

    // Base ortonormal do plano perpendicular a M
    float nx, ny, nz;
    if (fabsf(*mz) < 0.9f) { nx = 0.0f; ny = 0.0f; nz = 1.0f; }
    else { nx = 1.0f; ny = 0.0f; nz = 0.0f; }

    float ux = *my * nz - *mz * ny;
    float uy = *mz * nx - *mx * nz;
    float uz = *mx * ny - *my * nx;
    normalizar3D(&ux, &uy, &uz);

    float vx = *my * uz - *mz * uy;
    float vy = *mz * ux - *mx * uz;
    float vz = *mx * uy - *my * ux;

    // S girado deterministicamente no plano tangente (golden angle)
    float phi = 2.0f * (float)M_PI * i * golden;
    float sx = ux * cosf(phi) + vx * sinf(phi);
    float sy = uy * cosf(phi) + vy * sinf(phi);
    float sz = uz * cosf(phi) + vz * sinf(phi);

    // aux = S x M  =>  M x aux = S
    *ax = sy * *mz - sz * *my;
    *ay = sz * *mx - sx * *mz;
    *az = sx * *my - sy * *mx;
}

void cenario2(Bolha *b, int *num_bolhas) {
    *num_bolhas = MAX_BOLHAS;
    int n_pares = MAX_BOLHAS / 2;

    for (int i = 0; i < n_pares; i++) {
        float mx, my, mz, ax, ay, az;
        gerar_ms_fibonacci(i, n_pares, &mx, &my, &mz, &ax, &ay, &az);

        // posicoes uniformes dentro de uma esfera de raio 3, raios aleatorios
        srand((unsigned)(i + 1));
        float cx1, cy1, cz1, r_pos1;
        do {
            cx1 = ((float)rand() / (float)RAND_MAX) * 6.0f - 3.0f;
            cy1 = ((float)rand() / (float)RAND_MAX) * 6.0f - 3.0f;
            cz1 = ((float)rand() / (float)RAND_MAX) * 6.0f - 3.0f;
            r_pos1 = sqrtf(cx1*cx1 + cy1*cy1 + cz1*cz1);
        } while (r_pos1 > 3.0f);
        float r1 = ((float)rand() / (float)RAND_MAX) * R_MAX;
        inicializar_bolha(&b[i], cx1, cy1, cz1, r1,
                          mx, my, mz, ax, ay, az, 0);

        srand((unsigned)(i + 1 + n_pares));
        float cx2, cy2, cz2, r_pos2;
        do {
            cx2 = ((float)rand() / (float)RAND_MAX) * 6.0f - 3.0f;
            cy2 = ((float)rand() / (float)RAND_MAX) * 6.0f - 3.0f;
            cz2 = ((float)rand() / (float)RAND_MAX) * 6.0f - 3.0f;
            r_pos2 = sqrtf(cx2*cx2 + cy2*cy2 + cz2*cz2);
        } while (r_pos2 > 3.0f);
        float r2 = ((float)rand() / (float)RAND_MAX) * R_MAX;
        inicializar_bolha(&b[i + n_pares], cx2, cy2, cz2, r2,
                          -mx, -my, -mz, ax, ay, az, 0);
    }

    // Caciques o mais longe possivel dentro da esfera inicial (raio 3)
    b[0].cx = -3.0f; b[0].cy = 0.0f; b[0].cz = 0.0f;
    if (MAX_BOLHAS >= 129) {
        b[128].cx = 3.0f; b[128].cy = 0.0f; b[128].cz = 0.0f;
    }
    // Iguala raios iniciais dos caciques para simetria
    float r_cacique = 0.4f;
    b[0].r = r_cacique;
    if (MAX_BOLHAS >= 129) b[128].r = r_cacique;
}

static void medir(const Bolha *b, int num_bolhas, int k0, int k1,
                  float *n0, float *n1, float *r0, float *r1,
                  float *shell0, float *shell1, float *std0, float *std1,
                  float *align0, float *align1, float *sep) {
    *r0 = b[k0].r; *r1 = b[k1].r;
    float dsum0 = 0, dsum1 = 0;
    float dsq0 = 0, dsq1 = 0;
    int c0 = 0, c1 = 0;
    float al0 = 0, al1 = 0;
    for (int i = 0; i < num_bolhas; i++) {
        if (i == k0 || i == k1) continue;
        if (b[i].timeout > 0 && b[i].cacique_idx == k0) {
            float dx = b[i].cx - b[k0].cx;
            float dy = b[i].cy - b[k0].cy;
            float dz = b[i].cz - b[k0].cz;
            float d = sqrtf(dx*dx + dy*dy + dz*dz);
            dsum0 += d;
            dsq0 += d*d;
            if (d > 0.0001f) {
                float ndir = b[i].dir_x*dx + b[i].dir_y*dy + b[i].dir_z*dz;
                al0 += fabsf(ndir / d);
            }
            c0++;
        }
        if (b[i].timeout > 0 && b[i].cacique_idx == k1) {
            float dx = b[i].cx - b[k1].cx;
            float dy = b[i].cy - b[k1].cy;
            float dz = b[i].cz - b[k1].cz;
            float d = sqrtf(dx*dx + dy*dy + dz*dz);
            dsum1 += d;
            dsq1 += d*d;
            if (d > 0.0001f) {
                float ndir = b[i].dir_x*dx + b[i].dir_y*dy + b[i].dir_z*dz;
                al1 += fabsf(ndir / d);
            }
            c1++;
        }
    }
    *n0 = (float)c0; *n1 = (float)c1;
    *shell0 = (c0 > 0 && *r0 > 0.0001f) ? (dsum0 / c0) / *r0 : 0.0f;
    *shell1 = (c1 > 0 && *r1 > 0.0001f) ? (dsum1 / c1) / *r1 : 0.0f;
    *std0 = (c0 > 0) ? sqrtf((dsq0 / c0) - (*shell0 * *r0) * (*shell0 * *r0)) / *r0 : 0.0f;
    *std1 = (c1 > 0) ? sqrtf((dsq1 / c1) - (*shell1 * *r1) * (*shell1 * *r1)) / *r1 : 0.0f;
    *align0 = (c0 > 0) ? (al0 / c0) : 0.0f;
    *align1 = (c1 > 0) ? (al1 / c1) : 0.0f;
    float sx = b[k0].cx - b[k1].cx;
    float sy = b[k0].cy - b[k1].cy;
    float sz = b[k0].cz - b[k1].cz;
    *sep = sqrtf(sx*sx + sy*sy + sz*sz);
}

static int parse_rule(const char *s) {
    if (!s) return 0;
    if (strcmp(s, "tangent") == 0) return 0;
    if (strcmp(s, "inward") == 0) return 1;
    if (strcmp(s, "outward") == 0) return 2;
    return atoi(s);
}

int main(int argc, char **argv) {
    int frames = 1000;
    if (argc > 1) frames = atoi(argv[1]);
    if (argc > 2) rule_kd = parse_rule(argv[2]);
    if (argc > 3) rule_d1 = parse_rule(argv[3]);
    if (argc > 4) rule_d2 = parse_rule(argv[4]);
    if (argc > 5) rule_sd_convert = atoi(argv[5]);

    Bolha *b = (Bolha *)calloc(MAX_BOLHAS, sizeof(Bolha));
    int num_bolhas;
    srand(12345);
    cenario2(b, &num_bolhas);

    Interacao *hits = NULL;
    int hits_cap = 0;
    int fase_expansao = FASE_EXPANSAO_FRAMES;
    float dt = 0.016f;

    int k0 = 0, k1 = 128;
    for (int frame = 0; frame < frames; frame++) {
        float delta_r = 0.1f * dt;

        proc_generation++;
        if (proc_generation == 0) {
            proc_generation = 1;
            for (int i = 0; i < MAX_BOLHAS; i++) proc_hit[i] = 0;
        }

        if (fase_expansao > 0) {
            fase_expansao--;
        } else {
            int max_hits = num_bolhas * (num_bolhas - 1) / 2;
            if (max_hits < 1) max_hits = 1;
            if (hits_cap < max_hits) {
                hits = (Interacao*)realloc(hits, max_hits * sizeof(Interacao));
                hits_cap = max_hits;
            }
            int num_hits = 0;
            for (int i = 0; i < num_bolhas; i++) {
                for (int j = i + 1; j < num_bolhas; j++) {
                    if (b[i].tipo == 1 && b[j].tipo == 1) continue;
                    Interacao h;
                    if (detectar_interacao(&b[i], &b[j], i, j, b, num_bolhas, &h, delta_r)) {
                        hits[num_hits++] = h;
                    } else if (detectar_interacao(&b[j], &b[i], j, i, b, num_bolhas, &h, delta_r)) {
                        hits[num_hits++] = h;
                    }
                }
            }
            // Ordena por prioridade. Cada bolha comum participa de no maximo
            // uma interacao por frame; o cacique pode processar varias KxS/D
            // no mesmo frame, pois nao e reemitido nessas interacoes.
            // Quando KxK ocorre, os caciques sao reemitidos e nenhum outro
            // choque e aplicado neste frame, entao delegados nao sao reemitidos.
            qsort(hits, num_hits, sizeof(Interacao), cmp_interacao_prio);
            int lim_hits = num_hits;
            if (num_hits > 0 && hits[0].prio == 0) {
                while (lim_hits > 0 && hits[lim_hits - 1].prio > 0) lim_hits--;
            }
            if (num_hits > 0 && hits[0].prio == 0) {
                printf("[frame %d] num_hits=%d lim_hits=%d first=(%d,%d,p=%d)\n", frame, num_hits, lim_hits, hits[0].a, hits[0].b, hits[0].prio);
            }
            int kk_frame = 0;
            for (int k = 0; k < lim_hits; k++) {
                if (hits[k].prio == 0) {
                    kk_frame = 1;
                    float ar_before = 0, ar_after = 0;
                    int rc0=0, rc1=0; float rsum0=0, rsum1=0;
                    for (int i = 0; i < num_bolhas; i++) {
                        if (i == k0 || i == k1) continue;
                        if (b[i].timeout > 0 && b[i].cacique_idx == k0) { rsum0 += b[i].r; rc0++; }
                        if (b[i].timeout > 0 && b[i].cacique_idx == k1) { rsum1 += b[i].r; rc1++; }
                    }
                    ar_before = ((rc0+rc1) > 0) ? (rsum0 + rsum1) / (rc0 + rc1) : 0.0f;
                    aplicar_interacao(&hits[k], b);
                    rc0=0; rc1=0; rsum0=0; rsum1=0;
                    for (int i = 0; i < num_bolhas; i++) {
                        if (i == k0 || i == k1) continue;
                        if (b[i].timeout > 0 && b[i].cacique_idx == k0) { rsum0 += b[i].r; rc0++; }
                        if (b[i].timeout > 0 && b[i].cacique_idx == k1) { rsum1 += b[i].r; rc1++; }
                    }
                    ar_after = ((rc0+rc1) > 0) ? (rsum0 + rsum1) / (rc0 + rc1) : 0.0f;
                    printf("[KK frame %d] ar_before=%.6f ar_after=%.6f\n", frame, ar_before, ar_after);
                    continue;
                }
                int ia = hits[k].a;
                int ib = hits[k].b;
                int reusa_k_a = (hits[k].prio == 1 && (ia % 128 == 0) && !hits[k].reemit_a);
                int reusa_k_b = (hits[k].prio == 1 && (ib % 128 == 0) && !hits[k].reemit_b);
                if ((proc_hit[ia] == proc_generation && !reusa_k_a) ||
                    (proc_hit[ib] == proc_generation && !reusa_k_b)) {
                    continue;
                }
                aplicar_interacao(&hits[k], b);
                if (!reusa_k_a) proc_hit[ia] = proc_generation;
                if (!reusa_k_b) proc_hit[ib] = proc_generation;
            }
        }

        for (int i = 0; i < num_bolhas; i++) {
            b[i].r += delta_r;
        }
    }

    float n0, n1, r0, r1, shell0, shell1, std0, std1, align0, align1, sep;
    medir(b, num_bolhas, k0, k1, &n0, &n1, &r0, &r1, &shell0, &shell1, &std0, &std1, &align0, &align1, &sep);

    float rsum0 = 0.0f, rsum1 = 0.0f;
    int rc0 = 0, rc1 = 0;
    for (int i = 0; i < num_bolhas; i++) {
        if (i == k0 || i == k1) continue;
        if (b[i].timeout > 0 && b[i].cacique_idx == k0) { rsum0 += b[i].r; rc0++; }
        if (b[i].timeout > 0 && b[i].cacique_idx == k1) { rsum1 += b[i].r; rc1++; }
    }
    float ar0 = (rc0 > 0) ? rsum0 / rc0 : 0.0f;
    float ar1 = (rc1 > 0) ? rsum1 / rc1 : 0.0f;

    printf("frames=%d kd=%d d1=%d d2=%d sd=%d n0=%.2f n1=%.2f r0=%.4f r1=%.4f "
           "ar0=%.4f ar1=%.4f "
           "shell0=%.4f shell1=%.4f std0=%.4f std1=%.4f align0=%.4f align1=%.4f sep=%.4f "
           "k0x=%.4f k1x=%.4f cxc=%d\n",
           frames, rule_kd, rule_d1, rule_d2, rule_sd_convert,
           n0, n1, r0, r1, ar0, ar1, shell0, shell1, std0, std1, align0, align1, sep,
           b[k0].cx, b[k1].cx, contagem_cacique_cacique);

    free(hits);
    free(b);
    return 0;
}
