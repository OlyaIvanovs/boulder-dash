boulder-dash.out: main.c lib/stb_image.o levels.h
	clang -lSDL2 -lm -lSDL2_mixer main.c lib/stb_image.o -o boulder-dash.out

lib/stb_image.o: lib/stb_image.h lib/stb_image.c
	clang -c lib/stb_image.c -o lib/stb_image.o
