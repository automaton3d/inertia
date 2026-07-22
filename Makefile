# Makefile — build da simulação e teste GPU (MSVC via nmake)
#
# Uso:
#   nmake /f Makefile sim       # compila universo_sim.exe (versão GPU)
#   nmake /f Makefile gpu       # compila teste_gpu.exe
#   nmake /f Makefile clean     # limpa arquivos gerados
#   nmake /f Makefile run       # compila e roda a simulação
#   nmake /f Makefile rungpu    # compila e roda o teste GPU

CC = cl
CFLAGS = /Ox /W3 /std:c11 /MD

# Caminhos baseados na sua instalação do vcpkg
SDL_INC = /I"E:\vcpkg\installed\x64-windows\include"
SDL_LIB = "E:\vcpkg\installed\x64-windows\lib\SDL3.lib"

# Libs extras (d3d12/dxgi só necessários se teste_gpu.c usar D3D12 diretamente)
GPU_LIBS = d3d12.lib dxgi.lib
WIN_LIBS = shell32.lib user32.lib gdi32.lib

# --- Alvo principal: simulação (versão GPU, spin_rev_gpu.c) ---
sim: universo_sim.exe

universo_sim.exe: spin_rev_gpu.c
	$(CC) $(CFLAGS) /Fe:universo_sim.exe spin_rev_gpu.c $(SDL_INC) $(SDL_LIB) $(GPU_LIBS) $(WIN_LIBS) /link /SUBSYSTEM:CONSOLE

# --- Alvo secundário: teste GPU ---
gpu: teste_gpu.exe

teste_gpu.exe: teste_gpu.c
	$(CC) $(CFLAGS) /Fe:teste_gpu.exe teste_gpu.c $(SDL_INC) $(SDL_LIB) $(GPU_LIBS) $(WIN_LIBS) /link /SUBSYSTEM:CONSOLE

# --- Limpeza ---
clean:
	-del /q *.obj *.exe 2>nul

# --- Execução ---
run: universo_sim.exe
	universo_sim.exe

rungpu: teste_gpu.exe
	teste_gpu.exe
