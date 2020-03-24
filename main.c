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

#define COUNT(arr) (sizeof(arr)/sizeof(*arr))

typedef enum {
    COLOR_WHITE = 0,
    COLOR_YELLOW,
} Color;

typedef char Level[LEVEL_HEIGHT][LEVEL_WIDTH];

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
} v2;

typedef struct {
    int num;
    v2 objects[LEVEL_WIDTH * LEVEL_HEIGHT / 3];
} Objects;

typedef struct {
    int lifetime;
    v2 pos;
} Lock;

#define NUM_LOCKS 10

typedef struct {
    u64 start_time;
    int num_frames;
    v2 start_frame;
    int fps;
} Animation;

typedef struct {
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    v2 window_offset;
    int tile_size;
} DrawContext;

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

bool can_move(Level level, v2 pos) {
    if (pos.x < 0 || pos.x >= LEVEL_WIDTH || pos.y < 0 || pos.y >= LEVEL_HEIGHT) {
        return false;
    }
    char tile_type = level[pos.y][pos.x];
    if (tile_type == ' ' || tile_type == '.' || tile_type == '_' || tile_type == 'd') {
        return true;
    }
    return false;
}

void add_lock(Lock *locks, int x, int y) {
    for (int i = 0; i < NUM_LOCKS; i++) {
        if (locks[i].lifetime == 0) {
            locks[i].lifetime = 2;
            locks[i].pos.x = x;
            locks[i].pos.y = y;
            return;
        }
    }
    assert(!"Not enough space for locks");
}

void drop_objects(Level level, Objects *objects, char obj_sym, Lock *locks) {
    for (int i = 0; i < objects->num; i++) {
        int x = objects->objects[i].x;
        int y = objects->objects[i].y;
        char tile_under = level[y + 1][x];
        char tile_above = level[y - 1][x];
        assert(level[y][x] ==  obj_sym);
        if (tile_under == '_') {
            // Drop down
            level[y][x] = '_';
            level[y + 1][x] = obj_sym;
            objects->objects[i].y += 1;
            continue;
        }
        if ((tile_under == 'r' || tile_under == 'd') && (tile_above != 'd' && tile_above != 'r' && tile_above != 'l') ) {
            if (level[y][x - 1] == '_' && level[y + 1][x - 1] == '_') {
                // Drop left
                level[y][x] = 'l';
                add_lock(locks, x, y);
                level[y][x - 1] = obj_sym;
                objects->objects[i].x -= 1;
                continue;
            }
            if (level[y][x + 1] == '_' && level[y + 1][x + 1] == '_') {
                // Drop right
                level[y][x] = 'l';
                add_lock(locks, x, y);
                level[y][x + 1] = obj_sym;
                objects->objects[i].x += 1;
                continue;
            }
        }
    }
}

void collect_diamond(Objects *diamonds, v2 pos) {
    for (int i = 0; i < diamonds->num; i++) {
        if (diamonds->objects[i].x == pos.x && diamonds->objects[i].y == pos.y) {
            v2 *pos = &diamonds->objects[i];
            v2 *pos_lst = &diamonds->objects[diamonds->num-1];
            *pos = *pos_lst;
            diamonds->num -= 1;
        }
    }
}

void draw_tile(DrawContext context, v2 src, v2 dst) {
    SDL_Rect src_rect = {src.x, src.y, 32, 32};
    SDL_Rect dst_rect = {
        context.window_offset.x + dst.x * context.tile_size,
        context.window_offset.y + dst.y * context.tile_size,
        context.tile_size,
        context.tile_size
    };
    SDL_RenderCopy(context.renderer, context.texture, &src_rect, &dst_rect);
}

void draw_number(DrawContext context, int num, v2 pos, Color color, int min_digits) {
    int digits[15] = {};
    int num_digits = 0;
    while (num > 0) {
        int digit = num % 10;
        digits[num_digits] = digit;
        num_digits++;
        num = num / 10;
    }
    if (num_digits < min_digits) {
        num_digits = min_digits;
    }
    for (int i = 0; i < num_digits; i++) {
        v2 src = {0, 385 + digits[num_digits - i - 1] * 30};
        if (color == COLOR_YELLOW) {
            src.x = 32;
        }
        v2 dst = {pos.x + i, pos.y};
        draw_tile(context, src, dst);
    }
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
    int viewport_height = window_height / tile_size;

    v2 window_offset = {};
    window_offset.x = (window_width % tile_size) / 2;  // to adjust tiles
    window_offset.y = (window_height % tile_size) / 2;

    int viewport_x_max = LEVEL_WIDTH - viewport_width;
    int viewport_y_max = LEVEL_HEIGHT - viewport_height;

    DrawContext draw_context = {renderer, texture, window_offset, tile_size};

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

    Objects rocks = {};
    Objects diamonds = {};
    Lock locks[NUM_LOCKS] = {};
    int diamonds_collected = 0;
    v2 player_pos = {};

    for (int y = 0; y < LEVEL_HEIGHT; ++y) {
        for (int x = 0; x < LEVEL_WIDTH; ++x) {
            if (level[y][x] == 'E') {
                player_pos.x = x;
                player_pos.y = y;
            }
            if (level[y][x] == 'r') {
                rocks.objects[rocks.num].x = x;
                rocks.objects[rocks.num].y = y;
                rocks.num++;
                assert(rocks.num < COUNT(rocks.objects));
            }
            if (level[y][x] == 'd') {
                diamonds.objects[diamonds.num].x = x;
                diamonds.objects[diamonds.num].y = y;
                diamonds.num++;
                assert(diamonds.num < COUNT(diamonds.objects));
            }
        }
    }

    int diamonds_to_collect = diamonds.num;

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

        // Move player
        if (seconds_since(player_last_move_time) > kPlayerDelay) {
            v2 next_player_pos = player_pos;

            if (input.right) {
                next_player_pos.x += 1;
            } else if (input.left) {
                next_player_pos.x -= 1;
            } else if (input.up) {
                next_player_pos.y -= 1;
            } else if (input.down) {
                next_player_pos.y += 1;
            }

            if (can_move(level, next_player_pos)) {
                if (level[next_player_pos.y][next_player_pos.x] == 'd') {
                    collect_diamond(&diamonds, next_player_pos);
                    diamonds_collected += 1;
                }
                level[player_pos.y][player_pos.x] = '_';
                level[next_player_pos.y][next_player_pos.x] = 'E';
                player_pos = next_player_pos;
                player_last_move_time = time_now();
            }

            // Move viewport
            {
                int rel_player_x = player_pos.x - viewport_x;
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
                int rel_player_y = player_pos.y - viewport_y;
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
        }

        // Drop rocks
        if (seconds_since(drop_last_time) > kDropDelay) {
            drop_last_time = time_now();
            drop_objects(level, &rocks, 'r', locks);
            drop_objects(level, &diamonds, 'd', locks);

            // Clear locks
            for (int i = 0; i < NUM_LOCKS; i++) {
                Lock *lock = &locks[i];
                if (lock->lifetime > 0) {
                    lock->lifetime--;
                    if (lock->lifetime == 0) {
                        if (level[lock->pos.y][lock->pos.x] == 'l') {
                            level[lock->pos.y][lock->pos.x] = '_';
                        }
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

        // Draw status
        // Display number of diamonds to collect
        v2 pos_start = {0, 0};
        draw_number(draw_context, diamonds_to_collect, pos_start, COLOR_YELLOW, 2);

        // Display number of collected diamonds
        v2 pos_diamonds = {10, 0};
        draw_number(draw_context, diamonds_collected, pos_diamonds, COLOR_YELLOW, 2);

        // Display score
        v2 pos_score = {viewport_width - 6, 0};
        int score = 10 * diamonds_collected;
        draw_number(draw_context, score, pos_score, COLOR_WHITE, 6);

        // Display time
        v2 pos_time = {viewport_width / 2, 0};
        int level_time = 150;
        int time_to_show = level_time - (int)(seconds_since(start));
        if (time_to_show < 0) {
            time_to_show = 0;
        }
        draw_number(draw_context, time_to_show, pos_time, COLOR_WHITE, 3);


        // Draw level
        for (int y = 1; y < viewport_height; y++) {
            for (int x = 0; x < viewport_width; x++) {
                v2 src = {0, 192};
                v2 dst = {x, y};
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
                }  else if (tile_type == 'l') {
                    src.x = 288;
                    src.y = 0;
                } else if (tile_type == 'E') {
                    v2 frame = get_frame(player_animation);
                    src.x = frame.x;
                    src.y = frame.y;
                } else if (tile_type == 'd') {
                    v2 frame = get_frame(&anim_diamond);
                    src.x = frame.x;
                    src.y = frame.y;
                }
                draw_tile(draw_context, src, dst);
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
