#include "audio.h"
#include "lib/stb_vorbis.c"

// Both will be initialised by init_audio()
static Sound *gSounds;
static AudioBuffer gBuffer;

Sound *load_all_sounds() {
  char *file_names[] = {
      "sounds/bd1.ogg",       "sounds/stone.ogg",           "sounds/diamond_1.ogg",
      "sounds/diamond_2.ogg", "sounds/diamond_3.ogg",       "sounds/diamond_4.ogg",
      "sounds/diamond_5.ogg", "sounds/diamond_6.ogg",       "sounds/diamond_7.ogg",
      "sounds/diamond_8.ogg", "sounds/diamond_collect.ogg", "sounds/walk_d.ogg",
      "sounds/cover.ogg",     "sounds/crack.ogg",           "sounds/finished.ogg",
      "sounds/exploded.ogg",  "sounds/timeout_1.ogg",       "sounds/timeout_2.ogg",
      "sounds/timeout_3.ogg", "sounds/timeout_4.ogg",       "sounds/timeout_5.ogg",
      "sounds/timeout_6.ogg", "sounds/timeout_7.ogg",       "sounds/timeout_8.ogg",
      "sounds/timeout_9.ogg", "sounds/amoeba.ogg",          "sounds/walk_e.ogg",
      "sounds/stone_2.ogg",   "sounds/magic_wall.ogg",
  };

  Sound *sounds = malloc(sizeof(Sound) * COUNT(file_names));

  for (int i = 0; i < COUNT(file_names); ++i) {
    Sound *sound = sounds + i;
    char *file_name = file_names[i];

    int channels;
    int sample_rate;
    sound->len_samples =
        stb_vorbis_decode_filename(file_name, &channels, &sample_rate, &sound->samples);
    if (channels == 1) {
      // Make two channels out of one by duplicating each sample
      short *new_samples = malloc(2 * sound->len_samples * sizeof(short));
      for (int i = 0; i < sound->len_samples; ++i) {
        short sample = sound->samples[i];
        new_samples[2 * i] = sample;
        new_samples[2 * i + 1] = sample;
      }
      sound->len_samples *= 2;
      free(sound->samples);
      sound->samples = new_samples;
    }
    assert(channels <= 2);
    assert(sample_rate == 44100);
  }

  return sounds;
}

void audio_callback(void *userdata, u8 *stream, int len) {
  AudioBuffer *buffer = &gBuffer;
  assert((buffer->size * sizeof(short)) % len == 0);
  if (buffer->start_time == 0) {
    buffer->start_time = time_now();  // for the first call
  }
  SDL_memcpy(stream, buffer->start + buffer->cursor, len);
  SDL_memset(buffer->start + buffer->cursor, 0, len);
  buffer->cursor += len / sizeof(short);
  buffer->cursor %= buffer->size;
}

void play_sound(SoundId sound_id) {
  AudioBuffer *buffer = &gBuffer;
  Sound sound = gSounds[sound_id];

  SDL_LockAudioDevice(buffer->audio_device_id);
  // Calculate the write cursor based on how much time has passed relatively to
  // the first audio_callback call
  double buffers_passed = seconds_since(buffer->start_time) / buffer->len_in_seconds;
  double whole_buffers;
  int write_cursor = (int)((modf(buffers_passed, &whole_buffers)) * (double)(buffer->size));
  write_cursor += 10000;  // experimentally obtained to put the write cursor ahead the buffer cursor
  write_cursor %= buffer->size;

  if (write_cursor + sound.len_samples > buffer->size) {
    int chunk1_len_samples = buffer->size - write_cursor;
    int chunk2_len_samples = sound.len_samples - chunk1_len_samples;
    SDL_MixAudioFormat((u8 *)(buffer->start + write_cursor), (u8 *)sound.samples, AUDIO_S16,
                       chunk1_len_samples * sizeof(short), SDL_MIX_MAXVOLUME);
    SDL_MixAudioFormat((u8 *)buffer->start, (u8 *)(sound.samples + chunk1_len_samples), AUDIO_S16,
                       chunk2_len_samples * sizeof(short), SDL_MIX_MAXVOLUME);
  } else {
    SDL_MixAudioFormat((u8 *)(buffer->start + write_cursor), (u8 *)sound.samples, AUDIO_S16,
                       sound.len_samples * sizeof(short), SDL_MIX_MAXVOLUME);
  }
  SDL_UnlockAudioDevice(buffer->audio_device_id);
}

// Returns audiodevice id on success, returns 0 on error
SDL_AudioDeviceID init_audio() {
  gSounds = load_all_sounds();

  const int kFrequency = 44100;  // Sample frames per second
  const int kChannels = 2;
  const int kCallbackSampleFrames = 2048;
  const int kCallbackBuffersPerMinute =
      (int)(((double)kFrequency / (double)kCallbackSampleFrames) * 30.0);  // approximately

  // Create audio device
  SDL_AudioSpec desired_audiospec = {};
  SDL_AudioSpec audiospec = {};
  desired_audiospec.freq = kFrequency;
  desired_audiospec.format = AUDIO_S16;
  desired_audiospec.channels = kChannels;
  desired_audiospec.samples = kCallbackSampleFrames;
  desired_audiospec.callback = audio_callback;

  SDL_AudioDeviceID audio_device_id =
      SDL_OpenAudioDevice(NULL, 0, &desired_audiospec, &audiospec, 0);
  if (audio_device_id == 0) {
    printf("Failed to open audio: %s\n", SDL_GetError());
    return 0;
  }

  // Init buffer
  gBuffer.size =
      kCallbackBuffersPerMinute * kCallbackSampleFrames * kChannels;  // enough samples for a minute
  gBuffer.start = malloc(gBuffer.size * sizeof(short));
  SDL_memset(gBuffer.start, 0, gBuffer.size);
  gBuffer.cursor = 0;
  gBuffer.len_in_seconds = (double)gBuffer.size / (double)(kFrequency * kChannels);
  gBuffer.start_time = 0;  // invalid time, will be replaced by audio_callback()
  gBuffer.audio_device_id = audio_device_id;

  SDL_PauseAudioDevice(audio_device_id, 0);

  return audio_device_id;
}
