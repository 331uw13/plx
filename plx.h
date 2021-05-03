#ifndef PLX_H
#define PLX_H

#define PSF2_MAGIC 0x72b54a86


typedef unsigned long int   u64;
typedef unsigned int        u32;
typedef unsigned short      u16;
typedef unsigned char       u8;
typedef u32                 col_t;


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
	u8 tab_width;
    u8* data;
    u32 data_size;
};

struct plx_fb {
	u32 width;
	u32 height;
	int fd;
	u64 size;
	col_t* data;
	col_t draw_color;
	col_t clear_color;
};

u8 plx_open(char* path, struct plx_fb* fb);
void plx_close();
void plx_load_font(const char* filename, struct plx_font* font);
void plx_unload_font(struct plx_font* font);

void plx_clear         (struct plx_fb* fb);
void plx_clear_region  (struct plx_fb* fb, u32 x, u32 y, u32 w, u32 h);

void plx_draw_pixel    (struct plx_fb* fb, u32 x, u32 y);
void plx_draw_region   (struct plx_fb* fb, u32 x, u32 y, u32 w, u32 h);
void plx_draw_line     (struct plx_fb* fb, u32 x0, u32 y0, u32 x1, u32 y1);

void plx_draw_char     (struct plx_fb* fb, u32 x, u32 y, char c, struct plx_font* font);
void plx_draw_text     (struct plx_fb* fb, char* text, u32 size, u32 x, u32 y, struct plx_font* font);

u8 plx_keyinput();
void plx_delay(u32 ms);


#define PLX_STDIN_NONBLOCK     (1<<0)
// ...
void plx_set_flag(u32 flag, u8 b);



#endif
