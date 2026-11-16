/* .img parsing, strip decoding, zoom/scroll math. No gint: tested on the PC. */
#pragma once
#include <stdint.h>

#define SW 396 /* screen */
#define SH 224
#define FIT_BYTES (SW * SH * 2)
#define MAX_FILE  (256 * 1024)
#define STRIP     112           /* rows per JPEG strip, both levels */
#define MAX_H     1120          /* max height at 1x (5 screens) */
#define MAX_STRIPS (2 * MAX_H / STRIP + MAX_H / STRIP)

/* .img (big-endian):
     "CGI2", u16 h (1x height: multiple of 16, SH..MAX_H), u16 n1, u16 n2, u16 0,
     u32 offsets[n1 + n2 + 1]  (from file start; strip k = offsets[k]..offsets[k+1])
     n1 strips: level 1 = 396 x h,       strip k = rows k*STRIP.. (last one shorter)
     n2 strips: level 2 = 792 x 2h, or n2 = 0 (no zoom detail) */
typedef struct {
	uint8_t const *data;
	int h, n1, n2;
	uint32_t off[MAX_STRIPS + 1];
} img_t;

/* What is on screen. zoom 1, 2, 4 = screen pixels per level-1 pixel.
   cx, cy: centre of the screen in level-1 pixels (kept inside the image). */
typedef struct { int zoom, cx, cy; } view_t;

int img_open(img_t *im, uint8_t const *data, int size);   /* 0 = valid */
void view_reset(view_t *v);                               /* 1x, top of the image */
void view_zoom(img_t const *im, view_t *v, int dir);      /* +1 in, -1 out */
void view_pan(img_t const *im, view_t *v, int dx, int dy);/* dx: 1/4 screen, dy: 1/2 screen */
void view_jump(img_t const *im, view_t *v, int dir);      /* -1 top, +1 bottom; zoom and cx kept */
/* UP/DOWN (dy = -1/+1). With continuous reading, returns dy when the view was already at that end
   (go to the previous/next image), else 0. */
int view_scroll(img_t const *im, view_t *v, int dy, int continuous);
/* Decodes the visible strips into dst (SW x SH RGB565). 0 = ok. */
int view_render(img_t const *im, view_t const *v, uint16_t *dst);

/* Where the user was and the settings, kept in a small text file between runs:
   line 1 "PH3", line 2 folder (empty = root), line 3 name (image, or >folder at the root),
   line 4 "<viewing 0/1> <zoom> <cx> <cy> <colour> <continuous> <info>" */
#define NAME_LEN 40
#define STATE_MAX 128
#define N_COLOURS 8
typedef struct { int colour, continuous, info; } settings_t;   /* colour 0..N_COLOURS-1, flags 0/1 */
typedef struct { char folder[NAME_LEN], name[NAME_LEN]; int viewing; view_t v; settings_t set; } state_t;
void state_init(state_t *s);                                /* defaults: root, nothing selected, blue, continuous off, info on */
int state_format(state_t const *s, char *buf);              /* buf >= STATE_MAX; returns an even length */
int state_parse(state_t *s, char const *text, int len);     /* 0 = valid (else *s untouched); cx/cy are clamped later */
