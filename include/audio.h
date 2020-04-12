#ifndef AUDIO_H
#define AUDIO_H

#include "base.h"

typedef enum SoundId {
  SOUND_BD1,
  SOUND_STONE,
  SOUND_DIAMOND_1,
  SOUND_DIAMOND_2,
  SOUND_DIAMOND_3,
  SOUND_DIAMOND_4,
  SOUND_DIAMOND_5,
  SOUND_DIAMOND_6,
  SOUND_DIAMOND_7,
  SOUND_DIAMOND_8,
  SOUND_DIAMOND_COLLECT,
  SOUND_WALK_D,
  SOUND_COVER,
  SOUND_CRACK,
  SOUND_FINISHED,
  SOUND_EXPLODED,
  SOUND_TIMEOUT_1,
  SOUND_TIMEOUT_2,
  SOUND_TIMEOUT_3,
  SOUND_TIMEOUT_4,
  SOUND_TIMEOUT_5,
  SOUND_TIMEOUT_6,
  SOUND_TIMEOUT_7,
  SOUND_TIMEOUT_8,
  SOUND_TIMEOUT_9,
  SOUND_AMOEBA,
  SOUND_WALK_E,
  SOUND_STONE_2,
  SOUND_MAGIC_WALL,
} SoundId;

typedef struct Sound {
  short *samples;
  int len_samples;
} Sound;

typedef struct {
  short *start;
  int cursor;
  int size;  // in samples
  u64 start_time;
  SDL_AudioDeviceID audio_device_id;
  double len_in_seconds;
} AudioBuffer;

SDL_AudioDeviceID init_audio();
void play_sound(SoundId);

#endif  // AUDIO_H
