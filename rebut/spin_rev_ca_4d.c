#define _CRT_SECURE_NO_WARNINGS
#define _WIN32_WINNT 0x0600
#include <windows.h>
#define _USE_MATH_DEFINES
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#define MAX_BOLHAS 256
#define MAX_PONTOS 500

#define L 20
#define R_FUGA 15.0f
#define R_MAX 0.5f
#define FASE_EXPANSAO_FRAMES 1  // frames de expansao simultanea sem interacoes

// === HISTORICO DO CENTRO DE MASSA (apenas visualizacao) ===
#define MAX_HIST 200
float *hist_cmx = NULL;
float *hist_cmy = NULL;
float *hist_cmz = NULL;
int hist_count = 0;
int hist_capacity = 0;

int contagem_cacique_cacique = 0;
static FILE *kk_log = NULL;

int window_width = 800;
int window_height = 800;

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
    int spin_target; // +1 = S para fora (up), -1 = S para dentro (down)
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

enum Vista { VISTA_XY, VISTA_XZ, VISTA_YZ, VISTA_ISO };
enum Vista vista_atual = VISTA_XY;
float zoom = 1.0f;
bool mostrar_bolhas = true;
bool modo_somente_cm = false;
bool mostrar_somente_caciques = false;
bool mostrar_seta_S = true;
bool mostrar_seta_M = false;
bool mostrar_seta_dir = false;

// === PAN ===
float pan_x = 0.0f;
float pan_y = 0.0f;
float pan_z = 0.0f;
float pan_speed = 2.0f;

// Controle de uma interacao por bolha por frame
static unsigned int proc_hit[MAX_BOLHAS];
static unsigned int proc_generation = 1;

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

static float delta_pbc(float a, float b, float period);
static void wrap_pos(float *x, float period);

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
    b->spin_target = 0;
    b->fase = 0;
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
}

// Usa dir·S como sonda para orientar uma translacao tangente a esfera do
// cacique, alinhando o eixo radial (dir) com o sentido desejado sem girar S.
// spin-up => dir (de D para K) = -S (S para fora);
// spin-down => dir = +S (S para dentro).
// O deslocamento e de um passo de luz (delta_r) ao longo da tangente que
// aponta para a reta radial definida por S.
static void ajustar_radial_por_translacao(Bolha *p, const Bolha *k, float delta_r) {
    float rx = p->cx - k->cx;
    float ry = p->cy - k->cy;
    float rz = p->cz - k->cz;
    float r2 = rx*rx + ry*ry + rz*rz;
    if (r2 <= 0.0001f) return;
    float r = sqrtf(r2);

    float dirx = -rx / r;
    float diry = -ry / r;
    float dirz = -rz / r;

    float sx = p->sx, sy = p->sy, sz = p->sz;
    float s2 = sx*sx + sy*sy + sz*sz;
    if (s2 <= 0.0001f) return;
    float s = sqrtf(s2);
    sx /= s; sy /= s; sz /= s;

    int spin = p->spin_target;
    if (spin == 0) spin = (k->mx > 0.0f) ? 1 : -1;
    // dir alvo: de D para K, de modo que S fique radial (para fora ou dentro)
    float tx = -spin * sx;
    float ty = -spin * sy;
    float tz = -spin * sz;

    float dot = dirx*tx + diry*ty + dirz*tz;
    if (dot >= 0.9999f) return;

    // Componente de target_dir perpendicular a dir: tangente a esfera,
    // apontando para a reta radial de S.
    float perpx = tx - dot * dirx;
    float perpy = ty - dot * diry;
    float perpz = tz - dot * dirz;
    float perp2 = perpx*perpx + perpy*perpy + perpz*perpz;
    if (perp2 <= 0.0001f) {
        // dir e target_dir opostos: escolhe uma tangente arbitraria
        float ax = fabsf(dirx), ay = fabsf(diry), az = fabsf(dirz);
        if (ax < ay) {
            if (ax < az) { perpx = 0.0f; perpy = -dirz; perpz = diry; }
            else { perpx = -diry; perpy = dirx; perpz = 0.0f; }
        } else {
            if (ay < az) { perpx = -diry; perpy = dirx; perpz = 0.0f; }
            else { perpx = 0.0f; perpy = -dirz; perpz = diry; }
        }
        perp2 = perpx*perpx + perpy*perpy + perpz*perpz;
        if (perp2 <= 0.0001f) { perpx = 1.0f; perpy = 0.0f; perpz = 0.0f; perp2 = 1.0f; }
    }
    float perp = sqrtf(perp2);
    perpx /= perp; perpy /= perp; perpz /= perp;

    p->cx += delta_r * perpx;
    p->cy += delta_r * perpy;
    p->cz += delta_r * perpz;

    // atualiza dir para o novo centro, ja que o deslocamento foi tangente
    rx = p->cx - k->cx;
    ry = p->cy - k->cy;
    rz = p->cz - k->cz;
    r2 = rx*rx + ry*ry + rz*rz;
    if (r2 > 0.0001f) {
        r = sqrtf(r2);
        p->dir_x = -rx / r;
        p->dir_y = -ry / r;
        p->dir_z = -rz / r;
    }
}

// 'dir' e a normal local (ex.: do D para o cacique). Retorna um deslocamento
// puramente tangencial de modulo delta_r perpendicular a 'dir'.
// Tenta t = dir x S; se S for paralelo a dir, usa t = dir x M.
static void vetor_fuga(const Bolha *b, float dir_x, float dir_y, float dir_z,
                       float delta_r, float *ox, float *oy, float *oz) {
    float tx = dir_y * b->sz - dir_z * b->sy;
    float ty = dir_z * b->sx - dir_x * b->sz;
    float tz = dir_x * b->sy - dir_y * b->sx;
    float n = sqrtf(tx*tx + ty*ty + tz*tz);
    if (n < 0.0001f) {
        // S paralelo a dir (S radial): usa M para definir o plano tangente
        tx = dir_y * b->mz - dir_z * b->my;
        ty = dir_z * b->mx - dir_x * b->mz;
        tz = dir_x * b->my - dir_y * b->mx;
        n = sqrtf(tx*tx + ty*ty + tz*tz);
    }
    if (n < 0.0001f) {
        // M tambem paralelo: escolhe um eixo arbitrario perpendicular a dir
        float ax = fabsf(dir_x), ay = fabsf(dir_y), az = fabsf(dir_z);
        float ux = 0.0f, uy = 0.0f, uz = 0.0f;
        if (ax < ay) { if (ax < az) ux = 1.0f; else uy = 1.0f; }
        else { if (ay < az) uz = 1.0f; else uy = 1.0f; }
        float dot = ux*dir_x + uy*dir_y + uz*dir_z;
        ux -= dot*dir_x; uy -= dot*dir_y; uz -= dot*dir_z;
        float len = sqrtf(ux*ux + uy*uy + uz*uz);
        if (len > 0.0001f) { ux /= len; uy /= len; uz /= len; }
        else { ux = 1.0f; uy = 0.0f; uz = 0.0f; }
        tx = uy * dir_z - uz * dir_y;
        ty = uz * dir_x - ux * dir_z;
        tz = ux * dir_y - uy * dir_x;
        n = sqrtf(tx*tx + ty*ty + tz*tz);
    }
    if (n > 0.0001f) { tx /= n; ty /= n; tz /= n; }
    else { tx = 1.0f; ty = 0.0f; tz = 0.0f; }
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
        float eps = 0.001f;
        float rsum = a->r + b->r + eps;
        float rdiff = fabsf(a->r - b->r) - eps;
        if (d2 > rsum * rsum) return 0;
        if (rdiff > 0.0f && d2 < rdiff * rdiff) return 0;
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
        float eps = 0.001f;
        float rsum = a->r + b->r + eps;
        float rdiff = fabsf(a->r - b->r) - eps;
        if (d2 > rsum * rsum) return 0;
        if (rdiff > 0.0f && d2 < rdiff * rdiff) return 0;
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
        float eps = 0.001f;
        float rsum = a->r + b->r + eps;
        float rdiff = fabsf(a->r - b->r) - eps;
        if (d2 > rsum * rsum) return 0;
        if (rdiff > 0.0f && d2 < rdiff * rdiff) return 0;
        if (d2 < 0.0001f) { dx = 1.0f; dy = 0.0f; dz = 0.0f; }
        else { normalizar3D(&dx, &dy, &dz); }

        // Ambos se atraem 1 passo ao longo da linha centro-centro;
        // o S vira delegado, herdando o cacique do delegado
        Bolha *c = &bolhas[del->cacique_idx];
        float cx = c->cx - s->cx;
        float cy = c->cy - s->cy;
        float cz = c->cz - s->cz;
        normalizar3D(&cx, &cy, &cz);
        h->info_x = cx; h->info_y = cy; h->info_z = cz;
        h->info_alvo = (a_is_del ? idx_b : idx_a);

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
        float eps = 0.001f;
        float rsum = a->r + b->r + eps;
        float rdiff = fabsf(a->r - b->r) - eps;
        if (d2 > rsum * rsum) return 0;
        if (rdiff > 0.0f && d2 < rdiff * rdiff) return 0;
        float ux = dx, uy = dy, uz = dz;
        normalizar3D(&ux, &uy, &uz);

        // K nao se move e nao altera raio
        h->reemit_a = 0;
        h->zera_raio_a = 0;
        h->ax = a->cx; h->ay = a->cy; h->az = a->cz;

        // K nao interage com D ja formado: so converte S em D.
        if (b->timeout > 0) {
            // D ja e delegado: K nao move, nao consome interacao
            return 0;
        }

        // K x S: S vira D, raio zerado e deslocado 1 passo para dentro do K
        h->reemit_b = 1;
        h->zera_raio_b = 1;
        h->bx = b->cx + delta_r * ux;
        h->by = b->cy + delta_r * uy;
        h->bz = b->cz + delta_r * uz;

        // dir do alvo aponta para o cacique
        h->info_x = ux; h->info_y = uy; h->info_z = uz;
        h->info_alvo = idx_b;

        if (a->timeout > 0) h->consome_timeout = 1;
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
        float eps = 0.001f;
        float rsum = a->r + b->r + eps;
        float rdiff = fabsf(a->r - b->r) - eps;
        if (d2 > rsum * rsum) return 0;
        if (rdiff > 0.0f && d2 < rdiff * rdiff) {
            // Caso extremo: uma bolha dentro da outra. Forca repulsao.
            if (d2 < 0.0001f) { dx = 1.0f; dy = 0.0f; dz = 0.0f; }
            else { normalizar3D(&dx, &dy, &dz); }
            h->ax = a->cx + delta_r * dx;
            h->ay = a->cy + delta_r * dy;
            h->az = a->cz + delta_r * dz;
            h->bx = b->cx - delta_r * dx;
            h->by = b->cy - delta_r * dy;
            h->bz = b->cz - delta_r * dz;
            h->reemit_a = 1; h->reemit_b = 1;
            h->zera_raio_a = 1; h->zera_raio_b = 1;
            h->consome_timeout = 1;
            h->consome_timeout_b = 1;
            h->prio = (repelir ? 2 : 3);
            return 1;
        }
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
            // D x D (mesma tribo): troca radial. A mais proxima do centro
            // sobe 1 passo (para fora), limitada pela superficie de K; a mais
            // externa desce 1 passo (para dentro), limitada pelo proprio raio.
            // O vetor S e imutavel; a nuvem esferica vem das translacoes.
            h->zera_raio_a = 0;
            h->zera_raio_b = 0;

            Bolha *k = &bolhas[a->cacique_idx];
            float ax = a->cx - k->cx, ay = a->cy - k->cy, az = a->cz - k->cz;
            float bx = b->cx - k->cx, by = b->cy - k->cy, bz = b->cz - k->cz;
            float da2 = ax*ax + ay*ay + az*az;
            float db2 = bx*bx + by*by + bz*bz;
            int a_inner = (da2 < db2) ||
                          ((da2 == db2) &&
                           ((a->cx < b->cx) ||
                            ((a->cx == b->cx) && (a->cy < b->cy)) ||
                            ((a->cx == b->cx) && (a->cy == b->cy) && (a->cz < b->cz))));

            // Troca radial: interna sobe, externa desce, ambas limitadas
            // geometricamente pela superficie de K e pelo centro.
            float da = sqrtf(da2), db = sqrtf(db2);
            float max_out_a = k->r - a->r - da;
            if (max_out_a < 0.0f) max_out_a = 0.0f;
            float max_in_a = da - a->r;
            if (max_in_a < 0.0f) max_in_a = 0.0f;

            float o1x, o1y, o1z;
            if (a_inner) {
                float step = (delta_r < max_out_a) ? delta_r : max_out_a;
                o1x = -step * a->dir_x; o1y = -step * a->dir_y; o1z = -step * a->dir_z;
            } else {
                float step = (delta_r < max_in_a) ? delta_r : max_in_a;
                o1x = step * a->dir_x; o1y = step * a->dir_y; o1z = step * a->dir_z;
            }
            h->ax = a->cx + o1x;
            h->ay = a->cy + o1y;
            h->az = a->cz + o1z;

            float max_out_b = k->r - b->r - db;
            if (max_out_b < 0.0f) max_out_b = 0.0f;
            float max_in_b = db - b->r;
            if (max_in_b < 0.0f) max_in_b = 0.0f;

            float o2x, o2y, o2z;
            if (!a_inner) {
                float step = (delta_r < max_out_b) ? delta_r : max_out_b;
                o2x = -step * b->dir_x; o2y = -step * b->dir_y; o2z = -step * b->dir_z;
            } else {
                float step = (delta_r < max_in_b) ? delta_r : max_in_b;
                o2x = step * b->dir_x; o2y = step * b->dir_y; o2z = step * b->dir_z;
            }
            h->bx = b->cx + o2x;
            h->by = b->cy + o2y;
            h->bz = b->cz + o2z;
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
                alvo->timeout = bolhas[src].timeout;
            }
        }

        // Inicializa spin_target do novo delegado. Se veio de outro D, herda;
        // se veio de K x S, usa a orientacao natural do S capturado.
        if (alvo->cacique_idx != -1) {
            int herdei = 0;
            if (src != -1 && (src % 128 != 0) &&
                bolhas[src].timeout > 0 && bolhas[src].cacique_idx != -1 &&
                bolhas[src].spin_target != 0) {
                alvo->spin_target = bolhas[src].spin_target;
                herdei = 1;
            }
            if (!herdei) {
                float ux = h->info_x, uy = h->info_y, uz = h->info_z;
                float nrm = sqrtf(ux*ux + uy*uy + uz*uz);
                if (nrm > 0.0001f) {
                    ux /= nrm; uy /= nrm; uz /= nrm;
                    float dot = alvo->sx*ux + alvo->sy*uy + alvo->sz*uz;
                    alvo->spin_target = (dot < 0.0f) ? 1 : -1;
                } else {
                    alvo->spin_target = 1;
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
                a->spin_target = 0;
            }
        }
    }
    if (h->consome_timeout_b) {
        Bolha *b = &bolhas[h->b];
        if (b->timeout > 0) {
            b->timeout--;
            if (b->timeout == 0) {
                b->dir_x = b->dir_y = b->dir_z = 0.0f;
                b->spin_target = 0;
            }
        }
    }

    // Delegados recalculam dir (S so e alterado na conversao inicial)
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

    // D x D mesma tribo: contagio do spin_target (efeito manada).
    Bolha *pa = &bolhas[h->a];
    Bolha *pb = &bolhas[h->b];
    if (pa->timeout > 0 && pb->timeout > 0 &&
        pa->cacique_idx != -1 && pa->cacique_idx == pb->cacique_idx) {
        if (pa->spin_target != 0 && pb->spin_target != 0 &&
            pa->spin_target != pb->spin_target) {
            // O D mais externo (maior distancia ao cacique) impoe o spin_target.
            Bolha *k = &bolhas[pa->cacique_idx];
            float ax = pa->cx - k->cx, ay = pa->cy - k->cy, az = pa->cz - k->cz;
            float bx = pb->cx - k->cx, by = pb->cy - k->cy, bz = pb->cz - k->cz;
            float da2 = ax*ax + ay*ay + az*az;
            float db2 = bx*bx + by*by + bz*bz;
            int a_outer = (da2 > db2) ||
                          ((da2 == db2) &&
                           ((pa->cx > pb->cx) ||
                            ((pa->cx == pb->cx) && (pa->cy > pb->cy)) ||
                            ((pa->cx == pb->cx) && (pa->cy == pb->cy) && (pa->cz > pb->cz))));
            if (a_outer) pb->spin_target = pa->spin_target;
            else pa->spin_target = pb->spin_target;
        }
    }
}

void project(float x, float y, float z, float *px, float *py) {
    float scale = (window_width < window_height ? window_width : window_height) / 14.0f;
    scale *= zoom;

    if (!isfinite(x) || !isfinite(y) || !isfinite(z)) { x = y = z = 0; }

    // Aplica pan antes da projeção
    x += pan_x;
    y += pan_y;
    z += pan_z;

    switch (vista_atual) {
        case VISTA_XY:
            *px = (window_width/2.0f) + x*scale;
            *py = (window_height/2.0f) - y*scale;
            break;
        case VISTA_XZ:
            *px = (window_width/2.0f) + x*scale;
            *py = (window_height/2.0f) - z*scale;
            break;
        case VISTA_YZ:
            *px = (window_width/2.0f) + y*scale;
            *py = (window_height/2.0f) - z*scale;
            break;
        case VISTA_ISO:
            *px = (window_width/2.0f) + (x - y)*scale;
            *py = (window_height/2.0f) - ((x + y)/2.0f - z)*scale;
            break;
    }
}


// ============================================
// Circulo para distinguir bolhas P no modo V
// ============================================
void desenhar_circulo(SDL_Renderer *ren, float cx, float cy, float cz, float raio, Uint8 r, Uint8 g, Uint8 b) {
    int segmentos = 24;
    float px_ant, py_ant;
    int primeiro = 1;

    for (int i = 0; i <= segmentos; i++) {
        float ang = 2.0f * (float)M_PI * i / segmentos;
        // Ponto na circunferencia: precisamos de uma base ortonormal no plano da tela
        // Simplificacao: circulo no plano XY da projecao
        float px, py;
        project(cx + raio * cosf(ang), cy + raio * sinf(ang), cz, &px, &py);

        if (!primeiro) {
            SDL_SetRenderDrawColor(ren, r, g, b, 255);
            SDL_RenderLine(ren, px_ant, py_ant, px, py);
        }
        px_ant = px;
        py_ant = py;
        primeiro = 0;
    }
}

// Desenha uma esfera de arame centrada em (cx,cy,cz) com dado raio
void desenhar_esfera_arame(SDL_Renderer *ren, float cx, float cy, float cz,
                           float raio, Uint8 r, Uint8 g, Uint8 b) {
    int segmentos = 24;
    float px_ant, py_ant;
    int primeiro;

    SDL_SetRenderDrawColor(ren, r, g, b, 255);

    // circulo XY
    primeiro = 1;
    for (int i = 0; i <= segmentos; i++) {
        float ang = 2.0f * (float)M_PI * i / segmentos;
        float x = cx + raio * cosf(ang);
        float y = cy + raio * sinf(ang);
        float px, py;
        project(x, y, cz, &px, &py);
        if (!primeiro) SDL_RenderLine(ren, px_ant, py_ant, px, py);
        px_ant = px; py_ant = py; primeiro = 0;
    }

    // circulo XZ
    primeiro = 1;
    for (int i = 0; i <= segmentos; i++) {
        float ang = 2.0f * (float)M_PI * i / segmentos;
        float x = cx + raio * cosf(ang);
        float z = cz + raio * sinf(ang);
        float px, py;
        project(x, cy, z, &px, &py);
        if (!primeiro) SDL_RenderLine(ren, px_ant, py_ant, px, py);
        px_ant = px; py_ant = py; primeiro = 0;
    }

    // circulo YZ
    primeiro = 1;
    for (int i = 0; i <= segmentos; i++) {
        float ang = 2.0f * (float)M_PI * i / segmentos;
        float y = cy + raio * cosf(ang);
        float z = cz + raio * sinf(ang);
        float px, py;
        project(cx, y, z, &px, &py);
        if (!primeiro) SDL_RenderLine(ren, px_ant, py_ant, px, py);
        px_ant = px; py_ant = py; primeiro = 0;
    }
}

// Classifica o spin de uma ilha contando as setas S dos delegados dentro
// de uma esfera de raio fixo 1 em torno do cacique (recurso de visualizacao).
// "Para fora" significa S alinhado com o vetor radial do cacique ao delegado.
// Retorna "UP", "DOWN" ou NULL quando nao ha bolhas na regiao.
const char *classificar_spin(int k_idx, const Bolha *b, int num_bolhas,
                             float *px, float *py,
                             Uint8 *cor_r, Uint8 *cor_g, Uint8 *cor_b) {
    const Bolha *k = &b[k_idx];
    float R = 1.0f; // raio fixo da esfera de visualizacao
    project(k->cx, k->cy, k->cz, px, py);

    float score = 0.0f;
    int n = 0;
    for (int i = 0; i < num_bolhas; i++) {
        if (i == k_idx) continue;
        if (b[i].timeout <= 0 || b[i].cacique_idx != k_idx) continue;
        float dx = b[i].cx - k->cx;
        float dy = b[i].cy - k->cy;
        float dz = b[i].cz - k->cz;
        float d2 = dx*dx + dy*dy + dz*dz;
        if (d2 > R*R || d2 <= 0.0001f) continue;
        float d = sqrtf(d2);
        // ignora bolhas cujo centro esta dentro do proprio raio do centro,
        // pois a direcao radial ali e ambigua
        if (d < b[i].r) continue;
        float dot = (b[i].sx*dx + b[i].sy*dy + b[i].sz*dz) / d;
        score += dot;
        n++;
    }
    if (n == 0) return NULL;

    if (score > 0.0f) {
        *cor_r = 0; *cor_g = 255; *cor_b = 100; // verde
        return "UP";
    } else if (score < 0.0f) {
        *cor_r = 255; *cor_g = 50; *cor_b = 50; // vermelho
        return "DOWN";
    }
    return NULL;
}

void desenhar_spin_labels(SDL_Renderer *ren, const Bolha *b, int num_bolhas) {
    for (int k = 0; k < num_bolhas; k++) {
        if (k % 128 != 0) continue; // caciques estao em multiplos de 128
        if (b[k].r < 0.0001f) continue;

        float px, py;
        Uint8 cr, cg, cb;
        const char *label = classificar_spin(k, b, num_bolhas, &px, &py, &cr, &cg, &cb);
        if (label) {
            SDL_SetRenderDrawColor(ren, cr, cg, cb, 255);
            float scale = 1.5f;
            SDL_SetRenderScale(ren, scale, scale);
            SDL_RenderDebugText(ren, (px - 12.0f) / scale, (py - 25.0f) / scale, label);
            SDL_SetRenderScale(ren, 1.0f, 1.0f);
        }
    }
}

void desenhar_seta(SDL_Renderer *ren, float cx, float cy, float cz,
                   float vx, float vy, float vz, float r, float color_r, float color_g, float color_b) {
    float px1, py1, px2, py2;
    project(cx, cy, cz, &px1, &py1);
    project(cx + vx * r, cy + vy * r, cz + vz * r, &px2, &py2);

    SDL_SetRenderDrawColor(ren, (Uint8)color_r, (Uint8)color_g, (Uint8)color_b, 255);
    SDL_RenderLine(ren, px1, py1, px2, py2);

    float tamanho_ponta = 4.0f;

    float dx = px2 - px1;
    float dy = py2 - py1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len > 0) {
        float ux = dx / len;
        float uy = dy / len;
        float nx = -uy;
        float ny = ux;

        SDL_RenderLine(ren, px2, py2, px2 - ux * tamanho_ponta - nx * (tamanho_ponta*0.6f), py2 - uy * tamanho_ponta - ny * (tamanho_ponta*0.6f));
        SDL_RenderLine(ren, px2, py2, px2 - ux * tamanho_ponta + nx * (tamanho_ponta*0.6f), py2 - uy * tamanho_ponta + ny * (tamanho_ponta*0.6f));
    }
}


// Atualiza o historico do centro de massa (deve ser chamado antes da renderizacao)
void atualizar_historico_cm(const Bolha *b, int num_bolhas) {
    float cmx = 0, cmy = 0, cmz = 0;
    int count = 0;
    for (int k = 0; k < num_bolhas; k++) {
        if (b[k].tipo == 0) {
            cmx += b[k].cx; cmy += b[k].cy; cmz += b[k].cz;
            count++;
        }
    }
    if (count > 0) { cmx /= count; cmy /= count; cmz /= count; }

    // Aumenta a capacidade do historico conforme necessario
    if (hist_count == hist_capacity) {
        int new_cap = hist_capacity == 0 ? MAX_HIST : hist_capacity * 2;
        hist_cmx = (float *)realloc(hist_cmx, sizeof(float) * new_cap);
        hist_cmy = (float *)realloc(hist_cmy, sizeof(float) * new_cap);
        hist_cmz = (float *)realloc(hist_cmz, sizeof(float) * new_cap);
        hist_capacity = new_cap;
    }

    hist_cmx[hist_count] = cmx;
    hist_cmy[hist_count] = cmy;
    hist_cmz[hist_count] = cmz;
    hist_count++;
}

void desenhar_trajetoria_cm(SDL_Renderer *ren) {
    // Desenha trilha 3D do CM (todas as posicoes anteriores persistem)
    for (int i = 1; i < hist_count; i++) {
        float px0, py0, px1, py1;
        project(hist_cmx[i - 1], hist_cmy[i - 1], hist_cmz[i - 1], &px0, &py0);
        project(hist_cmx[i], hist_cmy[i], hist_cmz[i], &px1, &py1);
        SDL_SetRenderDrawColor(ren, 0, 255, 128, 120);
        SDL_RenderLine(ren, px0, py0, px1, py1);
    }

    // Ponto branco do CM atual no grafico principal
    if (hist_count > 0) {
        float px, py;
        project(hist_cmx[hist_count - 1], hist_cmy[hist_count - 1], hist_cmz[hist_count - 1], &px, &py);
        SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
        SDL_FRect r = {px - 3, py - 3, 6, 6};
        SDL_RenderFillRect(ren, &r);
    }
}

void desenhar_contador_fugidas(SDL_Renderer *ren, Bolha *b, int num_bolhas) {
    int fugidas = 0;
    for (int i = 0; i < num_bolhas; i++) {
        if (b[i].r > R_FUGA) fugidas++;
    }

    // Barra de fundo (cinza escuro)
    SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
    SDL_FRect bg = {10, 10, 200, 20};
    SDL_RenderFillRect(ren, &bg);

    // Barra de preenchimento (vermelho, proporcional)
    float ratio = (float)fugidas / num_bolhas;
    if (ratio > 1.0f) ratio = 1.0f;
    SDL_SetRenderDrawColor(ren, 255, 50, 50, 255);
    SDL_FRect fill = {10, 10, 200 * ratio, 20};
    SDL_RenderFillRect(ren, &fill);

    // Borda
    SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
    SDL_RenderRect(ren, &bg);

    // Marcadores
    for (int i = 1; i <= 4; i++) {
        float x = 10 + 200 * i / 5.0f;
        SDL_RenderLine(ren, x, 10, x, 30);
    }

    // Contador de cacique x cacique
    char buf[64];
    snprintf(buf, sizeof(buf), "cacique x cacique: %d", contagem_cacique_cacique);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_RenderDebugText(ren, 10.0f, 38.0f, buf);
}

void desenhar_versao(SDL_Renderer *ren) {
    char buf[64];
    snprintf(buf, sizeof(buf), "build %s %s", __DATE__, __TIME__);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_RenderDebugText(ren, window_width - 200.0f, 10.0f, buf);
}

void desenhar_eixos(SDL_Renderer *ren) {
    float origem_x = 0, origem_y = 0, origem_z = 0;
    float px_origem, py_origem;
    project(origem_x, origem_y, origem_z, &px_origem, &py_origem);

    float comprimento = 5.8f / zoom;

    float px, py;
    project(comprimento, 0, 0, &px, &py);
    SDL_SetRenderDrawColor(ren, 255, 0, 0, 255);
    SDL_RenderLine(ren, px_origem, py_origem, px, py);
    project(comprimento + 0.4f, 0, 0, &px, &py);
    SDL_FRect r = {px-3, py-3, 6, 6};
    SDL_RenderFillRect(ren, &r);

    project(0, comprimento, 0, &px, &py);
    SDL_SetRenderDrawColor(ren, 0, 255, 0, 255);
    SDL_RenderLine(ren, px_origem, py_origem, px, py);
    project(0, comprimento + 0.4f, 0, &px, &py);
    r = (SDL_FRect){px-3, py-3, 6, 6};
    SDL_RenderFillRect(ren, &r);

    project(0, 0, comprimento, &px, &py);
    SDL_SetRenderDrawColor(ren, 0, 0, 255, 255);
    SDL_RenderLine(ren, px_origem, py_origem, px, py);
    project(0, 0, comprimento + 0.4f, &px, &py);
    r = (SDL_FRect){px-3, py-3, 6, 6};
    SDL_RenderFillRect(ren, &r);

    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    project(0,0,0, &px, &py);
    r = (SDL_FRect){px-3, py-3, 6, 6};
    SDL_RenderFillRect(ren, &r);
}

void desenhar_cubo_universo(SDL_Renderer *ren) {
    float h = L * 0.5f;
    float c[8][3] = {
        {-h,-h,-h}, { h,-h,-h}, { h, h,-h}, {-h, h,-h},
        {-h,-h, h}, { h,-h, h}, { h, h, h}, {-h, h, h}
    };
    int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}
    };
    SDL_SetRenderDrawColor(ren, 80, 80, 80, 255);
    for (int i = 0; i < 12; i++) {
        float px0, py0, px1, py1;
        project(c[edges[i][0]][0], c[edges[i][0]][1], c[edges[i][0]][2], &px0, &py0);
        project(c[edges[i][1]][0], c[edges[i][1]][1], c[edges[i][1]][2], &px1, &py1);
        SDL_RenderLine(ren, px0, py0, px1, py1);
    }
}

// === DESENHO DOS PONTOS DAS BOLHAS EM LOTE VIA SDL_RenderGeometry ===
static SDL_Vertex *g_pts_vertices = NULL;
static int *g_pts_indices = NULL;
static int g_pts_capacity = 0;

void desenhar_pontos_bolhas(SDL_Renderer *ren, Bolha *b, int num_bolhas, float raio_maximo) {
    if (!mostrar_bolhas) return;

    if (num_bolhas > g_pts_capacity) {
        g_pts_vertices = (SDL_Vertex *)realloc(g_pts_vertices, sizeof(SDL_Vertex) * 4 * num_bolhas);
        g_pts_indices = (int *)realloc(g_pts_indices, sizeof(int) * 6 * num_bolhas);
        g_pts_capacity = num_bolhas;
    }

    int vcount = 0;
    int icount = 0;

    for (int i = 0; i < num_bolhas; i++) {
        if (b[i].r < 0.00005f || b[i].r > raio_maximo) continue;
        if (mostrar_somente_caciques && (i % 128 != 0)) continue;

        float px, py;
        project(b[i].cx, b[i].cy, b[i].cz, &px, &py);

        SDL_FColor c;
        float size;
        if (i % 128 == 0) {
            c = (SDL_FColor){ 1.0f, 50.0f/255.0f, 50.0f/255.0f, 1.0f };
            size = 5.0f;
        } else if (b[i].tipo == 1) {
            c = (SDL_FColor){ 1.0f, 140.0f/255.0f, 0.0f, 1.0f };
            size = 4.0f;
        } else if (b[i].cacique_idx != -1) {
            c = (SDL_FColor){ 1.0f, 1.0f, 100.0f/255.0f, 1.0f };
            size = 3.5f;
        } else {
            c = (SDL_FColor){ 100.0f/255.0f, 160.0f/255.0f, 1.0f, 1.0f };
            size = 2.5f;
        }

        float x0 = px - size * 0.5f;
        float y0 = py - size * 0.5f;
        float x1 = x0 + size;
        float y1 = y0 + size;

        g_pts_vertices[vcount + 0].position = (SDL_FPoint){ x0, y0 };
        g_pts_vertices[vcount + 1].position = (SDL_FPoint){ x1, y0 };
        g_pts_vertices[vcount + 2].position = (SDL_FPoint){ x1, y1 };
        g_pts_vertices[vcount + 3].position = (SDL_FPoint){ x0, y1 };

        for (int k = 0; k < 4; k++) {
            g_pts_vertices[vcount + k].color = c;
            g_pts_vertices[vcount + k].tex_coord = (SDL_FPoint){ 0.0f, 0.0f };
        }

        g_pts_indices[icount + 0] = vcount + 0;
        g_pts_indices[icount + 1] = vcount + 1;
        g_pts_indices[icount + 2] = vcount + 2;
        g_pts_indices[icount + 3] = vcount + 0;
        g_pts_indices[icount + 4] = vcount + 2;
        g_pts_indices[icount + 5] = vcount + 3;

        vcount += 4;
        icount += 6;
    }

    if (icount > 0) {
        SDL_RenderGeometry(ren, NULL, g_pts_vertices, vcount, g_pts_indices, icount);
    }
}

int carregar_bolhas(Bolha *bolhas, const char *arquivo) {
    FILE *f = fopen(arquivo, "r");
    if (!f) {
        printf("ERRO: Nao foi possivel abrir '%s'\n", arquivo);
        return 0;
    }

    int count = 0;
    char linha[256];
    while (fgets(linha, sizeof(linha), f) && count < MAX_BOLHAS) {
        if (linha[0] == '#' || linha[0] == '\n' || linha[0] == ' ') continue;

        float cx, cy, cz, raio, mx, my, mz, ax, ay, az;
        int tipo;

        if (sscanf(linha, "%f %f %f %f %f %f %f %f %f %f %d",
                   &cx, &cy, &cz, &raio, &mx, &my, &mz, &ax, &ay, &az, &tipo) == 11) {
            inicializar_bolha(&bolhas[count], cx, cy, cz, raio, mx, my, mz, ax, ay, az, tipo);
            count++;
        }
    }
    fclose(f);
    return count;
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

void cenario1(Bolha *b, int *num_bolhas) {
    *num_bolhas = MAX_BOLHAS;
    int n_pares = MAX_BOLHAS / 2;

    for (int i = 0; i < n_pares; i++) {
        float mx, my, mz, ax, ay, az;
        gerar_ms_fibonacci(i, n_pares, &mx, &my, &mz, &ax, &ay, &az);

        srand((unsigned)(i + 1));
        inicializar_bolha(&b[i], 0.0f, 0.0f, 0.0f, 1.0f,
                          mx, my, mz, ax, ay, az, 0);

        srand((unsigned)(i + 1 + n_pares));
        inicializar_bolha(&b[i + n_pares], 0.0f, 0.0f, 0.0f, 1.0f,
                          -mx, -my, -mz, ax, ay, az, 0);
    }
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
    // S dos caciques na direcao radial (spin up/down)
    {
        int sign0 = (b[0].mx > 0.0f) ? 1 : -1;
        b[0].sx = (float)sign0; b[0].sy = 0.0f; b[0].sz = 0.0f;
        if (MAX_BOLHAS >= 129) {
            int sign1 = (b[128].mx > 0.0f) ? 1 : -1;
            b[128].sx = -(float)sign1; b[128].sy = 0.0f; b[128].sz = 0.0f;
        }
    }
}

void inicializar_cena(Bolha *b, int *num_bolhas) {
    cenario2(b, num_bolhas);
}

int SDL_main(int argc, char *argv[]) {
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("Erro ao inicializar SDL\n");
        return 1;
    }

    // Usa a altura da tela, mantendo a proporcao quadrada 1:1.
    {
        int nd = 0;
        SDL_DisplayID *displays = SDL_GetDisplays(&nd);
        if (displays && nd > 0) {
            SDL_Rect rect;
            if (SDL_GetDisplayBounds(displays[0], &rect)) {
                window_height = (rect.h > 120) ? rect.h - 80 : rect.h;
                window_width = window_height;
            }
            SDL_free(displays);
        }
    }

    SDL_Window *win = SDL_CreateWindow("Toy Universe 3D - Bolhas - REV",
                                       window_width, window_height, 0);

    SDL_Renderer *ren = SDL_CreateRenderer(win, "direct3d12");
    if (!ren) {
        printf("D3D12 nao disponivel, tentando renderer padrao.\n");
        ren = SDL_CreateRenderer(win, NULL);
    }
    if (!ren) {
        printf("Erro ao criar renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    Bolha *b = (Bolha *)calloc(MAX_BOLHAS, sizeof(Bolha));
    if (!b) {
        printf("Erro ao alocar memoria para as bolhas\n");
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    // int num_bolhas = carregar_bolhas(b, "bolhas.dat");
    int num_bolhas;
    inicializar_cena(b, &num_bolhas);

    printf("\n=== DEBUG DE CARREGAMENTO ===\n");
    int count_p = 0;
    for (int i = 0; i < num_bolhas; i++) {
        if (b[i].tipo == 1) {
            count_p++;
            printf("P[%d] pos=(%.1f, %.1f, %.1f) r=%.2f\n",
                   i, b[i].cx, b[i].cy, b[i].cz, b[i].r);
        }
    }
    printf("Total bolhas: %d | Bolhas P: %d\n\n", num_bolhas, count_p);
    fflush(stdout);

    uint64_t last_time = SDL_GetTicks();
    int rodando = 1;
    SDL_Event e;

    Interacao *hits = NULL;
    int hits_cap = 0;
    int fase_expansao = FASE_EXPANSAO_FRAMES;
    int evoluir = 1;  // evolucao ligada; tecla E desativada

    int frame_count = 0;
    while (rodando) {
        frame_count++;
        uint64_t current_time = SDL_GetTicks();
        float dt = (current_time - last_time) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
        last_time = current_time;

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) rodando = 0;
            if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.scancode == SDL_SCANCODE_X) vista_atual = VISTA_XY;
                if (e.key.scancode == SDL_SCANCODE_Y) vista_atual = VISTA_YZ;
                if (e.key.scancode == SDL_SCANCODE_Z) vista_atual = VISTA_XZ;
                if (e.key.scancode == SDL_SCANCODE_I) vista_atual = VISTA_ISO;
                if (e.key.scancode == SDL_SCANCODE_ESCAPE) rodando = 0;
                if (e.key.scancode == SDL_SCANCODE_V) {
                    mostrar_bolhas = !mostrar_bolhas;
                }
                if (e.key.scancode == SDL_SCANCODE_C) {
                    modo_somente_cm = !modo_somente_cm;
                }
                if (e.key.scancode == SDL_SCANCODE_K) {
                    mostrar_somente_caciques = !mostrar_somente_caciques;
                }
                if (e.key.scancode == SDL_SCANCODE_G) {
                    mostrar_seta_dir = !mostrar_seta_dir;
                }
                if (e.key.scancode == SDL_SCANCODE_S) {
                    mostrar_seta_S = !mostrar_seta_S;
                }
                if (e.key.scancode == SDL_SCANCODE_M) {
                    mostrar_seta_M = !mostrar_seta_M;
                }
                // === PAN ===
                if (e.key.scancode == SDL_SCANCODE_LEFT)  pan_x -= pan_speed / zoom;
                if (e.key.scancode == SDL_SCANCODE_RIGHT) pan_x += pan_speed / zoom;
                if (e.key.scancode == SDL_SCANCODE_UP)    pan_y += pan_speed / zoom;
                if (e.key.scancode == SDL_SCANCODE_DOWN)  pan_y -= pan_speed / zoom;
                if (e.key.scancode == SDL_SCANCODE_PAGEUP)   pan_z += pan_speed / zoom;
                if (e.key.scancode == SDL_SCANCODE_PAGEDOWN) pan_z -= pan_speed / zoom;
            }
            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                if (e.wheel.y > 0) {
                    zoom *= 1.1f;
                } else if (e.wheel.y < 0) {
                    zoom *= 0.9f;
                }
                if (zoom < 0.05f) zoom = 0.05f;
                if (zoom > 20.0f) zoom = 20.0f;
            }
        }

        if (evoluir) {
            // Passo de luz comum (calculado antes das interacoes para reemissao dos caciques)
            float delta_r = 0.1f * dt;

            // Novo frame: limpa controle de uma interacao por bolha
            proc_generation++;
            if (proc_generation == 0) {
                proc_generation = 1;
                for (int i = 0; i < MAX_BOLHAS; i++) proc_hit[i] = 0;
            }

            // Fase de expansao pura: sem interacoes por alguns frames
            if (fase_expansao > 0) {
                fase_expansao--;
            } else {
            // Testa todos os pares a cada frame (O(N^2))
            int max_hits = num_bolhas * (num_bolhas - 1) / 2;
            if (max_hits < 1) max_hits = 1;
            if (hits_cap < max_hits) {
                hits = (Interacao*)realloc(hits, max_hits * sizeof(Interacao));
                hits_cap = max_hits;
            }
            int num_hits = 0;
            for (int i = 0; i < num_bolhas; i++) {
                for (int j = i + 1; j < num_bolhas; j++) {
                    // P x P nao interage
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
            for (int k = 0; k < lim_hits; k++) {
                int ia = hits[k].a;
                int ib = hits[k].b;

                if (hits[k].prio == 0) {
                    if (!kk_log) kk_log = fopen("kk_log.txt", "w");
                    float rsum = 0.0f;
                    int rc = 0;
                    for (int i = 0; i < num_bolhas; i++) {
                        if (i == 0 || i == 128) continue;
                        if (b[i].timeout > 0 && b[i].cacique_idx != -1) {
                            rsum += b[i].r;
                            rc++;
                        }
                    }
                    float ar_before = (rc > 0) ? rsum / rc : 0.0f;
                    fprintf(kk_log, "[frame %d] antes KK: num_hits=%d lim_hits=%d ar_delegados=%.6f\n",
                            frame_count, num_hits, lim_hits, ar_before);
                    fflush(kk_log);
                }

                int reusa_k_a = (hits[k].prio == 1 && (ia % 128 == 0) && !hits[k].reemit_a);
                int reusa_k_b = (hits[k].prio == 1 && (ib % 128 == 0) && !hits[k].reemit_b);
                if ((proc_hit[ia] == proc_generation && !reusa_k_a) ||
                    (proc_hit[ib] == proc_generation && !reusa_k_b)) {
                    continue;
                }
                aplicar_interacao(&hits[k], b);

                if (hits[k].prio == 0) {
                    float rsum = 0.0f;
                    int rc = 0;
                    for (int i = 0; i < num_bolhas; i++) {
                        if (i == 0 || i == 128) continue;
                        if (b[i].timeout > 0 && b[i].cacique_idx != -1) {
                            rsum += b[i].r;
                            rc++;
                        }
                    }
                    float ar_after = (rc > 0) ? rsum / rc : 0.0f;
                    fprintf(kk_log, "[frame %d] depois KK: cxc=%d ar_delegados=%.6f\n",
                            frame_count, contagem_cacique_cacique, ar_after);
                    fflush(kk_log);
                }

                if (!reusa_k_a) proc_hit[ia] = proc_generation;
                if (!reusa_k_b) proc_hit[ib] = proc_generation;
            }
            }

            // Passo de luz: aplica o delta_r ja calculado
            for (int i = 0; i < num_bolhas; i++) {
                b[i].r += delta_r;
            }

            // Usa dir·S como sonda: cada delegado da um passo de luz na
            // direcao que alinha o seu eixo radial com o seu S. S fica
            // imutavel; apenas o centro e transladado.
            for (int i = 0; i < num_bolhas; i++) {
                if (b[i].cacique_idx != -1 && b[i].timeout > 0) {
                    ajustar_radial_por_translacao(&b[i], &b[b[i].cacique_idx], delta_r);
                }
            }

            // Atualizacoes de estado terminadas; renderizacao comeca abaixo
            atualizar_historico_cm(b, num_bolhas);
        }

        SDL_SetRenderDrawColor(ren, 15, 15, 15, 255);
        SDL_RenderClear(ren);

        if (!modo_somente_cm) {
            desenhar_contador_fugidas(ren, b, num_bolhas);
            desenhar_versao(ren);
            desenhar_eixos(ren);

            float raio_maximo = 2.0f * (window_height / zoom);

            // Desenha todos os pontos das bolhas em uma unica chamada
            desenhar_pontos_bolhas(ren, b, num_bolhas, raio_maximo);

            // No modo V (mostrar_bolhas==false) as setas devem ser desenhadas
            // independentemente do raio da bolha.
            for (int i = 0; i < num_bolhas; i++) {
                if (mostrar_somente_caciques && (i % 128 != 0)) continue;

                if (mostrar_bolhas) {
                    if (b[i].r < 0.00005f) continue;
                    if (b[i].r > raio_maximo) continue;
                }

                float tamanho_seta = 0.25f;

                // Seta M (opcional, tecla M)
                if (mostrar_seta_M) {
                    desenhar_seta(ren, b[i].cx, b[i].cy, b[i].cz, b[i].mx, b[i].my, b[i].mz, tamanho_seta, 120, 120, 40);
                }

                // Seta S para todas as bolhas (default ligada, tecla S)
                if (mostrar_seta_S) {
                    Uint8 sr, sg, sb;
                    if (i % 128 == 0) {
                        sr = 255; sg = 0; sb = 0;        // cacique: vermelho
                    } else if (b[i].tipo == 1) {
                        sr = 255; sg = 140; sb = 0;      // P: laranja
                    } else if (b[i].cacique_idx != -1) {
                        sr = 255; sg = 255; sb = 100;    // delegado: amarelo
                    } else {
                        sr = 0; sg = 255; sb = 255;      // S comum: ciano
                    }
                    desenhar_seta(ren, b[i].cx, b[i].cy, b[i].cz, b[i].sx, b[i].sy, b[i].sz, tamanho_seta, sr, sg, sb);
                }

                // Seta dir para delegados (opcional, tecla G)
                if (b[i].cacique_idx != -1 && mostrar_seta_dir) {
                    desenhar_seta(ren, b[i].cx, b[i].cy, b[i].cz, b[i].dir_x, b[i].dir_y, b[i].dir_z, tamanho_seta, 0, 255, 0);
                }

            }

            // Labels de spin (UP/DOWN) em torno dos caciques
            desenhar_spin_labels(ren, b, num_bolhas);
        }

        desenhar_trajetoria_cm(ren);
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    free(hits);
    free(g_pts_vertices);
    free(g_pts_indices);
    free(b);
    free(hist_cmx);
    free(hist_cmy);
    free(hist_cmz);

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}