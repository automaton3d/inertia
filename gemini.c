#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>

#define WINDOW_WIDTH  800
#define WINDOW_HEIGHT 800

/* Estrutura idêntica ao dicionário do Python */
typedef struct {
    float cx, cy;
    float r;
    float mx, my; // Vetor de momento unitário
    char tipo;    // 'p' ou 's'
} Bolha;

/* Mapeamento do espaço lógico [-6, 6] para pixels da tela (800x800) */
/* Mantendo o eixo Y invertido para que o "para cima" do Python seja "para cima" na tela */
float map_x(float x) {
    return ((x + 6.0f) / 12.0f) * WINDOW_WIDTH;
}

float map_y(float y) {
    return ((6.0f - y) / 12.0f) * WINDOW_HEIGHT;
}

float map_r(float r) {
    return (r / 12.0f) * WINDOW_WIDTH;
}

/* Função auxiliar para desenhar uma circunferência vazia na SDL3 */
void draw_circle(SDL_Renderer *renderer, float cx, float cy, float r) {
    const int pontos = 64;
    float passo = 2.0f * (float)M_PI / pontos;
    for (int i = 0; i < pontos; i++) {
        float theta1 = i * passo;
        float theta2 = (i + 1) * passo;
        
        float x1 = map_x(cx + r * cosf(theta1));
        float y1 = map_y(cy + r * sinf(theta1));
        float x2 = map_x(cx + r * cosf(theta2));
        float y2 = map_y(cy + r * sinf(theta2));
        
        SDL_RenderLine(renderer, x1, y1, x2, y2);
    }
}

/* Função auxiliar para desenhar o vetor de momento (m) com uma ponta de seta simples */
void draw_arrow(SDL_Renderer *renderer, float cx, float cy, float r, float mx, float my) {
    float x_start = map_x(cx);
    float y_start = map_y(cy);
    float x_end = map_x(cx + r * mx);
    float y_end = map_y(cy + r * my);
    
    // Desenha a haste
    SDL_RenderLine(renderer, x_start, y_start, x_end, y_end);
    
    // Desenha uma ponta de seta básica
    float angulo = atan2f(y_end - y_start, x_end - x_start);
    float arrow_len = 10.0f;
    float x_p1 = x_end - arrow_len * cosf(angulo - 0.5f);
    float y_p1 = y_end - arrow_len * sinf(angulo - 0.5f);
    float x_p2 = x_end - arrow_len * cosf(angulo + 0.5f);
    float y_p2 = y_end - arrow_len * sinf(angulo + 0.5f);
    
    SDL_RenderLine(renderer, x_end, y_end, x_p1, y_p1);
    SDL_RenderLine(renderer, x_end, y_end, x_p2, y_p2);
}

int main(int argc, char *argv[]) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Erro ao inicializar SDL: %s", SDL_GetError());
        return 1;
    }

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    if (!SDL_CreateWindowAndRenderer("Toy Universe Simulation (C + SDL3)", WINDOW_WIDTH, WINDOW_HEIGHT, 0, &window, &renderer)) {
        SDL_Log("Erro ao criar janela/renderer: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    /* Inicialização do cenário de Bolhas */
    Bolha bolhas[3];
    int num_bolhas = 3;
    int frame = 0;

    // P
    bolhas[0].cx = -2.5f; bolhas[0].cy = -1.0f; bolhas[0].r = 0.25f;
    bolhas[0].mx = 1.0f;  bolhas[0].my = 0.4f;  bolhas[0].tipo = 'p';
    float norm0 = sqrtf(bolhas[0].mx * bolhas[0].mx + bolhas[0].my * bolhas[0].my);
    bolhas[0].mx /= norm0; bolhas[0].my /= norm0;

    // S1
    bolhas[1].cx = -0.8f; bolhas[1].cy = -1.2f; bolhas[1].r = 0.45f;
    bolhas[1].mx = 0.3f;  bolhas[1].my = 1.0f;  bolhas[1].tipo = 's';
    float norm1 = sqrtf(bolhas[1].mx * bolhas[1].mx + bolhas[1].my * bolhas[1].my);
    bolhas[1].mx /= norm1; bolhas[1].my /= norm1;

    // S2
    bolhas[2].cx = 1.2f;  bolhas[2].cy = 0.5f;  bolhas[2].r = 0.6f;
    bolhas[2].mx = -0.5f; bolhas[2].my = 0.8f;  bolhas[2].tipo = 's';
    float norm2 = sqrtf(bolhas[2].mx * bolhas[2].mx + bolhas[2].my * bolhas[2].my);
    bolhas[2].mx /= norm2; bolhas[2].my /= norm2;

    const float dt = 0.028f;
    const float speed = 0.24f;

    int rodando = 1;
    SDL_Event evento;

    /* Loop principal (limitado a aprox. 30 FPS para bater com o matplotlib do Python) */
    while (rodando) {
        uint64_t start_time = SDL_GetTicks();

        while (SDL_PollEvent(&evento)) {
            if (evento.type == SDL_EVENT_QUIT) {
                rodando = 0;
            }
        }

        // --- 1. UPDATE DA FÍSICA ---
        frame++;

        // Crescimento das bolhas
        for (int i = 0; i < num_bolhas; i++) {
            bolhas[i].r += speed * dt;
        }

        // Colisão p-s
        Bolha *p = &bolhas[0];
        for (int i = 1; i < num_bolhas; i++) {
            Bolha *s = &bolhas[i];
            
            // ponto_p = p.centro + p.raio * p.m
            float ponto_px = p->cx + p->r * p->mx;
            float ponto_py = p->cy + p->r * p->my;
            
            // Distância de ponto_p até o centro de s
            float dx = ponto_px - s->cx;
            float dy = ponto_py - s->cy;
            float dist = sqrtf(dx * dx + dy * dy);
            
            if (fabsf(dist - s->r) < 0.12f && frame % 18 == 0 && frame > 20) {
                float r_s = s->r;
                
                // p['centro'] = contato
                p->cx = ponto_px;
                p->cy = ponto_py;
                p->r = 0.03f;
                
                // s['centro'] = s['centro'] + r_s * p['m']
                s->cx = s->cx + r_s * p->mx;
                s->cy = s->cy + r_s * p->my;
                s->r = 0.03f;
                
                printf("p-s%d frame %d\n", i, frame);
            }
        }

        // Colisão s-s
        for (int i = 1; i < num_bolhas; i++) {
            for (int j = i + 1; j < num_bolhas; j++) {
                Bolha *s1 = &bolhas[i];
                Bolha *s2 = &bolhas[j];
                
                // Regra restritiva: mesmo raio e superpostas, ignorado
                if (fabsf(s1->r - s2->r) < 0.01f) {
                    float dist_centros = sqrtf((s1->cx - s2->cx)*(s1->cx - s2->cx) + (s1->cy - s2->cy)*(s1->cy - s2->cy));
                    if (dist_centros < 0.1f) {
                        continue; 
                    }
                }
                
                float ponto_s1x = s1->cx + s1->r * s1->mx;
                float ponto_s1y = s1->cy + s1->r * s1->my;
                
                float dx = ponto_s1x - s2->cx;
                float dy = ponto_s1y - s2->cy;
                float dist = sqrtf(dx * dx + dy * dy);
                
                if (fabsf(dist - s2->r) < 0.12f && frame % 25 == 0 && frame > 30) {
                    s1->cx = ponto_s1x;
                    s1->cy = ponto_s1y;
                    s2->cx = ponto_s1x;
                    s2->cy = ponto_s1y;
                    s1->r = s2->r = 0.03f;
                    
                    printf("s-s frame %d (permitido)\n", frame);
                }
            }
        }

        // --- 2. RENDERIZAÇÃO ---
        // Fundo escuro (cinza bem escuro para vermos o grid/linhas de forma confortável)
        SDL_SetRenderDrawColor(renderer, 18, 18, 18, 255);
        SDL_RenderClear(renderer);

        // Grid de referência (linhas cinzas verticais e horizontais a cada 1 unidade lógica)
        SDL_SetRenderDrawColor(renderer, 45, 45, 45, 255);
        for (int g = -5; g <= 5; g++) {
            // Horizontais
            SDL_RenderLine(renderer, map_x(-6.0f), map_y((float)g), map_x(6.0f), map_y((float)g));
            // Verticais
            SDL_RenderLine(renderer, map_x((float)g), map_y(-6.0f), map_x((float)g), map_y(6.0f));
        }

        // Desenhar as bolhas e seus vetores 'm'
        for (int i = 0; i < num_bolhas; i++) {
            if (bolhas[i].tipo == 'p') {
                SDL_SetRenderDrawColor(renderer, 255, 50, 50, 255); // Vermelho para 'p'
            } else {
                SDL_SetRenderDrawColor(renderer, 50, 100, 255, 255); // Azul para 's'
            }
            
            // Desenha a casca
            draw_circle(renderer, bolhas[i].cx, bolhas[i].cy, bolhas[i].r);
            
            // Desenha a seta de momento (verde limão)
            SDL_SetRenderDrawColor(renderer, 50, 255, 50, 255);
            draw_arrow(renderer, bolhas[i].cx, bolhas[i].cy, bolhas[i].r, bolhas[i].mx, bolhas[i].my);
        }

        SDL_RenderPresent(renderer);

        // Controle básico de framerate para ~33ms por frame (30 FPS)
        uint64_t elapsed = SDL_GetTicks() - start_time;
        if (elapsed < 30) {
            SDL_Delay(30 - elapsed);
        }
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}