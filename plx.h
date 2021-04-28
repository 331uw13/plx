#ifndef PLX_H
#define PLX_H

#include <math.h>

#define FRAMEBUFFER_DEVICE "/dev/fb0"
#define MOUSEINPUT_DEVICE "/dev/input/mouse0"

#define PLX_STARTED (1<<0)
#define PLX_ERR (1<<1)
#define PLX_MOUSEPOS (1<<2)
#define PLX_MOUSECLICK_LEFT (1<<3)
#define PLX_MOUSECLICK_RIGHT (1<<4)
#define PLX_STDIN_NO_BLOCK (1<<5)      // block when reading input?
#define PLX_NO_RAWMODE (1<<7)            // set raw mode?

#define PSF2_MAGIC 0x72b54a86


typedef unsigned long int u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef u32 col_t;


struct psf2header {
	u8 magic[4];
	u32 version;
	u32 headersize;
	u32 flags;
	u32 length;
	u32 charsize;
	u32 height;
	u32 width;
};

struct plx_font {
    struct psf2header header;
    u8 scale;
    u8 spacing;
	u8 tabwidth;
    u8* data;
    u32 data_size;
};

int plx_getstatus();
void plx_hint(u32 hint);
void plx_init();
void plx_exit();
void plx_getres(u32* x, u32* h);
void plx_delay(u32 ms);
void plx_color(u8 r, u8 g, u8 b);
void plx_clear_color(u8 r, u8 g, u8 b);
void plx_set_line_space(u16 space);
void plx_swap_buffers();
void plx_load_font(const char* filename, struct plx_font* font);
void plx_unload_font(struct plx_font* font);

// Drawing
void plx_draw_pixel(u32 x, u32 y);
void plx_draw_rect(u32 x, u32 y, u32 w, u32 h);
void plx_draw_rect_hollow(u32 x, u32 y, u32 w, u32 h, u32 tx, u32 ty);
void plx_draw_line(u32 x0, u32 y0, u32 x1, u32 y1);
void plx_draw_char(u32 x, u32 y, char c, struct plx_font* font);
void plx_draw_text(u32 x, u32 y, char* text, u32 size, struct plx_font* font);

// Input
void plx_mouseinput(u32 flag, ...);
u8 plx_keyinput();

#endif
