#define _CRT_SECURE_NO_WARNINGS
#define _WIN32_WINNT 0x0600
#include <windows.h>
#define _USE_MATH_DEFINES
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define L 128
#define MAX_BOLHAS (9*L)
#define MAX_PONTOS 500

#define R_FUGA 15.0f
#define R_MAX 0.50f
#define FASE_EXPANSAO_FRAMES 1  // frames de expansao simultanea sem interacoes

// === HISTORICO DO CENTRO DE MASSA (apenas visualizacao) ===
#define MAX_HIST 200
float *hist_cmx = NULL;
float *hist_cmy = NULL;
float *hist_cmz = NULL;
int hist_count = 0;
int hist_capacity = 0;

int contagem_cacique_cacique = 0;
char g_raio_medio_str[128] = "raio medio: --";
float g_r_teorico = 0.0f;
int g_frame_number = 0;

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
    int modo_m;
    float dir_x, dir_y, dir_z;
    int timeout;
    int cacique_idx;
} Bolha;

typedef struct {
    int a, b;
    int info_alvo;
    int consome_timeout;
    int consome_timeout_b;
    int toggle_modo;
    int reemit_a;
    int reemit_b;
    float ax, ay, az;
    float bx, by, bz;
    float info_x, info_y, info_z;
} Interacao;

enum Vista { VISTA_XY, VISTA_XZ, VISTA_YZ, VISTA_ISO };
enum Vista vista_atual = VISTA_XY;
float zoom = 14.0f / L;
bool zoom_out_ativado = false;
bool mostrar_bolhas = true;
bool modo_somente_cm = false;
bool mostrar_somente_caciques = false;

// === PAN ===
float pan_x = 0.0f;
float pan_y = 0.0f;
float pan_z = 0.0f;
float pan_speed = 2.0f;


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
    b->modo_m = 0;
    b->cacique_idx = -1;
    inicializar_pontos(b);
}

static float delta_pbc(float a, float b, float period) {
    float d = a - b;
    float h = period * 0.5f;
    if (d > h) d -= period;
    else if (d < -h) d += period;
    return d;
}

static void wrap_pos(float *x, float period) {
    float h = period * 0.5f;
    float v = fmodf(*x + h, period);
    if (v < 0.0f) v += period;
    *x = v - h;
}

void reemitir_bolha(Bolha *b, float cx, float cy, float cz) {
    b->cx = cx; b->cy = cy; b->cz = cz;
    wrap_pos(&b->cx, L); wrap_pos(&b->cy, L); wrap_pos(&b->cz, L);
    b->r = 0.0f;
    // raio zerado no antípoda; delta_r comum é aplicado a seguir
}

int detectar_interacao(Bolha *a, Bolha *b, int idx_a, int idx_b,
                       Bolha *bolhas, int num_bolhas, Interacao *h) {
    (void)bolhas; (void)num_bolhas;

    h->a = idx_a; h->b = idx_b;
    h->info_alvo = -1;
    h->consome_timeout = 0;
    h->consome_timeout_b = 0;
    h->toggle_modo = 0;
    h->reemit_a = 1;
    h->reemit_b = 1;

    if (b->tipo != 0) return 0;

    int a_cacique = 1;
    int b_cacique = 1;

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
        h->ax = a->cx - a->r * dx;
        h->ay = a->cy - a->r * dy;
        h->az = a->cz - a->r * dz;
        h->bx = b->cx + b->r * dx;
        h->by = b->cy + b->r * dy;
        h->bz = b->cz + b->r * dz;
        return 1;
    }
    // Se caciques nao estao em contato de superficie, nada mais a fazer
    if (a_cacique && b_cacique) return 0;

    // suprime S -> cacique (deixa P -> cacique e cacique -> cacique)
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

    float vx, vy, vz;
    int s = 0;
    if (a->timeout > 0) {
        vx = a->dir_x; vy = a->dir_y; vz = a->dir_z;
    } else if (a->tipo == 1) {
        vx = a->mx; vy = a->my; vz = a->mz;
    } else if (a_cacique) {
        s = a->modo_m & 3;
        if (s == 0) { vx = a->mx; vy = a->my; vz = a->mz; }
        else if (s == 1) { vx = a->sx; vy = a->sy; vz = a->sz; }
        else if (s == 2) { vx = -a->mx; vy = -a->my; vz = -a->mz; }
        else { vx = -a->sx; vy = -a->sy; vz = -a->sz; }
    } else {
        // S comum: modo_m escolhe entre M (0) e S (1)
        if (a->modo_m) { vx = a->sx; vy = a->sy; vz = a->sz; }
        else { vx = a->mx; vy = a->my; vz = a->mz; }
    }

    float tip_x = a->cx + a->r * vx;
    float tip_y = a->cy + a->r * vy;
    float tip_z = a->cz + a->r * vz;

    float dx = delta_pbc(tip_x, b->cx, L);
    float dy = delta_pbc(tip_y, b->cy, L);
    float dz = delta_pbc(tip_z, b->cz, L);
    if (dx*dx + dy*dy + dz*dz >= (b->r + 0.001f) * (b->r + 0.001f)) return 0;

    // Vetor ativo do alvo (para reemissao independente do iniciador)
    float vbx, vby, vbz;
    int sb = 0;
    if (b->timeout > 0) {
        vbx = b->dir_x; vby = b->dir_y; vbz = b->dir_z;
    } else if (b_cacique) {
        sb = b->modo_m & 3;
        if (sb == 0) { vbx = b->mx; vby = b->my; vbz = b->mz; }
        else if (sb == 1) { vbx = b->sx; vby = b->sy; vbz = b->sz; }
        else if (sb == 2) { vbx = -b->mx; vby = -b->my; vbz = -b->mz; }
        else { vbx = -b->sx; vby = -b->sy; vbz = -b->sz; }
    } else if (b->tipo == 1) {
        vbx = b->mx; vby = b->my; vbz = b->mz;
    } else {
        // S comum: modo_m escolhe entre M (0) e S (1)
        if (b->modo_m) { vbx = b->sx; vby = b->sy; vbz = b->sz; }
        else { vbx = b->mx; vby = b->my; vbz = b->mz; }
    }

    float bx = b->cx + b->r * vbx;
    float by = b->cy + b->r * vby;
    float bz = b->cz + b->r * vbz;

    if (a_cacique && b_cacique) {
        // cacique x cacique: ambos reemitem nos pontos antipodais ao contato
        contagem_cacique_cacique++;
        h->ax = 2.0f * a->cx - tip_x;
        h->ay = 2.0f * a->cy - tip_y;
        h->az = 2.0f * a->cz - tip_z;
        h->bx = 2.0f * b->cx - tip_x;
        h->by = 2.0f * b->cy - tip_y;
        h->bz = 2.0f * b->cz - tip_z;
    } else if (repelir) {
        // delegado x delegado de tribos diferentes: repulsao antipodal
        h->ax = 2.0f * a->cx - tip_x;
        h->ay = 2.0f * a->cy - tip_y;
        h->az = 2.0f * a->cz - tip_z;
        h->bx = 2.0f * b->cx - tip_x;
        h->by = 2.0f * b->cy - tip_y;
        h->bz = 2.0f * b->cz - tip_z;
    } else if (atrair) {
        // delegado x delegado da mesma tribo: ambos seguem o seu dir
        h->ax = a->cx + a->r * a->dir_x;
        h->ay = a->cy + a->r * a->dir_y;
        h->az = a->cz + a->r * a->dir_z;
        h->bx = b->cx + b->r * b->dir_x;
        h->by = b->cy + b->r * b->dir_y;
        h->bz = b->cz + b->r * b->dir_z;
    } else if (a_cacique && !b_cacique) {
        // cacique -> S/delegado: cacique reemite no centro atual (sem deslocar);
        // o alvo reemite no ponto de contato (tip) e vira/atualiza delegado.
        h->ax = a->cx; h->ay = a->cy; h->az = a->cz;
        h->bx = tip_x; h->by = tip_y; h->bz = tip_z;

        // dir aponta do alvo para o centro do cacique (vetor raio, PBC)
        h->info_x = delta_pbc(a->cx, h->bx, L);
        h->info_y = delta_pbc(a->cy, h->by, L);
        h->info_z = delta_pbc(a->cz, h->bz, L);
        h->info_alvo = idx_b;
    } else {
        // iniciador no ponto de contato, alvo no transporte paralelo
        h->ax = tip_x; h->ay = tip_y; h->az = tip_z;
        h->bx = bx; h->by = by; h->bz = bz;
    }

    if (a->timeout > 0) h->consome_timeout = 1;
    if (a->timeout > 0 && b->timeout > 0) h->consome_timeout_b = 1;
    // S comuns alternam M/S em aplicar_interacao.
    h->toggle_modo = 0;

    return 1;
}

void aplicar_interacao(const Interacao *h, Bolha *bolhas) {
    if (h->reemit_a) reemitir_bolha(&bolhas[h->a], h->ax, h->ay, h->az);
    if (h->reemit_b) reemitir_bolha(&bolhas[h->b], h->bx, h->by, h->bz);

    if (h->info_alvo >= 0) {
        float ix = h->info_x, iy = h->info_y, iz = h->info_z;
        normalizar3D(&ix, &iy, &iz);
        Bolha *alvo = &bolhas[h->info_alvo];
        alvo->dir_x = ix;
        alvo->dir_y = iy;
        alvo->dir_z = iz;

        // Cacique cria delegado; delegado repassa cacique_idx.
        if (1) {
            alvo->cacique_idx = h->a;
        } else if (bolhas[h->a].timeout > 0 && bolhas[h->a].cacique_idx != -1) {
            alvo->cacique_idx = bolhas[h->a].cacique_idx;
        }
        // Renova timeout: cacique da vida cheia; delegado passa o que resta (decaindo)
        if (1) {
            alvo->timeout = L / 16;
        } else if (alvo->timeout == 0 && bolhas[h->a].timeout > 0) {
            alvo->timeout = (bolhas[h->a].timeout > 1) ? (bolhas[h->a].timeout - 1) : 0;
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

    // Todos sao caciques: nao ha S comuns para ajustar modo_m.

    // Caciques avancam o ciclo a cada reemissao (todos sao caciques)
    for (int lado = 0; lado < 2; lado++) {
        int idx = (lado == 0) ? h->a : h->b;
        if (((lado == 0) ? h->reemit_a : h->reemit_b)) {
            bolhas[idx].modo_m = (bolhas[idx].modo_m + 1) % 4;
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

void desenhar_seta(SDL_Renderer *ren, float cx, float cy, float cz,
                   float vx, float vy, float vz, float r, float color_r, float color_g, float color_b) {
    float px1, py1, px2, py2;
    project(cx, cy, cz, &px1, &py1);
    project(cx + vx * r, cy + vy * r, cz + vz * r, &px2, &py2);

    SDL_SetRenderDrawColor(ren, (Uint8)color_r, (Uint8)color_g, (Uint8)color_b, 255);
    SDL_RenderLine(ren, px1, py1, px2, py2);

    float tamanho_ponta = 5.0f * (mostrar_bolhas ? 1.0f : 0.5f);

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


void desenhar_trajetoria_cm(SDL_Renderer *ren, Bolha *b, int num_bolhas) {
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

    // Desenha trilha 3D do CM (todas as posicoes anteriores persistem)
    for (int i = 1; i < hist_count; i++) {
        float px0, py0, px1, py1;
        project(hist_cmx[i - 1], hist_cmy[i - 1], hist_cmz[i - 1], &px0, &py0);
        project(hist_cmx[i], hist_cmy[i], hist_cmz[i], &px1, &py1);
        SDL_SetRenderDrawColor(ren, 0, 255, 128, 120);
        SDL_RenderLine(ren, px0, py0, px1, py1);
    }

    // Ponto branco do CM atual no grafico principal
    float px, py;
    project(cmx, cmy, cmz, &px, &py);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_FRect r = {px - 3, py - 3, 6, 6};
    SDL_RenderFillRect(ren, &r);
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

    // Raio medio
    SDL_RenderDebugText(ren, 10.0f, 56.0f, g_raio_medio_str);
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

    int max_points = num_bolhas * MAX_PONTOS;
    if (max_points > g_pts_capacity) {
        g_pts_vertices = (SDL_Vertex *)realloc(g_pts_vertices, sizeof(SDL_Vertex) * 4 * max_points);
        g_pts_indices = (int *)realloc(g_pts_indices, sizeof(int) * 6 * max_points);
        g_pts_capacity = max_points;
    }

    int vcount = 0;
    int icount = 0;

    for (int i = 0; i < num_bolhas; i++) {
        if (b[i].r < 0.00005f || b[i].r > raio_maximo) continue;

        SDL_FColor c;
        float size;
        int step = 1;
        float ox = 0.0f, oy = 0.0f;

        // Todos sao caciques
        c = (SDL_FColor){ 1.0f, 50.0f/255.0f, 50.0f/255.0f, 1.0f };
        size = 2.0f;

        float r_vis = b[i].r;
        for (int p = 0; p < MAX_PONTOS; p += step) {
            float px, py;
            project(b[i].cx + r_vis * b[i].pontos[p].dx,
                    b[i].cy + r_vis * b[i].pontos[p].dy,
                    b[i].cz + r_vis * b[i].pontos[p].dz, &px, &py);

            float x0 = px + ox;
            float y0 = py + oy;
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

    // Uma unica semente para distribuicao uniforme no toro
    srand(12345);

    for (int i = 0; i < n_pares; i++) {
        float mx, my, mz, ax, ay, az;
        gerar_ms_fibonacci(i, n_pares, &mx, &my, &mz, &ax, &ay, &az);

        // posicoes aleatorias no toro de lado L, raios uniformes [0, R_MAX]
        float cx1 = ((float)rand() / (float)RAND_MAX) * L - L * 0.5f;
        float cy1 = ((float)rand() / (float)RAND_MAX) * L - L * 0.5f;
        float cz1 = ((float)rand() / (float)RAND_MAX) * L - L * 0.5f;
        float r1 = ((float)rand() / (float)RAND_MAX) * R_MAX;
        inicializar_bolha(&b[i], cx1, cy1, cz1, r1,
                          mx, my, mz, ax, ay, az, 0);

        float cx2 = ((float)rand() / (float)RAND_MAX) * L - L * 0.5f;
        float cy2 = ((float)rand() / (float)RAND_MAX) * L - L * 0.5f;
        float cz2 = ((float)rand() / (float)RAND_MAX) * L - L * 0.5f;
        float r2 = ((float)rand() / (float)RAND_MAX) * R_MAX;
        inicializar_bolha(&b[i + n_pares], cx2, cy2, cz2, r2,
                          -mx, -my, -mz, ax, ay, az, 0);
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

    // Raio teorico para empacotamento CFC (densidade maxima pi/(3*sqrt(2)))
    g_r_teorico = L / cbrtf(4.0f * sqrtf(2.0f) * (float)num_bolhas);

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

    while (rodando) {
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
                if (e.key.scancode == SDL_SCANCODE_M) {
                    zoom_out_ativado = !zoom_out_ativado;
                    zoom = zoom_out_ativado ? 0.25f : 1.0f;
                }
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
                if (zoom > 5.0f) zoom = 5.0f;
            }
        }

        if (evoluir) {
            g_frame_number++;

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
                    if (detectar_interacao(&b[i], &b[j], i, j, b, num_bolhas, &h)) {
                        hits[num_hits++] = h;
                    } else if (detectar_interacao(&b[j], &b[i], j, i, b, num_bolhas, &h)) {
                        hits[num_hits++] = h;
                    }
                }
            }

            // Aplica as reemissões e atualizações de estado de uma só vez
            for (int k = 0; k < num_hits; k++) {
                aplicar_interacao(&hits[k], b);
            }
            }

            // Passo de luz comum: todas as bolhas aumentam r pelo mesmo delta_r
            // (aplicado depois das reemissões para que as reemitidas também recebam o passo)
            float delta_r = 0.1f * dt;
            for (int i = 0; i < num_bolhas; i++) {
                b[i].r += delta_r;
            }

            // Atualiza display do raio medio a cada 30 frames
            if ((g_frame_number % 30) == 0) {
                double soma = 0.0;
                for (int i = 0; i < num_bolhas; i++) soma += b[i].r;
                float raio_medio = (float)(soma / num_bolhas);
                float erro = 0.0f;
                if (g_r_teorico > 0.0f) {
                    erro = (raio_medio - g_r_teorico) / g_r_teorico * 100.0f;
                }
                snprintf(g_raio_medio_str, sizeof(g_raio_medio_str),
                         "medio %.4f | teorico %.4f | erro %.1f%%",
                         raio_medio, g_r_teorico, erro);
            }
        }

        SDL_SetRenderDrawColor(ren, 15, 15, 15, 255);
        SDL_RenderClear(ren);

        if (!modo_somente_cm) {
            desenhar_contador_fugidas(ren, b, num_bolhas);
            // desenhar_eixos(ren);  // vetores removidos neste teste
            desenhar_cubo_universo(ren);

            float raio_maximo = 2.0f * (window_height / zoom);

            // Desenha todos os pontos das bolhas em uma unica chamada
            desenhar_pontos_bolhas(ren, b, num_bolhas, raio_maximo);

            // Vetores removidos neste teste; apenas pontos e cubo sao desenhados.
        }

        desenhar_trajetoria_cm(ren, b, num_bolhas);
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