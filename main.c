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

static inline v2 sum_v2(v2 a, v2 b) {
  return V2(a.x + b.x, a.y + b.y);
}

typedef struct Objects {
  v2 objects[LEVEL_WIDTH * LEVEL_HEIGHT / 3];
  int num;
} Objects;

typedef struct {
  v2 pos;
  int lifetime;
} Lock;

typedef struct Enemy {
  v2 pos;
  v2 direction;
} Enemy;

typedef struct Enemies {
  Enemy objects[20];
  int num;
} Enemies;

typedef struct {
  v2 start_frame;
  u64 start_time;
  int num_frames;
  int fps;
  int times_to_play;  // how many times to play animation in total; 0 if indefinitely
} Animation;

typedef struct Explosion {
  bool active;
  char type;
  v2 pos_start;
  v2 pos_end;
  u64 start_time;
  double duration;  // in seconds
} Explosion;

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
  Enemies enemies;
  Enemies butterflies;
  Lock locks[10];
  Explosion explosions[5];
  v2 player_pos;  // in tiles
  v2 enemy_pos;
  int time_left;
  int score_per_diamond;
  int min_diamonds;
  int diamonds_collected;
  bool can_exit;
} Level;

typedef struct Viewport {
  // in pixels
  int x;
  int y;
  v2 max;

  // in tiles
  int width;
  int height;
} Viewport;

void load_level(Level *level, int num_level) {
  SDL_memset(level, 0, sizeof(*level));
  SDL_memcpy(level->tiles, gLevels[num_level], LEVEL_HEIGHT * LEVEL_WIDTH);

  for (int y = 0; y < LEVEL_HEIGHT; ++y) {
    for (int x = 0; x < LEVEL_WIDTH; ++x) {
      if (level->tiles[y][x] == 'E') {
        level->player_pos.x = x;
        level->player_pos.y = y;
      }

      if (level->tiles[y][x] == 'f') {
        level->enemies.objects[level->enemies.num].pos.x = x;
        level->enemies.objects[level->enemies.num].pos.y = y;
        level->enemies.objects[level->enemies.num].direction = V2(1, 0);  // to the right
        level->enemies.num++;
        assert(level->enemies.num < COUNT(level->enemies.objects));
      }

      if (level->tiles[y][x] == 'b') {
        level->butterflies.objects[level->butterflies.num].pos.x = x;
        level->butterflies.objects[level->butterflies.num].pos.y = y;
        level->butterflies.objects[level->butterflies.num].direction = V2(1, 0);  // to the right
        level->butterflies.num++;
        assert(level->butterflies.num < COUNT(level->butterflies.objects));
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
  level->min_diamonds = (level->diamonds.num + level->butterflies.num * 9) / 6;
  level->diamonds_collected = 0;
  level->can_exit = false;
}

v2 get_frame(Animation *animation) {
  int frames_played_since_start = (int)(seconds_since(animation->start_time) * animation->fps);
  int frame_index = frames_played_since_start % animation->num_frames;
  if (animation->times_to_play > 0 &&
      frames_played_since_start / animation->num_frames > animation->times_to_play) {
    frame_index = animation->num_frames - 1;
  }
  return V2(animation->start_frame.x + frame_index * 32, animation->start_frame.y);
}

v2 turn_right(v2 direction) {
  return V2(-direction.y, direction.x);
}

v2 turn_left(v2 direction) {
  return V2(direction.y, -direction.x);
}

bool can_move(Level *level, v2 pos) {
  if (pos.x < 0 || pos.x >= LEVEL_WIDTH || pos.y < 0 || pos.y >= LEVEL_HEIGHT) {
    return false;
  }
  char tile_type = level->tiles[pos.y][pos.x];
  if (tile_type == ' ' || tile_type == '.' || tile_type == '_' || tile_type == 'd' ||
      (tile_type == 'X' && level->can_exit)) {
    return true;
  }
  return false;
}

bool enemy_can_move(Level *level, v2 pos) {
  if (pos.x < 0 || pos.x >= LEVEL_WIDTH || pos.y < 0 || pos.y >= LEVEL_HEIGHT) {
    return false;
  }
  char tile_type = level->tiles[pos.y][pos.x];
  if (tile_type == '_' || tile_type == ' ') {
    return true;
  }
  return false;
}

void move_enemies(Level *level, char obj_sym) {
  Enemies *enemies;

  if (obj_sym == 'f') {
    enemies = &level->enemies;
  } else if (obj_sym == 'b') {
    enemies = &level->butterflies;
  } else {
    assert(!"Unknown obj sym");
  }

  for (int i = 0; i < enemies->num; ++i) {
    Enemy *enemy = &enemies->objects[i];

    assert(level->tiles[enemy->pos.y][enemy->pos.x] == obj_sym);
    level->tiles[enemy->pos.y][enemy->pos.x] = '_';  // "erase"

    v2 pos_forward = sum_v2(enemy->pos, enemy->direction);
    v2 pos_right = sum_v2(enemy->pos, turn_right(enemy->direction));
    v2 pos_right_diag = sum_v2(pos_right, V2(-enemy->direction.x, -enemy->direction.y));

    if (enemy_can_move(level, pos_right) &&
        level->tiles[pos_right_diag.y][pos_right_diag.x] != '_') {
      // Turn and move right
      enemy->pos = pos_right;
      enemy->direction = turn_right(enemy->direction);
    } else if (enemy_can_move(level, pos_forward)) {
      // Move forward
      enemy->pos = pos_forward;
    } else {
      // Turn left in place
      enemy->direction = turn_left(enemy->direction);
      enemy->pos = enemy->pos;
    }

    level->tiles[enemy->pos.y][enemy->pos.x] = obj_sym;  // "draw"
  }
}

void remove_enemy(Enemies *enemies, v2 pos) {
  for (int i = 0; i < enemies->num; i++) {
    if (enemies->objects[i].pos.x == pos.x && enemies->objects[i].pos.y == pos.y) {
      v2 *pos = &enemies->objects[i].pos;
      v2 *pos_lst = &enemies->objects[enemies->num - 1].pos;
      *pos = *pos_lst;
      enemies->num -= 1;
    }
  }
}

void remove_obj(Objects *objs, v2 pos) {
  for (int i = 0; i < objs->num; i++) {
    if (objs->objects[i].x == pos.x && objs->objects[i].y == pos.y) {
      v2 *pos = &objs->objects[i];
      v2 *pos_lst = &objs->objects[objs->num - 1];
      *pos = *pos_lst;
      objs->num -= 1;
    }
  }
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

void add_explosion(Level *level, v2 pos, char type) {
  assert(type == 'f' || type == 'b');

  v2 start = sum_v2(pos, V2(-1, -1));
  v2 end = sum_v2(pos, V2(1, 1));

  // Don't blow up the outer walls
  if (start.x == 0) {
    start.x++;
    end.x++;
  }
  if (end.x == LEVEL_WIDTH - 1) {
    start.x--;
    end.x--;
  }
  if (start.y == 1) {
    start.y++;
    end.y++;
  }
  if (end.y == LEVEL_HEIGHT - 1) {
    start.y--;
    end.y--;
  }

  // Remove objects and set tiles
  for (int y = start.y; y <= end.y; ++y) {
    for (int x = start.x; x <= end.x; ++x) {
      char tile = level->tiles[y][x];
      if (tile == 'r') {
        remove_obj(&level->rocks, V2(x, y));
      } else if (tile == 'd') {
        remove_obj(&level->diamonds, V2(x, y));
      } else if (tile == 'f') {
        remove_enemy(&level->enemies, V2(x, y));
      } else if (tile == 'b') {
        remove_enemy(&level->butterflies, V2(x, y));
      }
      level->tiles[y][x] = '*';  // ignore this tile when draw
    }
  }

  // Activate explosion
  bool added = false;
  for (int i = 0; i < COUNT(level->explosions); ++i) {
    Explosion *explosion = &level->explosions[i];
    if (explosion->active) continue;

    explosion->active = true;
    explosion->type = type;
    explosion->pos_start = start;
    explosion->pos_end = end;
    explosion->start_time = time_now();

    if (type == 'f') {
      explosion->duration = 4.0 / 15.0;  // NOTE: based on the animation
    } else if (type == 'b') {
      explosion->duration = 7.0 / 15.0;  // NOTE: based on the animation
    }
    added = true;
    break;
  }
  assert(added);
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

    // Kill enemy
    if (tile_under == 'f' || tile_under == 'b') {
      play_sound(SOUND_EXPLODED);
      play_fall_sound = true;

      add_explosion(level, V2(x, y + 1), tile_under);
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

void draw_tile_px(DrawContext context, v2 src, v2 dst) {
  SDL_Rect src_rect = {src.x, src.y, 32, 32};
  SDL_Rect dst_rect = {context.window_offset.x + dst.x, context.window_offset.y + dst.y,
                       context.tile_size, context.tile_size};
  SDL_RenderCopy(context.renderer, context.texture, &src_rect, &dst_rect);
}

void draw_tile(DrawContext context, v2 src, v2 dst) {
  draw_tile_px(context, src, V2(dst.x * context.tile_size, dst.y * context.tile_size));
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

  SDL_Renderer *renderer =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (renderer == NULL) {
    printf("Couldn't create renderer: %s\n", SDL_GetError());
    return 1;
  }

  // Load texture
  SDL_Texture *texture;
  {
    int width, height, num_channels;
    void *pixels = stbi_load("bd-sprites.png", &width, &height, &num_channels, 0);

    SDL_Rect rect = {0, 0, width, height};

    texture =
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
  }

  Viewport viewport;
  viewport.x = 0;
  viewport.y = 0;
  viewport.width = 30;

  int tile_size = window_width / viewport.width;

  viewport.height = (window_height / tile_size) - 1;  // subtract one for the status bar on top
  viewport.max = V2(LEVEL_WIDTH - viewport.width, LEVEL_HEIGHT - viewport.height);

  // Increase viewport size by one so that we can draw parts of tiles
  viewport.width++;
  viewport.height++;

  v2 window_offset = {};
  window_offset.x = (window_width % tile_size) / 2;  // to adjust tiles
  window_offset.y = (window_height % tile_size) / 2;

  DrawContext draw_context = {renderer, texture, window_offset, tile_size};

  u64 start = time_now();

  bool rock_is_pushed = false;
  u64 rock_start_move_time = start;

  // Init animations
  Animation anim_diamond = {};
  anim_diamond.start_time = start;
  anim_diamond.num_frames = 8;
  anim_diamond.fps = 15;
  anim_diamond.start_frame = V2(0, 320);

  Animation anim_enemy = {};
  anim_enemy.start_time = start;
  anim_enemy.num_frames = 8;
  anim_enemy.fps = 15;
  anim_enemy.start_frame = V2(0, 288);

  Animation anim_enemy_exploded = {};
  anim_enemy_exploded.start_time = start;
  anim_enemy_exploded.num_frames = 4;
  anim_enemy_exploded.fps = 15;
  anim_enemy_exploded.start_frame = V2(32, 0);
  anim_enemy_exploded.times_to_play = 1;

  Animation anim_butterfly = {};
  anim_butterfly.start_time = start;
  anim_butterfly.num_frames = 8;
  anim_butterfly.fps = 15;
  anim_butterfly.start_frame = V2(0, 352);

  Animation anim_butterfly_exploded = {};
  anim_butterfly_exploded.start_time = start;
  anim_butterfly_exploded.num_frames = 7;
  anim_butterfly_exploded.fps = 15;
  anim_butterfly_exploded.start_frame = V2(64, 224);
  anim_butterfly_exploded.times_to_play = 1;

  Animation anim_idle1 = {};
  anim_idle1.start_time = start;
  anim_idle1.num_frames = 8;
  anim_idle1.fps = 15;
  anim_idle1.start_frame = V2(0, 33);

  Animation anim_go_left = {};
  anim_go_left.start_time = start;
  anim_go_left.num_frames = 8;
  anim_go_left.fps = 25;
  anim_go_left.start_frame = V2(0, 128);

  Animation anim_go_right = {};
  anim_go_right.start_time = start;
  anim_go_right.num_frames = 8;
  anim_go_right.fps = 25;
  anim_go_right.start_frame = V2(0, 160);

  Animation anim_idle2 = {};
  anim_idle2.start_time = start;
  anim_idle2.num_frames = 8;
  anim_idle2.fps = 10;
  anim_idle2.start_frame = V2(0, 66);

  Animation anim_idle3 = {};
  anim_idle3.start_time = start;
  anim_idle3.num_frames = 8;
  anim_idle3.fps = 10;
  anim_idle3.start_frame = V2(0, 98);

  Animation anim_exit = {};
  anim_exit.start_time = start;
  anim_exit.num_frames = 2;  // no exit annimation until all diamonds are collected
  anim_exit.fps = 4;
  anim_exit.start_frame = V2(32, 192);

  // Init level
  Level level = {};
  int level_id = 3;
  load_level(&level, level_id);

  u64 player_last_move_time = start;
  u64 drop_last_time = start;
  u64 enemy_move_last_time = start;
  const double kPlayerDelay = 0.1;
  const double kDropDelay = 0.15;
  const double kEnemyMoveDelay = 0.15;
  Input input = {false, false, false, false};

  Animation *player_animation = &anim_idle1;

  int score = 0;

  play_sound(SOUND_BD1);
  int walking_sound_cooldown = 1;

  int is_running = 1;
main_loop:
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
        if (event.key.keysym.sym == 'r') {
          load_level(&level, level_id);
          goto main_loop;
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
      char next_tile = level.tiles[next_player_pos.y][next_player_pos.x];
      if (can_move(&level, next_player_pos)) {
        if (next_tile == 'd') {
          remove_obj(&level.diamonds, next_player_pos);
          level.diamonds_collected += 1;
          score += level.score_per_diamond;
          if (level.diamonds_collected == level.min_diamonds) {
            level.score_per_diamond = 20;
            white_tunnel = true;
            level.can_exit = true;
            play_sound(SOUND_CRACK);
          } else {
            play_sound(SOUND_DIAMOND_COLLECT);
          }
        }

        if (next_tile == 'X') {
          play_sound(SOUND_FINISHED);
          load_level(&level, ++level_id);
          continue;
        }

        SoundId walking_sound = SOUND_WALK_D;
        if (next_tile == '.') {
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

      // Push rock
      if (next_tile == 'r' && can_move_rock(&level, level.player_pos, next_player_pos)) {
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
    }

    // Move viewport
    {
      v2 viewport_pos = {viewport.x / tile_size, viewport.y / tile_size};
      v2 target_pos = viewport_pos;

      int rel_player_x = level.player_pos.x - viewport_pos.x;
      if (rel_player_x >= 20) {
        target_pos.x += rel_player_x - 20;
        if (target_pos.x > viewport.max.x) {
          target_pos.x = viewport.max.x;
        }
      }
      if (rel_player_x <= 9) {
        target_pos.x -= 9 - rel_player_x;
        if (target_pos.x < 0) {
          target_pos.x = 0;
        }
      }

      int rel_player_y = level.player_pos.y - viewport_pos.y;
      if (rel_player_y >= 13) {
        target_pos.y += rel_player_y - 13;
        if (target_pos.y > viewport.max.y) {
          target_pos.y = viewport.max.y;
        }
      }
      if (rel_player_y <= 6) {
        target_pos.y -= 6 - rel_player_y;
        if (target_pos.y < 0) {
          target_pos.y = 0;
        }
      }

      if (viewport.x < target_pos.x * tile_size) {
        viewport.x += tile_size;
      }
      if (viewport.x > target_pos.x * tile_size) {
        viewport.x -= tile_size;
      }
      if (viewport.y < target_pos.y * tile_size) {
        viewport.y += tile_size;
      }
      if (viewport.y > target_pos.y * tile_size) {
        viewport.y -= tile_size;
      }
    }

    // Move enemy
    if (seconds_since(enemy_move_last_time) > kEnemyMoveDelay) {
      enemy_move_last_time = time_now();

      move_enemies(&level, 'f');
      move_enemies(&level, 'b');
    }

    // Drop rocks and diamonds
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

    // Process active explosions
    for (int i = 0; i < COUNT(level.explosions); ++i) {
      Explosion *e = &level.explosions[i];
      if (!e->active) continue;

      if (seconds_since(e->start_time) > e->duration) {
        e->active = false;
        for (int y = e->pos_start.y; y <= e->pos_end.y; ++y) {
          for (int x = e->pos_start.x; x <= e->pos_end.x; ++x) {
            if (e->type == 'f') {
              level.tiles[y][x] = '_';
            } else if (e->type == 'b') {
              level.tiles[y][x] = 'd';

              // Add new diamond to level
              level.diamonds.objects[level.diamonds.num++] = V2(x, y);
              assert(level.diamonds.num < COUNT(level.diamonds.objects));
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
    v2 pos_score = {viewport.width - 7, 0};
    draw_number(draw_context, score, pos_score, COLOR_WHITE, 6);

    // Display time
    v2 pos_time = {viewport.width / 2, 0};
    int time_to_show = level.time_left - (int)(seconds_since(start));
    if (time_to_show < 0) {
      time_to_show = 0;
    }
    draw_number(draw_context, time_to_show, pos_time, COLOR_WHITE, 3);

    // Draw level
    for (int y = 0; y < viewport.height; y++) {
      for (int x = 0; x < viewport.width; x++) {
        v2 src = {0, 192};
        v2 dst = {x * tile_size - viewport.x % tile_size,
                  (y + 1) * tile_size - viewport.y % tile_size};
        char tile_type = level.tiles[viewport.y / tile_size + y][viewport.x / tile_size + x];
        if (tile_type == '*') {
          continue;  // ignore tile completely
        }
        if (tile_type == 'r') {
          src = V2(0, 224);
        } else if (tile_type == 'w') {
          src = V2(96, 192);
        } else if (tile_type == 'W') {
          src = V2(32, 192);
        } else if (tile_type == '.') {
          src = V2(32, 224);
        } else if (tile_type == 'l') {
          src = V2(288, 0);
        } else if (tile_type == '_' && white_tunnel) {
          src = V2(300, 0);
        } else if (tile_type == 'E') {
          src = get_frame(player_animation);
        } else if (tile_type == 'd') {
          src = get_frame(&anim_diamond);
        } else if (tile_type == 'f') {
          src = get_frame(&anim_enemy);
        } else if (tile_type == 'b') {
          src = get_frame(&anim_butterfly);
        } else if (tile_type == 'X') {
          if (level.can_exit) {
            src = get_frame(&anim_exit);
          } else {
            src = V2(32, 192);
          }
        }
        draw_tile_px(draw_context, src, dst);
      }
    }

    // Draw explosions
    for (int i = 0; i < COUNT(level.explosions); ++i) {
      Explosion *e = &level.explosions[i];
      if (!e->active) continue;

      Animation *anim;
      if (e->type == 'f') {
        anim = &anim_enemy_exploded;
      } else if (e->type == 'b') {
        anim = &anim_butterfly_exploded;
      }
      anim->start_time = e->start_time;

      v2 src = get_frame(anim);
      for (int y = e->pos_start.y; y <= e->pos_end.y; ++y) {
        for (int x = e->pos_start.x; x <= e->pos_end.x; ++x) {
          draw_tile_px(draw_context, src,
                       V2(x * tile_size - viewport.x, y * tile_size - viewport.y));
        }
      }
    }

    SDL_RenderPresent(renderer);
    SDL_RenderClear(renderer);

    // {
    //     u64 now = time_now();
    //     double elapsed_ms = (double)(now - start) * 1000 /
    //     gPerformanceFrequency; start = now; printf("MS %.3lf \n",
    //     elapsed_ms);
    // }
  }

  SDL_CloseAudioDevice(audio_device_id);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
