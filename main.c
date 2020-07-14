#include <SDL2/SDL_rect.h>
#include <SDL2/SDL_render.h>
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "audio.h"
#include "base.h"
#include "levels.h"
#include "lib/stb_image.h"

// ======================================= Types ===================================================

typedef enum StateId {
  MENU,
  LEVEL_STARTING,
  LEVEL_GAMEPLAY,
  LEVEL_ENDING,
  PLAYER_DYING,
  OUT_OF_TIME,
  YOU_WIN,
  QUIT_GAME,
} StateId;

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

  bool quit;
  bool reset;
  bool pickup;  // collect diamond without moving with Ctrl
} Input;

typedef struct {
  int x;
  int y;
} v2;

typedef struct Stone {
  v2 pos;
  bool falling;
} Stone;

static inline v2 V2(int x, int y) {
  v2 result = {x, y};
  return result;
}

static inline v2 sum_v2(v2 a, v2 b) {
  return V2(a.x + b.x, a.y + b.y);
}

typedef struct Rect {
  int left;
  int top;
  int right;
  int bottom;
} Rect;

static inline Rect create_rect(int left, int top, int right, int bottom) {
  Rect result = {left, top, right, bottom};
  return result;
}

typedef struct Objects {
  Stone objects[LEVEL_WIDTH * LEVEL_HEIGHT / 3];
  int num;
} Objects;

typedef struct Waters {
  v2 pos[LEVEL_WIDTH * LEVEL_HEIGHT / 3];
  int num;
} Waters;

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

typedef struct MagicWall {
  v2 bricks[20];
  u64 start_time;
  int num;
  bool is_on;
} MagicWall;

typedef struct {
  v2 start_frame;
  u64 start_time;
  int num_frames;
  int fps;
  int times_to_play;  // how many times to play animation in total; 0 if indefinitely
} Animation;

typedef struct AnimationMoving {
  v2 start_frame;
  v2 end_frame;
  u64 start_time;
  double duration;  // in seconds
} AnimationMoving;

typedef struct Explosion {
  bool active;
  char type;
  Rect area;
  u64 start_time;
  double duration;  // in seconds
} Explosion;

typedef struct {
  SDL_Renderer *renderer;
  SDL_Texture *texture;
  v2 window_offset;
} DrawContext;

typedef struct Level {
  Tiles tiles;
  Objects diamonds;
  Objects rocks;
  Enemies enemies;
  Enemies butterflies;
  Lock locks[10];
  Explosion explosions[5];
  Waters waters;
  v2 player_pos;  // in tiles
  v2 enemy_pos;
  MagicWall magic_wall;
  int time_left;
  int score_per_diamond;
  int min_diamonds;
  int diamonds_collected;
} Level;

typedef struct Viewport {
  // in pixels
  int x;
  int y;
  v2 max;

  // in tiles
  int width;
  int height;

  // Viewport will move if player moves outside this area
  Rect player_area;
} Viewport;

typedef enum AnimationId {
  ANIM_DIAMOND,
  ANIM_ENEMY,
  ANIM_ENEMY_EXPLODED,
  ANIM_BUTTERFLY,
  ANIM_BUTTERFLY_EXPLODED,
  ANIM_IDLE1,
  ANIM_GO_LEFT,
  ANIM_GO_RIGHT,
  ANIM_IDLE2,
  ANIM_IDLE3,
  ANIM_EXIT,
  ANIM_PLAYER_HERE,
  ANIM_WATER,
  ANIM_MAGIC_WALL,

  ANIM_COUNT,
} AnimationId;

typedef struct GameState {
  Level level;
  DrawContext draw_context;
  Viewport viewport;
  StateId state_id;
  int level_id;
  int score;
} GameState;

// ======================================= Globals =================================================

Animation gAnimations[ANIM_COUNT] = {
    {{0, 320}, 0, 8, 15, 0},   // ANIM_DIAMOND,
    {{0, 288}, 0, 8, 15, 0},   // ANIM_ENEMY,
    {{32, 0}, 0, 4, 15, 1},    // ANIM_ENEMY_EXPLODED,
    {{0, 352}, 0, 8, 15, 0},   // ANIM_BUTTERFLY,
    {{64, 224}, 0, 7, 15, 1},  // ANIM_BUTTERFLY_EXPLODED,
    {{0, 33}, 0, 8, 15, 0},    // ANIM_IDLE1,
    {{0, 128}, 0, 8, 25, 0},   // ANIM_GO_LEFT,
    {{0, 160}, 0, 8, 25, 0},   // ANIM_GO_RIGHT,
    {{0, 66}, 0, 8, 10, 0},    // ANIM_IDLE2,
    {{0, 98}, 0, 8, 10, 0},    // ANIM_IDLE3,
    {{32, 192}, 0, 2, 4, 0},   // ANIM_EXIT,
    {{32, 0}, 0, 3, 3, 0},     // ANIM_PLAYER_HERE,
    {{0, 256}, 0, 8, 25, 0},   // ANIM_WATER,
    {{96, 192}, 0, 5, 20, 0},  // ANIM_MAGIC_WALL,
};

int gTileSize;

// ======================================= Functions ===============================================

u64 time_now() {
  return SDL_GetPerformanceCounter();
}

double seconds_since(u64 timestamp) {
  return (double)(time_now() - timestamp) / gPerformanceFrequency;
}

void process_input(Input *input) {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_QUIT) {
      input->quit = true;
    }
    if (event.type == SDL_KEYUP) {
      if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
        input->quit = true;
      }
    }

    if (event.type == SDL_KEYDOWN) {
      if (event.key.keysym.scancode == SDL_SCANCODE_RIGHT) {
        input->right = true;
      }
      if (event.key.keysym.scancode == SDL_SCANCODE_LEFT) {
        input->left = true;
      }
      if (event.key.keysym.scancode == SDL_SCANCODE_UP) {
        input->up = true;
      }
      if (event.key.keysym.scancode == SDL_SCANCODE_DOWN) {
        input->down = true;
      }
      if (event.key.keysym.scancode == SDL_SCANCODE_LCTRL) {
        input->pickup = true;
      }
    }

    if (event.type == SDL_KEYUP) {
      if (event.key.keysym.scancode == SDL_SCANCODE_RIGHT) {
        input->right = false;
      }
      if (event.key.keysym.scancode == SDL_SCANCODE_LEFT) {
        input->left = false;
      }
      if (event.key.keysym.scancode == SDL_SCANCODE_UP) {
        input->up = false;
      }
      if (event.key.keysym.scancode == SDL_SCANCODE_DOWN) {
        input->down = false;
      }
      if (event.key.keysym.sym == 'r') {
        input->reset = true;
      }
      if (event.key.keysym.scancode == SDL_SCANCODE_LCTRL) {
        input->pickup = false;
      }
    }
  }
}

void add_water(Level *level, int x, int y) {
  level->waters.pos[level->waters.num].x = x;
  level->waters.pos[level->waters.num].y = y;
  level->waters.num++;
  level->tiles[y][x] = 'a';
}

void load_level(Level *level, int num_level) {
  SDL_memset(level, 0, sizeof(*level));
  SDL_memcpy(level->tiles, gLevels[num_level], LEVEL_HEIGHT * LEVEL_WIDTH);

  level->magic_wall.num = 0;
  level->magic_wall.is_on = false;

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
        level->rocks.objects[level->rocks.num].pos.x = x;
        level->rocks.objects[level->rocks.num].pos.y = y;
        level->rocks.objects[level->rocks.num].falling = false;
        level->rocks.num++;
        assert(level->rocks.num < COUNT(level->rocks.objects));
      }

      if (level->tiles[y][x] == 'd') {
        level->diamonds.objects[level->diamonds.num].pos.x = x;
        level->diamonds.objects[level->diamonds.num].pos.y = y;
        level->diamonds.objects[level->diamonds.num].falling = false;
        level->diamonds.num++;
        assert(level->diamonds.num < COUNT(level->diamonds.objects));
      }

      if (level->tiles[y][x] == 'a') {
        add_water(level, x, y);
      }

      if (level->tiles[y][x] == 'm') {
        level->magic_wall.bricks[level->magic_wall.num].x = x;
        level->magic_wall.bricks[level->magic_wall.num].y = y;
        level->magic_wall.num++;
      }
    }
  }

  level->time_left = 150;
  level->score_per_diamond = 10;
  level->min_diamonds = gLevel_min_diamonds[num_level];
  level->diamonds_collected = 0;
}

v2 lerp(v2 vec1, v2 vec2, double t) {
  int x = (int)(vec1.x * (1 - t) + vec2.x * t);
  int y = (int)(vec1.y * (1 - t) + vec2.y * t);
  return V2(x, y);
}

v2 get_frame_from(u64 start_time, AnimationId anim_id) {
  Animation *animation = &gAnimations[anim_id];
  int frames_played_since_start = (int)(seconds_since(start_time) * animation->fps);
  int frame_index = frames_played_since_start % animation->num_frames;
  if (animation->times_to_play > 0 &&
      frames_played_since_start / animation->num_frames > animation->times_to_play) {
    frame_index = animation->num_frames - 1;
  }
  return V2(animation->start_frame.x + frame_index * 32, animation->start_frame.y);
}

v2 get_frame(AnimationId anim_id) {
  Animation *animation = &gAnimations[anim_id];
  return get_frame_from(animation->start_time, anim_id);
}

v2 get_moving_frame() {
  AnimationMoving anim = {{97, 476}, {129, 444}, 0, 0.8};
  double cycles_passed = seconds_since(anim.start_time) / anim.duration;
  double whole_cycles;
  double part_cycle = modf(cycles_passed, &whole_cycles);
  return lerp(anim.start_frame, anim.end_frame, part_cycle);
}

v2 turn_right(v2 direction) {
  return V2(-direction.y, direction.x);
}

v2 turn_left(v2 direction) {
  return V2(direction.y, -direction.x);
}

bool out_of_bounds(v2 pos) {
  return (pos.x < 0 || pos.x >= LEVEL_WIDTH || pos.y < 0 || pos.y >= LEVEL_HEIGHT);
}

bool can_move(Level *level, v2 pos) {
  if (out_of_bounds(pos)) {
    return false;
  }
  char tile_type = level->tiles[pos.y][pos.x];
  if (tile_type == ' ' || tile_type == '.' || tile_type == '_' || tile_type == 'd' ||
      (tile_type == 'x')) {
    return true;
  }
  return false;
}

bool enemy_can_move(Level *level, v2 pos) {
  if (out_of_bounds(pos)) {
    return false;
  }
  char tile_type = level->tiles[pos.y][pos.x];
  if (tile_type == '_' || tile_type == ' ' || tile_type == 'p') {
    return true;
  }
  return false;
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
    if (objs->objects[i].pos.x == pos.x && objs->objects[i].pos.y == pos.y) {
      v2 *pos = &objs->objects[i].pos;
      v2 *pos_lst = &objs->objects[objs->num - 1].pos;
      *pos = *pos_lst;
      objs->num -= 1;
    }
  }
}

void add_obj(Objects *objs, v2 pos) {
  objs->objects[objs->num++].pos = V2(pos.x, pos.y);
  assert(objs->num < COUNT(objs->objects));
}

void add_diamond(Level *level, int x, int y) {
  level->tiles[y][x] = 'd';
  level->diamonds.objects[level->diamonds.num++].pos = V2(x, y);
  assert(level->diamonds.num < COUNT(level->diamonds.objects));
}

void run_magic_wall(Level *level) {
  for (int i = 0; i < level->magic_wall.num; i++) {
    level->tiles[level->magic_wall.bricks[i].y][level->magic_wall.bricks[i].x] = 'M';
  }
  level->magic_wall.start_time = time_now();
  level->magic_wall.is_on = true;
  play_looped_sound(SOUND_MAGIC_WALL);
}

void stop_magic_wall(Level *level) {
  for (int i = 0; i < level->magic_wall.num; i++) {  // 20 number of bricks for magic wall for level
    level->tiles[level->magic_wall.bricks[i].y][level->magic_wall.bricks[i].x] = 'm';
  }
  level->magic_wall.start_time = 0;
  level->magic_wall.is_on = false;
  stop_looped_sounds();
}

void add_explosion(Level *level, v2 pos, char type) {
  assert(type == 'f' || type == 'b' || type == 'p');

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

  Rect area = create_rect(start.x, start.y, end.x, end.y);

  // Remove objects and set tiles
  for (int y = area.top; y <= area.bottom; ++y) {
    for (int x = area.left; x <= area.right; ++x) {
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
    explosion->area = area;
    explosion->start_time = time_now();

    if (type == 'f' || type == 'p') {
      explosion->duration = 4.0 / 15.0;  // NOTE: based on the animation
    } else if (type == 'b') {
      explosion->duration = 7.0 / 15.0;  // NOTE: based on the animation
    }
    added = true;
    break;
  }
  assert(added);
}

// Return True if enemy kills player
bool move_enemies(Level *level, char obj_sym) {
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

    if (level->tiles[enemy->pos.y][enemy->pos.x] == 'p') {
      play_sound(SOUND_EXPLODED);
      add_explosion(level, V2(enemy->pos.x, enemy->pos.y), 'p');
      return true;
    }

    level->tiles[enemy->pos.y][enemy->pos.x] = obj_sym;  // "draw"
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

// Returns true if player is killed
bool drop_objects(Level *level, char obj_sym) {
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
    Stone *stone = &objs->objects[i];
    int x = stone->pos.x;
    int y = stone->pos.y;
    bool falling = stone->falling;

    assert(level->tiles[y][x] == obj_sym);

    char tile_above = level->tiles[y - 1][x];
    char tile_under = level->tiles[y + 1][x];

    // A falling rock or diamond activate magic wall
    if (tile_under == 'm' && falling && !level->magic_wall.is_on) {
      run_magic_wall(level);
      tile_under = level->tiles[y + 1][x];
      play_sound(SOUND_DIAMOND_1);
    }

    // Kill enemy
    if (tile_under == 'f' || tile_under == 'b') {
      play_sound(SOUND_EXPLODED);
      play_fall_sound = true;
      add_explosion(level, V2(x, y + 1), tile_under);
    }

    // Kill player
    if (falling && tile_under == 'p') {
      play_sound(SOUND_EXPLODED);
      play_fall_sound = true;
      add_explosion(level, V2(x, y + 1), tile_under);
      return true;
    }

    // If there is space in the position below the magic wall then the boulder morphs into a falling
    // diamond and moves down two positions, to be below the magic wall
    if (tile_under == 'M' && level->tiles[y + 2][x] == '_' && falling) {
      stone->pos.y += 2;
      level->tiles[y][x] = '_';

      if (obj_sym == 'r') {  // if rock is falling
        play_sound(SOUND_DIAMOND_1);
        remove_obj(&level->rocks, V2(x, y + 2));  // remove rock
        level->tiles[y + 2][x] = 'd';
        add_obj(&level->diamonds, V2(x, y + 2));
        obj_sym = 'd';
        objs = &level->diamonds;
      } else if (obj_sym == 'd') {  // if diamond is falling
        play_sound(SOUND_STONE);
        remove_obj(&level->diamonds, V2(x, y + 2));  // remove diamond
        add_obj(&level->rocks, V2(x, y + 2));
        level->tiles[y + 2][x] = 'r';
        obj_sym = 'r';
        objs = &level->rocks;
      }
    }

    // Otherwise the rock simply disappears
    if (tile_under == 'M' && level->tiles[y + 2][x] != '_') {
      level->tiles[y][x] = '_';

      if (obj_sym == 'r') {
        remove_obj(&level->rocks, V2(x, y));  // remove rock
        continue;
      }
    }

    if (tile_under == '_') {
      stone->falling = true;

      // Drop down
      level->tiles[y][x] = '_';
      level->tiles[y + 1][x] = obj_sym;
      stone->pos.y += 1;

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
    } else {
      stone->falling = false;
    }

    // Slide off rocks and diamonds
    if ((tile_under == 'r' || tile_under == 'd' || tile_under == 'w') &&
        (tile_above != 'd' && tile_above != 'r' && tile_above != 'l')) {
      if (level->tiles[y][x - 1] == '_' && level->tiles[y + 1][x - 1] == '_') {
        // Drop left
        level->tiles[y][x] = 'l';
        add_lock(level->locks, x, y);
        level->tiles[y][x - 1] = obj_sym;
        stone->pos.x -= 1;
        continue;
      }
      if (level->tiles[y][x + 1] == '_' && level->tiles[y + 1][x + 1] == '_') {
        // Drop right
        level->tiles[y][x] = 'l';
        add_lock(level->locks, x, y);
        level->tiles[y][x + 1] = obj_sym;
        stone->pos.x += 1;
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

  return false;
}

void draw_tile_px(DrawContext *context, v2 src, v2 dst) {
  SDL_Rect src_rect = {src.x, src.y, 32, 32};
  SDL_Rect dst_rect = {context->window_offset.x + dst.x, context->window_offset.y + dst.y,
                       gTileSize, gTileSize};
  SDL_RenderCopy(context->renderer, context->texture, &src_rect, &dst_rect);
}

void draw_tile(DrawContext *context, v2 src, v2 dst) {
  draw_tile_px(context, src, V2(dst.x * gTileSize, dst.y * gTileSize));
}

void draw_outside_border(DrawContext *context, Viewport *viewport) {
  int top_border = -1;
  int bottom_border = viewport->height - 1;
  v2 black = V2(128, 0);  // V2(128, 0) white
  for (int x = 0; x < (viewport->width - 1); x++) {
    draw_tile(context, black, V2(x, top_border));
    draw_tile(context, black, V2(x, bottom_border));
  }

  int left_border = -1;
  int right_border = viewport->width - 1;
  for (int y = 0; y < (viewport->height - 1); y++) {
    draw_tile(context, V2(128, 0), V2(left_border, y));
    draw_tile(context, V2(128, 0), V2(right_border, y));
  }
}

void draw_number(DrawContext *context, int num, v2 pos, Color color, int min_digits) {
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

void draw_char(DrawContext *context, v2 pos, char letter) {
  int num_letter = tolower(letter) - tolower('a');
  v2 src = {288, 529 + num_letter * 16};
  v2 dst = {pos.x, pos.y};
  SDL_Rect src_rect = {src.x, src.y, 32, 16};
  SDL_Rect dst_rect = {context->window_offset.x + dst.x, context->window_offset.y + dst.y,
                       gTileSize, gTileSize};
  SDL_RenderCopy(context->renderer, context->texture, &src_rect, &dst_rect);
}

void draw_status_bar(GameState *state) {
  Viewport *viewport = &state->viewport;
  DrawContext *draw_context = &state->draw_context;
  Level *level = &state->level;

  // Draw status bar's background black
  for (int x = 0; x < (viewport->width - 1); x++) {
    draw_tile(draw_context, V2(128, 0), V2(x, 0));
  }

  if (state->state_id == OUT_OF_TIME) {
    // Write 'Out of time'
    int start = 10;
    char text[11] = "OUT OF TIME";
    for (int i = 0; i < sizeof(text); i++) {
      draw_char(draw_context, V2((start + i) * gTileSize, 0), text[i]);
    }
    return;
  }

  // Display overall score
  v2 pos_score = {viewport->width - 7, 0};
  draw_number(draw_context, state->score, pos_score, COLOR_WHITE, 6);

  if (state->state_id == LEVEL_STARTING) return;

  // Display time
  v2 pos_time = {viewport->width / 2, 0};
  draw_number(draw_context, level->time_left, pos_time, COLOR_WHITE, 3);

  if (state->state_id == LEVEL_ENDING) return;

  // Display number of diamonds to collect
  v2 white_diamond = {256, 32};
  draw_tile(draw_context, white_diamond, V2(2, 0));

  if (level->diamonds_collected < level->min_diamonds) {
    draw_number(draw_context, level->min_diamonds, V2(0, 0), COLOR_YELLOW, 2);
  } else {
    draw_tile(draw_context, white_diamond, V2(0, 0));
    draw_tile(draw_context, white_diamond, V2(1, 0));
  }
  draw_number(draw_context, level->score_per_diamond, V2(3, 0), COLOR_WHITE, 2);

  // Display number of collected diamonds
  v2 pos_diamonds = {10, 0};
  draw_number(draw_context, level->diamonds_collected, pos_diamonds, COLOR_YELLOW, 2);
}

void update_screen(DrawContext *draw_context) {
  SDL_RenderPresent(draw_context->renderer);
  SDL_RenderClear(draw_context->renderer);
}

void move_viewport(Level *level, Viewport *viewport, int step) {
  v2 viewport_pos = {viewport->x / gTileSize, viewport->y / gTileSize};
  v2 target_pos = viewport_pos;

  int rel_player_x = level->player_pos.x - viewport_pos.x;
  if (rel_player_x >= viewport->player_area.right) {
    target_pos.x += rel_player_x - viewport->player_area.right;
    if (target_pos.x > viewport->max.x) {
      target_pos.x = viewport->max.x;
    }
  }
  if (rel_player_x <= viewport->player_area.left) {
    target_pos.x -= viewport->player_area.left - rel_player_x;
    if (target_pos.x < 0) {
      target_pos.x = 0;
    }
  }

  int rel_player_y = level->player_pos.y - viewport_pos.y;
  if (rel_player_y >= viewport->player_area.bottom) {
    target_pos.y += rel_player_y - viewport->player_area.bottom;
    if (target_pos.y > viewport->max.y) {
      target_pos.y = viewport->max.y;
    }
  }
  if (rel_player_y <= viewport->player_area.top) {
    target_pos.y -= viewport->player_area.top - rel_player_y;
    if (target_pos.y < 0) {
      target_pos.y = 0;
    }
  }

  if (viewport->x < target_pos.x * gTileSize) {
    viewport->x += step;
  }
  if (viewport->x > target_pos.x * gTileSize) {
    viewport->x -= step;
  }
  if (viewport->y < target_pos.y * gTileSize) {
    viewport->y += step;
  }
  if (viewport->y > target_pos.y * gTileSize) {
    viewport->y -= step;
  }
}

void draw_explosions(Explosion *explosions, DrawContext *draw_context, Viewport *viewport) {
  for (int i = 0; i < COUNT(&explosions); ++i) {
    Explosion *e = &explosions[i];

    if (seconds_since(e->start_time) > e->duration) {
      e->active = false;
    }
    if (!e->active) continue;

    AnimationId anim;
    if (e->type == 'f' || e->type == 'p') {
      anim = ANIM_ENEMY_EXPLODED;
    } else if (e->type == 'b') {
      anim = ANIM_BUTTERFLY_EXPLODED;
    }

    v2 src = get_frame_from(e->start_time, anim);
    for (int y = e->area.top; y <= e->area.bottom; ++y) {
      for (int x = e->area.left; x <= e->area.right; ++x) {
        draw_tile_px(draw_context, src,
                     V2(x * gTileSize - viewport->x, y * gTileSize - viewport->y));
      }
    }
  }
}

void draw_level(Tiles tiles, DrawContext *draw_context, Viewport *viewport) {
  for (int y = 0; y < viewport->height; y++) {
    for (int x = 0; x < viewport->width; x++) {
      v2 src = {0, 192};
      v2 dst = {x * gTileSize - viewport->x % gTileSize, y * gTileSize - viewport->y % gTileSize};
      char tile_type = tiles[viewport->y / gTileSize + y][viewport->x / gTileSize + x];
      if (tile_type == '*') {
        continue;  // ignore tile completely
      }
      if (tile_type == 'r') {
        src = V2(0, 224);
      } else if (tile_type == 'w' || tile_type == 'm') {
        src = V2(96, 192);
      } else if (tile_type == 'W') {
        src = V2(32, 192);
      } else if (tile_type == 'L') {
        src = get_moving_frame();
      } else if (tile_type == '.') {
        src = V2(32, 224);
      } else if (tile_type == 'E') {
        src = get_frame(ANIM_EXIT);
      } else if (tile_type == 'N') {
        src = get_frame(ANIM_GO_RIGHT);
      } else if (tile_type == 'd') {
        src = get_frame(ANIM_DIAMOND);
      } else if (tile_type == 'f') {
        src = get_frame(ANIM_ENEMY);
      } else if (tile_type == 'b') {
        src = get_frame(ANIM_BUTTERFLY);
      } else if (tile_type == 'X') {
        src = V2(32, 192);
      } else if (tile_type == 'x') {
        src = get_frame(ANIM_EXIT);
      } else if (tile_type == 'S') {
        src = get_frame(ANIM_PLAYER_HERE);
      } else if (tile_type == 'a') {
        src = get_frame(ANIM_WATER);
      } else if (tile_type == 'M') {
        src = get_frame(ANIM_MAGIC_WALL);
      }

      draw_tile_px(draw_context, src, dst);
    }
  }
  draw_outside_border(draw_context, viewport);
}

StateId level_starting(GameState *state) {
  Viewport *viewport = &state->viewport;
  Level *level = &state->level;
  DrawContext *draw_context = &state->draw_context;
  Tiles load_tiles;
  SDL_memcpy(load_tiles, gLoadTiles, LEVEL_HEIGHT * LEVEL_WIDTH);

  Input input = {};

  play_sound(SOUND_COVER);
  load_level(level, state->level_id);

  srand(time(NULL));
  u64 start = time_now();
  bool player_appeared = false;

  while (seconds_since(start) <= 3.5) {
    draw_level(level->tiles, draw_context, viewport);

    process_input(&input);
    if (input.quit) {
      return QUIT_GAME;
    }

    draw_level(load_tiles, draw_context, viewport);
    // Remove 'wall-tile' from tiles of loading picture if random number (0, 99) > 96
    for (int y = 0; y < LEVEL_HEIGHT; y++) {
      for (int x = 0; x < LEVEL_WIDTH; x++) {
        char *tile = &load_tiles[y][x];
        if (*tile != 'L') continue;
        if ((rand() % 100) > 96) {
          *tile = '*';
        }
      }
    }

    move_viewport(level, viewport, 4);

    if (seconds_since(start) > 3.0 && !player_appeared) {
      v2 pos = level->player_pos;
      level->tiles[pos.y][pos.x] = 'S';  // add 'bomb' animation before player is appeared
      play_sound(SOUND_CRACK);
      player_appeared = true;
    }

    draw_status_bar(state);
    update_screen(draw_context);
  }
  return LEVEL_GAMEPLAY;
}

StateId level_ending(GameState *state) {
  const double kScorePlusDelay = 0.02;
  Level *level = &state->level;
  DrawContext *draw_context = &state->draw_context;
  Input input = {};
  u64 start = time_now();
  u64 score_plus_last_time = start;

  play_sound(SOUND_FINISHED);
  stop_looped_sounds();

  while ((seconds_since(start) < 3.0) || (level->time_left > 0)) {
    draw_level(level->tiles, draw_context, &state->viewport);
    draw_status_bar(state);

    if (seconds_since(score_plus_last_time) > kScorePlusDelay) {
      score_plus_last_time = time_now();
      level->time_left--;
      state->score += 5;
    }

    process_input(&input);
    if (input.quit) {
      return QUIT_GAME;
    }

    update_screen(draw_context);
  }

  state->level_id++;
  if (state->level_id >= sizeof(gLevels)) {
    return QUIT_GAME;  // TODO; you won!
  }
  return LEVEL_STARTING;
}

StateId player_dying(GameState *state) {
  Level *level = &state->level;
  DrawContext *draw_context = &state->draw_context;
  Input input = {};
  u64 start = time_now();
  stop_looped_sounds();

  while (seconds_since(start) < 2.5) {
    draw_level(level->tiles, draw_context, &state->viewport);
    draw_explosions(level->explosions, draw_context, &state->viewport);
    draw_status_bar(state);

    process_input(&input);
    if (input.quit) {
      return QUIT_GAME;
    }

    update_screen(draw_context);
  }
  return LEVEL_STARTING;
}

StateId out_of_time(GameState *state) {
  return player_dying(state);
}

StateId level_gameplay(GameState *state) {
  Level *level = &state->level;
  Viewport *viewport = &state->viewport;
  DrawContext *draw_context = &state->draw_context;

  bool rock_is_pushed = false;
  bool play_sound_water = false;
  u64 start = time_now();
  int level_time = level->time_left;
  u64 rock_start_move_time = start;
  u64 player_last_move_time = start;
  int walking_sound_cooldown = 1;
  u64 drop_last_time = start;
  u64 enemy_last_move_time = start;
  u64 flooding_last_time = start;
  const double kPlayerDelay = 0.1;
  const double kDropDelay = 0.15;
  const double kEnemyMoveDelay = 0.15;
  const double kFloodingDelay = 1.25;

  AnimationId player_animation = ANIM_IDLE1;
  AnimationId previos_direction_anim = ANIM_GO_RIGHT;

  Input input = {};
  while (true) {
    bool white_tunnel = false;
    double frame_time = seconds_since(start);  // for animation

    process_input(&input);
    if (input.quit) {
      return QUIT_GAME;
    }
    if (input.reset) {
      return LEVEL_STARTING;
    }

    // Move player
    if (seconds_since(player_last_move_time) > kPlayerDelay) {
      v2 next_player_pos = level->player_pos;

      if (input.right) {
        next_player_pos.x += 1;
      } else if (input.left) {
        next_player_pos.x -= 1;
      } else if (input.up) {
        next_player_pos.y -= 1;
      } else if (input.down) {
        next_player_pos.y += 1;
      }

      char next_tile = level->tiles[next_player_pos.y][next_player_pos.x];
      if (can_move(level, next_player_pos)) {
        if (next_tile == 'd') {
          remove_obj(&level->diamonds, next_player_pos);
          level->diamonds_collected += 1;
          state->score += level->score_per_diamond;
          if (level->diamonds_collected == level->min_diamonds) {
            level->score_per_diamond = 20;
            white_tunnel = true;
            play_sound(SOUND_CRACK);

            // Player can leave the level
            for (int y = 0; y < LEVEL_HEIGHT; y++) {
              for (int x = 0; x < LEVEL_WIDTH; x++) {
                if (level->tiles[y][x] == 'X') {
                  level->tiles[y][x] = 'x';
                }
              }
            }
          } else {
            play_sound(SOUND_DIAMOND_COLLECT);
          }
        }

        // Level ends. Go to the next level.
        if (next_tile == 'x') {
          level->tiles[next_player_pos.y][next_player_pos.x] = 'N';
          return LEVEL_ENDING;
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

        if (input.pickup) {
          // Collect diamond or earth without moving with Ctrl
          if (next_tile == 'd' || next_tile == '.') {
            level->tiles[next_player_pos.y][next_player_pos.x] = '_';
          }
        } else {
          // Move player
          level->tiles[level->player_pos.y][level->player_pos.x] = '_';
          level->tiles[next_player_pos.y][next_player_pos.x] = 'p';
          level->player_pos = next_player_pos;
        }
        player_last_move_time = time_now();
      }

      // Push rock
      if (next_tile == 'r' && can_move_rock(level, level->player_pos, next_player_pos)) {
        if (!rock_is_pushed) {
          rock_start_move_time = time_now();
          rock_is_pushed = true;
        } else if (seconds_since(rock_start_move_time) > 0.5f) {
          int rock_next_x;
          if (level->player_pos.x < next_player_pos.x) {
            rock_next_x = next_player_pos.x + 1;
          } else {
            rock_next_x = next_player_pos.x - 1;
          }

          level->tiles[level->player_pos.y][level->player_pos.x] = '_';
          level->tiles[next_player_pos.y][next_player_pos.x] = 'p';

          for (int i = 0; i < level->rocks.num; i++) {
            if (level->rocks.objects[i].pos.x == next_player_pos.x &&
                level->rocks.objects[i].pos.y == next_player_pos.y) {
              level->rocks.objects[i].pos.x = rock_next_x;
              level->tiles[next_player_pos.y][rock_next_x] = 'r';
              break;
            }
          }
          level->player_pos = next_player_pos;
        }
      }

      if (level->player_pos.x == next_player_pos.x) {
        rock_start_move_time = time_now();
        rock_is_pushed = false;
      }
    }

    move_viewport(level, viewport, gTileSize);

    // Flooding
    if (level->waters.num > 0 && seconds_since(flooding_last_time) > kFloodingDelay) {
      flooding_last_time = time_now();
      if (!play_sound_water) {
        play_looped_sound(SOUND_AMOEBA);
        play_sound_water = true;
      }

      bool expanded = false;
      for (int i = 0; i < level->waters.num; i++) {
        v2 water_pos = level->waters.pos[i];

        v2 neighbours[4] = {
            sum_v2(water_pos, V2(-1, 0)),
            sum_v2(water_pos, V2(1, 0)),
            sum_v2(water_pos, V2(0, -1)),
            sum_v2(water_pos, V2(0, 1)),
        };

        for (int j = 0; j < 4; j++) {
          v2 pos = neighbours[j];
          if (out_of_bounds(pos)) continue;
          char tile = level->tiles[pos.y][pos.x];
          if (tile == '_' || tile == '.') {
            add_water(level, pos.x, pos.y);
            expanded = true;
            break;
          }
        }
        if (expanded) break;  // only add one tile of water at a time
      }
      if (!expanded) {
        for (int i = 0; i < level->waters.num; i++) {
          int x = level->waters.pos[i].x;
          int y = level->waters.pos[i].y;
          level->tiles[y][x] = 'd';
          add_obj(&level->diamonds, V2(x, y));
        }
        level->waters.num = 0;  // disable flooding
      }
    }

    // Move enemy
    if (seconds_since(enemy_last_move_time) > kEnemyMoveDelay) {
      enemy_last_move_time = time_now();

      bool player_killed = move_enemies(level, 'f') || move_enemies(level, 'b');
      if (player_killed) {
        return PLAYER_DYING;
      }
    }

    // Drop rocks and diamonds
    if (seconds_since(drop_last_time) > kDropDelay) {
      drop_last_time = time_now();
      if (drop_objects(level, 'r') || drop_objects(level, 'd')) {
        return PLAYER_DYING;
      }

      // Clear locks
      for (int i = 0; i < COUNT(level->locks); i++) {
        Lock *lock = &level->locks[i];
        if (lock->lifetime > 0) {
          lock->lifetime--;
          if (lock->lifetime == 0) {
            if (level->tiles[lock->pos.y][lock->pos.x] == 'l') {
              level->tiles[lock->pos.y][lock->pos.x] = '_';
            }
          }
        }
      }
    }

    // Process active explosions
    for (int i = 0; i < COUNT(level->explosions); ++i) {
      Explosion *e = &level->explosions[i];
      if (!e->active) continue;

      if (seconds_since(e->start_time) > e->duration) {
        e->active = false;
        for (int y = e->area.top; y <= e->area.bottom; ++y) {
          for (int x = e->area.left; x <= e->area.right; ++x) {
            if (e->type == 'f') {
              level->tiles[y][x] = '_';
            } else if (e->type == 'b') {
              level->tiles[y][x] = 'd';
              add_obj(&level->diamonds, V2(x, y));
            }
          }
        }
      }
    }

    // Choose player animation
    if (seconds_since(player_last_move_time) > 5) {
      if (seconds_since(player_last_move_time) > 10) {
        player_animation = ANIM_IDLE3;
      } else {
        player_animation = ANIM_IDLE2;
      }
    } else if (input.right) {
      player_animation = ANIM_GO_RIGHT;
      previos_direction_anim = ANIM_GO_RIGHT;
    } else if (input.left) {
      player_animation = ANIM_GO_LEFT;
      previos_direction_anim = ANIM_GO_LEFT;
    } else if (input.up || input.down) {
      player_animation = previos_direction_anim;
    } else {
      player_animation = ANIM_IDLE1;
    }

    // Check if time for running magic wall is over
    if (level->magic_wall.is_on && seconds_since(level->magic_wall.start_time) > 30) {
      stop_magic_wall(level);
    }

    // Draw level
    draw_level(level->tiles, draw_context, viewport);

    // Draw player
    draw_tile(draw_context, get_frame(player_animation),
              V2(level->player_pos.x - viewport->x / gTileSize,
                 level->player_pos.y - viewport->y / gTileSize));

    // Draw white tunnel
    if (white_tunnel) {
      for (int y = 0; y < viewport->height; y++) {
        for (int x = 0; x < viewport->width; x++) {
          char tile_type = level->tiles[viewport->y / gTileSize + y][viewport->x / gTileSize + x];
          if (tile_type == '_') {
            draw_tile(draw_context, V2(300, 0), V2(x, y));
          }
        }
      }
    }

    // Draw explosions
    draw_explosions(level->explosions, draw_context, viewport);

    // Time left
    level->time_left = level_time - (int)(seconds_since(start));
    // Time is over
    if (level->time_left < 0) {
      level->time_left = 0;
      return OUT_OF_TIME;
    }

    draw_status_bar(state);
    update_screen(draw_context);

    // {
    //     u64 now = time_now();
    //     double elapsed_ms = (double)(now - start) * 1000 /
    //     gPerformanceFrequency; start = now; printf("MS %.3lf \n",
    //     elapsed_ms);
    // }
  }

  return QUIT_GAME;
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
  viewport.width = 30;
  gTileSize = window_width / viewport.width;
  viewport.height = (window_height / gTileSize);
  viewport.max = V2(LEVEL_WIDTH - viewport.width, LEVEL_HEIGHT - viewport.height);

  // Place viewport not at (0, 0) so it moves nicely on level 0 startup.
  viewport.x = viewport.max.x * gTileSize;
  viewport.y = viewport.max.y * gTileSize;

  viewport.player_area = create_rect(viewport.width / 3, viewport.height / 3,
                                     viewport.width * 2 / 3, viewport.height * 2 / 3);

  // Increase viewport size by one so that we can draw parts of tiles
  viewport.width++;
  viewport.height++;

  v2 window_offset = {};
  window_offset.x = (window_width % gTileSize) / 2;  // to adjust tiles
  window_offset.y = (window_height % gTileSize) / 2;

  DrawContext draw_context = {renderer, texture, window_offset};

  // Persistent game state
  GameState state = {};
  state.score = 0;
  state.level_id = 7;
  state.draw_context = draw_context;
  state.viewport = viewport;
  state.state_id = LEVEL_STARTING;

  bool is_running = true;
  while (is_running) {
    switch (state.state_id) {
      case LEVEL_STARTING: {
        state.state_id = level_starting(&state);
      } break;
      case LEVEL_GAMEPLAY: {
        state.state_id = level_gameplay(&state);
      } break;
      case LEVEL_ENDING: {
        state.state_id = level_ending(&state);
      } break;
      case PLAYER_DYING: {
        state.state_id = player_dying(&state);
      } break;
      case OUT_OF_TIME: {
        state.state_id = out_of_time(&state);
      } break;
      case QUIT_GAME: {
        is_running = false;
      } break;
      default:
        break;
    }
  }

  SDL_CloseAudioDevice(audio_device_id);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
