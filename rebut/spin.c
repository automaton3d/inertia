#define _CRT_SECURE_NO_WARNINGS
#define _WIN32_WINNT 0x0600
#include <windows.h>
#define _USE_MATH_DEFINES
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_BOLHAS 141
#define MAX_PONTOS 500

#define L 12800

int window_width = 800;
int window_height = 800;

typedef struct { float dx, dy, dz; } PontoSuperficie;

typedef struct {
    float cx, cy, cz;
    float r;
    float mx, my, mz; // Vetor original
    float sx, sy, sz; // Vetor perpendicular S
    PontoSuperficie pontos[MAX_PONTOS];
    int cooldown;
    int tipo; 
    int modo_m;
    float dir_x, dir_y, dir_z; // vetor de influência do cacique
    int timeout;  
} Bolha;

enum Vista { VISTA_XY, VISTA_XZ, VISTA_YZ, VISTA_ISO };
enum Vista vista_atual = VISTA_XY;
float zoom = 1.0f;
bool zoom_out_ativado = false;
bool mostrar_bolhas = true; // Novo estado para alternar a visualização

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
    // 1. Configura posição e raio
    b->cx = cx; b->cy = cy; b->cz = cz;
    b->r = raio;
    b->tipo = tipo;
    b->cooldown = 40;

    // 2. Normaliza o vetor M principal
    b->mx = mx; b->my = my; b->mz = mz;
    normalizar3D(&b->mx, &b->my, &b->mz);

    // 3. Calcula S como o produto vetorial de M e Auxiliar (S = M x Aux)
    b->sx = b->my * az - b->mz * ay;
    b->sy = b->mz * ax - b->mx * az;
    b->sz = b->mx * ay - b->my * ax;

    // 4. Fallback: Se M e Aux forem paralelos (produto vetorial = 0), 
    // define S usando um vetor padrão para garantir ortogonalidade.
    if (sqrtf(b->sx * b->sx + b->sy * b->sy + b->sz * b->sz) < 0.0001f) {
        // Se M é muito próximo de (1,0,0), usa (0,1,0) como auxiliar
        if (fabs(b->mx) > 0.9f) {
            b->sx = 0.0f; b->sy = b->mz; b->sz = -b->my;
        } else {
            b->sx = 0.0f; b->sy = b->mz; b->sz = -b->my;
        }
    }

    // 5. Normaliza o vetor S resultante para manter a unidade
    normalizar3D(&b->sx, &b->sy, &b->sz);

    // 6. Inicializa pontos da superfície
    inicializar_pontos(b);
}

void reemitir_bolha(Bolha *b, float cx, float cy, float cz) {
    b->cx = cx; b->cy = cy; b->cz = cz;
    b->r = 0.0001f;
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

void desenhar_seta(SDL_Renderer *ren, float cx, float cy, float cz, 
                   float vx, float vy, float vz, float r, float color_r, float color_g, float color_b) {
    float px1, py1, px2, py2;
    project(cx, cy, cz, &px1, &py1);
    
    project(cx + vx * r, cy + vy * r, cz + vz * r, &px2, &py2);

    SDL_SetRenderDrawColor(ren, (Uint8)color_r, (Uint8)color_g, (Uint8)color_b, 255);
    SDL_RenderLine(ren, px1, py1, px2, py2);

    // O tamanho da ponta agora é escalonado pelo zoom para não ficar gigante no zoom out
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

void desenhar_eixos(SDL_Renderer *ren) {
    float origem_x = 0, origem_y = 0, origem_z = 0;
    float px_origem, py_origem;
    project(origem_x, origem_y, origem_z, &px_origem, &py_origem);
    
    float comprimento = 5.8f / zoom;   // tamanho visual constante
    
    // EIXO X
    float px, py;
    project(comprimento, 0, 0, &px, &py);
    SDL_SetRenderDrawColor(ren, 255, 0, 0, 255);
    SDL_RenderLine(ren, px_origem, py_origem, px, py);
    project(comprimento + 0.4f, 0, 0, &px, &py);
    SDL_FRect r = {px-3, py-3, 6, 6};
    SDL_RenderFillRect(ren, &r);

    // EIXO Y
    project(0, comprimento, 0, &px, &py);
    SDL_SetRenderDrawColor(ren, 0, 255, 0, 255);
    SDL_RenderLine(ren, px_origem, py_origem, px, py);
    project(0, comprimento + 0.4f, 0, &px, &py);
    r = (SDL_FRect){px-3, py-3, 6, 6};
    SDL_RenderFillRect(ren, &r);

    // EIXO Z
    project(0, 0, comprimento, &px, &py);
    SDL_SetRenderDrawColor(ren, 0, 0, 255, 255);
    SDL_RenderLine(ren, px_origem, py_origem, px, py);
    project(0, 0, comprimento + 0.4f, &px, &py);
    r = (SDL_FRect){px-3, py-3, 6, 6};
    SDL_RenderFillRect(ren, &r);

    // Origem
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    project(0,0,0, &px, &py);
    r = (SDL_FRect){px-3, py-3, 6, 6};
    SDL_RenderFillRect(ren, &r);
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
        // Ignora comentários e linhas vazias
        if (linha[0] == '#' || linha[0] == '\n' || linha[0] == ' ') continue;

        float cx, cy, cz, raio, mx, my, mz, ax, ay, az;
        int tipo;

        // O sscanf agora lê os 11 parâmetros esperados corretamente
        // Certifique-se de que o número de variáveis corresponde ao %f e %d
        if (sscanf(linha, "%f %f %f %f %f %f %f %f %f %f %d", 
                   &cx, &cy, &cz, &raio, &mx, &my, &mz, &ax, &ay, &az, &tipo) == 11) {
            
            inicializar_bolha(&bolhas[count], cx, cy, cz, raio, mx, my, mz, ax, ay, az, tipo);
            count++;
        }
    }
    fclose(f);
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
                }
                if (e.key.scancode == SDL_SCANCODE_ESCAPE) rodando = 0;


                if (e.key.scancode == SDL_SCANCODE_V) {
                   mostrar_bolhas = !mostrar_bolhas;
                }
            }
            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                if (e.wheel.y > 0) {
                    zoom *= 1.1f;   // aproxima
                } else if (e.wheel.y < 0) {
                    zoom *= 0.9f;   // afasta
                }
                if (zoom < 0.05f) zoom = 0.05f; 
                if (zoom > 5.0f) zoom = 5.0f;
            }
        }

        // Expansão
        for (int i = 0; i < num_bolhas; i++) {
            b[i].r += 0.5f * dt;
            if (b[i].cooldown > 0) b[i].cooldown--;
        }

        // ==================== LOOP DE COLISÕES ====================
        for (int i = 0; i < num_bolhas; i++) {
            for (int j = i + 1; j < num_bolhas; j++) {

                if (b[i].cooldown > 0 || b[j].cooldown > 0) continue;
                if (b[i].tipo == 1 && b[j].tipo == 1) continue;

                int hit = 0;

                // ====================== TESTE VIA M ======================
                if ((b[i].modo_m || i == 0) && (b[j].tipo != 1 || i == 0)) {
                    float sM_ix = b[i].cx + b[i].r * b[i].mx;
                    float sM_iy = b[i].cy + b[i].r * b[i].my;
                    float sM_iz = b[i].cz + b[i].r * b[i].mz;

                    float dM_sq = (sM_ix - b[j].cx)*(sM_ix - b[j].cx) +
                                  (sM_iy - b[j].cy)*(sM_iy - b[j].cy) +
                                  (sM_iz - b[j].cz)*(sM_iz - b[j].cz);

                    if (dM_sq < (b[j].r * b[j].r) + 0.1f) {
                        // === ANCORAGEM DO CACIQUE ===
                        if (i == 0) {
                            float px = b[0].cx + b[0].r * b[0].sx * 0.45f;
                            float py = b[0].cy + b[0].r * b[0].sy * 0.45f;
                            float pz = b[0].cz + b[0].r * b[0].sz * 0.45f;
                            // Ancora de volta ao centro
                            px = px * 0.75f + b[0].cx * 0.25f;
                            py = py * 0.75f + b[0].cy * 0.25f;
                            pz = pz * 0.75f + b[0].cz * 0.25f;
                            reemitir_bolha(&b[0], px, py, pz);
                        } else {
                            reemitir_bolha(&b[i], sM_ix, sM_iy, sM_iz);
                        }
                        hit = 1;

                        // Propagação de delegados
                        if (b[i].tipo == 0 && b[i].timeout > 0) {
                            reemitir_bolha(&b[j],
                                b[j].cx + b[j].r * b[i].dir_x,
                                b[j].cy + b[j].r * b[i].dir_y,
                                b[j].cz + b[j].r * b[i].dir_z);
                            b[i].timeout--;
                            if (b[i].timeout == 0) {
                                b[i].dir_x = b[i].dir_y = b[i].dir_z = 0.0f;
                            } else {
                                b[j].dir_x = b[i].dir_x;
                                b[j].dir_y = b[i].dir_y;
                                b[j].dir_z = b[i].dir_z;
                                b[j].timeout = L;
                            }
                        } else if (b[j].tipo == 0 && b[j].timeout > 0) {
                            reemitir_bolha(&b[i],
                                b[i].cx + b[i].r * b[j].dir_x,
                                b[i].cy + b[i].r * b[j].dir_y,
                                b[i].cz + b[i].r * b[j].dir_z);
                            b[j].timeout--;
                            if (b[j].timeout == 0) {
                                b[j].dir_x = b[j].dir_y = b[j].dir_z = 0.0f;
                            } else {
                                b[i].dir_x = b[j].dir_x;
                                b[i].dir_y = b[j].dir_y;
                                b[i].dir_z = b[j].dir_z;
                                b[i].timeout = L;
                            }
                        }
                    }
                }

                // Teste simétrico via M
                if (!hit && (b[j].modo_m || j == 0) && (b[i].tipo != 1 || j == 0)) {
                    float sM_jx = b[j].cx + b[j].r * b[j].mx;
                    float sM_jy = b[j].cy + b[j].r * b[j].my;
                    float sM_jz = b[j].cz + b[j].r * b[j].mz;

                    float dM_sq = (sM_jx - b[i].cx)*(sM_jx - b[i].cx) +
                                  (sM_jy - b[i].cy)*(sM_jy - b[i].cy) +
                                  (sM_jz - b[i].cz)*(sM_jz - b[i].cz);

                    if (dM_sq < (b[i].r * b[i].r) + 0.1f) {
                        if (j == 0) {
                            float px = b[0].cx + b[0].r * b[0].sx * 0.45f;
                            float py = b[0].cy + b[0].r * b[0].sy * 0.45f;
                            float pz = b[0].cz + b[0].r * b[0].sz * 0.45f;
                            px = px * 0.75f + b[0].cx * 0.25f;
                            py = py * 0.75f + b[0].cy * 0.25f;
                            pz = pz * 0.75f + b[0].cz * 0.25f;
                            reemitir_bolha(&b[0], px, py, pz);
                        } else {
                            reemitir_bolha(&b[j], sM_jx, sM_jy, sM_jz);
                        }
                        hit = 1;
                    }
                }

                // ====================== TESTE VIA S ======================
                if (!hit && ((!b[i].modo_m || i == 0) && (b[j].tipo != 1 || i == 0))) {
                    float sS_ix = b[i].cx + b[i].r * b[i].sx;
                    float sS_iy = b[i].cy + b[i].r * b[i].sy;
                    float sS_iz = b[i].cz + b[i].r * b[i].sz;

                    float dS_sq = (sS_ix - b[j].cx)*(sS_ix - b[j].cx) +
                                  (sS_iy - b[j].cy)*(sS_iy - b[j].cy) +
                                  (sS_iz - b[j].cz)*(sS_iz - b[j].cz);

                    if (dS_sq < (b[j].r * b[j].r) + 0.1f) {
                        if (i == 0) {
                            float px = b[0].cx + b[0].r * b[0].sx * 0.45f;
                            float py = b[0].cy + b[0].r * b[0].sy * 0.45f;
                            float pz = b[0].cz + b[0].r * b[0].sz * 0.45f;
                            px = px * 0.75f + b[0].cx * 0.25f;
                            py = py * 0.75f + b[0].cy * 0.25f;
                            pz = pz * 0.75f + b[0].cz * 0.25f;
                            reemitir_bolha(&b[0], px, py, pz);
                        } else {
                            reemitir_bolha(&b[i], sS_ix, sS_iy, sS_iz);
                        }
                        hit = 1;

                        if ((i == 0 && b[j].tipo == 0) || (b[i].tipo == 0 && b[i].timeout > 0)) {
                            b[j].dir_x = b[0].cx - b[j].cx;
                            b[j].dir_y = b[0].cy - b[j].cy;
                            b[j].dir_z = b[0].cz - b[j].cz;
                            normalizar3D(&b[j].dir_x, &b[j].dir_y, &b[j].dir_z);
                            b[j].timeout = L;

                            float pull = 0.09f;
                            b[j].cx += (b[0].cx - b[j].cx) * pull;
                            b[j].cy += (b[0].cy - b[j].cy) * pull;
                            b[j].cz += (b[0].cz - b[j].cz) * pull;

                        } else if ((j == 0 && b[i].tipo == 0) || (b[j].tipo == 0 && b[j].timeout > 0)) {
                            b[i].dir_x = b[0].cx - b[i].cx;
                            b[i].dir_y = b[0].cy - b[i].cy;
                            b[i].dir_z = b[0].cz - b[i].cz;
                            normalizar3D(&b[i].dir_x, &b[i].dir_y, &b[i].dir_z);
                            b[i].timeout = L;

                            float pull = 0.09f;
                            b[i].cx += (b[0].cx - b[i].cx) * pull;
                            b[i].cy += (b[0].cy - b[i].cy) * pull;
                            b[i].cz += (b[0].cz - b[i].cz) * pull;

                        } else {
                            float tx = b[0].cx - b[j].cx;
                            float ty = b[0].cy - b[j].cy;
                            float tz = b[0].cz - b[j].cz;
                            normalizar3D(&tx, &ty, &tz);

                            reemitir_bolha(&b[j],
                                b[j].cx + b[j].r * tx * 0.45f,
                                b[j].cy + b[j].r * ty * 0.45f,
                                b[j].cz + b[j].r * tz * 0.45f);
                        }
                    }
                }

                if (hit) {
                    b[i].modo_m = !b[i].modo_m;
                    b[j].modo_m = !b[j].modo_m;
                    b[i].cooldown = (i == 0 ? 1 : 22);
                    b[j].cooldown = (j == 0 ? 1 : 22);
                }
            }
        }
    SDL_SetRenderDrawColor(ren, 15, 15, 15, 255);
            SDL_RenderClear(ren);
            desenhar_eixos(ren);

    // ==================== DESENHO DAS BOLHAS ====================
    float raio_maximo = 2.0f * (window_height / zoom);
    for (int i = 0; i < num_bolhas; i++) {
        if (b[i].r < 0.05f) continue;
        if (b[i].r > raio_maximo) continue;
        if (mostrar_bolhas) {

        if (i == 0) {
            // === CACIQUE ===
            SDL_SetRenderDrawColor(ren, 255, 50, 50, 255); // vermelho
            for (int p = 0; p < MAX_PONTOS; p++) {
                float px, py;
                project(b[i].cx + b[i].r * b[i].pontos[p].dx,
                        b[i].cy + b[i].r * b[i].pontos[p].dy,
                        b[i].cz + b[i].r * b[i].pontos[p].dz, &px, &py);
                SDL_FRect rect = { px, py, 2.0f, 2.0f };
                SDL_RenderFillRect(ren, &rect);
            }
        } else if (b[i].tipo == 1) {
            // === BOLHAS P ===
            SDL_SetRenderDrawColor(ren, 255, 240, 0, 255); // amarelo-ouro
            float size = 2.2f;
            for (int p = 0; p < MAX_PONTOS; p += 2) {
                float px, py;
                project(b[i].cx + b[i].r * b[i].pontos[p].dx,
                        b[i].cy + b[i].r * b[i].pontos[p].dy,
                        b[i].cz + b[i].r * b[i].pontos[p].dz, &px, &py);
                SDL_FRect rect = { px-1, py-1, size, size };
                SDL_RenderFillRect(ren, &rect);
            }
        } else {
            // === BOLHAS S ===
            if (b[i].timeout > 0) {
            // Delegado
            SDL_SetRenderDrawColor(ren, 255, 255, 100, 255); // amarelo claro
        } else {
            SDL_SetRenderDrawColor(ren, 100, 160, 255, 255); // azul claro
        }   
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
// Define o tamanho da seta baseado no modo de exibição
// Quando mostrar_bolhas é falso, usa (1.0f / zoom) * 0.2f para 20% do tamanho base
float tamanho_seta = mostrar_bolhas ? b[i].r : ((1.0f / zoom) * 0.2f); 

// Seta M (Amarelo escuro)
desenhar_seta(ren, b[i].cx, b[i].cy, b[i].cz, b[i].mx, b[i].my, b[i].mz, tamanho_seta, 120, 120, 40); 

// Seta S (Ciano)
desenhar_seta(ren, b[i].cx, b[i].cy, b[i].cz, b[i].sx, b[i].sy, b[i].sz, tamanho_seta, 0, 255, 255);
}

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
