#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdint.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "lib/stb_image.h"

#include "levels.h"

typedef uint64_t u64;

int main()
{
    SDL_Init(SDL_INIT_VIDEO & SDL_INIT_JOYSTICK);

    SDL_Window *window = SDL_CreateWindow(
        "Boulder-Dash",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        960,
        480,
        SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP
    );

    if (window == NULL) {
        printf("Couldn't create window: %s\n", SDL_GetError());
        return 1;
    }

    int window_width, window_height;
    SDL_GetWindowSize(window, &window_width, &window_height);

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL) {
        printf("Couldn't create renderer: %s\n", SDL_GetError());
        return 1;
    }

    int width, height, num_channels;
    void *pixels = stbi_load("bd-sprites.png", &width, &height, &num_channels, 0);

    SDL_Rect rect = {0, 0, width, height};

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STATIC, width, height);
    if (texture == NULL) {
        printf("Couldn't create texture: %s\n", SDL_GetError());
        return 1;
    }

    int result = SDL_UpdateTexture(texture, &rect, pixels, width*num_channels); // load to video memory
    if (result != 0) {
        printf("Couldn't update texture: %s\n", SDL_GetError());
        return 1;
    }

    int viewport_x = 0;
    int viewport_y = 0;
    int viewport_width = 30;
    int tile_size = window_width / viewport_width;
    int window_xoffset = (window_width % viewport_width) / 2;
    int viewport_height = window_height / tile_size;
    int window_yoffset = (window_height % viewport_height) / 2;

    int num_loops = 0;
    u64 start = SDL_GetPerformanceCounter();
    double frequency = (double)SDL_GetPerformanceFrequency();

    SDL_GL_SetSwapInterval(1);

    int is_running = 1;
    while (is_running) {
        
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                is_running = 0;
                break;
            }
            if (event.type == SDL_KEYUP) {
                if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    is_running = 0;
                    break;
                }
            }
        }

        for (int y = 0; y < viewport_height; y++) {
            for (int x = 0; x < viewport_width; x++) {
                SDL_Rect src = {0, 192, 32, 32};
                SDL_Rect dst = {x * tile_size, y * tile_size, tile_size, tile_size};
                char tile_type = cave_1[viewport_y + y][viewport_x + x];                
                if (tile_type == 'r') {
                    src.x = 0;
                    src.y = 224;        
                } else if (tile_type == 'w') {
                    src.x = 96;
                    src.y = 192;
                } else if (tile_type == 'W') {
                    src.x = 32;
                    src.y = 192;
                } else if (tile_type == '.') {
                    src.x = 32;
                    src.y = 224;
                }
                SDL_RenderCopy(renderer, texture, &src, &dst);
            }
        }
        // result = SDL_RenderCopy(renderer, texture, &src, &dst);
        // if (result != 0) {
        //     printf("Couldn't renders texture: %s\n", SDL_GetError());
        //     return 1;
        // }
        
        SDL_RenderPresent(renderer);

        {
            u64 now = SDL_GetPerformanceCounter();
            double elapsed_ms = (double)(now - start) * 1000 / frequency; 
            start = now;
            printf("MS %.3lf \n", elapsed_ms);
        }

        num_loops++;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}