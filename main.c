#include <SDL2/SDL.h>
#include <stdio.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "lib/stb_image.h"

int main()
{
    SDL_Init(SDL_INIT_VIDEO & SDL_INIT_JOYSTICK);

    SDL_Window *window = SDL_CreateWindow(
        "Boulder-Dash",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        640,
        980,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    if (window == NULL) {
        printf("Couldn't create window: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL) {
        printf("Couldn't create renderer: %s\n", SDL_GetError());
        return 1;
    }

    int width, height, num_channels;
    void *pixels = stbi_load("bd-sprites.png", &width, &height, &num_channels, 0);

    SDL_Rect rect;
    rect.x = 0;
    rect.y = 0;
    rect.w = width;
    rect.h = height;

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STATIC, width, height);
    if (texture == NULL) {
        printf("Couldn't create texture: %s\n", SDL_GetError());
        return 1;
    }

    int result = SDL_UpdateTexture(texture, &rect, pixels, width*num_channels);
    if (result != 0) {
        printf("Couldn't update texture: %s\n", SDL_GetError());
        return 1;
    }

    result = SDL_RenderCopy(renderer, texture, &rect, &rect);
    if (result != 0) {
        printf("Couldn't renders texture: %s\n", SDL_GetError());
        return 1;
    }

    while (1) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                SDL_DestroyWindow(window);
                SDL_Quit();
                return 0;
            }
        }
        
        SDL_RenderPresent(renderer);
        SDL_Delay(100);
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}