#include <SDL3/SDL.h>
#include <stdio.h>

void testar_backend(SDL_GPUShaderFormat fmt, const char *name) {
    SDL_GPUDevice *gpu = SDL_CreateGPUDevice(fmt, false, name);
    if (gpu) {
        printf("Backend %s inicializado com sucesso!\n", name);
        SDL_DestroyGPUDevice(gpu);
    } else {
        printf("Backend %s falhou: %s\n", name, SDL_GetError());
    }
}

int main(void) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("Erro ao inicializar SDL: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow("Teste GPU SDL3", 800, 600, 0);

    testar_backend(SDL_GPU_SHADERFORMAT_DXIL, "direct3d12");
    testar_backend(SDL_GPU_SHADERFORMAT_DXBC, "direct3d11");
    testar_backend(SDL_GPU_SHADERFORMAT_SPIRV, "vulkan");
    testar_backend(SDL_GPU_SHADERFORMAT_SPIRV, "software");

    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
