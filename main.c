#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "lib/stb_image.h"

#include "levels.h"

typedef uint64_t u64;

static double gPerformanceFrequency; 

typedef struct {
    bool right;
    bool left;
    bool up;
    bool down;
} Input;

typedef struct {
    int x;
    int y;
} Rock;

typedef struct {
    int x;
    int y;
} v2;

typedef struct {
    u64 start_time;
    int num_frames;
    v2 start_frame;
    int fps;
} Animation;

u64 time_now() {
    return SDL_GetPerformanceCounter();
}

double seconds_since(u64 timestamp) {
    return (double)(time_now() - timestamp) / gPerformanceFrequency;
}

v2 get_frame(Animation *animation) {
    v2 result;
    int frame_index = (int)(seconds_since(animation->start_time) * animation->fps) % animation->num_frames;
    result.x = animation->start_frame.x + frame_index * 32;
    result.y = animation->start_frame.y;
    return result;
}

bool can_move(char *level, int x, int y) {
    if (x < 0 || x >= LEVEL_WIDTH || y < 0 || y >= LEVEL_HEIGHT) {
        return false;
    }
    char tile_type = level[y * LEVEL_WIDTH + x];
    if (tile_type == ' ' || tile_type == '.' || tile_type == '_') {
        return true;
    }
    return false;
}

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

    int viewport_x_max = LEVEL_WIDTH - viewport_width;
    int viewport_y_max = LEVEL_HEIGHT - viewport_height;
    // printf("height %d \n", viewport_height);
    // printf("window height %d \n", window_height);
    // printf("tile_size %d\n", tile_size);
    // printf("window y offset %d\n", window_yoffset);

    int num_loops = 0;
    u64 start = time_now(); 
    gPerformanceFrequency = (double)SDL_GetPerformanceFrequency();

    SDL_GL_SetSwapInterval(1);

    // Init animations
    Animation anim_diamond = {};
    anim_diamond.start_time = start;
    anim_diamond.num_frames = 8;
    anim_diamond.fps = 15;
    anim_diamond.start_frame.x = 0;
    anim_diamond.start_frame.y = 320;

    Animation anim_idle1 = {};
    anim_idle1.start_time = start;
    anim_idle1.num_frames = 8;
    anim_idle1.fps = 15;
    anim_idle1.start_frame.x = 0;
    anim_idle1.start_frame.y = 33;

    Animation anim_go_left = {};
    anim_go_left.start_time = start;
    anim_go_left.num_frames = 8;
    anim_go_left.fps = 15;
    anim_go_left.start_frame.x = 0;
    anim_go_left.start_frame.y = 128;

    Animation anim_go_right = {};
    anim_go_right.start_time = start;
    anim_go_right.num_frames = 8;
    anim_go_right.fps = 15;
    anim_go_right.start_frame.x = 0;
    anim_go_right.start_frame.y = 160;

    // Init level
    char level[LEVEL_HEIGHT][LEVEL_WIDTH];
    memcpy (level, cave_1, LEVEL_HEIGHT*LEVEL_WIDTH);

    const int kMaxRocks = LEVEL_HEIGHT * LEVEL_WIDTH / 3; 
    Rock rocks[kMaxRocks]; // allocate 1/3 of the level size for rocks  
    int num_rocks = 0;

    int player_x, player_y;
    for (int y = 0; y < LEVEL_HEIGHT; ++y) {
        for (int x = 0; x < LEVEL_WIDTH; ++x) {
            if (level[y][x] == 'E') {
                player_x = x;
                player_y = y;
            }
            if (level[y][x] == 'r') {
                rocks[num_rocks].x = x;
                rocks[num_rocks].y = y;
                num_rocks++;
            }
        }
    }

    u64 player_last_move_time = start;   
    u64 drop_last_time = start;   
    const double kPlayerDelay = 0.1;
    const double kDropDelay = 0.15;
    Input input = {false, false, false, false};

    Animation *player_animation = &anim_idle1;

    int is_running = 1;
    while (is_running) {
        double frame_time = seconds_since(start); // for animation
        
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
                if (event.key.keysym.scancode == SDL_SCANCODE_UP) {
                    input.up = true;
                }
                if (event.key.keysym.scancode == SDL_SCANCODE_DOWN) {
                    input.down = true;
                }
            }
            
            if (event.type == SDL_KEYUP) {
                if (event.key.keysym.scancode == SDL_SCANCODE_RIGHT) {
                    input.right = false;
                }
                if (event.key.keysym.scancode == SDL_SCANCODE_LEFT) {
                    input.left = false;
                }
                if (event.key.keysym.scancode == SDL_SCANCODE_UP) {
                    input.up = false;
                }
                if (event.key.keysym.scancode == SDL_SCANCODE_DOWN) {
                    input.down = false;
                }
            }
        }

        if (seconds_since(player_last_move_time) > kPlayerDelay) {
            if (input.right && can_move(&level[0][0], player_x + 1, player_y)) {
                level[player_y][player_x] = '_';
                player_x += 1;
                level[player_y][player_x] = 'E';
                player_last_move_time = time_now();
            } else if (input.left && can_move(&level[0][0], player_x - 1, player_y)) {
                level[player_y][player_x] = '_';
                player_x -= 1;
                level[player_y][player_x] = 'E';
                player_last_move_time = time_now();
            } else if (input.up && can_move(&level[0][0], player_x, player_y - 1)) {
                level[player_y][player_x] = '_';
                player_y -= 1;
                level[player_y][player_x] = 'E';
                player_last_move_time = time_now();
            } else if (input.down && can_move(&level[0][0], player_x, player_y + 1)) {
                level[player_y][player_x] = '_';
                player_y += 1;
                level[player_y][player_x] = 'E';
                player_last_move_time = time_now();
            }
            int rel_player_x = player_x - viewport_x;
            if (rel_player_x >= 20) {
                viewport_x += rel_player_x - 20;
                if (viewport_x > viewport_x_max) {
                    viewport_x = viewport_x_max;
                } 
            }
            if (rel_player_x <= 9) {
                viewport_x -= 9 - rel_player_x;
                if (viewport_x < 0) {
                    viewport_x = 0;
                } 
            }
            int rel_player_y = player_y - viewport_y;
            if (rel_player_y >= 13) {
                viewport_y += rel_player_y - 13;
                if (viewport_y > viewport_y_max) {
                    viewport_y = viewport_y_max;
                } 
            }
            if (rel_player_y <= 6) {
                viewport_y -= 6 - rel_player_y;
                if (viewport_y < 0) {
                    viewport_y = 0;
                } 
            }
        }

        // Drop rocks
        if (seconds_since(drop_last_time) > kDropDelay) {
            drop_last_time = time_now();
            for (int i = 0; i < num_rocks; i++) {
                int x = rocks[i].x;
                int y = rocks[i].y;
                char tile_under_rock = level[y + 1][x];
                assert(level[y][x] == 'r');
                if (tile_under_rock == '_') {
                    // Drop down
                    level[y][x] = '_';
                    level[y + 1][x] = 'r';
                    rocks[i].y += 1;
                    continue;
                }
                if (tile_under_rock == 'r' || tile_under_rock == 'd') {
                    if (level[y][x - 1] == '_' && level[y + 1][x - 1] == '_') { 
                        // Drop left
                        level[y][x] = '_';
                        level[y][x - 1] = 'r';
                        rocks[i].x -= 1;
                        continue;
                    }
                    if (level[y][x + 1] == '_' && level[y + 1][x + 1] == '_') { 
                        // Drop right
                        level[y][x] = '_';
                        level[y][x + 1] = 'r';
                        rocks[i].x += 1;
                        continue;
                    }
                
                }
            }
        }

         // Choose player animation
        if (input.right) {
            player_animation = &anim_go_right;
        } else if (input.left) {
            player_animation = &anim_go_left;
        } else {
            player_animation = &anim_idle1;
        }

        // Draw level
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
                    v2 frame = get_frame(player_animation);
                    src.x = frame.x;
                    src.y = frame.y;
                } else if (tile_type == 'd') {
                    v2 frame = get_frame(&anim_diamond);
                    src.x = frame.x;
                    src.y = frame.y;
                }
                SDL_Rect dst = {window_xoffset + x * tile_size, window_yoffset + y * tile_size, tile_size, tile_size};
                SDL_RenderCopy(renderer, texture, &src, &dst);
            }

        }
        
        SDL_RenderPresent(renderer);

        // {
        //     u64 now = time_now();
        //     double elapsed_ms = (double)(now - start) * 1000 / gPerformanceFrequency; 
        //     start = now;
        //     printf("MS %.3lf \n", elapsed_ms);
        // }

        num_loops++;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}