/* gfx.h - SDL backed immediate-mode 2D drawing with a built-in 5x7 font */
#ifndef DVDREGION_GFX_H
#define DVDREGION_GFX_H

#include <stdint.h>
#include <stddef.h>

#define GFX_W 1280
#define GFX_H 720

/* filled on gfx_init() */
extern uint32_t COL_BG;
extern uint32_t COL_PANEL;
extern uint32_t COL_PANEL2;
extern uint32_t COL_LINE;
extern uint32_t COL_TEXT;
extern uint32_t COL_DIM;
extern uint32_t COL_ACCENT;
extern uint32_t COL_OK;
extern uint32_t COL_WARN;
extern uint32_t COL_ERR;
extern uint32_t COL_SEL;

#define ALIGN_LEFT   0
#define ALIGN_CENTER 1
#define ALIGN_RIGHT  2

int  gfx_init(void);
void gfx_quit(void);

void gfx_begin(void);              /* lock the screen for drawing */
void gfx_end(void);                /* unlock + present */

void gfx_clear(uint32_t color);
void gfx_fill(int x, int y, int w, int h, uint32_t color);
void gfx_frame(int x, int y, int w, int h, uint32_t color);
void gfx_hline(int x, int y, int w, uint32_t color);
void gfx_vline(int x, int y, int h, uint32_t color);

/* Vertically centres a 1px line inside a box, so 2x-scaled glyphs line up. */
void gfx_text(int x, int y, uint32_t color, const char *s, int scale);
void gfx_text_box(int x, int y, int w, int h, uint32_t color, const char *s,
                  int scale, int align);
int  gfx_text_w(const char *s, int scale);
int  gfx_text_h(int scale);

void gfx_bar(int x, int y, int w, int h, float frac, uint32_t bg, uint32_t fg);

#endif /* DVDREGION_GFX_H */
