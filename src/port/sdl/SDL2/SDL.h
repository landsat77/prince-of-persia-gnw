/*
 * Minimal SDL2 compatibility layer for the Game & Watch Prince of Persia core.
 *
 * SDLPoP (external/sdlpop) is written against SDL2. Instead of porting every
 * call site, this header provides the (small) subset of the SDL2 API that
 * SDLPoP really uses, implemented in pop_sdl.c on top of the retro-go
 * platform (LCD, SAI audio, buttons) or on top of the Linux test harness.
 *
 * Only what is needed is implemented. Notable simplifications:
 *  - Supported pixel formats: INDEX8 (paletted sprites), RGB565 (every
 *    "24 bit" or "16 bit" surface, i.e. the screen buffers) and ARGB8888
 *    (temporary surfaces used for colour effects).
 *  - No window / renderer / texture: presenting is done by update_screen()
 *    which hands the 320x200 RGB565 screen to the platform.
 *  - Audio: one callback, pulled synchronously by the platform from
 *    SDL_Delay() / SDL_PollEvent() (no thread, so no locking needed).
 *
 * This file is part of the G&W port and is distributed under the GPLv3 like
 * SDLPoP itself.
 */
#ifndef GNW_POP_SDL_H
#define GNW_POP_SDL_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Basic types                                                        */
/* ------------------------------------------------------------------ */
typedef uint8_t  Uint8;
typedef int8_t   Sint8;
typedef uint16_t Uint16;
typedef int16_t  Sint16;
typedef uint32_t Uint32;
typedef int32_t  Sint32;
typedef uint64_t Uint64;
typedef int64_t  Sint64;

typedef enum { SDL_FALSE = 0, SDL_TRUE = 1 } SDL_bool;

#define SDL_LIL_ENDIAN  1234
#define SDL_BIG_ENDIAN  4321
#define SDL_BYTEORDER   SDL_LIL_ENDIAN

#define SDL_COMPILE_TIME_ASSERT(name, x) typedef int SDL_dummy_ ## name[(x) * 2 - 1]

#define SDL_SwapLE16(x) ((Uint16)(x))
#define SDL_SwapLE32(x) ((Uint32)(x))
#define SDL_SwapBE16(x) ((Uint16)((((Uint16)(x)) << 8) | (((Uint16)(x)) >> 8)))
#define SDL_SwapBE32(x) ((Uint32)__builtin_bswap32((Uint32)(x)))

#define SDL_memset memset
#define SDL_memcpy memcpy
#define SDL_strlen strlen
#define SDL_free   free
#define SDL_malloc malloc

#define SDL_ENABLE  1
#define SDL_DISABLE 0

#define SDL_ALPHA_OPAQUE      255
#define SDL_ALPHA_TRANSPARENT 0

/* Version */
typedef struct SDL_version { Uint8 major, minor, patch; } SDL_version;
#define SDL_MAJOR_VERSION 2
#define SDL_MINOR_VERSION 0
#define SDL_PATCHLEVEL    22
#define SDL_VERSION(x) do { (x)->major = 2; (x)->minor = 0; (x)->patch = 22; } while (0)
#define SDL_VERSIONNUM(X, Y, Z) ((X)*1000 + (Y)*100 + (Z))
#define SDL_COMPILEDVERSION SDL_VERSIONNUM(SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL)
#define SDL_VERSION_ATLEAST(X, Y, Z) (SDL_COMPILEDVERSION >= SDL_VERSIONNUM(X, Y, Z))
void SDL_GetVersion(SDL_version *ver);

/* ------------------------------------------------------------------ */
/* Init / misc                                                        */
/* ------------------------------------------------------------------ */
#define SDL_INIT_TIMER          0x00000001u
#define SDL_INIT_AUDIO          0x00000010u
#define SDL_INIT_VIDEO          0x00000020u
#define SDL_INIT_JOYSTICK       0x00000200u
#define SDL_INIT_HAPTIC         0x00001000u
#define SDL_INIT_GAMECONTROLLER 0x00002000u
#define SDL_INIT_NOPARACHUTE    0x00100000u

static inline int SDL_Init(Uint32 flags) { (void)flags; return 0; }
static inline int SDL_InitSubSystem(Uint32 flags) { (void)flags; return 0; }
static inline void SDL_Quit(void) {}
const char *SDL_GetError(void);
static inline SDL_bool SDL_SetHint(const char *name, const char *value) { (void)name; (void)value; return SDL_TRUE; }
#define SDL_HINT_RENDER_SCALE_QUALITY "SDL_RENDER_SCALE_QUALITY"
#define SDL_HINT_RENDER_VSYNC "SDL_RENDER_VSYNC"
#define SDL_HINT_IME_SHOW_UI "SDL_IME_SHOW_UI"

#define SDL_MESSAGEBOX_ERROR 0x10
struct SDL_Window;
static inline int SDL_ShowSimpleMessageBox(Uint32 flags, const char *title, const char *message, struct SDL_Window *window)
{ (void)flags; (void)window; printf("%s: %s\n", title, message); return 0; }

/* ------------------------------------------------------------------ */
/* Rects, colours, palettes, pixel formats, surfaces                  */
/* ------------------------------------------------------------------ */
typedef struct SDL_Rect { int x, y, w, h; } SDL_Rect;
typedef struct SDL_Color { Uint8 r, g, b, a; } SDL_Color;
#define SDL_Colour SDL_Color

typedef struct SDL_Palette {
    int ncolors;
    SDL_Color *colors;
    Uint32 version;
    int refcount;
} SDL_Palette;

enum {
    SDL_PIXELFORMAT_UNKNOWN  = 0,
    SDL_PIXELFORMAT_INDEX8   = 0x13000801u,
    SDL_PIXELFORMAT_RGB565   = 0x15151002u,
    SDL_PIXELFORMAT_RGB24    = 0x17101803u,
    SDL_PIXELFORMAT_ARGB8888 = 0x16362004u,
};
#define SDL_ISPIXELFORMAT_INDEXED(f) ((f) == SDL_PIXELFORMAT_INDEX8)

typedef struct SDL_PixelFormat {
    Uint32 format;
    SDL_Palette *palette;
    Uint8 BitsPerPixel;
    Uint8 BytesPerPixel;
    Uint8 padding[2];
    Uint32 Rmask, Gmask, Bmask, Amask;
} SDL_PixelFormat;

typedef enum {
    SDL_BLENDMODE_NONE  = 0x00000000,
    SDL_BLENDMODE_BLEND = 0x00000001,
    SDL_BLENDMODE_ADD   = 0x00000002,
    SDL_BLENDMODE_MOD   = 0x00000004,
} SDL_BlendMode;

#define SDL_SWSURFACE 0
#define SDL_PREALLOC  0x00000001  /* pixels not owned by the surface (e.g. in flash) */
#define SDL_SRCCOLORKEY 0x00001000
#define SDL_SRCALPHA    0x00010000

typedef struct SDL_Surface {
    Uint32 flags;
    SDL_PixelFormat *format;
    int w, h;
    int pitch;
    void *pixels;
    void *userdata;
    int locked;
    SDL_Rect clip_rect;
    int refcount;
    /* private (blit state) */
    SDL_PixelFormat fmt_storage;
    Uint8 has_colorkey;
    Uint8 alpha_mod;
    Uint8 blend_mode;
    Uint8 pad;
    Uint32 colorkey;
} SDL_Surface;

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
                                  Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask);
SDL_Surface *SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int width, int height, int depth, Uint32 format);
SDL_Surface *SDL_CreateRGBSurfaceFrom(void *pixels, int width, int height, int depth, int pitch,
                                      Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask);
void SDL_FreeSurface(SDL_Surface *surface);
static inline int SDL_LockSurface(SDL_Surface *s) { (void)s; return 0; }
static inline void SDL_UnlockSurface(SDL_Surface *s) { (void)s; }
int SDL_SetColorKey(SDL_Surface *surface, int flag, Uint32 key);
int SDL_GetColorKey(SDL_Surface *surface, Uint32 *key);
int SDL_SetSurfaceBlendMode(SDL_Surface *surface, SDL_BlendMode blendMode);
int SDL_SetSurfaceAlphaMod(SDL_Surface *surface, Uint8 alpha);
int SDL_SetSurfacePalette(SDL_Surface *surface, SDL_Palette *palette);
SDL_bool SDL_SetClipRect(SDL_Surface *surface, const SDL_Rect *rect);
int SDL_UpperBlit(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
#define SDL_BlitSurface SDL_UpperBlit
int SDL_UpperBlitScaled(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
#define SDL_BlitScaled SDL_UpperBlitScaled
int SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color);
SDL_Surface *SDL_ConvertSurface(SDL_Surface *src, const SDL_PixelFormat *fmt, Uint32 flags);
SDL_Surface *SDL_ConvertSurfaceFormat(SDL_Surface *src, Uint32 pixel_format, Uint32 flags);
Uint32 SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b);
Uint32 SDL_MapRGBA(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
SDL_Palette *SDL_AllocPalette(int ncolors);
void SDL_FreePalette(SDL_Palette *palette);
int SDL_SetPaletteColors(SDL_Palette *palette, const SDL_Color *colors, int firstcolor, int ncolors);
const char *SDL_GetPixelFormatName(Uint32 format);

/* G&W helpers (not SDL API) */
/* Indexed surface whose pixels live elsewhere (flash) and whose palette is
 * shared (reference counted) with other surfaces of the same sprite table. */
SDL_Surface *GNW_CreateIndexedSurfaceFrom(const void *pixels, int width, int height, int pitch, SDL_Palette *palette);
/* New surface holding the horizontal mirror of src (same format, shared palette). */
SDL_Surface *GNW_FlipSurfaceH(SDL_Surface *src);
/* Draw every pixel of an indexed surface that is not 0 with `color` (a pixel
 * value of dst's format), clipped by dst's clip rect. */
void GNW_BlitMono(SDL_Surface *src, int x, int y, SDL_Surface *dst, Uint32 color);
/* dst = max(src - sub8, 0) on each 8 bit colour channel (palette fades). */
void GNW_FadeSurface(SDL_Surface *dst, SDL_Surface *src, int sub8);

/* ------------------------------------------------------------------ */
/* Window / renderer / texture (unused on G&W, stubbed)               */
/* ------------------------------------------------------------------ */
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_RendererInfo { const char *name; Uint32 flags; } SDL_RendererInfo;
#define SDL_WINDOWPOS_UNDEFINED 0x1FFF0000u
#define SDL_WINDOW_FULLSCREEN_DESKTOP 0x00001001u
#define SDL_WINDOW_RESIZABLE 0x00000020u
#define SDL_WINDOW_ALLOW_HIGHDPI 0x00002000u
#define SDL_RENDERER_SOFTWARE 0x1
#define SDL_RENDERER_ACCELERATED 0x2
#define SDL_RENDERER_TARGETTEXTURE 0x8
#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_TEXTUREACCESS_TARGET 2

static inline SDL_Window *SDL_CreateWindow(const char *t, int x, int y, int w, int h, Uint32 f)
{ (void)t; (void)x; (void)y; (void)w; (void)h; (void)f; return (SDL_Window *)1; }
static inline void SDL_SetWindowTitle(SDL_Window *w, const char *t) { (void)w; (void)t; }
static inline void SDL_SetWindowIcon(SDL_Window *w, SDL_Surface *s) { (void)w; (void)s; }
static inline Uint32 SDL_GetWindowFlags(SDL_Window *w) { (void)w; return 0; }
static inline int SDL_SetWindowFullscreen(SDL_Window *w, Uint32 f) { (void)w; (void)f; return 0; }
static inline void SDL_GetWindowSize(SDL_Window *w, int *x, int *y) { (void)w; if (x) *x = 320; if (y) *y = 200; }
static inline SDL_Renderer *SDL_CreateRenderer(SDL_Window *w, int i, Uint32 f) { (void)w; (void)i; (void)f; return (SDL_Renderer *)1; }
static inline int SDL_GetRendererInfo(SDL_Renderer *r, SDL_RendererInfo *i) { (void)r; if (i) { i->name = "gnw"; i->flags = 0; } return 0; }
static inline int SDL_RenderSetIntegerScale(SDL_Renderer *r, SDL_bool e) { (void)r; (void)e; return 0; }
static inline int SDL_RenderSetLogicalSize(SDL_Renderer *r, int w, int h) { (void)r; (void)w; (void)h; return 0; }
static inline void SDL_RenderGetLogicalSize(SDL_Renderer *r, int *w, int *h) { (void)r; if (w) *w = 320; if (h) *h = 200; }
static inline int SDL_GetRendererOutputSize(SDL_Renderer *r, int *w, int *h) { (void)r; if (w) *w = 320; if (h) *h = 200; return 0; }
static inline void SDL_RenderGetViewport(SDL_Renderer *r, SDL_Rect *rc) { (void)r; if (rc) { rc->x = 0; rc->y = 0; rc->w = 320; rc->h = 200; } }
static inline void SDL_RenderGetScale(SDL_Renderer *r, float *x, float *y) { (void)r; if (x) *x = 1; if (y) *y = 1; }
static inline int SDL_RenderClear(SDL_Renderer *r) { (void)r; return 0; }
static inline int SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *a, const SDL_Rect *b) { (void)r; (void)t; (void)a; (void)b; return 0; }
static inline void SDL_RenderPresent(SDL_Renderer *r) { (void)r; }
static inline int SDL_SetRenderTarget(SDL_Renderer *r, SDL_Texture *t) { (void)r; (void)t; return 0; }
static inline SDL_Texture *SDL_CreateTexture(SDL_Renderer *r, Uint32 f, int a, int w, int h) { (void)r; (void)f; (void)a; (void)w; (void)h; return (SDL_Texture *)1; }
static inline int SDL_UpdateTexture(SDL_Texture *t, const SDL_Rect *r, const void *p, int pitch) { (void)t; (void)r; (void)p; (void)pitch; return 0; }
static inline void SDL_GL_GetDrawableSize(SDL_Window *w, int *x, int *y) { (void)w; if (x) *x = 320; if (y) *y = 200; }
static inline int SDL_ShowCursor(int toggle) { (void)toggle; return 0; }
static inline Uint32 SDL_GetMouseState(int *x, int *y) { if (x) *x = 0; if (y) *y = 0; return 0; }
static inline void SDL_StartTextInput(void) {}
static inline void SDL_StopTextInput(void) {}
static inline void SDL_SetTextInputRect(const SDL_Rect *r) { (void)r; }

/* ------------------------------------------------------------------ */
/* Keyboard                                                           */
/* ------------------------------------------------------------------ */
typedef enum {
    SDL_SCANCODE_UNKNOWN = 0,
    SDL_SCANCODE_A = 4, SDL_SCANCODE_B = 5, SDL_SCANCODE_C = 6, SDL_SCANCODE_D = 7,
    SDL_SCANCODE_E = 8, SDL_SCANCODE_F = 9, SDL_SCANCODE_G = 10, SDL_SCANCODE_H = 11,
    SDL_SCANCODE_I = 12, SDL_SCANCODE_J = 13, SDL_SCANCODE_K = 14, SDL_SCANCODE_L = 15,
    SDL_SCANCODE_M = 16, SDL_SCANCODE_N = 17, SDL_SCANCODE_O = 18, SDL_SCANCODE_P = 19,
    SDL_SCANCODE_Q = 20, SDL_SCANCODE_R = 21, SDL_SCANCODE_S = 22, SDL_SCANCODE_T = 23,
    SDL_SCANCODE_U = 24, SDL_SCANCODE_V = 25, SDL_SCANCODE_W = 26, SDL_SCANCODE_X = 27,
    SDL_SCANCODE_Y = 28, SDL_SCANCODE_Z = 29,
    SDL_SCANCODE_1 = 30, SDL_SCANCODE_2 = 31, SDL_SCANCODE_3 = 32, SDL_SCANCODE_4 = 33,
    SDL_SCANCODE_5 = 34, SDL_SCANCODE_6 = 35, SDL_SCANCODE_7 = 36, SDL_SCANCODE_8 = 37,
    SDL_SCANCODE_9 = 38, SDL_SCANCODE_0 = 39,
    SDL_SCANCODE_RETURN = 40, SDL_SCANCODE_ESCAPE = 41, SDL_SCANCODE_BACKSPACE = 42,
    SDL_SCANCODE_TAB = 43, SDL_SCANCODE_SPACE = 44, SDL_SCANCODE_MINUS = 45,
    SDL_SCANCODE_EQUALS = 46, SDL_SCANCODE_LEFTBRACKET = 47, SDL_SCANCODE_RIGHTBRACKET = 48,
    SDL_SCANCODE_BACKSLASH = 49, SDL_SCANCODE_NONUSHASH = 50, SDL_SCANCODE_SEMICOLON = 51,
    SDL_SCANCODE_APOSTROPHE = 52, SDL_SCANCODE_GRAVE = 53, SDL_SCANCODE_COMMA = 54,
    SDL_SCANCODE_PERIOD = 55, SDL_SCANCODE_SLASH = 56, SDL_SCANCODE_CAPSLOCK = 57,
    SDL_SCANCODE_F1 = 58, SDL_SCANCODE_F2 = 59, SDL_SCANCODE_F3 = 60, SDL_SCANCODE_F4 = 61,
    SDL_SCANCODE_F5 = 62, SDL_SCANCODE_F6 = 63, SDL_SCANCODE_F7 = 64, SDL_SCANCODE_F8 = 65,
    SDL_SCANCODE_F9 = 66, SDL_SCANCODE_F10 = 67, SDL_SCANCODE_F11 = 68, SDL_SCANCODE_F12 = 69,
    SDL_SCANCODE_PRINTSCREEN = 70, SDL_SCANCODE_SCROLLLOCK = 71, SDL_SCANCODE_PAUSE = 72,
    SDL_SCANCODE_INSERT = 73, SDL_SCANCODE_HOME = 74, SDL_SCANCODE_PAGEUP = 75,
    SDL_SCANCODE_DELETE = 76, SDL_SCANCODE_END = 77, SDL_SCANCODE_PAGEDOWN = 78,
    SDL_SCANCODE_RIGHT = 79, SDL_SCANCODE_LEFT = 80, SDL_SCANCODE_DOWN = 81, SDL_SCANCODE_UP = 82,
    SDL_SCANCODE_NUMLOCKCLEAR = 83, SDL_SCANCODE_KP_DIVIDE = 84, SDL_SCANCODE_KP_MULTIPLY = 85,
    SDL_SCANCODE_KP_MINUS = 86, SDL_SCANCODE_KP_PLUS = 87, SDL_SCANCODE_KP_ENTER = 88,
    SDL_SCANCODE_KP_1 = 89, SDL_SCANCODE_KP_2 = 90, SDL_SCANCODE_KP_3 = 91, SDL_SCANCODE_KP_4 = 92,
    SDL_SCANCODE_KP_5 = 93, SDL_SCANCODE_KP_6 = 94, SDL_SCANCODE_KP_7 = 95, SDL_SCANCODE_KP_8 = 96,
    SDL_SCANCODE_KP_9 = 97, SDL_SCANCODE_KP_0 = 98, SDL_SCANCODE_KP_PERIOD = 99,
    SDL_SCANCODE_NONUSBACKSLASH = 100, SDL_SCANCODE_APPLICATION = 101, SDL_SCANCODE_POWER = 102,
    SDL_SCANCODE_KP_EQUALS = 103,
    SDL_SCANCODE_MUTE = 127, SDL_SCANCODE_VOLUMEUP = 128, SDL_SCANCODE_VOLUMEDOWN = 129,
    SDL_SCANCODE_CLEAR = 156,
    SDL_SCANCODE_LCTRL = 224, SDL_SCANCODE_LSHIFT = 225, SDL_SCANCODE_LALT = 226, SDL_SCANCODE_LGUI = 227,
    SDL_SCANCODE_RCTRL = 228, SDL_SCANCODE_RSHIFT = 229, SDL_SCANCODE_RALT = 230, SDL_SCANCODE_RGUI = 231,
    SDL_SCANCODE_AUDIOMUTE = 262,
    SDL_NUM_SCANCODES = 512
} SDL_Scancode;
typedef Sint32 SDL_Keycode;

typedef enum {
    KMOD_NONE = 0x0000,
    KMOD_LSHIFT = 0x0001, KMOD_RSHIFT = 0x0002,
    KMOD_LCTRL = 0x0040, KMOD_RCTRL = 0x0080,
    KMOD_LALT = 0x0100, KMOD_RALT = 0x0200,
    KMOD_LGUI = 0x0400, KMOD_RGUI = 0x0800,
} SDL_Keymod;
#define KMOD_CTRL  (KMOD_LCTRL | KMOD_RCTRL)
#define KMOD_SHIFT (KMOD_LSHIFT | KMOD_RSHIFT)
#define KMOD_ALT   (KMOD_LALT | KMOD_RALT)
#define KMOD_GUI   (KMOD_LGUI | KMOD_RGUI)

typedef struct SDL_Keysym {
    SDL_Scancode scancode;
    SDL_Keycode sym;
    Uint16 mod;
    Uint32 unused;
} SDL_Keysym;

const Uint8 *SDL_GetKeyboardState(int *numkeys);
const char *SDL_GetScancodeName(SDL_Scancode scancode);

/* ------------------------------------------------------------------ */
/* Game controller / joystick / haptic (never connected on G&W)       */
/* ------------------------------------------------------------------ */
typedef struct _SDL_GameController SDL_GameController;
typedef struct _SDL_Joystick SDL_Joystick;
typedef struct _SDL_Haptic SDL_Haptic;
typedef Sint32 SDL_JoystickID;
typedef enum {
    SDL_CONTROLLER_AXIS_INVALID = -1,
    SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY,
    SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY,
    SDL_CONTROLLER_AXIS_TRIGGERLEFT, SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
    SDL_CONTROLLER_AXIS_MAX
} SDL_GameControllerAxis;
typedef enum {
    SDL_CONTROLLER_BUTTON_INVALID = -1,
    SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_GUIDE, SDL_CONTROLLER_BUTTON_START,
    SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
    SDL_CONTROLLER_BUTTON_MAX
} SDL_GameControllerButton;
static inline int SDL_NumJoysticks(void) { return 0; }
static inline SDL_bool SDL_IsGameController(int i) { (void)i; return SDL_FALSE; }
static inline SDL_GameController *SDL_GameControllerOpen(int i) { (void)i; return NULL; }
static inline void SDL_GameControllerClose(SDL_GameController *c) { (void)c; }
static inline SDL_GameController *SDL_GameControllerFromInstanceID(SDL_JoystickID id) { (void)id; return NULL; }
static inline int SDL_GameControllerAddMappingsFromFile(const char *f) { (void)f; return 0; }
static inline int SDL_GameControllerRumble(SDL_GameController *c, Uint16 a, Uint16 b, Uint32 d) { (void)c; (void)a; (void)b; (void)d; return -1; }
static inline SDL_Joystick *SDL_JoystickOpen(int i) { (void)i; return NULL; }
static inline int SDL_JoystickRumble(SDL_Joystick *j, Uint16 a, Uint16 b, Uint32 d) { (void)j; (void)a; (void)b; (void)d; return -1; }
static inline SDL_Haptic *SDL_HapticOpen(int i) { (void)i; return NULL; }
static inline int SDL_HapticRumbleInit(SDL_Haptic *h) { (void)h; return -1; }
static inline int SDL_HapticRumblePlay(SDL_Haptic *h, float s, Uint32 l) { (void)h; (void)s; (void)l; return -1; }

/* ------------------------------------------------------------------ */
/* Events                                                             */
/* ------------------------------------------------------------------ */
typedef enum {
    SDL_FIRSTEVENT = 0,
    SDL_QUIT = 0x100,
    SDL_WINDOWEVENT = 0x200,
    SDL_KEYDOWN = 0x300, SDL_KEYUP, SDL_TEXTEDITING, SDL_TEXTINPUT,
    SDL_MOUSEMOTION = 0x400, SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP, SDL_MOUSEWHEEL,
    SDL_JOYAXISMOTION = 0x600, SDL_JOYBALLMOTION, SDL_JOYHATMOTION, SDL_JOYBUTTONDOWN, SDL_JOYBUTTONUP,
    SDL_CONTROLLERAXISMOTION = 0x650, SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLERBUTTONUP,
    SDL_CONTROLLERDEVICEADDED, SDL_CONTROLLERDEVICEREMOVED,
    SDL_USEREVENT = 0x8000,
} SDL_EventType;

typedef enum {
    SDL_WINDOWEVENT_NONE, SDL_WINDOWEVENT_SHOWN, SDL_WINDOWEVENT_HIDDEN, SDL_WINDOWEVENT_EXPOSED,
    SDL_WINDOWEVENT_MOVED, SDL_WINDOWEVENT_RESIZED, SDL_WINDOWEVENT_SIZE_CHANGED,
    SDL_WINDOWEVENT_MINIMIZED, SDL_WINDOWEVENT_MAXIMIZED, SDL_WINDOWEVENT_RESTORED,
    SDL_WINDOWEVENT_ENTER, SDL_WINDOWEVENT_LEAVE, SDL_WINDOWEVENT_FOCUS_GAINED, SDL_WINDOWEVENT_FOCUS_LOST,
} SDL_WindowEventID;

#define SDL_BUTTON_LEFT 1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT 3
#define SDL_BUTTON_X1 4

#define SDL_APPINPUTFOCUS 0x02
#define SDL_APPACTIVE 0x04

typedef struct { Uint32 type; Uint32 timestamp; Uint32 windowID; Uint8 state; Uint8 repeat; Uint8 p2, p3; SDL_Keysym keysym; } SDL_KeyboardEvent;
typedef struct { Uint32 type; Uint32 timestamp; Uint32 windowID; char text[32]; } SDL_TextInputEvent;
typedef struct { Uint32 type; Uint32 timestamp; Uint32 windowID; Uint8 event; Uint8 p1, p2, p3; Sint32 data1, data2; } SDL_WindowEvent;
typedef struct { Uint32 type; Uint32 timestamp; Uint32 windowID; Uint32 which; Uint8 button; Uint8 state; Uint8 clicks; Uint8 p1; Sint32 x, y; } SDL_MouseButtonEvent;
typedef struct { Uint32 type; Uint32 timestamp; Uint32 windowID; Uint32 which; Sint32 x, y; Uint32 direction; } SDL_MouseWheelEvent;
typedef struct { Uint32 type; Uint32 timestamp; SDL_JoystickID which; Uint8 axis; Uint8 p1, p2, p3; Sint16 value; Uint16 p4; } SDL_JoyAxisEvent;
typedef struct { Uint32 type; Uint32 timestamp; SDL_JoystickID which; Uint8 button; Uint8 state; Uint8 p1, p2; } SDL_JoyButtonEvent;
typedef struct { Uint32 type; Uint32 timestamp; SDL_JoystickID which; Uint8 axis; Uint8 p1, p2, p3; Sint16 value; Uint16 p4; } SDL_ControllerAxisEvent;
typedef struct { Uint32 type; Uint32 timestamp; SDL_JoystickID which; Uint8 button; Uint8 state; Uint8 p1, p2; } SDL_ControllerButtonEvent;
typedef struct { Uint32 type; Uint32 timestamp; Sint32 which; } SDL_ControllerDeviceEvent;
typedef struct { Uint32 type; Uint32 timestamp; Uint32 windowID; Sint32 code; void *data1; void *data2; } SDL_UserEvent;

typedef union SDL_Event {
    Uint32 type;
    SDL_KeyboardEvent key;
    SDL_TextInputEvent text;
    SDL_WindowEvent window;
    SDL_MouseButtonEvent button;
    SDL_MouseWheelEvent wheel;
    SDL_JoyAxisEvent jaxis;
    SDL_JoyButtonEvent jbutton;
    SDL_ControllerAxisEvent caxis;
    SDL_ControllerButtonEvent cbutton;
    SDL_ControllerDeviceEvent cdevice;
    SDL_UserEvent user;
    Uint8 padding[56];
} SDL_Event;

int SDL_PollEvent(SDL_Event *event);
int SDL_PushEvent(SDL_Event *event);

/* ------------------------------------------------------------------ */
/* Timers                                                             */
/* ------------------------------------------------------------------ */
typedef int SDL_TimerID;
typedef Uint32 (*SDL_TimerCallback)(Uint32 interval, void *param);
Uint32 SDL_GetTicks(void);
Uint64 SDL_GetPerformanceCounter(void);
Uint64 SDL_GetPerformanceFrequency(void);
void SDL_Delay(Uint32 ms);
static inline SDL_TimerID SDL_AddTimer(Uint32 i, SDL_TimerCallback cb, void *p) { (void)i; (void)cb; (void)p; return 1; }
static inline SDL_bool SDL_RemoveTimer(SDL_TimerID id) { (void)id; return SDL_TRUE; }

/* ------------------------------------------------------------------ */
/* Audio                                                              */
/* ------------------------------------------------------------------ */
typedef Uint16 SDL_AudioFormat;
#define AUDIO_U8     0x0008
#define AUDIO_S16LSB 0x8010
#define AUDIO_S16SYS AUDIO_S16LSB
typedef void (*SDL_AudioCallback)(void *userdata, Uint8 *stream, int len);
typedef struct SDL_AudioSpec {
    int freq;
    SDL_AudioFormat format;
    Uint8 channels;
    Uint8 silence;
    Uint16 samples;
    Uint16 padding;
    Uint32 size;
    SDL_AudioCallback callback;
    void *userdata;
} SDL_AudioSpec;
typedef enum { SDL_AUDIO_STOPPED = 0, SDL_AUDIO_PLAYING, SDL_AUDIO_PAUSED } SDL_AudioStatus;
typedef struct SDL_AudioCVT {
    int needed; SDL_AudioFormat src_format, dst_format; double rate_incr;
    Uint8 *buf; int len, len_cvt, len_mult; double len_ratio;
} SDL_AudioCVT;

int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained);
void SDL_CloseAudio(void);
void SDL_PauseAudio(int pause_on);
SDL_AudioStatus SDL_GetAudioStatus(void);
static inline void SDL_LockAudio(void) {}
static inline void SDL_UnlockAudio(void) {}
static inline int SDL_BuildAudioCVT(SDL_AudioCVT *c, SDL_AudioFormat a, Uint8 b, int d, SDL_AudioFormat e, Uint8 f, int g)
{ (void)c; (void)a; (void)b; (void)d; (void)e; (void)f; (void)g; return -1; }
static inline int SDL_ConvertAudio(SDL_AudioCVT *c) { (void)c; return -1; }

/* ------------------------------------------------------------------ */
/* RWops (memory only)                                                */
/* ------------------------------------------------------------------ */
typedef struct SDL_RWops { const Uint8 *base; size_t size; size_t pos; } SDL_RWops;
SDL_RWops *SDL_RWFromConstMem(const void *mem, int size);
SDL_RWops *SDL_RWFromMem(void *mem, int size);
static inline SDL_RWops *SDL_RWFromFile(const char *f, const char *m) { (void)f; (void)m; return NULL; }
int SDL_RWclose(SDL_RWops *rw);
size_t SDL_RWread(SDL_RWops *rw, void *ptr, size_t size, size_t n);
static inline size_t SDL_RWwrite(SDL_RWops *rw, const void *p, size_t s, size_t n) { (void)rw; (void)p; (void)s; (void)n; return 0; }
static inline Sint64 SDL_RWtell(SDL_RWops *rw) { return rw ? (Sint64)rw->pos : -1; }

/* ------------------------------------------------------------------ */
/* Misc stdinc                                                        */
/* ------------------------------------------------------------------ */
static inline char *SDL_iconv_string(const char *to, const char *from, const char *in, size_t len)
{ (void)to; (void)from; char *r = (char *)malloc(len + 1); if (r) { memcpy(r, in, len); r[len] = 0; } return r; }
static inline size_t SDL_wcslen(const wchar_t *s) { size_t n = 0; while (s[n]) n++; return n; }

#ifdef __cplusplus
}
#endif

#endif /* GNW_POP_SDL_H */
