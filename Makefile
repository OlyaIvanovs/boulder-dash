boulder-dash.out: main.c audio.c lib/stb_image.o include/levels.h include/base.h include/audio.h
	clang -g -Iinclude -lSDL2 -lm main.c audio.c lib/stb_image.o -o boulder-dash.out

lib/stb_image.o: lib/stb_image.h lib/stb_image.c
	clang -c lib/stb_image.c -o lib/stb_image.o
