/* Stub of <SDL.h> for the host compile check.
 *
 * SDL 1.3 has no header of its own on the PS3; ps3libraries installs its
 * headers into portlibs/ppu/include. Only the handful of entry points this
 * project uses are declared. */
#ifndef STUB_SDL_H
#define STUB_SDL_H

#include <ppu-types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
	SDL_INIT_TIMER          = 0x00000001,
	SDL_INIT_AUDIO          = 0x00000010,
	SDL_INIT_VIDEO          = 0x00000020,
	SDL_INIT_JOYSTICK       = 0x00000200,
	SDL_INIT_NOPARACHUTE    = 0x00100000,
	SDL_INIT_EVERYTHING     = 0x0000FFFF
} SDL_InitFlags;

#define SDL_SWSURFACE   0x00000000
#define SDL_HWSURFACE   0x00000001
#define SDL_ASYNCBLIT   0x00000004
#define SDL_ANYFORMAT   0x10000000

typedef struct SDL_Rect
{
	s16 x, y;
	u16 w, h;
} SDL_Rect;

typedef struct SDL_Color
{
	u8 r, g, b, unused;
} SDL_Color;

typedef struct SDL_Palette
{
	int        ncolors;
	SDL_Color *colors;
} SDL_Palette;

typedef struct SDL_PixelFormat
{
	SDL_Palette *palette;
	u8  BitsPerPixel;
	u8  BytesPerPixel;
	u8  Rloss, Gloss, Bloss, Aloss;
	u8  Rshift, Gshift, Bshift, Ashift;
	u32 Rmask, Gmask, Bmask, Amask;
} SDL_PixelFormat;

typedef struct SDL_Surface
{
	u32            flags;
	SDL_PixelFormat *format;
	int            w, h;
	u16            pitch;
	void          *pixels;
} SDL_Surface;

int          SDL_Init(u32 flags);
int          SDL_InitSubSystem(u32 flags);
void         SDL_Quit(void);
void         SDL_QuitSubSystem(u32 flags);
const char  *SDL_GetError(void);
SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, u32 flags);
int          SDL_Flip(SDL_Surface *screen);
int          SDL_LockSurface(SDL_Surface *surface);
void         SDL_UnlockSurface(SDL_Surface *surface);
int          SDL_FillRect(SDL_Surface *dst, SDL_Rect *rect, u32 color);
u32          SDL_MapRGB(const SDL_PixelFormat *fmt, u8 r, u8 g, u8 b);
u32          SDL_MapRGBA(const SDL_PixelFormat *fmt, u8 r, u8 g, u8 b, u8 a);

#ifdef __cplusplus
}
#endif

#endif