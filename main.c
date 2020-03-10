#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "lib/stb_image.h"

#include "levels.h"

typedef uint64_t u64;

typedef struct {
    bool right;
    bool left;
    bool up;
    bool down;
} Input;

int main()
{
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK);

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
    int window_xoffset = (window_width % tile_size) / 2;  // to adjust tiles 
    int viewport_height = window_height / tile_size;
    int window_yoffset = (window_height % tile_size) / 2;
    // printf("height %d \n", viewport_height);
    // printf("window height %d \n", window_height);
    // printf("tile_size %d\n", tile_size);
    // printf("window y offset %d\n", window_yoffset);

    int num_loops = 0;
    u64 start = SDL_GetPerformanceCounter(); 
    double frequency = (double)SDL_GetPerformanceFrequency();

    SDL_GL_SetSwapInterval(1);
    char level[LEVEL_HEIGHT][LEVEL_WIDTH];

    memcpy (level, cave_1, LEVEL_HEIGHT*LEVEL_WIDTH);
    int player_x, player_y;
    for (int y = 0; y < LEVEL_HEIGHT; ++y) {
        for (int x = 0; x < LEVEL_WIDTH; ++x) {
            if (level[y][x] == 'E') {
                player_x = x;
                player_y = y;
                break;
            }
        }
    }

    u64 last_move_time = start;
    Input input = {false, false, false, false};
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
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.scancode == SDL_SCANCODE_RIGHT) {
                    input.right = true;
                }
                if (event.key.keysym.scancode == SDL_SCANCODE_LEFT) {
                    input.left = true;
                }
            }
            
            if (event.type == SDL_KEYUP) {
                if (event.key.keysym.scancode == SDL_SCANCODE_RIGHT) {
                    input.right = false;
                }
                if (event.key.keysym.scancode == SDL_SCANCODE_LEFT) {
                    input.left = false;
                }
            }
        }

        double time_since_last_move = (double)(SDL_GetPerformanceCounter() - last_move_time) / frequency ;
        if (time_since_last_move > 0.1) {
            if (input.right) {
                level[player_y][player_x] = ' ';
                player_x += 1;
                level[player_y][player_x] = 'E';
                last_move_time = SDL_GetPerformanceCounter();
            } else if (input.left  ) {
                level[player_y][player_x] = ' ';
                player_x -= 1;
                level[player_y][player_x] = 'E';
                last_move_time = SDL_GetPerformanceCounter();
            }
        }


        for (int y = 0; y < viewport_height; y++) {
            for (int x = 0; x < viewport_width; x++) {
                SDL_Rect src = {0, 192, 32, 32};
                char tile_type = level[viewport_y + y][viewport_x + x];                
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
                } else if (tile_type == 'E') {
                    src.x = 0;
                    src.y = 0;
                }
                SDL_Rect dst = {window_xoffset + x * tile_size, window_yoffset + y * tile_size, tile_size, tile_size};
                SDL_RenderCopy(renderer, texture, &src, &dst);
            }

        }
        
        SDL_RenderPresent(renderer);

        // {
        //     u64 now = SDL_GetPerformanceCounter();
        //     double elapsed_ms = (double)(now - start) * 1000 / frequency; 
        //     start = now;
        //     printf("MS %.3lf \n", elapsed_ms);
        // }

        num_loops++;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}