#ifndef BITMAP_CON_H
#define BITMAP_CON_H

#include "vidmodes.h"
#include "threadwrap.h"

extern struct video_stats vstat;
extern pthread_mutex_t vstatlock;
extern sem_t	drawn_sem;
extern int force_redraws;

int bitmap_gettext(int sx, int sy, int ex, int ey, void *fill);
int bitmap_puttext(int sx, int sy, int ex, int ey, void *fill);
void bitmap_gotoxy(int x, int y);
void bitmap_setcursortype(int type);
int bitmap_setfont(int font, int force, int font_no);
int bitmap_getfont(void);
int bitmap_loadfont(char *filename);

void send_rectangle(int xoffset, int yoffset, int width, int height, int force);
int bitmap_init_mode(int mode, int *width, int *height);
int bitmap_init(void (*drawrect_cb) (int xpos, int ypos, int width, int height, unsigned char *data)
				,void (*flush) (void));
int bitmap_movetext(int x, int y, int ex, int ey, int tox, int toy);
void bitmap_clreol(void);
void bitmap_clrscr(void);
void bitmap_getcustomcursor(int *s, int *e, int *r, int *b, int *v);
void bitmap_setcustomcursor(int s, int e, int r, int b, int v);
int bitmap_getvideoflags(void);
void bitmap_setvideoflags(int flags);
void bitmap_setscaling(int new_value);
int bitmap_getscaling(void);

#endif
