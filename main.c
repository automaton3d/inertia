#define _USE_MATH_DEFINES
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_BOLHAS 31
#define MAX_PONTOS 500

int window_width = 800;
int window_height = 800;

typedef struct { float dx, dy, dz; } PontoSuperficie;

typedef struct {
    float cx, cy, cz;
    float r;
    float mx, my, mz;
    PontoSuperficie pontos[MAX_PONTOS];
    int cooldown;
    int tipo;  // 0 = s, 1 = p
} Bolha;

enum Vista { VISTA_XY, VISTA_XZ, VISTA_YZ, VISTA_ISO };
enum Vista vista_atual = VISTA_XY;

// === VARIÁVEIS GLOBAIS (adicione no topo) ===
// === VARIÁVEIS GLOBAIS (adicione perto do topo) ===
float zoom = 1.0f;
bool zoom_out_ativado = false;

void normalizar3D(float *x, float *y, float *z) {                   // OK
    float norm = sqrtf((*x)*(*x) + (*y)*(*y) + (*z)*(*z));
    if (norm > 0.0001f) { *x /= norm; *y /= norm; *z /= norm; }
    else { *x = 1.0f; *y = 0.0f; *z = 0.0f; }
}

void inicializar_pontos(Bolha *b) {                                 // OK
    for (int i = 0; i < MAX_PONTOS; i++) {
        float u = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
        float theta = ((float)rand() / RAND_MAX) * 2.0f * (float)M_PI;
        float phi = asinf(u);
        b->pontos[i].dx = cosf(phi) * cosf(theta);
        b->pontos[i].dy = cosf(phi) * sinf(theta);
        b->pontos[i].dz = sinf(phi);
    }
}

void inicializar_bolha(Bolha *b, float cx, float cy, float cz, float raio_inicial, float mx, float my, float mz, int tipo) {
    b->cx = cx; b->cy = cy; b->cz = cz;
    b->mx = mx; b->my = my; b->mz = mz;
    normalizar3D(&b->mx, &b->my, &b->mz);
    b->r = raio_inicial;
    b->cooldown = 40;
    b->tipo = tipo;
    inicializar_pontos(b);
}

void reemitir_bolha(Bolha *b, float cx, float cy, float cz) {
    b->cx = cx; b->cy = cy; b->cz = cz;
    b->r = 0.0001;
    b->cooldown = 40;
    inicializar_pontos(b);
}

// === FUNÇÃO PROJECT ATUALIZADA ===
void project(float x, float y, float z, float *px, float *py) {
    float scale = (window_width < window_height ? window_width : window_height) / 14.0f;
    scale *= zoom;   // aplica zoom

    if (!isfinite(x) || !isfinite(y) || !isfinite(z)) { 
        x = y = z = 0; 
    }
    
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

void desenhar_eixos(SDL_Renderer *ren) {
    float origem_x = 0, origem_y = 0, origem_z = 0;
    float px_origem, py_origem;
    project(origem_x, origem_y, origem_z, &px_origem, &py_origem);
    
    float comprimento = 5.0f;
    
    // EIXO X (VERMELHO)
    float px_x, py_x;
    project(comprimento, 0, 0, &px_x, &py_x);
    SDL_SetRenderDrawColor(ren, 255, 0, 0, 255);
    SDL_RenderLine(ren, px_origem, py_origem, px_x, py_x);
    
    float px_x_seta1, py_x_seta1, px_x_seta2, py_x_seta2;
    project(comprimento - 0.3f, 0.15f, 0, &px_x_seta1, &py_x_seta1);
    project(comprimento - 0.3f, -0.15f, 0, &px_x_seta2, &py_x_seta2);
    SDL_RenderLine(ren, px_x, py_x, px_x_seta1, py_x_seta1);
    SDL_RenderLine(ren, px_x, py_x, px_x_seta2, py_x_seta2);
    
    project(comprimento + 0.5f, 0, 0, &px_x, &py_x);
    SDL_SetRenderDrawColor(ren, 255, 0, 0, 255);
    SDL_FRect rect_x = { px_x - 3, py_x - 3, 6, 6 };
    SDL_RenderFillRect(ren, &rect_x);
    
    // EIXO Y (VERDE)
    float px_y, py_y;
    project(0, comprimento, 0, &px_y, &py_y);
    SDL_SetRenderDrawColor(ren, 0, 255, 0, 255);
    SDL_RenderLine(ren, px_origem, py_origem, px_y, py_y);
    
    float px_y_seta1, py_y_seta1, px_y_seta2, py_y_seta2;
    project(0.15f, comprimento - 0.3f, 0, &px_y_seta1, &py_y_seta1);
    project(-0.15f, comprimento - 0.3f, 0, &px_y_seta2, &py_y_seta2);
    SDL_RenderLine(ren, px_y, py_y, px_y_seta1, py_y_seta1);
    SDL_RenderLine(ren, px_y, py_y, px_y_seta2, py_y_seta2);
    
    project(0, comprimento + 0.5f, 0, &px_y, &py_y);
    SDL_SetRenderDrawColor(ren, 0, 255, 0, 255);
    SDL_FRect rect_y = { px_y - 3, py_y - 3, 6, 6 };
    SDL_RenderFillRect(ren, &rect_y);
    
    // EIXO Z (AZUL)
    float px_z, py_z;
    project(0, 0, comprimento, &px_z, &py_z);
    SDL_SetRenderDrawColor(ren, 0, 0, 255, 255);
    SDL_RenderLine(ren, px_origem, py_origem, px_z, py_z);
    
    float px_z_seta1, py_z_seta1, px_z_seta2, py_z_seta2;
    project(0.15f, 0, comprimento - 0.3f, &px_z_seta1, &py_z_seta1);
    project(-0.15f, 0, comprimento - 0.3f, &px_z_seta2, &py_z_seta2);
    SDL_RenderLine(ren, px_z, py_z, px_z_seta1, py_z_seta1);
    SDL_RenderLine(ren, px_z, py_z, px_z_seta2, py_z_seta2);
    
    project(0, 0, comprimento + 0.5f, &px_z, &py_z);
    SDL_SetRenderDrawColor(ren, 0, 0, 255, 255);
    SDL_FRect rect_z = { px_z - 3, py_z - 3, 6, 6 };
    SDL_RenderFillRect(ren, &rect_z);
    
    // ORIGEM (BRANCA)
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_FRect rect_origem = { px_origem - 3, py_origem - 3, 6, 6 };
    SDL_RenderFillRect(ren, &rect_origem);
}

// Função para carregar bolhas de arquivo
int carregar_bolhas(Bolha *bolhas, const char *arquivo) {
    FILE *f = fopen(arquivo, "r");
    if (!f) {
        printf("ERRO: Não foi possível abrir '%s'\n", arquivo);
        return 0;
    }

    int count = 0;
    char linha[256];
    while (fgets(linha, sizeof(linha), f) && count < MAX_BOLHAS) {
        if (linha[0] == '#' || linha[0] == '\n' || linha[0] == ' ') continue;

        float cx, cy, cz, raio, mx, my, mz;
        int tipo = 0;

        if (sscanf(linha, "%f %f %f %f %f %f %f %d", 
                   &cx, &cy, &cz, &raio, &mx, &my, &mz, &tipo) >= 7) {
            
            inicializar_bolha(&bolhas[count], cx, cy, cz, raio, mx, my, mz, tipo);
            printf("Bolha %2d carregada: tipo=%d | pos=(%.1f, %.1f, %.1f) r=%.2f\n", 
                   count, tipo, cx, cy, cz, raio);
            count++;
        }
    }
    fclose(f);
    printf("\n=== TOTAL CARREGADO: %d bolhas ===\n", count);
    return count;
}


int SDL_main(int argc, char *argv[]) {
    // === FORÇA CONSOLE PARA VER PRINTF ===
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("Erro ao inicializar SDL\n");
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow("Toy Universe 3D - Bolhas", 
                                       window_width, window_height, 0);
    SDL_Renderer *ren = SDL_CreateRenderer(win, NULL);
    
    Bolha b[MAX_BOLHAS] = {0};

    int num_bolhas = carregar_bolhas(b, "bolhas.dat");
    
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
                    printf("Zoom: %.2fx\n", zoom);
                }
                if (e.key.scancode == SDL_SCANCODE_ESCAPE) rodando = 0;
            }
        }

        // Expansão
        for (int i = 0; i < num_bolhas; i++) {
            b[i].r += 0.5f * dt;
            if (b[i].cooldown > 0) b[i].cooldown--;
        }

        // Colisões (mantido)
        for (int i = 0; i < num_bolhas; i++) {
            for (int j = i + 1; j < num_bolhas; j++) {
                if (b[i].cooldown > 0 || b[j].cooldown > 0) continue;
                
                float pix = b[i].cx + b[i].r * b[i].mx;
                float piy = b[i].cy + b[i].r * b[i].my;
                float piz = b[i].cz + b[i].r * b[i].mz;

                float dx = pix - b[j].cx;
                float dy = piy - b[j].cy;
                float dz = piz - b[j].cz;
                float dist_sq = dx*dx + dy*dy + dz*dz;

                if (dist_sq < b[j].r * b[j].r + 0.0001f) {
                    float dist = sqrtf(dist_sq);
                    if (dist < 0.0001f) dist = 0.0001f;

                    float cx = b[j].cx + (dx / dist) * b[j].r;
                    float cy = b[j].cy + (dy / dist) * b[j].r;
                    float cz = b[j].cz + (dz / dist) * b[j].r;

                    reemitir_bolha(&b[i], cx, cy, cz);
                    reemitir_bolha(&b[j], b[j].cx + b[j].r * b[i].mx,
                                   b[j].cy + b[j].r * b[i].my,
                                   b[j].cz + b[j].r * b[i].mz);
                }
            }
        }

        SDL_SetRenderDrawColor(ren, 15, 15, 15, 255);
        SDL_RenderClear(ren);
        desenhar_eixos(ren);

        // ==================== DESENHO DAS BOLHAS ====================
        for (int i = 0; i < num_bolhas; i++) {
            if (b[i].r < 0.05f) continue;

            if (b[i].tipo == 1) {
                // === BOLHAS P - MUITO VISÍVEIS ===
                SDL_SetRenderDrawColor(ren, 255, 240, 0, 255);  // Amarelo forte
                float size = 2.2f;

                // Desenha pontos maiores para p
                for (int p = 0; p < MAX_PONTOS; p += 2) {  // mais denso
                    float px, py;
                    project(b[i].cx + b[i].r * b[i].pontos[p].dx, 
                           b[i].cy + b[i].r * b[i].pontos[p].dy, 
                           b[i].cz + b[i].r * b[i].pontos[p].dz, &px, &py);
                    SDL_FRect rect = { px-1, py-1, size, size };
                    SDL_RenderFillRect(ren, &rect);
                }

                // Vetor m gigante para p
                SDL_SetRenderDrawColor(ren, 255, 255, 100, 255);
                float px1, py1, px2, py2;
                project(b[i].cx, b[i].cy, b[i].cz, &px1, &py1);
                project(b[i].cx + b[i].mx * b[i].r * 2.5f, 
                       b[i].cy + b[i].my * b[i].r * 2.5f, 
                       b[i].cz + b[i].mz * b[i].r * 2.5f, &px2, &py2);
                SDL_RenderLine(ren, px1, py1, px2, py2);

            } else {
                // Bolhas s normais
                SDL_SetRenderDrawColor(ren, 100, 160, 255, 255);
                for (int p = 0; p < MAX_PONTOS; p++) {
                    float px, py;
                    project(b[i].cx + b[i].r * b[i].pontos[p].dx, 
                           b[i].cy + b[i].r * b[i].pontos[p].dy, 
                           b[i].cz + b[i].r * b[i].pontos[p].dz, &px, &py);
                    SDL_FRect rect = { px, py, 1.0f, 1.0f };
                    SDL_RenderFillRect(ren, &rect);
                }
            }
        }        
        
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}