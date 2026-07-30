# Makefile — build da simulação e testes (MSVC via nmake)
#
# Uso:
#   nmake /f Makefile sim       # compila universo_sim.exe (spin_rev_gpu.c)
#   nmake /f Makefile pimi      # compila universo_pimi.exe (spin_rev_new.c)
#   nmake /f Makefile test      # compila universo_test.exe (spin_rev_caciques.c)
#   nmake /f Makefile clean     # limpa arquivos gerados
#   nmake /f Makefile run       # compila e roda universo_sim.exe
#   nmake /f Makefile runtest   # compila e roda universo_test.exe

CC = cl
CFLAGS = /Ox /W3 /std:c11 /MD

# Caminhos baseados na sua instalação do vcpkg
SDL_INC = /I"E:\vcpkg\installed\x64-windows\include"
SDL_LIB = "E:\vcpkg\installed\x64-windows\lib\SDL3.lib"

# Libs extras
GPU_LIBS = d3d12.lib dxgi.lib
WIN_LIBS = shell32.lib user32.lib gdi32.lib

# --- Alvo principal: simulação (spin_rev_gpu.c) ---
sim: universo_sim.exe

universo_sim.exe: spin_rev_gpu.c
	$(CC) $(CFLAGS) /Fe:universo_sim.exe spin_rev_gpu.c $(SDL_INC) $(SDL_LIB) $(GPU_LIBS) $(WIN_LIBS) /link /SUBSYSTEM:CONSOLE

# --- Alvo secundário: versão new (spin_rev_new.c) ---
kimi: universo_kimi.exe

universo_kimi.exe: spin_rev_kimi.c
	$(CC) $(CFLAGS) /Fe:universo_kimi.exe spin_rev_kimi.c $(SDL_INC) $(SDL_LIB) $(GPU_LIBS) $(WIN_LIBS) /link /SUBSYSTEM:CONSOLE

# --- Alvo de teste: só caciques (spin_rev_caciques.c) ---
test: universo_test.exe

universo_test.exe: spin_rev_caciques.c
	$(CC) $(CFLAGS) /Fe:universo_test.exe spin_rev_caciques.c $(SDL_INC) $(SDL_LIB) $(GPU_LIBS) $(WIN_LIBS) /link /SUBSYSTEM:CONSOLE


# --- Headless probe ---
headless: headless.exe
 
headless.exe: spin_rev_headless.c
	$(CC) $(CFLAGS) /Fe:headless.exe spin_rev_headless.c /link /SUBSYSTEM:CONSOLE
 
runheadless: headless
	headless.exe 5000 0 0 1 1
 
headless_kklog: headless_kklog.exe
 
headless_kklog.exe: spin_rev_headless_kklog.c
	$(CC) $(CFLAGS) /Fe:headless_kklog.exe spin_rev_headless_kklog.c /link /SUBSYSTEM:CONSOLE
 
runheadless_kklog: headless_kklog
	headless_kklog.exe 5000 0 0 1 1

# --- Limpeza ---
clean:
	-del /q *.obj *.exe *.ilk *.pdb 2>nul

# --- Execução ---
run: sim
	universo_sim.exe

runtest: test
	universo_test.exe
