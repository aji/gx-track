/* gx-track.c, main gx-track source */
/* Copyright (C) 2014 Alex Iadicicco */

#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#include <GL/gl.h>
#include <math.h>

#include <stdio.h>
#include <stdint.h>

#include "play.h"

static int want_redraw = 0;
static int running = 0;

/* utilities */
/* --------- */

static void clamp3f(GLfloat *v, GLfloat low, GLfloat hi)
{
	if (v[0] < low) v[0] = low; if (v[0] > hi) v[0] = hi;
	if (v[1] < low) v[1] = low; if (v[1] > hi) v[1] = hi;
	if (v[2] < low) v[2] = low; if (v[2] > hi) v[2] = hi;
}

static int copy_to_texture(int *w, int *h, SDL_Surface *surf)
{
	SDL_PixelFormat fmt;
	SDL_Surface *converted;
	GLenum format;

	fmt.palette = NULL;
	fmt.BitsPerPixel = 32;
	fmt.BytesPerPixel = 4;
	fmt.Rloss = 0;
	fmt.Gloss = 0;
	fmt.Bloss = 0;
	fmt.Aloss = 0;
	fmt.Rshift =  0;
	fmt.Gshift =  8;
	fmt.Bshift = 16;
	fmt.Ashift = 24;
	fmt.Rmask = 0x000000ff;
	fmt.Gmask = 0x0000ff00;
	fmt.Bmask = 0x00ff0000;
	fmt.Amask = 0xff000000;
	fmt.colorkey = 0;
	fmt.alpha = 255;

	converted = SDL_ConvertSurface(surf, &fmt, SDL_SWSURFACE);

	if (converted == NULL) {
		fprintf(stderr, "failed to convert\n");
		return -1;
	}

	*w = converted->w;
	*h = converted->h;

	format = GL_RGBA;

	SDL_LockSurface(converted);
	glTexImage2D(GL_TEXTURE_2D, 0, format,
	             converted->w, converted->h,
	             0, format, GL_UNSIGNED_BYTE,
	             converted->pixels);
	SDL_UnlockSurface(converted);

	SDL_FreeSurface(converted);

	return 0;
}

static int load_to_texture(int *w, int *h, const char *file)
{
	SDL_Surface *loaded;
	int err = 0;

	loaded = IMG_Load(file);

	if (loaded == NULL) {
		fprintf(stderr, "failed to load\n");
		return -1;
	}

	if (copy_to_texture(w, h, loaded) < 0)
		err = -1;

	SDL_FreeSurface(loaded);

	return err;
}

static int load_texture(GLuint *tex, int *w, int *h, const char *file)
{
	glGenTextures(1, tex);
	glBindTexture(GL_TEXTURE_2D, *tex);

	if (load_to_texture(w, h, file) < 0) {
		glDeleteTextures(1, tex);
		return -1;
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	return 0;
}

/* fonter */
/* ------ */

static int f_char_wide, f_char_high;
static GLuint f_texture;

static int font_init(const char *font)
{
	int w, h;

	if (load_texture(&f_texture, &w, &h, font) < 0)
		return -1;

	f_char_wide = w / 16;
	f_char_high = h / 16;

	return 0;
}

static void font_glyph(int x, int y, unsigned char ch)
{
	float tx, ty;

	tx = (ch % 16) / 16.0;
	ty = (ch / 16) / 16.0;

	glBegin(GL_QUADS);
	glTexCoord2f (tx,            ty);
	glVertex2i   (x,             y);
	glTexCoord2f (tx+1.0/16.0,   ty);
	glVertex2i   (x+f_char_wide, y);
	glTexCoord2f (tx+1.0/16.0,   ty+1.0/16.0);
	glVertex2i   (x+f_char_wide, y+f_char_high);
	glTexCoord2f (tx,            ty+1.0/16.0);
	glVertex2i   (x,             y+f_char_high);
	glEnd();
}

static void font_enable(void)
{
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, f_texture);
}

static void font_disable(void)
{
	glDisable(GL_TEXTURE_2D);
}

static void font_str(int x, int y, const unsigned char *s)
{
	for (; *s; s++) {
		font_glyph(x, y, *s);
		x += f_char_wide;
	}

	glTexCoord2f(0.0, 0.0); /* aaaaaaaah */
}

/* abstract ui stuff */
/* ----------------- */

static int pat_c_row;
static int pat_c_col;

static int c_inst = 1;
static int c_octave = 2;
static int c_add = 1;
static int c_editing = 0;

static int c_jamming[32];
static int c_jamchan;

#define PAT_C_COL_SIZE 8

/* type: 0=nib, 1=note, 2=vol, 3=fx */
static const int8_t pat_c_col_type[8] = { 1, 0, 0, 2, 0, 3, 0, 0 };
static const int8_t pat_c_col_byte[8] = { 0, 1, 1, 2, 2, 3, 4, 4 };
static const int8_t pat_c_col_shft[8] = { 0, 4, 0, 4, 0, 0, 4, 0 };
static const int8_t pat_c_col_drow[8] = { 1, 0, 1, 0, 1, 0, 0, 1 };
static const int8_t pat_c_col_dcol[8] = { 0, 1,-1, 1,-1, 1, 1,-2 };

static const int8_t scancode_to_note[256] =
	{ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -2, 13, 15, -1, 18, 20,
	  22, -1, 25, 27, -1, 30, -1, -1, 12, 14, 16, 17, 19, 21, 23, 24,
	  26, 28, 29, 31, -1, -1, -1,  1,  3, -1,  6,  8, 10, -1, 13, 15,
	  -1, -1, -1, -1,  0,  2,  4,  5,  7,  9, 11, 12, 14, 16, -1, -1,
	  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, };

static int8_t sym_to_digit(SDLKey sym)
{
	switch (sym) {
	case SDLK_0: case SDLK_KP0: return 0;
	case SDLK_1: case SDLK_KP1: return 1;
	case SDLK_2: case SDLK_KP2: return 2;
	case SDLK_3: case SDLK_KP3: return 3;
	case SDLK_4: case SDLK_KP4: return 4;
	case SDLK_5: case SDLK_KP5: return 5;
	case SDLK_6: case SDLK_KP6: return 6;
	case SDLK_7: case SDLK_KP7: return 7;
	case SDLK_8: case SDLK_KP8: return 8;
	case SDLK_9: case SDLK_KP9: return 9;
	case SDLK_a: return 0xa;
	case SDLK_b: return 0xb;
	case SDLK_c: return 0xc;
	case SDLK_d: return 0xd;
	case SDLK_e: return 0xe;
	case SDLK_f: return 0xf;
	}

	return -1;
}

static void do_edit(SDL_keysym *ks)
{
	int chan, col, n, wrote;
	uint8_t *cell;

	chan = pat_c_col / PAT_C_COL_SIZE;
	col  = pat_c_col % PAT_C_COL_SIZE;

	wrote = 0;

	cell = pattern + (pat_c_row * 10 + chan) * 5 + pat_c_col_byte[col];

	switch (pat_c_col_type[col]) {
	case 0: /* nibble */
	case 2: /* volume */
	case 3: /* effect */
		n = sym_to_digit(ks->sym);

		if (n == -1)
			break;

		*cell &= ~(0xf << pat_c_col_shft[col]);
		*cell |= n << pat_c_col_shft[col];

		wrote = 1;

		break;

	case 1: /* note */
		switch (ks->sym) {
		case SDLK_DELETE:
		case SDLK_BACKSPACE:
			cell[0] = 0;
			cell[1] = 0;
			jam_note(chan, 0, -1);

			wrote = 1;
			break;
		}

		n = scancode_to_note[ks->scancode];

		if (wrote || n == -1)
			break;

		switch (n) {
		case -2:
			cell[0] = 0xff; /* note off */
			cell[1] = 0;
			jam_note(chan, 0, -1);
			break;
		default:
			cell[0] = 1 + c_octave * 12 + n;
			cell[1] = c_inst;
			jam_note(chan, cell[1], cell[0]);
			break;
		}

		wrote = 1;

		break;
	}

	if (wrote) {
		want_redraw = 1;

		pat_c_col += pat_c_col_dcol[col] + 10 * PAT_C_COL_SIZE;
		pat_c_row += pat_c_col_drow[col] * c_add + 0x40;

		pat_c_col %= 10 * PAT_C_COL_SIZE;
		pat_c_row %= 0x40;
	}
}

static int jam_key_event(SDL_Event *ev)
{
	int n, i, j, chan;

	n = scancode_to_note[ev->key.keysym.scancode];

	if (n < 0)
		return 0;

	if (ev->type == SDL_KEYUP) {
		if (c_jamming[n])
			jam_note(c_jamming[n] - 1, 0, -1);
		c_jamming[n] = 0;
	}

	if (ev->type == SDL_KEYDOWN) {
		for (j=0; j<6; j++) {
			chan = c_jamchan;
			c_jamchan = (c_jamchan + 1) % 6;

			for (i=0; i<32; i++) {
				if (c_jamming[i] == chan + 1) {
					chan = -1;
					break;
				}
			}

			if (chan != -1)
				break;
		}

		if (chan != -1) {
			jam_note(chan, c_inst, c_octave * 12 + n + 1);
			c_jamming[n] = chan + 1;
		}
	}

	return 1;
}

static void pattern_key_event(SDL_Event *ev)
{
	int n, try_edit = 0;

	switch (ev->type) {
	case SDL_KEYDOWN:
		n = sym_to_digit(ev->key.keysym.sym);

		if (ev->key.keysym.mod & KMOD_CTRL && n != -1) {
			c_octave = n;
			break;
		}

		switch (ev->key.keysym.sym) {
		case SDLK_RIGHT:
			pat_c_col = (pat_c_col + 1) % (10 * PAT_C_COL_SIZE);
			break;
		case SDLK_LEFT:
			pat_c_col = (pat_c_col - 1 + 10 * PAT_C_COL_SIZE)
			            % (10 * PAT_C_COL_SIZE);
			break;
		case SDLK_DOWN:
			if (ev->key.keysym.mod & KMOD_SHIFT) {
				if (c_inst > 1)
					c_inst--;
			} else if (ev->key.keysym.mod & KMOD_CTRL) {
				if (c_octave > 0)
					c_octave--;
			} else {
				pat_c_row = (pat_c_row + 0x01) % 0x40;
			}
			break;
		case SDLK_UP:
			if (ev->key.keysym.mod & KMOD_SHIFT) {
				c_inst++;
			} else if (ev->key.keysym.mod & KMOD_CTRL) {
				c_octave++;
			} else {
				pat_c_row = (pat_c_row + 0x3f) % 0x40;
			}
			break;

		case SDLK_TAB:
			if (ev->key.keysym.mod & KMOD_SHIFT) {
				pat_c_col = (pat_c_col - PAT_C_COL_SIZE
				                 + (10 * PAT_C_COL_SIZE))
					    % (10 * PAT_C_COL_SIZE);
			} else {
				pat_c_col = (pat_c_col + PAT_C_COL_SIZE)
					    % (10 * PAT_C_COL_SIZE);
			}
			break;

		case SDLK_PAGEDOWN:
			pat_c_row += 0x10;
			if (pat_c_row > 0x39)
				pat_c_row = 0x39;
			break;
		case SDLK_PAGEUP:
			pat_c_row -= 0x10;
			if (pat_c_row < 0)
				pat_c_row = 0;
			break;

		case SDLK_SPACE:
			c_editing = !c_editing;
			SDL_EnableKeyRepeat(c_editing ?
			                    SDL_DEFAULT_REPEAT_DELAY : 0,
			                    SDL_DEFAULT_REPEAT_INTERVAL);
			break;

		default:
			try_edit = 1;
			break;
		}
		break;

	case SDL_KEYUP:
		try_edit = 1;
		break;
	}

	if (!try_edit)
		return;

	if (c_editing) {
		if (ev->type == SDL_KEYDOWN)
			do_edit(&ev->key.keysym);
		return;
	}

	jam_key_event(ev);
}

/* video subsystem */
/* --------------- */

static SDL_Surface *screen;

static int init_video(void)
{
	Uint32 flags;

	/* init sdl */

	if (SDL_Init(SDL_INIT_EVERYTHING) < 0)
		return -1;

	flags = 0;
	flags |= SDL_HWSURFACE;
	flags |= SDL_DOUBLEBUF;
	flags |= SDL_OPENGL;

	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 5);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 5);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 5);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	screen = SDL_SetVideoMode(1280, 720, 24, flags);

	if (screen == NULL)
		return -1;

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	/* init projection matrix */

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, 1280, 720, 0, 100, -100);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	return 0;
}

static GLfloat *hue(double hue)
{
	static GLfloat c[3];
	double f, g;

	while (hue < 0)
		hue += 1.0;

	hue = fmod(hue, 1.0) * 3.0;

	f = 2.0 * fmod(hue, 1.0f);
	g = 2.0 - f;

	     if (hue < 1.0f) { c[0] = g; c[1] = f; c[2] = 0; }
	else if (hue < 2.0f) { c[0] = 0; c[1] = g; c[2] = f; }
	else                 { c[0] = f; c[1] = 0; c[2] = g; }

	clamp3f(c, 0.0f, 1.0f);

	return c;
}

struct draw_pattern_ctx {
	int x, y;
	int w, h;

	int center;
};

#define PATTERN_SPACE 8

static void pattern_project(struct draw_pattern_ctx *ctx,
                            int row, int chan, int *x, int *y)
{
	if (x) {
		*x = ctx->x + chan * (10 * f_char_wide + PATTERN_SPACE)
		     + 2 * f_char_wide + PATTERN_SPACE * 2;
	}

	if (y) {
		*y = ctx->y + row * (f_char_high + 2)
		     + (ctx->h / 2) - ctx->center * (f_char_high + 2);
	}
}

static void draw_pattern_cell(struct draw_pattern_ctx *ctx,
                              int row, int chan, uint8_t *cell)
{
	static const char *notes = "C-DbD-EbE-F-GbG-AbA-BbB-";
	static const char *tohex = "0123456789ABCDEF";
	int x, y;
	char S[16];

	pattern_project(ctx, row, chan, &x, &y);

	S[3] = '\0';

	/* note */
	if (cell[0] == 0) {
		S[0] = S[1] = S[2] = '\2';
	} else if (cell[0] == 0xff) {
		S[0] = '\3';
		S[1] = '\4';
		S[2] = '\5';
	} else {
		uint8_t note = cell[0] - 1;
		const char *n = notes + (note % 12) * 2;
		S[0] = n[0];
		S[1] = n[1];
		S[2] = '0' + note / 12;
	}
	glColor3f(1.0, 1.0, 1.0);
	font_str(x, y, S);
	x += 3 * f_char_wide + 1;

	S[2] = '\0';

	/* instrument */
	if (cell[1] == 0) {
		S[0] = S[1] = '\2';
	} else {
		S[0] = tohex[cell[1] >> 4];
		S[1] = tohex[cell[1] & 0xf];
	}
	glColor3f(0.5, 1.0, 1.0);
	font_str(x, y, S);
	x += 2 * f_char_wide + 1;

	/* volume effect */
	if (cell[2] == 0) {
		S[0] = S[1] = '\2';
	} else {
		S[0] = tohex[cell[2] >> 4];
		S[1] = tohex[cell[2] & 0xf];
	}
	glColor3f(0.5, 1.0, 0.5);
	font_str(x, y, S);
	x += 2 * f_char_wide + 1;

	/* standard effect */
	if (cell[3] == 0 && cell[4] == 0) {
		S[0] = S[1] = S[2] = '\2';
	} else {
		S[0] = tohex[cell[3] & 0xf];
		S[1] = tohex[cell[4] >> 4];
		S[2] = tohex[cell[4] & 0xf];
	}
	glColor3f(1.0, 1.0, 0.5);
	font_str(x, y, S);
	x += 3 * f_char_wide + 1;
}

static void draw_current_row_bg(struct draw_pattern_ctx *ctx, int row)
{
	int y1, y2;

	pattern_project(ctx, row, 0, NULL, &y1);
	y2 = y1 + f_char_high + 1;
	y1 --;

	glColor3f(0.2, 0.1, 0.3);
	glBegin(GL_QUADS);
	glVertex2d(0,      y1);
	glVertex2d(0,      y2);
	glVertex2d(ctx->w, y2);
	glVertex2d(ctx->w, y1);
	glEnd();
}

static void draw_major_row_bg(struct draw_pattern_ctx *ctx, int row)
{
	int y1, y2;

	pattern_project(ctx, row, 0, NULL, &y1);
	y2 = y1 + f_char_high + 1;
	y1 --;

	glColor3f(0.2, 0.1, 0.1);
	glBegin(GL_QUADS);
	glVertex2d(0,      y1);
	glVertex2d(0,      y2);
	glVertex2d(ctx->w, y2);
	glVertex2d(ctx->w, y1);
	glEnd();
}

static void draw_minor_row_bg(struct draw_pattern_ctx *ctx, int row)
{
	int y1, y2;

	pattern_project(ctx, row, 0, NULL, &y1);
	y2 = y1 + f_char_high + 1;
	y1 --;

	glColor3f(0.1, 0.1, 0.1);
	glBegin(GL_QUADS);
	glVertex2d(ctx->x, y1);
	glVertex2d(ctx->x, y2);
	glVertex2d(ctx->x + ctx->w, y2);
	glVertex2d(ctx->x + ctx->w, y1);
	glEnd();
}

static void draw_row_name(struct draw_pattern_ctx *ctx, int row)
{
	int x, y;
	char buf[16];

	pattern_project(ctx, row, 0, &x, &y);
	x -= 2 * f_char_wide + PATTERN_SPACE;

	snprintf(buf, 16, "%02X", row);

	glColor3f(0.6, 0.6, 0.6);
	font_str(x, y, buf);
}

static void draw_chan_sep(struct draw_pattern_ctx *ctx, int chan)
{
	int x;

	pattern_project(ctx, 0, chan, &x, NULL);
	x -= (PATTERN_SPACE >> 1) - 1;

	glBegin(GL_LINES);
	glColor3f(0.2, 0.2, 0.2);
	glVertex2d(x, ctx->y);
	glVertex2d(x, ctx->y + ctx->h);
	glEnd();
}

#define CURSOR_MARGIN 3

static void draw_cursor(struct draw_pattern_ctx *ctx)
{
	static const int char_lut[8] =
		{ 0, 3, 4, 5, 6, 7, 8, 9 };
	static const int extra_lut[8] =
		{ 0, 1, 1, 2, 2, 3, 3, 3 };

	int x, y, w, h, col;

	col = pat_c_col % PAT_C_COL_SIZE;
	pattern_project(ctx, pat_c_row, pat_c_col / PAT_C_COL_SIZE, &x, &y);

	x += char_lut[col] * f_char_wide + extra_lut[col];
	w = f_char_wide * (col ? 1 : 3);
	h = f_char_high;

	if (col == 0) {
		x -= CURSOR_MARGIN;
		w += CURSOR_MARGIN * 2;
	}

	y -= CURSOR_MARGIN;
	h += CURSOR_MARGIN * 2;

	if (c_editing)
		glColor3f(1.0, 0.0, 0.0);
	else
		glColor3f(0.0, 1.0, 1.0);

	glBegin(GL_LINE_LOOP);
	glVertex2d(x+w, y);
	glVertex2d(x,   y);
	glVertex2d(x,   y+h);
	glVertex2d(x+w, y+h);
	glEnd();
}

static int info_high = 40;

static void draw_pattern(uint8_t *pat)
{
	struct draw_pattern_ctx ctx;
	int row, chan;
	uint8_t *cell;

	ctx.x = 0;
	ctx.y = info_high;
	ctx.w = 1280;
	ctx.h = 720 - info_high;

	ctx.center = pat_c_row;

	font_enable();

	for (row=0; row<0x40; row++) {
		if (row == ph_row)
			draw_current_row_bg(&ctx, row);
		else if (row % 16 == 0)
			draw_major_row_bg(&ctx, row);
		else if (row % 4 == 0)
			draw_minor_row_bg(&ctx, row);

		draw_row_name(&ctx, row);

		for (chan=0; chan<10; chan++) {
			cell = pat + (row * 10 + chan) * 5;

			draw_pattern_cell(&ctx, row, chan, cell);
		}
	}

	for (chan=0; chan<=10; chan++)
		draw_chan_sep(&ctx, chan);

	font_disable();

	draw_cursor(&ctx);
}

static void draw_info(void)
{
	char buf[512];

	glColor3f(0.1, 0.1, 0.1);
	glBegin(GL_QUADS);
	glVertex2d(0, 0);
	glVertex2d(0, info_high);
	glVertex2d(1280, info_high);
	glVertex2d(1280, 0);
	glEnd();

	font_enable();

	snprintf(buf, 512, "oct=%d inst=%d add=%d", c_octave, c_inst, c_add);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	glScalef(2.0, 2.0, 1.0);
	glTranslatef(4.0, 4.0, 0.0);
	glColor3f(1.0, 1.0, 1.0);
	font_str(0, 0, buf);
	glPopMatrix();

	font_disable();
}

static void video_draw(void)
{
	double time;
	int i, j;

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	draw_pattern(pattern);

	draw_info();

	SDL_GL_SwapBuffers();
}

/* entry */
/* ----- */

static void process_event(SDL_Event *ev)
{
	switch (ev->type) {
	case SDL_QUIT:
		running = 0;
		break;

	case SDL_MOUSEMOTION:
		break;

	case SDL_KEYDOWN:
		switch (ev->key.keysym.sym) {
		case SDLK_F4:
			play_stop();
			break;

		case SDLK_F1:
			play_start(0);
			break;

		case SDLK_F2:
			play_start(pat_c_row);
			break;

		case SDLK_F3:
			play_row(pat_c_row);
			pat_c_row = (pat_c_row + 1) % 0x40;
			ph_row = pat_c_row;
			break;

		default:
			pattern_key_event(ev);
			break;
		}

		want_redraw = 1;
		break;

	case SDL_KEYUP:
		pattern_key_event(ev);
		break;

	case SDL_VIDEOEXPOSE:
		want_redraw = 1;
		break;
	}

	if (want_redraw) {
		want_redraw = 0;
		video_draw();
	}
}

static void main_loop(void)
{
	SDL_Event ev;

	running = 1;

	while (running && SDL_WaitEvent(&ev))
		process_event(&ev);
}

int main(int argc, char *argv[])
{

	if (init_video() < 0) {
		printf("failed to init video\n");
		return 1;
	}

	if (font_init("letters8x8.png") < 0) {
		printf("failed to init font\n");
		return 2;
	}

	if (play_init() < 0) {
		printf("failed to init playroutine\n");
		return 3;
	}

	//pattern_compile(example_pattern);

	printf("running..\n");
	main_loop();

	return 0;
}
