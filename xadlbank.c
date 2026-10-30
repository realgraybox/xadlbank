/*
 * Copyright (C) 2026 M. Glargaard, aka graybox
 *
 * This software is provided "as-is", without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would
 *    be appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not
 *    be misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source distribution.
 */


/*
 * xadlbank.c - Minimalist Xlib HMI AdLib BNK Soundbank Editor
 *
 * Compilation (Dynamic):
 *   gcc -O2 -Wall -Wextra -std=c99 xadlbank.c -lX11 -o xadlbank
 *
 * Compilation (Static, uClibc / Musl):
 *   gcc -O2 -static -Wall -Wextra -std=c99 xadlbank.c -lX11 -lxcb -lXau -lXdmcp -o xadlbank
 *
 * Usage:
 *   ./xadlbank <soundbank.bnk>
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdint.h>

#define MAX_INSTRUMENTS 500
#define BNK_HEADER_SIZE 28
#define BNK_NAME_SIZE   12
#define BNK_INST_SIZE   30
#define BNK_NAME_VISIBLE_MAX 8
#define BNK_NAME_STORED_SIZE 9

#define WIN_WIDTH   800
#define WIN_HEIGHT  600
#define FONT_NAME   "fixed"

#define FC_DEFAULT_WIDTH   400
#define FC_DEFAULT_HEIGHT  500
#define FC_FONT_NAME       "fixed"

#include "xfileselect.c"

/* Binary Instrument & Bank Model */
typedef struct {
    uint16_t midi_index;
    char     name_raw[10];  /* 10 bytes raw from file */
    char     name_disp[11]; /* Null-terminated for GUI display */
    uint8_t  raw[BNK_INST_SIZE];
} Instrument;

typedef struct {
    uint8_t    hdr_raw[BNK_HEADER_SIZE];
    uint16_t   version;
    uint16_t   count;
    Instrument inst[MAX_INSTRUMENTS];
    int        dirty;
    char       filepath[512];
} SoundBank;

static SoundBank g_bank;

/* GUI States */
enum AppMode {
    MODE_LIST = 0,
    MODE_INSPECTOR,
    MODE_EDIT_NAME
};

static enum AppMode g_mode = MODE_LIST;
static int g_selected_inst = 0;
static int g_scroll_offset = 0;
static int g_selected_param = 0; /* 0..29 in inspector */
static char g_input_buf[32] = {0};
static int  g_input_len = 0;
static char g_status_msg[128] = "";

static char g_orig_name[11] = {0};
static int g_confirm_load = 0;

/* Parameter metadata for the 30 raw bytes */
typedef struct {
    const char *label;
    int is_mod; /* 1 = Modulator, 0 = Carrier, -1 = Global */
    uint8_t max_val;
} ParamDesc;

static const ParamDesc g_param_desc[BNK_INST_SIZE] = {
    { "Percussive Mode (0:Mel, 1:Perc)",	-1, 1   }, /* 00 */
    { "Rhythm Drum Type",					-1, 255 }, /* 01 */

    { "Mod Key Scale Level (0..3)",			1, 3   }, /* 02 */
    { "Mod Frequency Multiplier (0..15)",	1, 15  }, /* 03 */
    { "Feedback (0..7)",                   	-1, 7   }, /* 04 */
    { "Mod Attack (0..15)",          	   	1, 15  }, /* 05 */
    { "Mod Sustain (0..15)",                1, 15  }, /* 06 */
    { "Mod Sustaining Voice / EG",          1, 1   }, /* 07 */
    { "Mod Decay (0..15)",                  1, 15  }, /* 08 */
    { "Mod Release (0..15)",                1, 15  }, /* 09 */
    { "Mod Level (0..63)",                  1, 63  }, /* 10 */
    { "Mod Tremolo / AM",                   1, 1   }, /* 11 */
    { "Mod Vibrato / VIB",                  1, 1   }, /* 12 */
    { "Mod Envelope Scale / KSR",           1, 1   }, /* 13 */
    { "Connection / Synthesis Type",       	-1, 1   }, /* 14 */

    { "Car Key Scale Level (0..3)",         0, 3   }, /* 15 */
    { "Car Frequency Multiplier (0..15)",   0, 15  }, /* 16 */
    { "Unknown / Preserved Raw Byte",       0, 255 }, /* 17 */
    { "Car Attack (0..15)",          		0, 15  }, /* 18 */
    { "Car Sustain (0..15)",   				0, 15  }, /* 19 */
    { "Car Sustaining Voice / EG",   		0, 1   }, /* 20 */
    { "Car Decay (0..15)",     		        0, 15  }, /* 21 */
    { "Car Release (0..15)", 			    0, 15  }, /* 22 */
    { "Car Level (0..63)",   				0, 63  }, /* 23 */
    { "Car Tremolo / AM",                   0, 1   }, /* 24 */
    { "Car Vibrato / VIB",                  0, 1   }, /* 25 */
    { "Car Envelope Scale / KSR",           0, 1   }, /* 26 */
    { "Unknown / Preserved Raw Byte",       0, 255 }, /* 27 */

    { "Mod Waveform (0..7)",                1, 7   }, /* 28 */
    { "Car Waveform (0..7)",                0, 7   }  /* 29 */
};

static int bank_is_loaded(void) {
    return g_bank.count > 0;
}

void set_window_icon(Display *dpy, Window win) {
	
	unsigned long icon_data[] = {
    8, 8, // width, height
    0xFF000033, 0xFFFFFFFF, 0xFF000033, 0xFFFFFFFF, 0xFF000033, 0xFFFFFFFF, 0xFF000033, 0xFFFFFFFF,
    0xFF000033, 0xFFFFFFFF, 0xFF000033, 0xFFFFFFFF, 0xFF000033, 0xFFFFFFFF, 0xFF000033, 0xFFFFFFFF,
    0xFF000033, 0xFFFFFFFF, 0xFF000033, 0xFFFFFFFF, 0xFF000033, 0xFFFFFFFF, 0xFF000033, 0xFFFFFFFF,
    0xFF000033, 0xFFFFFFFF, 0xFF000033, 0xFFFFFFFF, 0xFF000033, 0xFFFFFFFF, 0xFF000033, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFF000033, 0xFF000033, 0xFF000033, 0xFF000033, 0xFF000033, 0xFF000033, 0xFF000033, 0xFF000033
};
   
    int data_length = 2 + (8 * 8); // 2 (measure) + (width * height)

    Atom wm_icon = XInternAtom(dpy, "_NET_WM_ICON", False);
    Atom cardinal = XInternAtom(dpy, "CARDINAL", False);
    XChangeProperty(dpy, win, wm_icon, cardinal, 32, PropModeReplace, (unsigned char*)icon_data, data_length);
    XFlush(dpy); 
}

/* Endian utilities */
static inline uint16_t rd_le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t rd_le32(const uint8_t *p) { return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); }
static inline void wr_le16(uint8_t *p, uint16_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
static inline void wr_le32(uint8_t *p, uint32_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF; }

/* Direct BNK Loading */
/*int bnk_load(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return 0;

    fseek(fp, 0, SEEK_END);
    size_t sz = ftell(fp);
    rewind(fp);

    if (sz < BNK_HEADER_SIZE) { fclose(fp); return 0; }

    uint8_t *buf = malloc(sz);
    if (!buf || fread(buf, 1, sz, fp) != sz) {
        if (buf) free(buf);
        fclose(fp);
        return 0;
    }
    fclose(fp);

    if (memcmp(buf + 2, "ADLIB-", 6) != 0) {
        free(buf);
        return 0;
    }

    // Preserve raw 28-byte header
    memcpy(g_bank.hdr_raw, buf, BNK_HEADER_SIZE);

    g_bank.version = rd_le16(buf);
    g_bank.count   = rd_le16(buf + 10);
    if (g_bank.count > MAX_INSTRUMENTS) g_bank.count = MAX_INSTRUMENTS;

    uint32_t off_name = rd_le32(buf + 12);
    uint32_t off_data = rd_le32(buf + 16);

    for (uint16_t i = 0; i < g_bank.count; i++) {
        const uint8_t *np = buf + off_name + (i * BNK_NAME_SIZE);
        const uint8_t *dp = buf + off_data + (i * BNK_INST_SIZE);

        g_bank.inst[i].midi_index = rd_le16(np);
    
        // 1. Keep exact 10 raw bytes for lossless saving 
        memcpy(g_bank.inst[i].name_raw, np + 2, 10);


        // 2. Create clean string for UI display 
        memcpy(g_bank.inst[i].name_disp, np + 2, 10);
        g_bank.inst[i].name_disp[10] = '\0';
        for (int c = 0; c < 10; c++) {
            if ((unsigned char)g_bank.inst[i].name_disp[c] < 0x20 || g_bank.inst[i].name_disp[c] == 0x7F) {
                g_bank.inst[i].name_disp[c] = ' ';
            }
        }
        // Trim trailing spaces for GUI display only 
        int len = 10;
        while (len > 0 && g_bank.inst[i].name_disp[len - 1] == ' ') {
            g_bank.inst[i].name_disp[--len] = '\0';
        }

        // 3. Keep raw 30 hardware bytes
        memcpy(g_bank.inst[i].raw, dp, BNK_INST_SIZE);
    }

    strncpy(g_bank.filepath, filename, sizeof(g_bank.filepath) - 1);
    g_bank.dirty = 0;
    free(buf);
    return 1;
}*/

static int
range_ok(size_t total, size_t offset, size_t length)
{
    return offset <= total && length <= total - offset;
}

int
bnk_load(const char *filename)
{
    FILE *fp;
    uint8_t *buf = NULL;
    size_t sz;
    uint16_t total_ins;
    uint32_t off_name;
    uint32_t off_data;
    size_t name_table_size;
    uint16_t i;

    fp = fopen(filename, "rb");
    if (!fp)
        return 0;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }

    {
        long end = ftell(fp);
        if (end < 0) {
            fclose(fp);
            return 0;
        }
        sz = (size_t)end;
    }

    rewind(fp);

    if (sz < BNK_HEADER_SIZE) {
        fclose(fp);
        return 0;
    }

    buf = malloc(sz);
    if (!buf) {
        fclose(fp);
        return 0;
    }

    if (fread(buf, 1, sz, fp) != sz) {
        free(buf);
        fclose(fp);
        return 0;
    }
    fclose(fp);

    if (memcmp(buf + 2, "ADLIB-", 6) != 0) {
        free(buf);
        return 0;
    }

    /*
     * Header:
     *   0..1   version
     *   2..7   "ADLIB-"
     *   8..9   number used / reserved depending on variant
     *   10..11 number of name records
     *   12..15 name-table offset
     *   16..19 data-section offset
     */
    total_ins = rd_le16(buf + 10);
    off_name  = rd_le32(buf + 12);
    off_data  = rd_le32(buf + 16);

    /*
     * Never partially load a bank that cannot safely be represented
     * by the editor. Partial loading makes Save destructive.
     */
    if (total_ins == 0 || total_ins > MAX_INSTRUMENTS) {
        fprintf(stderr,
                "Unsupported BNK: %u instrument entries "
                "(xadlbank maximum is %u).\n",
                (unsigned)total_ins,
                (unsigned)MAX_INSTRUMENTS);
        free(buf);
        return 0;
    }

    if ((size_t)total_ins > SIZE_MAX / BNK_NAME_SIZE) {
        free(buf);
        return 0;
    }

    name_table_size = (size_t)total_ins * BNK_NAME_SIZE;

    if (off_name < BNK_HEADER_SIZE ||
        !range_ok(sz, (size_t)off_name, name_table_size) ||
        off_data > sz) {
        fprintf(stderr, "Invalid BNK name/data offsets.\n");
        free(buf);
        return 0;
    }

    /*
     * Clear stale state only after all basic validation succeeded.
     */
    memset(&g_bank, 0, sizeof(g_bank));

    memcpy(g_bank.hdr_raw, buf, BNK_HEADER_SIZE);
    g_bank.version = rd_le16(buf);
    g_bank.count   = total_ins;

    for (i = 0; i < total_ins; i++) {
        size_t name_pos;
        size_t data_pos;
        const uint8_t *np;
        const uint8_t *dp;
        uint16_t data_index;
        int c;
        int len;

        name_pos = (size_t)off_name + (size_t)i * BNK_NAME_SIZE;

        if (!range_ok(sz, name_pos, BNK_NAME_SIZE)) {
            fprintf(stderr, "Invalid BNK name entry %u.\n",
                    (unsigned)i);
            free(buf);
            memset(&g_bank, 0, sizeof(g_bank));
            return 0;
        }

        np = buf + name_pos;

        /*
         * Bytes 0..1: data section record index.
         * Byte 2: used flag.
         * Bytes 3..11: 9-byte name field.
         */
        data_index = rd_le16(np);

        data_pos = (size_t)off_data +
                   (size_t)data_index * BNK_INST_SIZE;

        if (!range_ok(sz, data_pos, BNK_INST_SIZE)) {
            fprintf(stderr,
                    "Invalid BNK: name entry %u refers to "
                    "missing record index %u.\n",
                    (unsigned)i,
                    (unsigned)data_index);
            free(buf);
            memset(&g_bank, 0, sizeof(g_bank));
            return 0;
        }

        dp = buf + data_pos;

        g_bank.inst[i].midi_index = data_index;

        /*
         * Retain raw name bytes for lossless saving.
         *
         * If your current Instrument still has name_raw[10],
         * preserve bytes 2..11 exactly. This includes the flag byte,
         * but you must write them back at entry + 2 on save.
         */
        memcpy(g_bank.inst[i].name_raw, np + 2, 10);

        /*
         * Human display begins at entry byte 3. Preserve/display
         * eight printable chars; byte 11 is usually the terminator.
         *
         * If your UI currently supports ten chars, reduce that to 8
         * for this conventional BNK layout.
         */
        memcpy(g_bank.inst[i].name_disp, np + 3, 8);
        g_bank.inst[i].name_disp[8] = '\0';

        for (c = 0; c < 8; c++) {
            unsigned char ch =
                (unsigned char)g_bank.inst[i].name_disp[c];

            if (ch < 0x20 || ch == 0x7f)
                g_bank.inst[i].name_disp[c] = ' ';
        }

        len = 8;
        while (len > 0 &&
               g_bank.inst[i].name_disp[len - 1] == ' ') {
            g_bank.inst[i].name_disp[--len] = '\0';
        }

        memcpy(g_bank.inst[i].raw, dp, BNK_INST_SIZE);
    }

    strncpy(g_bank.filepath, filename, sizeof(g_bank.filepath) - 1);
    g_bank.filepath[sizeof(g_bank.filepath) - 1] = '\0';
    g_bank.dirty = 0;

    free(buf);
    return 1;
}


int bnk_save(const char *filename) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) return 0;

    /* Write original preserved header (maintaining byte 8-9 and all reserved flags) */
    fwrite(g_bank.hdr_raw, 1, BNK_HEADER_SIZE, fp);

    /* Write exact 12-byte name records */
    for (uint16_t i = 0; i < g_bank.count; i++) {
        uint8_t n_entry[BNK_NAME_SIZE];
        wr_le16(n_entry, g_bank.inst[i].midi_index);
        memcpy(n_entry + 2, g_bank.inst[i].name_raw, 10);
        fwrite(n_entry, 1, BNK_NAME_SIZE, fp);
    }

    /* Write 30-byte hardware data records */
    for (uint16_t i = 0; i < g_bank.count; i++) {
        fwrite(g_bank.inst[i].raw, 1, BNK_INST_SIZE, fp);
    }

    fclose(fp);
    g_bank.dirty = 0;
    return 1;
}

/* Audio Preview via mop */
void sound_preview(int inst_idx) {
    if (inst_idx < 0 || inst_idx >= g_bank.count) return;
    char hex[61];
    for (int i = 0; i < BNK_INST_SIZE; i++) {
        sprintf(&hex[i * 2], "%02X", g_bank.inst[inst_idx].raw[i]);
    }
    hex[60] = '\0';

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "mop -r %s -n 60 -d 1000 > /dev/null 2>&1 &", hex);
    system(cmd);

    snprintf(g_status_msg, sizeof(g_status_msg), "Note preview #%03d [%s] on OPL2...", inst_idx, g_bank.inst[inst_idx].name_disp);

}

static void sound_preview_chord(int inst_idx) {
    char hex[BNK_INST_SIZE * 2 + 1];
    char cmd[256];
    int i;

    if (inst_idx < 0 || inst_idx >= (int)g_bank.count)
        return;

    for (i = 0; i < BNK_INST_SIZE; i++) {
        sprintf(hex + (i * 2), "%02X",
                (unsigned)g_bank.inst[inst_idx].raw[i]);
    }
    hex[BNK_INST_SIZE * 2] = '\0';

    snprintf(cmd, sizeof(cmd),
             "mop -r %s -n \"60,64,67\" -d 1500 "
             "> /dev/null 2>&1 &",
             hex);

    system(cmd);

    snprintf(g_status_msg, sizeof(g_status_msg),
             "Chord preview #%03d [%s]: C4 E4 G4",
             inst_idx,
             g_bank.inst[inst_idx].name_disp[0] ?
             g_bank.inst[inst_idx].name_disp : "<No Name>");
}

/* X11 Double-Buffered Rendering */
void draw_ui(Display *dpy, Pixmap buf, GC gc, XFontStruct *fs, int width, int height,
             unsigned long col_bg, unsigned long col_fg, unsigned long col_hi, unsigned long col_sel, unsigned long col_dim) {
    
    int line_h = fs->ascent + fs->descent + 4;
    char str[256];

    /* Clear Canvas */
    XSetForeground(dpy, gc, col_bg);
    XFillRectangle(dpy, buf, gc, 0, 0, width, height);

    /* Header Bar */
    XSetForeground(dpy, gc, col_sel);
    XFillRectangle(dpy, buf, gc, 0, 0, width, line_h * 2 + 6);
    
    XSetForeground(dpy, gc, col_hi);
    snprintf(str, sizeof(str), " xadlbank - %s %s", g_bank.filepath, g_bank.dirty ? "[MODIFIED]" : "");
    XDrawString(dpy, buf, gc, 10, fs->ascent + 4, str, strlen(str));

    XSetForeground(dpy, gc, col_fg);
    snprintf(str, sizeof(str), " Total Instruments: %d  |  Version: %u  |  Mode: %s",
             g_bank.count, g_bank.version, g_mode == MODE_INSPECTOR ? "INSPECTOR" : "LIST");
    XDrawString(dpy, buf, gc, 10, line_h + fs->ascent + 2, str, strlen(str));

    int y_start = line_h * 2 + 16;

    if (g_mode == MODE_LIST || g_mode == MODE_EDIT_NAME) {
        /* Column Headers */
        XSetForeground(dpy, gc, col_dim);
        XDrawString(dpy, buf, gc, 10, y_start, "ID   MIDI  NAME        MODULATOR (VOL ATK DEC SUS REL)  CARRIER (VOL ATK DEC SUS REL)  FB", 89);
        y_start += 6;
        XDrawLine(dpy, buf, gc, 10, y_start, width - 10, y_start);
        y_start += 14;

        int visible_rows = (height - y_start - line_h * 2) / line_h;
        if (visible_rows < 1) visible_rows = 1;

        /* Auto-scroll clamp */
        if (g_selected_inst < g_scroll_offset) g_scroll_offset = g_selected_inst;
        if (g_selected_inst >= g_scroll_offset + visible_rows) g_scroll_offset = g_selected_inst - visible_rows + 1;

        for (int r = 0; r < visible_rows; r++) {
            int idx = g_scroll_offset + r;
            if (idx >= g_bank.count) break;

            int row_y = y_start + (r * line_h);

            /* Selection Box */
            if (idx == g_selected_inst) {
                XSetForeground(dpy, gc, col_sel);
                XFillRectangle(dpy, buf, gc, 8, row_y - fs->ascent, width - 16, line_h);
                XSetForeground(dpy, gc, col_hi);
            } else {
                XSetForeground(dpy, gc, col_fg);
            }

            Instrument *ins = &g_bank.inst[idx];
            

			const char *show_name;
			if (g_mode == MODE_EDIT_NAME && idx == g_selected_inst) {
				show_name = g_input_buf; /* Live preview of what user is typing */
			} else {
				show_name = ins->name_disp[0] ? ins->name_disp : "<No Name>";
			}

			snprintf(str, sizeof(str),
					 "#%03d M:%03d %-10s  "
					 "%02X %02X %02X %02X %02X   "
					 "%02X %02X %02X %02X %02X   "
					 "%02X",
					 idx, ins->midi_index, show_name,
	
					 /* Mod: level, attack, decay, sustain, release */
					 ins->raw[10], ins->raw[5], ins->raw[8],
					 ins->raw[6],  ins->raw[9],

					 /* Carrier: level, attack, decay, sustain, release */
					 ins->raw[23], ins->raw[18], ins->raw[21],
					 ins->raw[19], ins->raw[22],

					 /* Feedback */
					 ins->raw[4]);



            XDrawString(dpy, buf, gc, 10, row_y, str, strlen(str));
        }

        /* Name Edit In-place Overlay */
        if (g_mode == MODE_EDIT_NAME) {
            XSetForeground(dpy, gc, col_hi);
            XDrawRectangle(dpy, buf, gc, 70, y_start + (g_selected_inst - g_scroll_offset) * line_h - fs->ascent, 90, line_h);
        }

    } else if (g_mode == MODE_INSPECTOR) {
        /* Modal Instrument Inspector */
        Instrument *ins = &g_bank.inst[g_selected_inst];

        XSetForeground(dpy, gc, col_hi);
        snprintf(str, sizeof(str), "=== INSPECTOR: Instrument #%03d [%s] (MIDI: %d) ===",
                g_selected_inst, ins->name_disp, ins->midi_index);

        XDrawString(dpy, buf, gc, 20, y_start, str, strlen(str));

        y_start += line_h + 6;

        /* Draw 30 parameters in 2 columns: Modulator vs Carrier */
        int col1_x = 30;
        int col2_x = width / 2 + 10;
        int row_count = 15;

        for (int p = 0; p < BNK_INST_SIZE; p++) {
            int cx = (p < row_count) ? col1_x : col2_x;
            int cy = y_start + ((p % row_count) * line_h);

            if (p == g_selected_param) {
                XSetForeground(dpy, gc, col_sel);
                XFillRectangle(dpy, buf, gc, cx - 4, cy - fs->ascent, (width / 2) - 30, line_h);
                XSetForeground(dpy, gc, col_hi);
            } else {
                XSetForeground(dpy, gc, col_fg);
            }

            snprintf(str, sizeof(str), "[%02d] %-32s : 0x%02X (%3u)",
                     p, g_param_desc[p].label, ins->raw[p], ins->raw[p]);
            XDrawString(dpy, buf, gc, cx, cy, str, strlen(str));
        }
    }

    /* Footer / Status Bar */
    XSetForeground(dpy, gc, col_dim);
    
    XDrawLine(dpy, buf, gc, 0, height - line_h - 8, width, height - line_h - 8);

    XSetForeground(dpy, gc, col_hi);
    if (g_mode == MODE_EDIT_NAME) {
        snprintf(str, sizeof(str), "EDIT NAME: %s_  [ENTER] Commit  [ESC] Cancel", g_input_buf);
        XDrawString(dpy, buf, gc, 10, height - 30, str, strlen(str));
    } else {
        XDrawString(dpy, buf, gc, 10, height - 30, g_status_msg, strlen(g_status_msg));
    }
    
    /* Bottom line: Permanent key shortcuts */
	XSetForeground(dpy, gc, col_fg);
	
	if (g_mode == MODE_INSPECTOR) {
		XDrawString(dpy, buf, gc, 10, height - 8,
            "INSPECTOR: [ARROWS] Select [+/-] Inc/Dec [ESC] Back", 51);
	} else {
		XDrawString(dpy, buf, gc, 10, height - 8,
            "[l] Load  [UP/DOWN] Select  [ENTER] Inspect  [p] Note [Shift+p] Chord [n] Rename [s] Save [Q] Quit", 98);
	}
}

static int load_bank_with_chooser(void) {
    char *path;

     path = x11_filechooser(
        ".",                 			// start_path (NULL => current dir)
        "Open File",                 	// window title
        "Select an AdLib .bnk file", 	// header text (shown above file list)
        ".bnk",        			    	// extension filter (NULL or "" => all)
        FC_MODE_FILE,                	// FC_MODE_FILE or FC_MODE_DIR
        "[q/Esc]: Cancel [Enter]: Open" // status/help line
    );
    
    if (path == NULL) {
        snprintf(g_status_msg, sizeof(g_status_msg),
                 "Load cancelled.");
        return 0;
    }
    
    if (!bnk_load(path)) {
        snprintf(g_status_msg, sizeof(g_status_msg),
                 "ERROR: Not a valid or supported BNK: %s", path);
        free(path);
        return 0;
    }

    g_selected_inst = 0;
    g_scroll_offset = 0;
    g_selected_param = 0;
    g_mode = MODE_LIST;

    snprintf(g_status_msg, sizeof(g_status_msg),
             "Loaded %u instruments from %s",
             (unsigned)g_bank.count, path);

    free(path);
    return 1;
}

int main(int argc, char *argv[]) {
    
    memset(&g_bank, 0, sizeof(g_bank));

	if (argc >= 2) {
		if (!bnk_load(argv[1])) {
			fprintf(stderr, "Error: Unable to load valid AdLib BNK file '%s'\n",
					argv[1]);
			return 1;
		}

		snprintf(g_status_msg, sizeof(g_status_msg),
				 "Loaded %u instrument%s from %s",
				 (unsigned)g_bank.count,
				 g_bank.count == 1 ? "" : "s",
				 g_bank.filepath);
	} else {
		snprintf(g_status_msg, sizeof(g_status_msg),
				 "No bank is loaded. Press L to load a .bnk file.");
	}

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "Error: Cannot open X Display. Check $DISPLAY.\n");
        return 1;
    }

    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);

    XFontStruct *fs = XLoadQueryFont(dpy, FONT_NAME);
    if (!fs) fs = XQueryFont(dpy, XGContextFromGC(DefaultGC(dpy, screen)));

    unsigned long col_bg  = BlackPixel(dpy, screen);
    unsigned long col_fg  = WhitePixel(dpy, screen);
    unsigned long col_hi  = 0x00FFFF;  /* Cyan */
    unsigned long col_sel = 0x003366;  /* Dark Blue */
    unsigned long col_dim = 0x888888;  /* Gray */

    Window win = XCreateSimpleWindow(dpy, root, 100, 100, WIN_WIDTH, WIN_HEIGHT, 1, col_dim, col_bg);
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | StructureNotifyMask | ButtonPressMask);
    XStoreName(dpy, win, "xadlbank - OPL2 Soundbank Editor");
    XMapWindow(dpy, win);
    
    set_window_icon(dpy, win);

    GC gc = XCreateGC(dpy, win, 0, NULL);
    
#ifdef PRETTYFONTS
    fs = XLoadQueryFont(dpy, "9x15");
    // fallback if 9x15 not found  - try "fixed"
	if (fs == NULL) {
		fprintf(stderr, "Warning: Could not load font '9x15'. Using 'fixed'\n");
		fs = XLoadQueryFont(dpy, "fixed");
	}

	// safety if all fails
	if (fs == NULL) {
		fprintf(stderr, "Error: No usefull fonts found on the system - closing.\n");
		exit(1); 
	}
#else
    if (fs) XSetFont(dpy, gc, fs->fid);
#endif

    int win_w = WIN_WIDTH;
    int win_h = WIN_HEIGHT;
    Pixmap backbuf = XCreatePixmap(dpy, win, win_w, win_h, DefaultDepth(dpy, screen));

    Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete, 1);

    int running = 1;
    while (running) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        int need_redraw = 0;

        if (ev.type == ConfigureNotify) {
            if (ev.xconfigure.width != win_w || ev.xconfigure.height != win_h) {
                win_w = ev.xconfigure.width;
                win_h = ev.xconfigure.height;
                XFreePixmap(dpy, backbuf);
                backbuf = XCreatePixmap(dpy, win, win_w, win_h, DefaultDepth(dpy, screen));
            }
            need_redraw = 1;
        }
    
		else if (ev.type == Expose) {
			need_redraw = 1;
		}

        if (ev.type == ClientMessage) {
            if ((Atom)ev.xclient.data.l[0] == wm_delete) running = 0;
        }

        if (ev.type == ButtonPress) {
			if (ev.type == ButtonPress) {
				int my = ev.xbutton.y;
				int mx = ev.xbutton.x;
				int btn = ev.xbutton.button;
				int line_h = fs->ascent + fs->descent + 4;

				/* 1. Mouse Wheel Scrolling (Global) */
				if (btn == Button4) { /* Wheel UP */
					if (g_mode == MODE_LIST) {
						g_selected_inst -= 3;
						if (g_selected_inst < 0) g_selected_inst = 0;
						need_redraw = 1;
					} else if (g_mode == MODE_INSPECTOR) {
						/* Optional: Wheel up adjusts parameter value */
						if (g_bank.inst[g_selected_inst].raw[g_selected_param] < g_param_desc[g_selected_param].max_val) {
							g_bank.inst[g_selected_inst].raw[g_selected_param]++;
							g_bank.dirty = 1;
							need_redraw = 1;
						}
					}
				} 
				else if (btn == Button5) { /* Wheel DOWN */
					if (g_mode == MODE_LIST) {
						g_selected_inst += 3;
						if (g_selected_inst >= g_bank.count) g_selected_inst = g_bank.count - 1;
						need_redraw = 1;
					} else if (g_mode == MODE_INSPECTOR) {
						/* Optional: Wheel down adjusts parameter value */
						if (g_bank.inst[g_selected_inst].raw[g_selected_param] > 0) {
							g_bank.inst[g_selected_inst].raw[g_selected_param]--;
							g_bank.dirty = 1;
							need_redraw = 1;
						}
					}
				}

				/* 2. Left Click: Select row / Inspector cell */
				else if (btn == Button1) {	/* Left Click */
					if (g_mode == MODE_LIST) {
						/* Align y_table_top with the actual top edge of the selection rectangle */
						int y_table_top = (line_h * 2 + 16 + 20) - fs->ascent;
						
						if (my >= /*y_start*/y_table_top && my < win_h - line_h * 2 - 8) {
							int clicked_row = (my - /*y_start*/y_table_top) / line_h;
							int target_idx = g_scroll_offset + clicked_row;
							if (target_idx >= 0 && target_idx < g_bank.count) {
								g_selected_inst = target_idx;
								need_redraw = 1;
							}
						}
					} else if (g_mode == MODE_INSPECTOR) {
						/* Align insp_y_top with the top edge of the parameter highlight boxes */
						int insp_y_top = (line_h * 2 + 16 + line_h + 6) - fs->ascent;
						
						if (my >= insp_y_top) {
							int row = (my - insp_y_top) / line_h;
							if (row >= 0 && row < 15) {
								int col = (mx < win_w / 2) ? 0 : 15;
								g_selected_param = row + col;
								if (g_selected_param >= BNK_INST_SIZE) g_selected_param = BNK_INST_SIZE - 1;
								need_redraw = 1;
							}
						}
					}
				}

				/* 3. Right Click: Audio Preview */
				else if (btn == Button3) {
					//sound_preview(g_selected_inst);
					 if (bank_is_loaded()) {
						if (ev.xbutton.state & ShiftMask)
							sound_preview_chord(g_selected_inst);
						else
							sound_preview(g_selected_inst);
					}
					need_redraw = 1;
				}
			}
        }

        if (ev.type == KeyPress) {
            KeySym ks;
            char text[16];
            int len = XLookupString(&ev.xkey, text, sizeof(text) - 1, &ks, NULL);
            int shift_held = (ev.xkey.state & ShiftMask) != 0;
            text[len] = '\0';

            if (g_mode == MODE_EDIT_NAME) {
                if (ks == XK_Return) {
					Instrument *ins = &g_bank.inst[g_selected_inst];

					if (strcmp(ins->name_disp, g_input_buf) != 0) {
						size_t n;

						/*
						 * Display string: 8 visible characters plus terminating NUL.
						 */
						strncpy(ins->name_disp, g_input_buf, BNK_NAME_VISIBLE_MAX);
						ins->name_disp[BNK_NAME_VISIBLE_MAX] = '\0';

						/*
						 * name_raw[] stores bytes 2..11 of the 12-byte name record:
						 *
						 *   name_raw[0] = original used/flag byte, preserve it
						 *   name_raw[1..9] = char[9] name data
						 *
						 * Clear only the actual stored name field.
						 */
						memset(ins->name_raw + 1, 0, BNK_NAME_STORED_SIZE);

						n = strlen(g_input_buf);
						if (n > BNK_NAME_VISIBLE_MAX)
							n = BNK_NAME_VISIBLE_MAX;

						memcpy(ins->name_raw + 1, g_input_buf, n);

						g_bank.dirty = 1;

						snprintf(g_status_msg, sizeof(g_status_msg),
								 "Renamed instrument #%03d to '%s'",
								 g_selected_inst, ins->name_disp);
					} else {
						snprintf(g_status_msg, sizeof(g_status_msg),
								 "Name unchanged.");
					}

					g_mode = MODE_LIST;
				} else if (ks == XK_Escape) {
					/* Explicitly restore display name from backup */
					strncpy(g_bank.inst[g_selected_inst].name_disp, g_orig_name, 10);
					g_bank.inst[g_selected_inst].name_disp[10] = '\0';

					g_mode = MODE_LIST;
					snprintf(g_status_msg, sizeof(g_status_msg), "Rename cancelled (original name kept).");
               
                } else if (ks == XK_BackSpace) {
                    if (g_input_len > 0) g_input_buf[--g_input_len] = '\0';
                } else if (len > 0 && g_input_len < BNK_NAME_VISIBLE_MAX &&
           (isalnum((unsigned char)text[0]) ||
            text[0] == '-' || text[0] == '_' || text[0] == ' ')) {
                    g_input_buf[g_input_len++] = text[0];
                    g_input_buf[g_input_len] = '\0';
                }
            } else if (g_mode == MODE_LIST) {
				if (ks != XK_l && ks != XK_L)
					g_confirm_load = 0;
                switch (ks) {
                    case XK_Up:
						if (bank_is_loaded() && g_selected_inst > 0)
                        /*if (g_selected_inst > 0)*/ g_selected_inst--;
                        break;
                    case XK_Down:
						if (bank_is_loaded() && g_selected_inst < (int)g_bank.count - 1)
                        /*if (g_selected_inst < g_bank.count - 1)*/ g_selected_inst++;
                        break;
                    case XK_Page_Up:
                        g_selected_inst -= 15;
                        if (g_selected_inst < 0) g_selected_inst = 0;
                        break;
                    case XK_Page_Down:
                        g_selected_inst += 15;
                        if (g_selected_inst >= g_bank.count) g_selected_inst = g_bank.count - 1;
                        break;
                    case XK_Return:
                    case XK_Tab:
						if (bank_is_loaded()) {
							g_mode = MODE_INSPECTOR;
							g_selected_param = 0;
						}
                        break;
                    case XK_l:
					case XK_L:
						if (g_bank.dirty && !g_confirm_load) {
							g_confirm_load = 1;
							snprintf(g_status_msg, sizeof(g_status_msg),
									 "Unsaved changes. Press L again to discard them and load another bank.");
						} else {
							g_confirm_load = 0;
							load_bank_with_chooser();
						}
						break;
                    case XK_n:
                    case XK_N:
                        g_mode = MODE_EDIT_NAME;
						/* Back up original display name */
						strncpy(g_orig_name, g_bank.inst[g_selected_inst].name_disp, 10);
						g_orig_name[10] = '\0';
						/* Pre-fill the editable input buffer */
						strncpy(g_input_buf, g_bank.inst[g_selected_inst].name_disp, 10);
						g_input_buf[10] = '\0';
						g_input_len = strlen(g_input_buf);
						
						snprintf(g_status_msg, sizeof(g_status_msg), "Renaming instrument #%03d...", g_selected_inst);

                        g_input_buf[10] = '\0';
                        g_input_len = strlen(g_input_buf);
                        break;
                    case XK_p:
					case XK_P:
					case XK_space:
						if (!bank_is_loaded()) {
							snprintf(g_status_msg, sizeof(g_status_msg),
									 "No bank is loaded. Press L to load a .bnk file.");
						} else if (shift_held && ks != XK_space) {
							sound_preview_chord(g_selected_inst);
						} else {
							sound_preview(g_selected_inst);
						}
						break;
                    case XK_s:
                    case XK_S:
						if (bank_is_loaded()) {
							if (bnk_save(g_bank.filepath))
								//set_status("Bank saved.", 0);
								snprintf(g_status_msg, sizeof(g_status_msg), "Successfully saved to '%s'", g_bank.filepath);
								
							else
								//set_status("ERROR: Could not save bank.", 0);
								snprintf(g_status_msg, sizeof(g_status_msg), "ERROR: Failed to save '%s'!", g_bank.filepath);
						} else {
							//set_status("No bank is loaded. Press L to load a .bnk file.", 0);
							snprintf(g_status_msg, sizeof(g_status_msg), "No bank is loaded. Press L to load a .bnk file.");
						}
                        break;
                    case XK_q:
                    case XK_Q:
                    case XK_Escape:
                        running = 0;
                        break;
                }
            } else if (g_mode == MODE_INSPECTOR) {
                Instrument *ins = &g_bank.inst[g_selected_inst];
                switch (ks) {
                    case XK_Up:
                        if (g_selected_param > 0) g_selected_param--;
                        break;
                    case XK_Down:
                        if (g_selected_param < BNK_INST_SIZE - 1) g_selected_param++;
                        break;
                    case XK_Left:
                        if (g_selected_param >= 15) g_selected_param -= 15;
                        break;
                    case XK_Right:
                        if (g_selected_param < 15) g_selected_param += 15;
                        break;
                    case XK_plus:
                    case XK_equal:
                    case XK_bracketright:
                        if (ins->raw[g_selected_param] < g_param_desc[g_selected_param].max_val) {
                            ins->raw[g_selected_param]++;
                            g_bank.dirty = 1;
                        }
                        break;
                    case XK_minus:
                    case XK_underscore:
                    case XK_bracketleft:
                        if (ins->raw[g_selected_param] > 0) {
                            ins->raw[g_selected_param]--;
                            g_bank.dirty = 1;
                        }
                        break;
                    case XK_p:
					case XK_P:
					case XK_space:
						if (shift_held && ks != XK_space)
							sound_preview_chord(g_selected_inst);
						else
							sound_preview(g_selected_inst);
						break;
                    case XK_Return:
                    case XK_Tab:
                    case XK_Escape:
                        g_mode = MODE_LIST;
                        snprintf(g_status_msg, sizeof(g_status_msg), "Back to list view.");
                        break;
                }
            }

            draw_ui(dpy, backbuf, gc, fs, win_w, win_h, col_bg, col_fg, col_hi, col_sel, col_dim);
            XCopyArea(dpy, backbuf, win, gc, 0, 0, win_w, win_h, 0, 0);
            need_redraw = 1;
        }
        if (need_redraw) {
			draw_ui(dpy, backbuf, gc, fs, win_w, win_h, col_bg, col_fg, col_hi, col_sel, col_dim);
			XCopyArea(dpy, backbuf, win, gc, 0, 0, win_w, win_h, 0, 0);
			XFlush(dpy);
		}
        
    }

    XFreePixmap(dpy, backbuf);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
