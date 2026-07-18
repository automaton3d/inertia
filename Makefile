# Makefile — build da simulação (MSVC via nmake)
#
# Uso:
#   nmake /f Makefile            # compila universo_sim.exe
#   nmake /f Makefile clean      # limpa arquivos gerados
#   nmake /f Makefile run        # compila e roda a simulação

CC = cl
CFLAGS = /Ox /W3 /std:c11 /MD

# Caminhos baseados na sua instalação do vcpkg
SDL_INC = /I"E:\vcpkg\installed\x64-windows\include"
SDL_LIB = "E:\vcpkg\installed\x64-windows\lib\SDL3.lib"

all: universo_sim.exe

universo_sim.exe: main.c
	cl /Ox /W3 /std:c11 /MD /Fe:universo_sim.exe main.c \
		/I"E:\vcpkg\installed\x64-windows\include" \
		"E:\vcpkg\installed\x64-windows\lib\SDL3.lib" \
		shell32.lib user32.lib gdi32.lib \
		/link /SUBSYSTEM:CONSOLE

clean:
	-del /q *.obj *.exe 2>nul

run: universo_sim.exe
	universo_sim.exe