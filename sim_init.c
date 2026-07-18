#include "sim.h"
#include <math.h>

// --- Funções de vetor ---
float vec_len(Vec2 v) { return sqrtf(v.x * v.x + v.y * v.y); }
Vec2  vec_add(Vec2 a, Vec2 b) { return (Vec2){a.x + b.x, a.y + b.y}; }
Vec2  vec_sub(Vec2 a, Vec2 b) { return (Vec2){a.x - b.x, a.y - b.y}; }
Vec2  vec_scale(Vec2 v, float s) { return (Vec2){v.x * s, v.y * s}; }
Vec2  vec_norm(Vec2 v) {
    float l = vec_len(v);
    return (l > 0.0f) ? (Vec2){v.x / l, v.y / l} : (Vec2){0.0f, 0.0f};
}

// --- Mapeamento mundo -> tela (com aspect ratio preservado) ---
float map_x(float wx) {
    return VIEW_OFFSET_X + (wx - WORLD_MIN) / WORLD_SIZE * VIEW_SIZE;
}
float map_y(float wy) {
    return VIEW_OFFSET_Y + VIEW_SIZE - (wy - WORLD_MIN) / WORLD_SIZE * VIEW_SIZE;
}
float map_r(float wr) {
    return wr / WORLD_SIZE * VIEW_SIZE;
}

// --- Inicialização das bolhas ---
void init_sim(Bolha* bolhas) {
    // ============================================================
    //  AJUSTES APLICADOS:
    //   - Raios reduzidos à metade
    //   - Posições aproximadas
    //   - OFFSET_X = -4.0 (mais à esquerda)
    //   - Ambos os p com mesmo x = -1.5
    //   - Vetores m dos p iguais: ~25° = (cos25°, sin25°)
    //     cos(25°) ≈ 0.9063, sin(25°) ≈ 0.4226
    // ============================================================

    // Vetor m comum para ambos os p (25 graus)
    Vec2 m_p = vec_norm((Vec2){0.9063f, 0.4226f});

    // ===== PAR ORIGINAL (grupo 0) =====
    // P1 — mesmo x = -1.5, y = -0.9
    bolhas[0].centro = (Vec2){-1.5f + OFFSET_X, -0.90f + OFFSET_Y};
    bolhas[0].raio   = 0.125f;
    bolhas[0].m      = m_p;
    bolhas[0].tipo   = 'p';
    bolhas[0].grupo  = 0;

    // S1
    bolhas[1].centro = (Vec2){-0.70f + OFFSET_X, -1.00f + OFFSET_Y};
    bolhas[1].raio   = 0.225f;
    bolhas[1].m      = vec_norm((Vec2){0.3f, 1.0f});
    bolhas[1].tipo   = 's';
    bolhas[1].grupo  = 0;

    // S2
    bolhas[2].centro = (Vec2){0.40f + OFFSET_X, -0.10f + OFFSET_Y};
    bolhas[2].raio   = 0.30f;
    bolhas[2].m      = vec_norm((Vec2){-0.5f, 0.8f});
    bolhas[2].tipo   = 's';
    bolhas[2].grupo  = 0;

    // ===== NOVO PAR (grupo 1) — ACIMA =====
    // P2 — mesmo x = -1.5, y = 3.1
    bolhas[3].centro = (Vec2){-1.5f + OFFSET_X, 3.10f + OFFSET_Y};
    bolhas[3].raio   = 0.15f;
    bolhas[3].m      = m_p;  // MESMO vetor de P1
    bolhas[3].tipo   = 'p';
    bolhas[3].grupo  = 1;

    // S3
    bolhas[4].centro = (Vec2){0.55f + OFFSET_X, 3.40f + OFFSET_Y};
    bolhas[4].raio   = 0.25f;
    bolhas[4].m      = vec_norm((Vec2){-0.7f, 0.5f});
    bolhas[4].tipo   = 's';
    bolhas[4].grupo  = 1;
}