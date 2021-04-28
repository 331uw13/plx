
// TODO: cleanup

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
//#include <sys/epoll.h>
#include <termios.h>
#include <linux/fb.h>
#include <linux/fs.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <zlib.h>

#include "plx.h"

struct __fbuffer {
    int fd;
    u32 w;
    u32 h;
    u64 size;
    col_t* data;
    col_t* swap_data;
} static fbuffer;

static struct termios termios_old_mode;

static int mouse_fd;
static int plx_status;
static col_t plx_current_color;
static col_t plx_current_clear_color;
static u16 plx_line_spacing;


// TODO: move "private" functions to bottom of the file.


// TODO: use this:
u8 plx_alloc_mem(void** p, u64 size) {
    u8 result = 0;
    if(p != NULL) {
        *p = malloc(size);
        if(*p == NULL) {
            fprintf(stderr, "Failed to allocate memory for %li bytes\nerrno: %i\n", size, errno);
            if(errno == ENOMEM) {
                fprintf(stderr, "Out of memory!\n");
            }
        }
        else {
            result = 1;
        }
    }
    return result;
}

col_t plx_rgb(u8 r, u8 g, u8 b) {
    return (r << 16) | (g << 8) | (b << 0) | (0x00 << 24);
}

void memset32b(void* dest, u32 data, u32 size) {
	for(u32 i = 0; i < size; i += sizeof data) {
		memmove(((char*)dest)+i, &data, sizeof data);
	}
}

// -------------------------------


int plx_getstatus() {
    return plx_status;
}

void plx_hint(u32 hint) {
	plx_status |= hint;
}

void plx_delay(u32 ms) {
    usleep(ms * 1000);
}

void plx_init() {
    plx_status = 0;
    mouse_fd = -1;
    fbuffer.fd = -1;
    fbuffer.data = NULL;
	fbuffer.swap_data = NULL;

    plx_line_spacing = 1;

    fbuffer.fd = open(FRAMEBUFFER_DEVICE, O_RDWR);
    if(fbuffer.fd < 0) {
        perror("open");
        fprintf(stderr, "Failed to open \"%s\"\nerrno: %i\n", FRAMEBUFFER_DEVICE, errno);
        plx_status |= PLX_ERR;
        return;
    }

    mouse_fd = open(MOUSEINPUT_DEVICE, O_RDONLY | O_NONBLOCK);
    if(mouse_fd < 0) {
        perror("open");
        fprintf(stderr, "Failed to open \"%s\"\nerrno: %i\n", MOUSEINPUT_DEVICE, errno);
        plx_status |= PLX_ERR;
        return;
    }

    struct fb_var_screeninfo si;
    ioctl(fbuffer.fd, FBIOGET_VSCREENINFO, &si);
    fbuffer.w = si.xres;
    fbuffer.h = si.yres;

    struct fb_fix_screeninfo fsi;
    ioctl(fbuffer.fd, FBIOGET_FSCREENINFO, &fsi);
    fbuffer.size = fsi.smem_len;

    const int prot = PROT_READ | PROT_WRITE;
    const int flags = MAP_SHARED;

    fbuffer.data = mmap(NULL, fbuffer.size, prot, flags, fbuffer.fd, 0);
    if(fbuffer.data == NULL || fbuffer.data == MAP_FAILED) {
        perror("mmap");
        fprintf(stderr, "Failed to map device \"%s\"\nerrno: %i\n", FRAMEBUFFER_DEVICE, errno);
        plx_status |= PLX_ERR;
        return;
    }

	if(!plx_alloc_mem((void*)&fbuffer.swap_data, fbuffer.size)) {
        plx_status |= PLX_ERR;
        return;
	}

	// Set cursor to be invisible.
	write(0, "\033[?25l", 6);

	if(!(plx_status & PLX_NO_RAWMODE)) {
		tcgetattr(0, &termios_old_mode);
		struct termios raw_mode;
		cfmakeraw(&raw_mode);
		tcsetattr(0, TCSANOW, &raw_mode);
	}
   
	if(plx_status & PLX_STDIN_NO_BLOCK) {
		fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
	}

	plx_color(255, 255, 255);
	plx_clear_color(0, 0, 0);
}

void plx_exit() {
	// Set cursor to be visible.
	write(0, "\033[?25h", 6);
   
	if(!(plx_status & PLX_NO_RAWMODE)) {
		tcsetattr(0, TCSANOW, &termios_old_mode);
	}

    if(fbuffer.data != NULL) {
        munmap(fbuffer.data, fbuffer.size);
    }
    if(fbuffer.swap_data != NULL) {
        free(fbuffer.swap_data);
    }
    if(fbuffer.fd >= 0) {
        close(fbuffer.fd);
    }
    if(mouse_fd >= 0) {
        close(mouse_fd);
    }

    puts("bye.");
}

void plx_getres(u32* x, u32* y) {
    *x = fbuffer.w;
    *y = fbuffer.h;
}

void plx_color(u8 r, u8 g, u8 b) {
    plx_current_color = plx_rgb(r, g, b);
}

void plx_clear_color(u8 r, u8 g, u8 b) {
    plx_current_clear_color = plx_rgb(r, g, b);
}

void plx_set_line_space(u16 space) {
    plx_line_spacing = space;
}

void plx_swap_buffers() {
    if(fbuffer.data != NULL && fbuffer.swap_data != NULL) {
        memmove(fbuffer.data, fbuffer.swap_data, fbuffer.size);	
		memset32b(fbuffer.swap_data, plx_current_clear_color, fbuffer.size);
    }
}

void plx_load_font(const char* filename, struct plx_font* font) {
    if(font == NULL || filename == NULL) { return; }

    gzFile file = gzopen(filename, "r");
    if(file == NULL) {
        perror("gzopen");
        fprintf(stderr, "Failed to open font file \"%s\"\nerrno: %i\n", filename, errno);
        return;
    }

    const u64 length = gzfread(&font->header, sizeof font->header, 1, file);

    if(gzeof(file)) {
        fprintf(stderr, "\"%s\" is missing header\n", filename);
        gzclose(file);
        return;
    }

    if(length == 0) {
        fprintf(stderr, "%s", gzerror(file, NULL));
        gzclose(file);
        return;
    }

	if(
			font->header.magic[0] != PSF2_MAGIC & 0 ||
			font->header.magic[1] != PSF2_MAGIC & 8 ||
			font->header.magic[2] != PSF2_MAGIC & 16 ||
			font->header.magic[3] != PSF2_MAGIC & 24) {
        fprintf(stderr, "\"%s\" header magic bytes dont match.\n", filename);
        gzclose(file);
        return;
	}

    font->data_size = /* num of chars */256 * font->header.height;
    if(!plx_alloc_mem((void*)&font->data, font->data_size)) {
        gzclose(file);
        return;
    }
    
	font->scale = 1;
    font->spacing = 0;
	font->tabwidth = 4;

    gzfread(font->data, font->data_size, 1, file);
    gzclose(file);
}

void plx_unload_font(struct plx_font* font) {
    if(font != NULL) {
        free(font->data);
        font->data = NULL;
    }
}

void plx_draw_pixel(u32 x, u32 y) {
    if(fbuffer.data == NULL) { return; }

    const u32 index = y * fbuffer.w + x;
    if(index < fbuffer.size / sizeof(col_t)) {
        fbuffer.swap_data[index] = plx_current_color;
    }
    
}

void plx_draw_rect(u32 x, u32 y, u32 w, u32 h) {
    if(fbuffer.data == NULL) { return; }
    const u32 index = y * fbuffer.w + x;
    if(index < fbuffer.size) {
        for(int j = y; j < y+h; j++) {
            for(int i = x; i < x+w; i++) {
                plx_draw_pixel(i, j);
            }
        }
    }
}

void plx_draw_rect_hollow(u32 x, u32 y, u32 w, u32 h, u32 tx, u32 ty) {
	plx_draw_rect(x, y, w, ty);         // left top  -->  right top
	plx_draw_rect(x, y, tx, h);         // left top  -->  left down
	plx_draw_rect(x+w, y, tx, h);       // right top -->  right down
	plx_draw_rect(x, y+h, w+tx, ty);    // left down -->  right down
}

void plx_draw_line(u32 x0, u32 y0, u32 x1, u32 y1) { 
    int width = x1 - x0;
    int height = y1 - y0;
    int dx0 = 0;
    int dy0 = 0;
    int dx1 = 0;
    int dy1 = 0;

    dx1 = dx0 = (width < 0) ? -1 : 1;
    dy0 = (height < 0) ? -1 : 1;

    int aw = abs(width);
    int ah = abs(height);
    int longest = aw;
    int shortest = ah;
    
    if(longest < shortest) {
        longest = ah;
        shortest = aw;
        dy1 = (height < 0) ? -1 : 1;
        dx1 = 0;
    }

    int numerator = longest >> 1;

    dx0 *= plx_line_spacing;
    dy0 *= plx_line_spacing;
    dx1 *= plx_line_spacing;
    dy1 *= plx_line_spacing;

    for(int i = 0; i < longest / plx_line_spacing; i++) {
        plx_draw_pixel(x0, y0);
        numerator += shortest;
        if(numerator > longest) {
            numerator -= longest;
            x0 += dx0;
            y0 += dy0;
        }
        else {
            x0 += dx1;
            y0 += dy1;
        }
    }
}

void plx_draw_char(u32 x, u32 y, char c, struct plx_font* font) {
    if(font == NULL || font->data == NULL) { return; }
    u32 origin_x = x;
	for(u8 i = 0; i < font->header.height; i++) {
        u8 g = font->data[c * font->header.height + i];
        for(u8 j = 0; j < 8; j++) {
            if(g & 0x80) {
                plx_draw_rect(x, y, font->scale, font->scale);
            }
            g = g << 1;
            x += font->scale;
        }
        y += font->scale;
        x = origin_x;
    }
}

void plx_draw_text(u32 x, u32 y, char* text, u32 size, struct plx_font* font) {
    if(font == NULL) { return; }
	const u32 xorigin = x;
    const u32 xoff = (font->header.width + font->spacing) * font->scale;
    const u32 yoff = (font->header.height + font->spacing) * font->scale;
    for(u32 i = 0; i < size; i++) {
		const char c = text[i];
		if(c >= 0x20 && c < 0x7F) { 
   			plx_draw_char(x, y, text[i], font);
        	x += xoff;
		}
		else {
			if(c == 0xA) { // new line
				y += yoff;
				x = xorigin;
			}
			else if(c == 0x9) { // tab
				x += xoff * font->tabwidth;
			}
		}
    }
}

void plx_mouseinput(u32 flag, ...) {

    struct input_event e;
    if(read(mouse_fd, &e, sizeof e) < 0) {
        return;
    }

    u8* data = (u8*)&e;

    va_list args;
    va_start(args, flag);

    if(flag & PLX_MOUSEPOS) {
        int x = (int)data[1];
        int y = (int)data[2];
        if(x > 0xFF / 2) {
            x -= 0xFF;
        }
        if(y > 0xFF / 2) {
            y -= 0xFF;
        }
        *va_arg(args, int*) = x * 0.3;
        *va_arg(args, int*) = y * 0.3;
    }

    if(flag & PLX_MOUSECLICK_LEFT) {
        *va_arg(args, int*) = data[0] & 0x1;
    }
    
    if(flag & PLX_MOUSECLICK_RIGHT) {
        *va_arg(args, int*) = !!(data[0] & 0x2);
    }

    va_end(args);
}

u8 plx_keyinput() {
    char c = 0;
    read(0, &c, 1);
    return c;
}


