#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "base.h"

#include "audio.h"
#include "levels.h"
#include "lib/stb_image.h"

u64 time_now() {
  return SDL_GetPerformanceCounter();
}

double seconds_since(u64 timestamp) {
  return (double)(time_now() - timestamp) / gPerformanceFrequency;
}

typedef enum {
  COLOR_WHITE = 0,
  COLOR_YELLOW,
} Color;

typedef char Tiles[LEVEL_HEIGHT][LEVEL_WIDTH];

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

static inline v2 V2(int x, int y) {
  v2 result = {x, y};
  return result;
}

typedef struct {
  v2 objects[LEVEL_WIDTH * LEVEL_HEIGHT / 3];
  int num;
} Objects;

typedef struct {
  v2 pos;
  int lifetime;
} Lock;

typedef struct {
  v2 start_frame;
  u64 start_time;
  int num_frames;
  int fps;
} Animation;

typedef struct {
  SDL_Renderer *renderer;
  SDL_Texture *texture;
  v2 window_offset;
  int tile_size;
} DrawContext;

typedef struct Level {
  Tiles tiles;
  Objects diamonds;
  Objects rocks;
  Lock locks[10];
  v2 player_pos;
  int time_left;
  int score_per_diamond;
  int min_diamonds;
  int diamonds_collected;
} Level;

void load_level(Level *level, int num_level) {
  SDL_memcpy(level->tiles, gLevels[num_level], LEVEL_HEIGHT * LEVEL_WIDTH);

  SDL_memset(&level->rocks, 0, sizeof(level->rocks));
  SDL_memset(&level->diamonds, 0, sizeof(level->diamonds));
  SDL_memset(level->locks, 0, sizeof(level->locks));

  for (int y = 0; y < LEVEL_HEIGHT; ++y) {
    for (int x = 0; x < LEVEL_WIDTH; ++x) {
      if (level->tiles[y][x] == 'E') {
        level->player_pos.x = x;
        level->player_pos.y = y;
      }

      if (level->tiles[y][x] == 'r') {
        level->rocks.objects[level->rocks.num].x = x;
        level->rocks.objects[level->rocks.num].y = y;
        level->rocks.num++;
        assert(level->rocks.num < COUNT(level->rocks.objects));
      }

      if (level->tiles[y][x] == 'd') {
        level->diamonds.objects[level->diamonds.num].x = x;
        level->diamonds.objects[level->diamonds.num].y = y;
        level->diamonds.num++;
        assert(level->diamonds.num < COUNT(level->diamonds.objects));
      }
    }
  }

  level->time_left = 150;
  level->score_per_diamond = 10;
  level->min_diamonds = level->diamonds.num / 6;
}

v2 get_frame(Animation *animation) {
  v2 result;
  int frame_index =
      (int)(seconds_since(animation->start_time) * animation->fps) % animation->num_frames;
  result.x = animation->start_frame.x + frame_index * 32;
  result.y = animation->start_frame.y;
  return result;
}

bool can_move(Level *level, v2 pos) {
  if (pos.x < 0 || pos.x >= LEVEL_WIDTH || pos.y < 0 || pos.y >= LEVEL_HEIGHT) {
    return false;
  }
  char tile_type = level->tiles[pos.y][pos.x];
  if (tile_type == ' ' || tile_type == '.' || tile_type == '_' || tile_type == 'd') {
    return true;
  }
  return false;
}

bool can_move_rock(Level *level, v2 pos, v2 next_pos) {
  if (((pos.x < next_pos.x) && (level->tiles[pos.y][next_pos.x + 1] == '_')) ||
      ((pos.x > next_pos.x) && (level->tiles[pos.y][next_pos.x - 1] == '_'))) {
    return true;
  }
  return false;
}

void add_lock(Lock *locks, int x, int y) {
  for (int i = 0; i < sizeof(&locks); i++) {
    if (locks[i].lifetime == 0) {
      locks[i].lifetime = 2;
      locks[i].pos.x = x;
      locks[i].pos.y = y;
      return;
    }
  }
  assert(!"Not enough space for locks");
}

void drop_objects(Level *level, char obj_sym) {
  bool play_fall_sound = false;
  Objects *objs;

  if (obj_sym == 'd') {
    objs = &level->diamonds;
  } else if (obj_sym == 'r') {
    objs = &level->rocks;
  } else {
    assert(!"Unknown obj sym");
  }

  for (int i = 0; i < objs->num; i++) {
    int x = objs->objects[i].x;
    int y = objs->objects[i].y;
    assert(level->tiles[y][x] == obj_sym);

    char tile_above = level->tiles[y - 1][x];
    char tile_under = level->tiles[y + 1][x];

    if (tile_under == '_') {
      // Drop down
      level->tiles[y][x] = '_';
      level->tiles[y + 1][x] = obj_sym;
      objs->objects[i].y += 1;

      // Determine whether we play sound.
      // Check every tile below and play sound only if falling on
      // a steady ground or on a stack of boulders that are already
      // on the ground
      play_fall_sound = true;  // in case we never enter the loop
      for (int i = y + 2; i < LEVEL_HEIGHT; ++i) {
        char tile = level->tiles[i][x];
        if (tile == '_') {
          play_fall_sound = false;  // the rock is still falling
          break;
        }
        if (tile == '.' || tile == 'W' || tile == 'w') {
          play_fall_sound = true;
          break;  // falling on solid ground.
        }
      }
      continue;  // don't check if we can slide
    }

    // Slide off rocks and diamonds
    if ((tile_under == 'r' || tile_under == 'd' || tile_under == 'w') &&
        (tile_above != 'd' && tile_above != 'r' && tile_above != 'l')) {
      if (level->tiles[y][x - 1] == '_' && level->tiles[y + 1][x - 1] == '_') {
        // Drop left
        level->tiles[y][x] = 'l';
        add_lock(level->locks, x, y);
        level->tiles[y][x - 1] = obj_sym;
        objs->objects[i].x -= 1;
        continue;
      }
      if (level->tiles[y][x + 1] == '_' && level->tiles[y + 1][x + 1] == '_') {
        // Drop right
        level->tiles[y][x] = 'l';
        add_lock(level->locks, x, y);
        level->tiles[y][x + 1] = obj_sym;
        objs->objects[i].x += 1;
        continue;
      }
    }
  }

  if (play_fall_sound) {
    if (obj_sym == 'r') {
      play_sound(SOUND_STONE);
    } else if (obj_sym == 'd') {
      static int diamond_sound_num = 0;
      play_sound(SOUND_DIAMOND_1 + diamond_sound_num);
      diamond_sound_num = (diamond_sound_num + 1) % 7;
    }
  }
}

void collect_diamond(Objects *diamonds, v2 pos) {
  for (int i = 0; i < diamonds->num; i++) {
    if (diamonds->objects[i].x == pos.x && diamonds->objects[i].y == pos.y) {
      v2 *pos = &diamonds->objects[i];
      v2 *pos_lst = &diamonds->objects[diamonds->num - 1];
      *pos = *pos_lst;
      diamonds->num -= 1;
    }
  }
}

void draw_tile(DrawContext context, v2 src, v2 dst) {
  SDL_Rect src_rect = {src.x, src.y, 32, 32};
  SDL_Rect dst_rect = {context.window_offset.x + dst.x * context.tile_size,
                       context.window_offset.y + dst.y * context.tile_size, context.tile_size,
                       context.tile_size};
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

int main() {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO) < 0) {
    return 1;
  }
  gPerformanceFrequency = (double)SDL_GetPerformanceFrequency();

  // Audio
  SDL_AudioDeviceID audio_device_id = init_audio();
  if (audio_device_id == 0) {
    printf("Couldn't init audio\n");
    return 1;
  }

  SDL_Window *window =
      SDL_CreateWindow("Boulder-Dash", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 960, 480,
                       SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);

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

  SDL_Texture *texture =
      SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STATIC, width, height);
  if (texture == NULL) {
    printf("Couldn't create texture: %s\n", SDL_GetError());
    return 1;
  }

  int result = SDL_UpdateTexture(texture, &rect, pixels,
                                 width * num_channels);  // load to video memory
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

  bool rock_is_pushed = false;
  u64 rock_start_move_time = time_now();

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
  anim_go_left.fps = 25;
  anim_go_left.start_frame.x = 0;
  anim_go_left.start_frame.y = 128;

  Animation anim_go_right = {};
  anim_go_right.start_time = start;
  anim_go_right.num_frames = 8;
  anim_go_right.fps = 25;
  anim_go_right.start_frame.x = 0;
  anim_go_right.start_frame.y = 160;

  Animation anim_idle2 = {};
  anim_idle2.start_time = start;
  anim_idle2.num_frames = 8;
  anim_idle2.fps = 10;
  anim_idle2.start_frame.x = 0;
  anim_idle2.start_frame.y = 66;

  Animation anim_idle3 = {};
  anim_idle3.start_time = start;
  anim_idle3.num_frames = 8;
  anim_idle3.fps = 10;
  anim_idle3.start_frame.x = 0;
  anim_idle3.start_frame.y = 98;

  Animation anim_exit = {};
  anim_exit.start_time = start;
  anim_exit.num_frames = 1;  // no exit annimation until all diamonds are collected
  anim_exit.fps = 4;
  anim_exit.start_frame.x = 32;
  anim_exit.start_frame.y = 192;

  // Init level
  Level level = {};
  load_level(&level, 0);

  u64 player_last_move_time = start;
  u64 drop_last_time = start;
  const double kPlayerDelay = 0.1;
  const double kDropDelay = 0.15;
  Input input = {false, false, false, false};

  Animation *player_animation = &anim_idle1;

  int score = 0;

  play_sound(SOUND_BD1);
  int walking_sound_cooldown = 1;

  int is_running = 1;
  while (is_running) {
    bool white_tunnel = false;
    double frame_time = seconds_since(start);  // for animation

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
      v2 next_player_pos = level.player_pos;

      if (input.right) {
        next_player_pos.x += 1;
      } else if (input.left) {
        next_player_pos.x -= 1;
      } else if (input.up) {
        next_player_pos.y -= 1;
      } else if (input.down) {
        next_player_pos.y += 1;
      }

      if (can_move(&level, next_player_pos)) {
        if (level.tiles[next_player_pos.y][next_player_pos.x] == 'd') {
          collect_diamond(&level.diamonds, next_player_pos);
          level.diamonds_collected += 1;
          score += level.score_per_diamond;
          if (level.diamonds_collected == level.min_diamonds) {
            level.score_per_diamond = 20;
            anim_exit.num_frames = 2;
            anim_exit.start_frame.x = 32;
            white_tunnel = true;
            play_sound(SOUND_CRACK);
          } else {
            play_sound(SOUND_DIAMOND_COLLECT);
          }
        }

        SoundId walking_sound = SOUND_WALK_D;
        if (level.tiles[next_player_pos.y][next_player_pos.x] == '.') {
          walking_sound = SOUND_WALK_E;
        }
        if (walking_sound_cooldown-- == 0) {
          play_sound(walking_sound);
          walking_sound_cooldown = 1;
        }

        level.tiles[level.player_pos.y][level.player_pos.x] = '_';
        level.tiles[next_player_pos.y][next_player_pos.x] = 'E';
        level.player_pos = next_player_pos;
        player_last_move_time = time_now();
      }

      // Move rock
      if (can_move_rock(&level, level.player_pos, next_player_pos)) {
        if (!rock_is_pushed) {
          rock_start_move_time = time_now();
          rock_is_pushed = true;
        } else if (seconds_since(rock_start_move_time) > 0.5f) {
          int rock_next_x;
          if (level.player_pos.x < next_player_pos.x) {
            rock_next_x = next_player_pos.x + 1;
          } else {
            rock_next_x = next_player_pos.x - 1;
          }

          level.tiles[level.player_pos.y][level.player_pos.x] = '_';
          level.tiles[next_player_pos.y][next_player_pos.x] = 'E';

          for (int i = 0; i < level.rocks.num; i++) {
            if (level.rocks.objects[i].x == next_player_pos.x &&
                level.rocks.objects[i].y == next_player_pos.y) {
              level.rocks.objects[i].x = rock_next_x;
              level.tiles[next_player_pos.y][rock_next_x] = 'r';
              break;
            }
          }
          level.player_pos = next_player_pos;
        }
      }

      if (level.player_pos.x == next_player_pos.x) {
        rock_start_move_time = time_now();
        rock_is_pushed = false;
      }

      // Move viewport
      {
        int rel_player_x = level.player_pos.x - viewport_x;
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
        int rel_player_y = level.player_pos.y - viewport_y;
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
      drop_objects(&level, 'r');
      drop_objects(&level, 'd');

      // Clear locks
      for (int i = 0; i < COUNT(level.locks); i++) {
        Lock *lock = &level.locks[i];
        if (lock->lifetime > 0) {
          lock->lifetime--;
          if (lock->lifetime == 0) {
            if (level.tiles[lock->pos.y][lock->pos.x] == 'l') {
              level.tiles[lock->pos.y][lock->pos.x] = '_';
            }
          }
        }
      }
    }

    // Choose player animation
    if (seconds_since(player_last_move_time) > 5) {
      if (seconds_since(player_last_move_time) > 10) {
        player_animation = &anim_idle3;
      } else {
        player_animation = &anim_idle2;
      }
    } else if (input.right) {
      player_animation = &anim_go_right;
    } else if (input.left) {
      player_animation = &anim_go_left;
    } else {
      player_animation = &anim_idle1;
    }

    // Draw status
    // Display number of diamonds to collect
    v2 white_diamond = {256, 32};
    draw_tile(draw_context, white_diamond, V2(2, 0));

    if (level.diamonds_collected < level.min_diamonds) {
      draw_number(draw_context, level.min_diamonds, V2(0, 0), COLOR_YELLOW, 2);
    } else {
      draw_tile(draw_context, white_diamond, V2(0, 0));
      draw_tile(draw_context, white_diamond, V2(1, 0));
    }
    draw_number(draw_context, level.score_per_diamond, V2(3, 0), COLOR_WHITE, 2);

    // Display number of collected diamonds
    v2 pos_diamonds = {10, 0};
    draw_number(draw_context, level.diamonds_collected, pos_diamonds, COLOR_YELLOW, 2);

    // Display overall score
    v2 pos_score = {viewport_width - 6, 0};
    draw_number(draw_context, score, pos_score, COLOR_WHITE, 6);

    // Display time
    v2 pos_time = {viewport_width / 2, 0};
    int time_to_show = level.time_left - (int)(seconds_since(start));
    if (time_to_show < 0) {
      time_to_show = 0;
    }
    draw_number(draw_context, time_to_show, pos_time, COLOR_WHITE, 3);

    // Draw level
    for (int y = 1; y < viewport_height; y++) {
      for (int x = 0; x < viewport_width; x++) {
        v2 src = {0, 192};
        v2 dst = {x, y};
        char tile_type = level.tiles[viewport_y + y][viewport_x + x];
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
        } else if (tile_type == 'l') {
          src.x = 288;
          src.y = 0;
        } else if (tile_type == '_' && white_tunnel) {
          src.x = 300;
          src.y = 0;
        } else if (tile_type == 'E') {
          v2 frame = get_frame(player_animation);
          src.x = frame.x;
          src.y = frame.y;
        } else if (tile_type == 'd') {
          v2 frame = get_frame(&anim_diamond);
          src.x = frame.x;
          src.y = frame.y;
        } else if (tile_type == 'X') {
          v2 frame = get_frame(&anim_exit);
          src.x = frame.x;
          src.y = frame.y;
        }
        draw_tile(draw_context, src, dst);
      }
    }

    SDL_RenderPresent(renderer);

    // {
    //     u64 now = time_now();
    //     double elapsed_ms = (double)(now - start) * 1000 /
    //     gPerformanceFrequency; start = now; printf("MS %.3lf \n",
    //     elapsed_ms);
    // }

    num_loops++;
  }

  SDL_CloseAudioDevice(audio_device_id);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
