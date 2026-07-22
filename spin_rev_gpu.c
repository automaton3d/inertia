#define _CRT_SECURE_NO_WARNINGS
#define _WIN32_WINNT 0x0600
#include <windows.h>
#define _USE_MATH_DEFINES
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_BOLHAS 1024
#define MAX_PONTOS 500

#define L 3
#define R_ESFERA 5.0f
#define R_FUGA 15.0f

// === HISTORICO DO CENTRO DE MASSA (apenas visualizacao) ===
#define MAX_HIST 200
float *hist_cmx = NULL;
float *hist_cmy = NULL;
float *hist_cmz = NULL;
int hist_count = 0;
int hist_capacity = 0;

int window_width = 800;
int window_height = 800;

typedef struct { float dx, dy, dz; } PontoSuperficie;

typedef struct {
    float cx, cy, cz;
    float r;
    float mx, my, mz;
    float sx, sy, sz;
    PontoSuperficie pontos[MAX_PONTOS];
    int cooldown;
    int tipo;
    int modo_m;
    float dir_x, dir_y, dir_z;
    int timeout;
} Bolha;

enum Vista { VISTA_XY, VISTA_XZ, VISTA_YZ, VISTA_ISO };
enum Vista vista_atual = VISTA_XY;
float zoom = 1.0f;
bool zoom_out_ativado = false;
bool mostrar_bolhas = true;
bool pelos_para_dentro = false;

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

void inicializar_bolha(Bolha *b, float cx, float cy, float cz, float raio,
                       float mx, float my, float mz,
                       float ax, float ay, float az, int tipo) {
    b->cx = cx; b->cy = cy; b->cz = cz;
    b->r = raio;
    b->tipo = tipo;
    b->cooldown = 40;

    b->mx = mx; b->my = my; b->mz = mz;
    normalizar3D(&b->mx, &b->my, &b->mz);

    b->sx = b->my * az - b->mz * ay;
    b->sy = b->mz * ax - b->mx * az;
    b->sz = b->mx * ay - b->my * ax;

    if (sqrtf(b->sx * b->sx + b->sy * b->sy + b->sz * b->sz) < 0.0001f) {
        b->sx = 0.0f; b->sy = b->mz; b->sz = -b->my;
    }

    normalizar3D(&b->sx, &b->sy, &b->sz);
    inicializar_pontos(b);
}

void reemitir_bolha(Bolha *b, float cx, float cy, float cz) {
    b->cx = cx; b->cy = cy; b->cz = cz;
    b->r = 0.0001f;
    b->cooldown = 40;
    inicializar_pontos(b);
}

void reemitir_esfera(Bolha *b) {
    float nx = b->sx, ny = b->sy, nz = b->sz;
    normalizar3D(&nx, &ny, &nz);
    float sinal = pelos_para_dentro ? -1.0f : 1.0f;
    reemitir_bolha(b,
        b->cx + nx * R_ESFERA * sinal,
        b->cy + ny * R_ESFERA * sinal,
        b->cz + nz * R_ESFERA * sinal);
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
        if (b[i].r < 0.05f || b[i].r > raio_maximo) continue;

        SDL_FColor c;
        float size;
        int step = 1;
        float ox = 0.0f, oy = 0.0f;

        if (i % 512 == 0) {
            c = (SDL_FColor){ 1.0f, 50.0f/255.0f, 50.0f/255.0f, 1.0f };
            size = 2.0f;
        } else if (b[i].tipo == 1) {
            c = (SDL_FColor){ 1.0f, 140.0f/255.0f, 0.0f, 1.0f };
            size = 2.2f;
            step = 2;
            ox = -1.0f;
            oy = -1.0f;
        } else {
            if (b[i].timeout > 0) {
                c = (SDL_FColor){ 1.0f, 1.0f, 100.0f/255.0f, 1.0f };
            } else {
                c = (SDL_FColor){ 100.0f/255.0f, 160.0f/255.0f, 1.0f, 1.0f };
            }
            size = 1.0f;
        }

        for (int p = 0; p < MAX_PONTOS; p += step) {
            float px, py;
            project(b[i].cx + b[i].r * b[i].pontos[p].dx,
                    b[i].cy + b[i].r * b[i].pontos[p].dy,
                    b[i].cz + b[i].r * b[i].pontos[p].dz, &px, &py);

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

    int num_bolhas = carregar_bolhas(b, "bolhas.dat");

    // Posiciona os dois caciques em lados opostos apos o carregamento
    if (num_bolhas > 0) {
        b[0].cx = -5.0f; b[0].cy = 5.0f; b[0].cz = 0.0f;
    }
    if (num_bolhas >= 513) {
        b[512].cx = 5.0f; b[512].cy = -5.0f; b[512].cz = 0.0f;
    }

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
                if (e.key.scancode == SDL_SCANCODE_D) {
                    pelos_para_dentro = !pelos_para_dentro;
                    printf("Modo: %s\n", pelos_para_dentro ? "DENTRO" : "FORA");
                    fflush(stdout);
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

        for (int i = 0; i < num_bolhas; i++) {
            b[i].r += 0.5f * dt;
            if (b[i].cooldown > 0) b[i].cooldown--;
        }

        for (int i = 0; i < num_bolhas; i++) {
            for (int j = i + 1; j < num_bolhas; j++) {

                if (b[i].cooldown > 0 || b[j].cooldown > 0) continue;
                // P × S: interage normalmente (sem alternar modo_m)
                // P × P: fantasma (sem interacao)
                if (b[i].tipo == 1 && b[j].tipo == 1) continue;

                int hit = 0;

                if (b[i].modo_m || i % 512 == 0) {
                    float sM_ix = b[i].cx + b[i].r * b[i].mx;
                    float sM_iy = b[i].cy + b[i].r * b[i].my;
                    float sM_iz = b[i].cz + b[i].r * b[i].mz;

                    float dM_sq = (sM_ix - b[j].cx)*(sM_ix - b[j].cx) +
                                  (sM_iy - b[j].cy)*(sM_iy - b[j].cy) +
                                  (sM_iz - b[j].cz)*(sM_iz - b[j].cz);

                    if (dM_sq < (b[j].r * b[j].r) + 0.1f) {
                        if (i % 512 == 0) {
                            float px = b[i].cx + b[i].r * b[i].sx * 0.45f;
                            float py = b[i].cy + b[i].r * b[i].sy * 0.45f;
                            float pz = b[i].cz + b[i].r * b[i].sz * 0.45f;
                            px = px * 0.75f + b[i].cx * 0.25f;
                            py = py * 0.75f + b[i].cy * 0.25f;
                            pz = pz * 0.75f + b[i].cz * 0.25f;
                            reemitir_bolha(&b[i], px, py, pz);
                        } else {
                            reemitir_esfera(&b[i]);
                        }
                        hit = 1;

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

                if (!hit && (b[j].modo_m || j % 512 == 0)) {
                    float sM_jx = b[j].cx + b[j].r * b[j].mx;
                    float sM_jy = b[j].cy + b[j].r * b[j].my;
                    float sM_jz = b[j].cz + b[j].r * b[j].mz;

                    float dM_sq = (sM_jx - b[i].cx)*(sM_jx - b[i].cx) +
                                  (sM_jy - b[i].cy)*(sM_jy - b[i].cy) +
                                  (sM_jz - b[i].cz)*(sM_jz - b[i].cz);

                    if (dM_sq < (b[i].r * b[i].r) + 0.1f) {
                        if (j % 512 == 0) {
                            float px = b[j].cx + b[j].r * b[j].sx * 0.45f;
                            float py = b[j].cy + b[j].r * b[j].sy * 0.45f;
                            float pz = b[j].cz + b[j].r * b[j].sz * 0.45f;
                            px = px * 0.75f + b[j].cx * 0.25f;
                            py = py * 0.75f + b[j].cy * 0.25f;
                            pz = pz * 0.75f + b[j].cz * 0.25f;
                            reemitir_bolha(&b[j], px, py, pz);
                        } else {
                            reemitir_esfera(&b[j]);
                        }
                        hit = 1;
                    }
                }

                if (!hit && (!b[i].modo_m || i % 512 == 0)) {
                    float sS_ix = b[i].cx + b[i].r * b[i].sx;
                    float sS_iy = b[i].cy + b[i].r * b[i].sy;
                    float sS_iz = b[i].cz + b[i].r * b[i].sz;

                    float dS_sq = (sS_ix - b[j].cx)*(sS_ix - b[j].cx) +
                                  (sS_iy - b[j].cy)*(sS_iy - b[j].cy) +
                                  (sS_iz - b[j].cz)*(sS_iz - b[j].cz);

                    if (dS_sq < (b[j].r * b[j].r) + 0.1f) {
                        if (i % 512 == 0) {
                            float px = b[i].cx + b[i].r * b[i].sx * 0.45f;
                            float py = b[i].cy + b[i].r * b[i].sy * 0.45f;
                            float pz = b[i].cz + b[i].r * b[i].sz * 0.45f;
                            px = px * 0.75f + b[i].cx * 0.25f;
                            py = py * 0.75f + b[i].cy * 0.25f;
                            pz = pz * 0.75f + b[i].cz * 0.25f;
                            reemitir_bolha(&b[i], px, py, pz);
                        } else {
                            reemitir_esfera(&b[i]);
                        }
                        hit = 1;

                        if ((i % 512 == 0 && b[j].tipo == 0) || (b[i].tipo == 0 && b[i].timeout > 0)) {
                            int alvo = (i % 512 == 0) ? i : 0;
                            b[j].dir_x = b[alvo].cx - b[j].cx;
                            b[j].dir_y = b[alvo].cy - b[j].cy;
                            b[j].dir_z = b[alvo].cz - b[j].cz;
                            normalizar3D(&b[j].dir_x, &b[j].dir_y, &b[j].dir_z);
                            b[j].timeout = L;

                        } else if ((j % 512 == 0 && b[i].tipo == 0) || (b[j].tipo == 0 && b[j].timeout > 0)) {
                            int alvo = (j % 512 == 0) ? j : 0;
                            b[i].dir_x = b[alvo].cx - b[i].cx;
                            b[i].dir_y = b[alvo].cy - b[i].cy;
                            b[i].dir_z = b[alvo].cz - b[i].cz;
                            normalizar3D(&b[i].dir_x, &b[i].dir_y, &b[i].dir_z);
                            b[i].timeout = L;

                        } else {
                            int alvo = (i % 512 == 0) ? i : ((j % 512 == 0) ? j : 0);
                            float tx = b[alvo].cx - b[j].cx;
                            float ty = b[alvo].cy - b[j].cy;
                            float tz = b[alvo].cz - b[j].cz;
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
                    b[i].cooldown = (i % 512 == 0 ? 1 : 22);
                    b[j].cooldown = (j % 512 == 0 ? 1 : 22);
                }
            }
        }

        SDL_SetRenderDrawColor(ren, 15, 15, 15, 255);
        SDL_RenderClear(ren);
        desenhar_contador_fugidas(ren, b, num_bolhas);
        desenhar_eixos(ren);

        float raio_maximo = 2.0f * (window_height / zoom);

        // Desenha todos os pontos das bolhas em uma unica chamada
        desenhar_pontos_bolhas(ren, b, num_bolhas, raio_maximo);

        for (int i = 0; i < num_bolhas; i++) {
            if (b[i].r < 0.05f) continue;
            if (b[i].r > raio_maximo) continue;

            float tamanho_seta = mostrar_bolhas ? b[i].r : ((1.0f / zoom) * 0.5f);

            desenhar_seta(ren, b[i].cx, b[i].cy, b[i].cz, b[i].mx, b[i].my, b[i].mz, tamanho_seta, 120, 120, 40);
            // Seta S: laranja para P, ciano para S
            if (b[i].tipo == 1) {
                desenhar_seta(ren, b[i].cx, b[i].cy, b[i].cz, b[i].sx, b[i].sy, b[i].sz, tamanho_seta, 255, 140, 0);
                // Circulo laranja ao redor da P no modo V (raio = metade do tamanho da seta)
                if (!mostrar_bolhas) {
                    desenhar_circulo(ren, b[i].cx, b[i].cy, b[i].cz, tamanho_seta * 0.5f, 255, 140, 0);
                }
            } else {
                desenhar_seta(ren, b[i].cx, b[i].cy, b[i].cz, b[i].sx, b[i].sy, b[i].sz, tamanho_seta, 0, 255, 255);
            }

            // Circulo vermelho ao redor do cacique no modo V
            if (!mostrar_bolhas && i % 512 == 0) {
                desenhar_circulo(ren, b[i].cx, b[i].cy, b[i].cz, tamanho_seta * 0.5f, 255, 0, 0);
            }
        }

        desenhar_trajetoria_cm(ren, b, num_bolhas);
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

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