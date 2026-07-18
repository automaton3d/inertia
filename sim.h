#ifndef SIM_H
#define SIM_H

#include <stddef.h>

/* ---------- Tipos ---------- */

typedef struct { int x, y; } IVec2;

typedef enum { TIPO_P = 'p', TIPO_S = 's' } Tipo;

typedef struct {
    IVec2 cel;    /* posição em células (inteiro puro)             */
    IVec2 P;      /* momento: vetor inteiro (direção + magnitude)  */
    int   M;      /* massa inteira (>= 1)                          */
    IVec2 acc;    /* acumulador DDA/Bresenham (inteiro)            */
    Tipo  tipo;
    int   grupo;
    int   vivo;   /* 0 = removida (absorvida numa fusão)           */
} Bolha;

/* ---------- Parâmetros de render ---------- */

#define WORLD_MIN   (-5)
#define WORLD_SIZE  (10)     /* mundo lógico: [-5, 5) em células * ESCALA */
#define ESCALA      (100)    /* 1 unidade "física" = 100 células          */
#define OFFSET_X    (0)
#define OFFSET_Y    (0)

/* Raio (em células) considerado "contato" para fusão p+s */
#define R_CONTATO   (25)

/* ---------- API ---------- */

void sim_init(Bolha *bolhas, size_t n);
void sim_step(Bolha *bolhas, size_t n);   /* 1 tick */

/* utilidades de render (mapeiam célula -> pixel) */
int  map_x(int cel_x, int largura_px);
int  map_y(int cel_y, int altura_px);
int  map_r(int cel_r, int menor_lado_px);

#endif
