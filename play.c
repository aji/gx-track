/* play.c, playroutine */
/* Copyright (C) 2014 Alex Iadicicco */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gxm.h"

const char *example_pattern =
#include "pattern.c"
	;

uint8_t patches[][7*4+2] = {
	{ }, /* patch 0 not used */

	{ 0x71, 0x0d, 0x33, 0x02, /* DT1, MUL */
	  0x23, 0x2d, 0x26, 0x80, /* TL */
	  0x5f, 0x99, 0x5f, 0x94, /* RS, AR */
	  0x0a, 0x0a, 0x0a, 0x0a, /* AM, D1R */
	  0x02, 0x02, 0x02, 0x02, /* D2R */
	  0x11, 0x11, 0x11, 0xa7, /* D1L, RR */
	  0x00, 0x00, 0x00, 0x00, /* SSG-EG */
	  0x32,   /* feedback, algorithm */
	  0xc0 }, /* L, R, AMS, FMS */

	{ 0x41, 0x41, 0x41, 0x41, /* DT1, MUL */
	  0x14, 0x16, 0x18, 0x0a, /* TL */
	  0x19, 0x18, 0x0a, 0x1f, /* RS, AR */
	  0x02, 0x03, 0x02, 0x01, /* AM, D1R */
	  0x08, 0x06, 0x07, 0x05, /* D2R */
	  0x85, 0x85, 0x85, 0x85, /* D1L, RR */
	  0x00, 0x00, 0x00, 0x00, /* SSG-EG */
	  0x02,   /* feedback, algorithm */
	  0xc0 }, /* L, R, AMS, FMS */

	{ 0x00, 0x04, 0x02, 0x01, /* DT1, MUL */
	  0x04, 0x04, 0x04, 0x04, /* TL */
	  0x1f, 0x1f, 0x1f, 0x1f, /* RS, AR */
	  0x04, 0x0f, 0x04, 0x0f, /* AM, D1R */
	  0x00, 0x00, 0x00, 0x00, /* D2R */
	  0xf8, 0xf7, 0xf8, 0xfa, /* D1L, RR */
	  0x00, 0x00, 0x00, 0x00, /* SSG-EG */
	  0x07,   /* feedback, algorithm */
	  0xc0 }, /* L, R, AMS, FMS */

	{ 0x62, 0x34, 0x43, 0x22, /* DT1, MUL */
	  0x2f, 0x20, 0x12, 0x00, /* TL */
	  0x1f, 0x1f, 0x1f, 0x1f, /* RS, AR */
	  0x00, 0x10, 0x07, 0x00, /* AM, D1R */
	  0x00, 0x00, 0x00, 0x00, /* D2R */
	  0xf2, 0xf4, 0xf6, 0xf3, /* D1L, RR */
	  0x00, 0x00, 0x00, 0x00, /* SSG-EG */
	  0x00,   /* feedback, algorithm */
	  0xc0 }, /* L, R, AMS, FMS */
};

uint8_t pattern[5*10*0x40];

static const char *notes = "C-DbD-EbE-F-GbG-AbA-BbB-";
static const char *digits = "0123456789abcdef";

static uint8_t to_note(char *s)
{
	char *p = strstr(notes, s);

	if (p == NULL)
		return 0;

	return (p - notes) >> 1;
}

static uint8_t to_num(char *s)
{
	uint8_t n = 0;
	char *p;

	for (; *s; s++) {
		n <<= 4;

		p = strchr(digits, tolower(*s));

		if (p == NULL)
			continue;

		n |= (p - digits) & 0xf;
	}

	return n;
}

void pattern_compile(const char *pat)
{
	const char *psrc;
	uint8_t *pdst;

	int i;
	char num[3];

	for (i=0; i<10*0x40; i++) {
		psrc = pat + 10 * i;
		pdst = pattern + 5 * i;

		num[0] = psrc[0];
		num[1] = psrc[1];
		num[2] = '\0';
		pdst[0] = to_note(num) + 1 + 12 * (psrc[2] - '0');

		if (!strcmp(num, "=="))
			pdst[0] = 0xff;
		if (!strcmp(num, "  "))
			pdst[0] = 0;

		num[0] = psrc[3];
		num[1] = psrc[4];
		pdst[1] = to_num(num);

		num[0] = psrc[5];
		num[1] = psrc[6];
		pdst[2] = to_num(num);

		/* 3 and 4 are done backwards on purpose */

		num[0] = psrc[8];
		num[1] = psrc[9];
		pdst[4] = to_num(num);

		num[0] = psrc[7];
		num[1] = '\0';
		pdst[3] = to_num(num);
	}
}

/* playhead */

int ph_tick;
int ph_row;

int ph_speed;

int ph_playing;

void ph_init(void)
{
	ph_tick = 0;
	ph_row = 0;

	ph_speed = 6;

	ph_playing = 0;
}

#include <SDL/SDL.h>
#include "gens-sound/ym2612.h"
#include "gens-bits.h"

static void request_redraw(void)
{
	SDL_Event ev;
	ev.type = SDL_VIDEOEXPOSE;
	SDL_PushEvent(&ev);
}

#define LEN MAX_UPDATE_LENGHT

static int samps_per_tick;
static int samps_left_in_tick;

static void ym_reg(unsigned bank, uint8_t a, uint8_t v)
{
	YM2612_Write(0 + bank * 2, a);
	YM2612_Write(1 + bank * 2, v);
}

static unsigned ch_div_lut[6] = { 0, 0, 0, 1, 1, 1 };
static unsigned ch_mod_lut[6] = { 0, 1, 2, 0, 1, 2 };

static unsigned ch_key_lut[6] = { 0, 1, 2, 4, 5, 6 };

static void ch_reg(uint8_t ch, uint8_t a, uint8_t v)
{
	ym_reg(ch_div_lut[ch], a + ch_mod_lut[ch], v);
}

#define CH_OFF(CH) (ym_reg(0, 0x28, ch_key_lut[CH]))
#define CH_ON(CH)  (ym_reg(0, 0x28, ch_key_lut[CH] | 0xf0))

static int freqtbl[12] = { 617, 653, 692, 733, 777, 823, 872,
                           924, 979, 1037, 1099, 1164 };

static void ym_note(int ch, int n)
{
	uint16_t freq;

	freq = freqtbl[n % 12];

	ch_reg(ch, 0xa4, ((n / 12) << 3) | (freq >> 8));
	ch_reg(ch, 0xa0, (freq & 0xff));
}

static void hard_reset(int chan)
{
	ym_reg(chan / 3, 0x80 + (chan % 3), 0xff);
	ym_reg(chan / 3, 0x84 + (chan % 3), 0xff);
	ym_reg(chan / 3, 0x88 + (chan % 3), 0xff);
	ym_reg(chan / 3, 0x8c + (chan % 3), 0xff);
	CH_OFF(chan);
}

static void select_patch(int chan, int patchnum)
{
	uint8_t *patch = patches[patchnum];
	uint8_t addr;
	int bank;

	bank = chan / 3;
	chan = chan % 3;

	for (addr=0x30; addr<0xa0; addr+=0x04)
		ym_reg(bank, addr + chan, *patch++);

	ym_reg(bank, 0xb0 + chan, *patch++);
	ym_reg(bank, 0xb4 + chan, *patch++);
}

static void fire_cell(uint8_t *cell, int chan)
{
	if (cell[0] == 0xff) /* 0xff == note off */
		CH_OFF(chan);

	if (cell[1])
		select_patch(chan, cell[1]);

	if (cell[0] && cell[0] != 0xff) {
		CH_OFF(chan);
		ym_note(chan, cell[0] - 1);
		CH_ON(chan);
	}

	switch (cell[3]) {
	case 0xf:
		if (cell[4] == 0)
			ph_playing = 0;
		else
			ph_speed = cell[4];
		break;
	}
}

static void row_tick(uint8_t *row, int tick)
{
	int chan;
	uint8_t *cell;

	for (chan=0; chan<6; chan++) {
		cell = row + 5 * chan;

		if (tick == 0)
			fire_cell(cell, chan);
	}
}

void play_stop(void)
{
	int chan, op;

	ph_playing = 0;

	for (chan = 0; chan < 6; chan++)
		hard_reset(chan);

	request_redraw();
}

void play_start(int row)
{
	if (ph_playing)
		play_stop();

	ph_playing = 1;

	ph_row = row;
	ph_tick = 0;

	row_tick(pattern + 10 * 5 * row, 0);

	request_redraw();
}

void play_row(int row)
{
	if (ph_playing)
		play_stop();

	row_tick(pattern + 10 * 5 * row, 0);

	request_redraw();
}

void jam_note(int chan, int patch, int n)
{
	CH_OFF(chan);

	if (n != 0xff && n != -1) {
		ym_note(chan, n - 1);
		select_patch(chan, patch);
		CH_ON(chan);
	}
}

static void play_tick(void)
{
	if (!ph_playing)
		return;

	ph_tick++;

	if (ph_tick % ph_speed == 0) {
		ph_tick = 0;
		ph_row = (ph_row + 1) % 0x40;
	}

	row_tick(pattern + 10 * 5 * ph_row, ph_tick);

	if (ph_tick == 0)
		request_redraw();
}

static void play_audio(void *user, Uint8 *_stream, int len)
{
	Sint16 *stream = (void*)_stream;
	int samps, i, left[LEN], right[LEN], *buf[2];

	len /= 2 * sizeof(*stream);

	buf[0] = left;
	buf[1] = right;

	while (len > 0) {
		samps = len;
		if (samps > samps_left_in_tick)
			samps = samps_left_in_tick;

		for (i=0; i<samps; i++) {
			left[i] = 0;
			right[i] = 0;
		}

		YM2612_Update(buf, samps);

		for (i=0; i<samps; i++) {
			stream[2*i+0] = left[i] / 3;
			stream[2*i+1] = right[i] / 3;
		}

		samps_left_in_tick -= samps;
		len -= samps;
		stream += 2 * samps;

		if (samps_left_in_tick == 0) {
			play_tick();
			samps_left_in_tick = samps_per_tick;
		}
	}
}

static void play_sample_patch(int ch)
{
	static const uint8_t patch[] = {
		0x71, 0x0d, 0x33, 0x01,
		0x23, 0x2d, 0x26, 0x00,
		0x5f, 0x99, 0x5f, 0x94,
		0x05, 0x05, 0x05, 0x07,
		0x02, 0x02, 0x02, 0x02,
		0x11, 0x11, 0x11, 0xa6,
		0x00, 0x00, 0x00, 0x00,
	};

	uint8_t addr, i;

	ym_reg(0, 0x28, ch_key_lut[ch]);

	for (addr=0x30, i=0; addr<0xa0; addr+=0x4, i++)
		ch_reg(ch, addr, patch[i]);

	ch_reg(ch, 0xb0, 0x32);
	ch_reg(ch, 0xb4, 0xc0);
}

int play_init(void)
{
	SDL_AudioSpec want, have;

	ph_init();

	memset(&want, 0, sizeof(want));
	want.freq = 44100;
	want.format = AUDIO_S16;
	want.samples = 512;
	want.channels = 2;
	want.callback = play_audio;

	if (SDL_OpenAudio(&want, &have) < 0) {
		printf("failed to init audio\n");
		return -1;
	}

	if (have.format != want.format) {
		printf("got wrong format\n");
		return -1;
	}

	if (have.channels != want.channels) {
		printf("got wrong num channels\n");
		return -1;
	}

	YM2612_Init(CLOCK_NTSC / 7, have.freq, 0);

	samps_per_tick = have.freq / 60;
	samps_left_in_tick = samps_per_tick;

	play_sample_patch(0);
	play_sample_patch(1);
	play_sample_patch(2);
	play_sample_patch(3);
	play_sample_patch(4);

	SDL_PauseAudio(0);

	return 0;
}
