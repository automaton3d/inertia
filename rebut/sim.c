#include "sim.h"

/* =========================================================
 * Modelo:
 *   Cada bolha tem momento P (vetor inteiro) e massa M (inteiro).
 *   Velocidade = P / M, mas NUNCA dividimos: usamos um acumulador
 *   inteiro (DDA/Bresenham). A cada tick somamos P em acc e, sempre
 *   que |acc.k| atinge M, andamos 1 célula no eixo k e descontamos M.
 *
 *   Colisão p + s (mesma vizinhança): fusão perfeitamente inelástica
 *       P_p <- P_p + P_s        (conservação de momento)
 *       M_p <- M_p + M_s
 *       s   -> vivo = 0
 *   Como P se conserva e M cresce, v = P/M cai automaticamente:
 *   é exatamente p = m*v da mecânica clássica, em inteiros.
 * ========================================================= */

/* ---------- vetor inteiro ---------- */

static IVec2 ivec_add(IVec2 a, IVec2 b) {
    return (IVec2){ a.x + b.x, a.y + b.y };
}

static int iabs(int v) { return v < 0 ? -v : v; }

/* Chebyshev: distância inteira sem sqrt */
static int cheby(IVec2 a, IVec2 b) {
    int dx = iabs(a.x - b.x);
    int dy = iabs(a.y - b.y);
    return dx > dy ? dx : dy;
}

/* ---------- render ---------- */

int map_x(int cel_x, int largura_px) {
    long total = (long)WORLD_SIZE * ESCALA;
    return (int)(( (long)(cel_x - WORLD_MIN * ESCALA) * largura_px ) / total);
}

int map_y(int cel_y, int altura_px) {
    long total = (long)WORLD_SIZE * ESCALA;
    /* eixo y invertido para tela */
    return altura_px - (int)(( (long)(cel_y - WORLD_MIN * ESCALA) * altura_px ) / total);
}

int map_r(int cel_r, int menor_lado_px) {
    long total = (long)WORLD_SIZE * ESCALA;
    return (int)(( (long)cel_r * menor_lado_px ) / total);
}

/* ---------- física discreta ---------- */

/* Move UMA bolha 1 tick, usando o acumulador. */
static void mover(Bolha *b) {
    if (!b->vivo) return;
    if (b->M <= 0) b->M = 1;              /* segurança */

    b->acc.x += b->P.x;
    b->acc.y += b->P.y;

    while (b->acc.x >=  b->M) { b->cel.x += 1; b->acc.x -= b->M; }
    while (b->acc.x <= -b->M) { b->cel.x -= 1; b->acc.x += b->M; }
    while (b->acc.y >=  b->M) { b->cel.y += 1; b->acc.y -= b->M; }
    while (b->acc.y <= -b->M) { b->cel.y -= 1; b->acc.y += b->M; }
}

/* Fusão inelástica p <- p + s (conserva momento, soma massa). */
static void fundir(Bolha *p, Bolha *s) {
    p->P = ivec_add(p->P, s->P);
    p->M = p->M + s->M;
    /* acc do p permanece: a fase do movimento não pula quando M muda */
    s->vivo = 0;
}

/* Procura s vivos em contato com cada p (mesmo grupo) e funde. */
static void resolver_colisoes(Bolha *bolhas, size_t n) {
    for (size_t i = 0; i < n; i++) {
        Bolha *p = &bolhas[i];
        if (!p->vivo || p->tipo != TIPO_P) continue;

        for (size_t j = 0; j < n; j++) {
            if (i == j) continue;
            Bolha *s = &bolhas[j];
            if (!s->vivo || s->tipo != TIPO_S) continue;
            if (s->grupo != p->grupo) continue;
            if (cheby(p->cel, s->cel) <= R_CONTATO) {
                fundir(p, s);
            }
        }
    }
}

void sim_step(Bolha *bolhas, size_t n) {
    for (size_t i = 0; i < n; i++) mover(&bolhas[i]);
    resolver_colisoes(bolhas, n);
}

/* ---------- init (equivalente inteiro ao seu setup) ---------- */

static IVec2 cel_de(double wx, double wy) {
    return (IVec2){
        (int)(wx * ESCALA) + OFFSET_X,
        (int)(wy * ESCALA) + OFFSET_Y
    };
}

void sim_init(Bolha *bolhas, size_t n) {
    for (size_t i = 0; i < n; i++) {
        bolhas[i] = (Bolha){0};
        bolhas[i].vivo = 1;
        bolhas[i].M = 1;
    }
    if (n < 5) return;

    /* ---- Grupo 0 ---- */

    /* S1 — parado, só massa */
    bolhas[0].cel   = cel_de(-1.5, -3.10);
    bolhas[0].P     = (IVec2){0, 0};
    bolhas[0].tipo  = TIPO_S;
    bolhas[0].grupo = 0;

    /* S2 — parado */
    bolhas[1].cel   = cel_de( 0.55, -3.40);
    bolhas[1].P     = (IVec2){0, 0};
    bolhas[1].tipo  = TIPO_S;
    bolhas[1].grupo = 0;

    /* P0 — momento inicial "quase leste, levemente para cima"
       Antes: vec_norm({1, 0.05}) (|m|=1)
       Agora: P = (20, 1) → mesma direção, magnitude inteira.
       v_inicial = (20,1)/1 = (20,1) céls/tick. */
    bolhas[2].cel   = cel_de(-1.5, -3.10);   /* mesma pos de S1 (contato imediato) */
    bolhas[2].P     = (IVec2){20, 1};
    bolhas[2].M     = 1;
    bolhas[2].tipo  = TIPO_P;
    bolhas[2].grupo = 0;

    /* ---- Grupo 1 ---- */

    /* P1 — mesmo momento inicial de P0 */
    bolhas[3].cel   = cel_de(-1.5, 3.10);
    bolhas[3].P     = (IVec2){20, 1};
    bolhas[3].M     = 1;
    bolhas[3].tipo  = TIPO_P;
    bolhas[3].grupo = 1;

    /* S3 — parado (grupo 1 tem só 1 s → frente ficará mais rápida
       que o grupo 0 depois das fusões) */
    bolhas[4].cel   = cel_de(0.55, 3.40);
    bolhas[4].P     = (IVec2){0, 0};
    bolhas[4].tipo  = TIPO_S;
    bolhas[4].grupo = 1;
}
