#include <stdio.h>
#include <string.h>
#include "view.h"
#include "tjpgd.h"

static int u16(uint8_t const *p) { return p[0] << 8 | p[1]; }
static uint32_t u32(uint8_t const *p) { return (uint32_t)u16(p) << 16 | u16(p + 2); }

int img_open(img_t *im, uint8_t const *data, int size)
{
	if(size < 12 || memcmp(data, "CGI2", 4)) return -1;
	im->data = data;
	im->h = u16(data + 4);
	im->n1 = u16(data + 6);
	im->n2 = u16(data + 8);
	if(im->h % 16 || im->h < SH || im->h > MAX_H) return -1;
	if(im->n1 != (im->h + STRIP - 1) / STRIP) return -1;
	if(im->n2 && im->n2 != (2 * im->h + STRIP - 1) / STRIP) return -1;
	int n = im->n1 + im->n2, hdr = 12 + 4 * (n + 1);
	if(size < hdr) return -1;
	for(int k = 0; k <= n; k++) {
		im->off[k] = u32(data + 12 + 4 * k);
		if(k == 0 ? im->off[0] != (uint32_t)hdr : im->off[k] <= im->off[k - 1]) return -1;
	}
	return im->off[n] == (uint32_t)size ? 0 : -1;
}

static int clamp(int x, int lo, int hi)
{
	return x < lo ? lo : x > hi ? hi : x;
}

static void view_clamp(img_t const *im, view_t *v)
{
	int hw = SW / v->zoom / 2, hh = SH / v->zoom / 2;
	v->cx = clamp(v->cx, hw, SW - hw);
	v->cy = clamp(v->cy, hh, im->h - hh);
}

void view_reset(view_t *v)
{
	*v = (view_t){1, SW / 2, SH / 2};
}

void view_zoom(img_t const *im, view_t *v, int dir)
{
	int z = dir > 0 ? v->zoom * 2 : v->zoom / 2;
	if(z < 1 || z > 4) return;
	v->zoom = z;
	view_clamp(im, v);
}

void view_pan(img_t const *im, view_t *v, int dx, int dy)
{
	v->cx += dx * SW / v->zoom / 4;
	v->cy += dy * SH / v->zoom / 2;
	view_clamp(im, v);
}

void view_jump(img_t const *im, view_t *v, int dir)
{
	v->cy = dir < 0 ? 0 : im->h;
	view_clamp(im, v);
}

int view_scroll(img_t const *im, view_t *v, int dy, int continuous)
{
	int before = v->cy;
	view_pan(im, v, 0, dy);
	return continuous && v->cy == before ? dy : 0;
}

typedef struct {
	uint8_t const *p;
	size_t left;
	uint16_t *dst;
	int y0, wx, wy, mag;   /* strip's first row; window top-left (level pixels); zoom */
} stream_t;

static size_t jpg_in(JDEC *jd, uint8_t *buf, size_t n)
{
	stream_t *s = jd->device;
	if(n > s->left) n = s->left;
	if(buf) memcpy(buf, s->p, n);
	s->p += n;
	s->left -= n;
	return n;
}

/* Copies the visible part of a decoded block to the screen, each pixel as mag x mag */
static int jpg_out(JDEC *jd, void *bitmap, JRECT *r)
{
	stream_t *s = jd->device;
	uint16_t const *px = bitmap;
	int m = s->mag;
	for(int y = r->top; y <= r->bottom; y++) {
		int sy = (s->y0 + y - s->wy) * m;
		for(int x = r->left; x <= r->right; x++, px++) {
			int sx = (x - s->wx) * m;
			if(sy < 0 || sy >= SH || sx < 0 || sx >= SW) continue;
			for(int dy = 0; dy < m; dy++)
				for(int dx = 0; dx < m; dx++) s->dst[(sy + dy) * SW + sx + dx] = *px;
		}
	}
	return 1;
}

int view_render(img_t const *im, view_t const *v, uint16_t *dst)
{
	static uint32_t pool[4096];  /* 16 KB TJpgDec work area (~9 KB needed with JD_FASTDECODE 2) */
	int lv2 = v->zoom > 1 && im->n2;                   /* zoom detail available */
	int s = lv2 ? 2 : 1, mag = v->zoom / s;            /* level pixels per 1x pixel; screen px per level px */
	int lw = SW * s, lh = im->h * s, ww = SW / mag, wh = SH / mag;
	int wx = clamp(v->cx * s - ww / 2, 0, lw - ww);
	int wy = clamp(v->cy * s - wh / 2, 0, lh - wh);

	for(int k = wy / STRIP; k <= (wy + wh - 1) / STRIP; k++) {
		int i = (lv2 ? im->n1 : 0) + k, rows = lh - k * STRIP < STRIP ? lh - k * STRIP : STRIP;
		stream_t st = {im->data + im->off[i], im->off[i + 1] - im->off[i], dst, k * STRIP, wx, wy, mag};
		JDEC jd;
		int rc = jd_prepare(&jd, jpg_in, pool, sizeof pool, &st);
		if(rc != JDR_OK) return rc;
		if(jd.width != lw || jd.height != rows) return -1;   /* would write outside the strip */
		if((rc = jd_decomp(&jd, jpg_out, 0)) != JDR_OK) return rc;
	}
	return 0;
}

void state_init(state_t *s)
{
	memset(s, 0, sizeof *s);
	view_reset(&s->v);
	s->set.info = 1;
}

int state_format(state_t const *s, char *buf)
{
	int n = snprintf(buf, STATE_MAX, "PH3\n%s\n%s\n%d %d %d %d %d %d %d\n", s->folder, s->name, s->viewing,
		s->v.zoom, s->v.cx, s->v.cy, s->set.colour, s->set.continuous, s->set.info);
	if(n % 2) buf[n++] = ' ';   /* BFile_Write needs an even size */
	return n;
}

static int copy_name(char *dst, char const *src)   /* printable ASCII, fits NAME_LEN; may be empty */
{
	int n = strlen(src);
	if(n >= NAME_LEN) return -1;
	for(int i = 0; i < n; i++) if(src[i] < 32 || src[i] > 126) return -1;
	memcpy(dst, src, n + 1);
	return 0;
}

int state_parse(state_t *s, char const *text, int len)
{
	char buf[STATE_MAX], *line[4], *p = buf;
	if(len <= 0 || len >= STATE_MAX) return -1;
	memcpy(buf, text, len);
	buf[len] = 0;
	for(int i = 0; i < 4; i++) {   /* magic, folder, name, numbers: each ends with a newline */
		char *nl = strchr(p, '\n');
		if(!nl) return -1;
		line[i] = p;
		*nl = 0;
		p = nl + 1;
	}
	state_t t;
	if(strcmp(line[0], "PH3") || copy_name(t.folder, line[1]) || copy_name(t.name, line[2])) return -1;
	if(sscanf(line[3], "%d %d %d %d %d %d %d", &t.viewing, &t.v.zoom, &t.v.cx, &t.v.cy,
		&t.set.colour, &t.set.continuous, &t.set.info) != 7) return -1;
	if((t.viewing & ~1) || (t.v.zoom != 1 && t.v.zoom != 2 && t.v.zoom != 4)) return -1;
	if(t.set.colour < 0 || t.set.colour >= N_COLOURS || (t.set.continuous & ~1) || (t.set.info & ~1)) return -1;
	*s = t;
	return 0;
}
