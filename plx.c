#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <termios.h>
#include <linux/fb.h>
#include <linux/fs.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <zlib.h>

#include "plx.h"


static struct termios termios_old_mode;

void memset_col(void* dest, col_t data, u32 count) {
	for(u32 i = 0; i < count; i += sizeof data) {
		memmove(((char*)dest)+i, &data, sizeof data);
	}
}

// -------------------------------



u8 plx_open(char* path, struct plx_fb* fb) {
	u8 res = 0;

    fb->fd = open(path, O_RDWR);
	if(fb->fd < 0) {
		fprintf(stderr, "Failed to open \"%s\".\nerrno: %i\n", path, errno);
		perror("open");
		goto finish;
	}

	struct fb_var_screeninfo si;
    struct fb_fix_screeninfo fsi;
    
	ioctl(fb->fd, FBIOGET_VSCREENINFO, &si);
	fb->width = si.xres;
    fb->height = si.yres;

    ioctl(fb->fd, FBIOGET_FSCREENINFO, &fsi);
    fb->size = fsi.smem_len;

    fb->data = mmap(NULL, fb->size, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
	if(fb->data == MAP_FAILED || fb->data == NULL) {
		fprintf(stderr, "Failed to map \"%s\" into memory.\nerrno: %i\n", path, errno);
		perror("mmap");
		goto finish;
	}

	write(0, "\033[?25l", 6);          // Set cursor to be invisible.
	
	tcgetattr(0, &termios_old_mode);   // Save current.
	struct termios raw_mode;
	cfmakeraw(&raw_mode);
	tcsetattr(0, TCSANOW, &raw_mode);

	res = 1;

finish:
	return res;
}

void plx_close(struct plx_fb* fb) {
	if(fb != NULL) {
		write(0, "\033[?25h", 6);   // Set cursor to be visible.
		munmap(fb->data, fb->size);
		close(fb->fd);
		tcsetattr(0, TCSANOW, &termios_old_mode);
	}
}

void plx_set_flag(u32 flag, u8 b) {
	switch(flag) {
		
		case PLX_STDIN_NONBLOCK:
			fcntl(0, F_SETFL, b ? 
					fcntl(0, F_GETFL) | O_NONBLOCK :
					fcntl(0, F_GETFL) & ~O_NONBLOCK);
			break;

		default: break;
	}
}

void plx_clear(struct plx_fb* fb) {
	if(fb != NULL) {
		memset_col(fb->data, fb->clear_color, fb->size);
	}
}

void plx_clear_region(struct plx_fb* fb, u32 x, u32 y, u32 w, u32 h) {
	if(fb != NULL && x+w <= fb->width && y+h <= fb->height) {
		for(u32 i = y; i < y+h; i++) {
			memset_col(fb->data + i * fb->width + x, fb->clear_color, w * sizeof(col_t));
		}
	}
}

void plx_draw_pixel(struct plx_fb* fb, u32 x, u32 y) {
	if(fb != NULL && x <= fb->width && y <= fb->height) {
		const u32 i = y * fb->width + x;
		fb->data[i] = fb->draw_color;
	}
}

void plx_draw_region(struct plx_fb* fb, u32 x, u32 y, u32 w, u32 h) {
	if(fb != NULL && x+w <= fb->width && y+h <= fb->height) {
		for(u32 i = y; i < y+h; i++) {
			memset_col(fb->data + i * fb->width + x, fb->draw_color, w * sizeof(col_t));
		}
	}
}

void plx_draw_line(struct plx_fb* fb, u32 x0, u32 y0, u32 x1, u32 y1) {
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

    for(int i = 0; i < longest; i++) {
        plx_draw_pixel(fb, x0, y0);
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

    font->data_size = 256 * font->header.height;
    font->data = malloc(font->data_size);
	if(font->data == NULL) {
        gzclose(file);
        return;
    }
    
	font->scale = 1;
    font->spacing = 0;
	font->tab_width = 4;

    gzfread(font->data, font->data_size, 1, file);
    gzclose(file);
}

void plx_unload_font(struct plx_font* font) {
    if(font != NULL) {
        free(font->data);
        font->data = NULL;
    }
}

void plx_draw_char(struct plx_fb* fb, u32 x, u32 y, char c, struct plx_font* font) {
    if(font == NULL || font->data == NULL || c == 0) { return; }
	u32 origin_x = x;
	for(u8 i = 0; i < font->header.height; i++) {
        u8 g = font->data[c * font->header.height + i];
        for(u8 j = 0; j < 8; j++) {
            if(g & 0x80) {
                plx_draw_region(fb, x, y, font->scale, font->scale);
            }
            g = g << 1;
            x += font->scale;
        }
        y += font->scale;
        x = origin_x;
    }
}

void plx_draw_text(struct plx_fb* fb, char* text, u32 size, u32 x, u32 y, struct plx_font* font) {
    if(font == NULL) { return; }
	const u32 xorigin = x;
    const u32 xoff = (font->header.width + font->spacing) * font->scale;
    const u32 yoff = (font->header.height + font->spacing) * font->scale;
    for(u32 i = 0; i < size; i++) {
		const char c = text[i];
		if(c > 0x1F && c < 0x7F) {
   			plx_draw_char(fb, x, y, text[i], font);
        	x += xoff;
		}
		else {
			if(c == '\n') {
				y += yoff;
				x = xorigin;
			}
			else if(c == '\t') {
				x += xoff * font->tab_width;
			}
		}
    }
}

u8 plx_keyinput() {
    char c = 0;
    read(0, &c, 1);
    return c;
}

void plx_delay(u32 ms) {
    usleep(ms * 1000);
}
