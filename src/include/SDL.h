/* Minimal source compatibility for GnGeo's software renderer. No SDL runtime. */
#ifndef H1_SDL_H
#define H1_SDL_H
#include <stdint.h>
#include <stdio.h>
typedef uint8_t Uint8; typedef int8_t Sint8;
typedef uint16_t Uint16; typedef int16_t Sint16;
typedef uint32_t Uint32; typedef int32_t Sint32;
typedef unsigned int uint;
typedef struct {Sint16 x,y;Uint16 w,h;} SDL_Rect;
typedef struct {void *pixels; unsigned pitch; int w,h;} SDL_Surface;
typedef void SDL_Joystick;
typedef void SDL_AudioSpec;
#define SDLK_LAST 512
#define SDL_Swap16(x) __builtin_bswap16(x)
#define SDL_Swap32(x) __builtin_bswap32(x)
int SDL_SetClipRect(SDL_Surface *,const SDL_Rect *);
int SDL_FillRect(SDL_Surface *,const SDL_Rect *,Uint32);
int SDL_LockSurface(SDL_Surface *);
void SDL_UnlockSurface(SDL_Surface *);
#endif
