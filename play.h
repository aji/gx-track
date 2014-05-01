/* play.h, global playroutine data */

#ifndef __INC_PLAY_H__
#define __INC_PLAY_H__

extern const char *example_pattern;

extern uint8_t pattern[5*10*0x40];
extern void pattern_compile(const char*);

/* playhead */
extern int ph_tick;
extern int ph_row;

extern int ph_speed;

extern int ph_changed;
extern int ph_playing;

extern void play_stop(void);
extern void play_start(int row);

extern void play_row(int row);

/* tracker helpers */
extern void jam_note(int chan, int patch, int n);

/* initialization */
extern int play_init(void);

#endif
