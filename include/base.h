#ifndef BASE_H
#define BASE_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_audio.h>

#define COUNT(arr) (sizeof(arr) / sizeof(*arr))

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

double gPerformanceFrequency;

u64 time_now();
double seconds_since(u64 timestamp);

#endif  // BASE_H
