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
 *   ./xadlbank [soundbank.bnk]
 *
 * BNK handling:
 *   The complete file is kept in memory and edits are applied to the bytes
 *   where they live (name table entry / data record). Saving writes that
 *   image back, so the header, offsets, table order, shared data records,
 *   unused records and any trailing bytes are preserved exactly.
 */

/* POSIX (mkstemp, fchmod, fsync, realpath) plus BSD bits (strcasecmp in
 * xfileselect), also when built with plain -std=c99. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define BNK_HEADER_SIZE      28  /* usual header incl. 8 reserved bytes */
#define BNK_MIN_HEADER_SIZE  20  /* some banks put the name table here */
#define BNK_NAME_SIZE        12
#define BNK_INST_SIZE        30
#define BNK_NAME_FIELD       9   /* char[9] at name entry offset 3 */
#define BNK_NAME_VISIBLE_MAX 8   /* 8 chars + NUL when written by us */
#define BNK_MAX_FILE_SIZE    (16u * 1024u * 1024u)

#define WIN_WIDTH   800
#define WIN_HEIGHT  600

/* Text: xbmfont (xbmfont/), bitmap fonts built into the program, so xadlbank
   looks the same on every X server. 6x13 looks like X's own "fixed" font;
   PRETTYFONTS builds use 9x15 (the window is then made wide enough for it). */
#define XBMFONT_IMPLEMENTATION
#include "xbmfont.h"
#ifdef PRETTYFONTS
#include "fonts/xbf_fixed_9x15.h"
#define TEXT_FONT   xbf_fixed_9x15
#else
#include "fonts/xbf_fixed_6x13.h"
#define TEXT_FONT   xbf_fixed_6x13
#endif

#define FC_DEFAULT_WIDTH   400   /* for a 13 px font; grows with a bigger one */
#define FC_DEFAULT_HEIGHT  500
#define FC_FONT            TEXT_FONT

#include "xfileselect/xfileselect.c"

/* Instrument = one entry of the name table */
typedef struct {
    uint16_t data_index;   /* record index into the data section */
    uint8_t  flags;        /* "used" flag byte of the name entry */
    int      has_data;     /* 0 if data_index points outside the file */
    size_t   name_pos;     /* file offset of the 12-byte name entry */
    uint8_t *raw;          /* 30 bytes inside the image, or g_no_data */
    char     name_disp[BNK_NAME_FIELD + 1];
} Instrument;

typedef struct {
    uint8_t    *image;     /* complete file contents */
    size_t      size;
    uint8_t     ver_major;
    uint8_t     ver_minor;
    uint16_t    num_used;
    uint16_t    count;     /* number of usable name entries */
    uint16_t    count_declared;
    size_t      off_name;
    size_t      off_data;
    unsigned    missing;   /* entries without a data record */
    Instrument *inst;
    int         dirty;
    char        filepath[PATH_MAX];
    char        note[160]; /* load warnings, empty if none */
} SoundBank;

static SoundBank g_bank;
static uint8_t g_no_data[BNK_INST_SIZE];

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
static char g_input_buf[BNK_NAME_FIELD + 1] = {0};
static int  g_input_len = 0;
static char g_status_msg[PATH_MAX + 256] = "";

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
    return g_bank.image != NULL && g_bank.count > 0;
}

static void set_window_icon(Display *dpy, Window win) {

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
static inline uint32_t rd_le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

/* ------------------------------------------------------------------ */
/* BNK model                                                          */
/* ------------------------------------------------------------------ */

static int range_ok(uint64_t total, uint64_t offset, uint64_t length)
{
    return offset <= total && length <= total - offset;
}

static void bnk_free(SoundBank *b)
{
    free(b->image);
    free(b->inst);
    memset(b, 0, sizeof(*b));
}

/* Build the display name from the 9-byte name field (stops at NUL). */
static void inst_refresh_name(const SoundBank *b, Instrument *ins)
{
    const uint8_t *p = b->image + ins->name_pos + 3;
    int c, len = 0;

    for (c = 0; c < BNK_NAME_FIELD && p[c] != '\0'; c++) {
        unsigned char ch = p[c];
        ins->name_disp[len++] = (ch < 0x20 || ch >= 0x7f) ? '?' : (char)ch;
    }
    ins->name_disp[len] = '\0';

    while (len > 0 && ins->name_disp[len - 1] == ' ')
        ins->name_disp[--len] = '\0';
}

/*
 * Load a bank into *out. Returns 1 on success. On failure *out is left
 * empty and err holds a human-readable reason. The caller's current bank
 * is never touched, so a failed load does not lose open work.
 *
 * The loader is tolerant: a truncated name table is clamped, and entries
 * that refer to data records beyond the end of the file are kept (shown
 * as missing, read-only) instead of rejecting the whole bank.
 */
static int bnk_load(const char *filename, SoundBank *out, char *err, size_t errlen)
{
    FILE *fp;
    uint8_t *buf;
    long end;
    size_t sz;
    uint32_t off_name, off_data;
    uint16_t declared, count, i;
    uint64_t avail;

    memset(out, 0, sizeof(*out));

    fp = fopen(filename, "rb");
    if (!fp) {
        snprintf(err, errlen, "Cannot open '%s': %s", filename, strerror(errno));
        return 0;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (end = ftell(fp)) < 0) {
        snprintf(err, errlen, "Cannot determine size of '%s'", filename);
        fclose(fp);
        return 0;
    }
    rewind(fp);
    sz = (size_t)end;

    if (sz < BNK_HEADER_SIZE) {
        snprintf(err, errlen, "File too small to be a BNK (%lu bytes)", (unsigned long)sz);
        fclose(fp);
        return 0;
    }
    if (sz > BNK_MAX_FILE_SIZE) {
        snprintf(err, errlen, "File too large to be a BNK (%lu bytes)", (unsigned long)sz);
        fclose(fp);
        return 0;
    }

    buf = malloc(sz);
    if (!buf) {
        snprintf(err, errlen, "Out of memory");
        fclose(fp);
        return 0;
    }
    if (fread(buf, 1, sz, fp) != sz) {
        snprintf(err, errlen, "Read error on '%s'", filename);
        free(buf);
        fclose(fp);
        return 0;
    }
    fclose(fp);

    if (memcmp(buf + 2, "ADLIB-", 6) != 0) {
        if (memcmp(buf, "Accomp. Bank", 12) == 0)
            snprintf(err, errlen, "AdLib Gold BNK2 (OPL3) banks are not supported");
        else
            snprintf(err, errlen, "Not an AdLib BNK (no 'ADLIB-' signature)");
        free(buf);
        return 0;
    }

    /*
     * Header:
     *   0      major version
     *   1      minor version
     *   2..7   "ADLIB-"
     *   8..9   number of used entries
     *   10..11 number of name entries
     *   12..15 name-table offset
     *   16..19 data-section offset
     *   20..27 reserved (some banks omit these and start the name
 *          table at offset 20)
     */
    declared = rd_le16(buf + 10);
    off_name = rd_le32(buf + 12);
    off_data = rd_le32(buf + 16);

    if (declared == 0) {
        snprintf(err, errlen, "Bank contains no instrument entries");
        free(buf);
        return 0;
    }
    if (off_name < BNK_MIN_HEADER_SIZE || off_name >= sz) {
        snprintf(err, errlen, "Name table offset %lu is invalid (header or past end of file)",
                 (unsigned long)off_name);
        free(buf);
        return 0;
    }

    avail = (sz - off_name) / BNK_NAME_SIZE;
    count = (avail < declared) ? (uint16_t)avail : declared;
    if (count == 0) {
        snprintf(err, errlen, "Name table is empty or truncated");
        free(buf);
        return 0;
    }

    out->inst = calloc(count, sizeof(Instrument));
    if (!out->inst) {
        snprintf(err, errlen, "Out of memory");
        free(buf);
        return 0;
    }

    out->image          = buf;
    out->size           = sz;
    out->ver_major      = buf[0];
    out->ver_minor      = buf[1];
    out->num_used       = rd_le16(buf + 8);
    out->count          = count;
    out->count_declared = declared;
    out->off_name       = off_name;
    out->off_data       = off_data;

    for (i = 0; i < count; i++) {
        Instrument *ins = &out->inst[i];
        uint64_t data_pos;

        ins->name_pos   = off_name + (size_t)i * BNK_NAME_SIZE;
        ins->data_index = rd_le16(buf + ins->name_pos);
        ins->flags      = buf[ins->name_pos + 2];

        data_pos = (uint64_t)off_data + (uint64_t)ins->data_index * BNK_INST_SIZE;
        if (range_ok(sz, data_pos, BNK_INST_SIZE)) {
            ins->raw = buf + data_pos;
            ins->has_data = 1;
        } else {
            ins->raw = g_no_data;
            ins->has_data = 0;
            out->missing++;
        }

        inst_refresh_name(out, ins);
    }

    if (count < declared && out->missing)
        snprintf(out->note, sizeof(out->note),
                 "warning: name table truncated (%u of %u), %u without data",
                 (unsigned)count, (unsigned)declared, out->missing);
    else if (count < declared)
        snprintf(out->note, sizeof(out->note),
                 "warning: name table truncated (%u of %u entries)",
                 (unsigned)count, (unsigned)declared);
    else if (out->missing)
        snprintf(out->note, sizeof(out->note),
                 "warning: %u entr%s without data record (read-only)",
                 out->missing, out->missing == 1 ? "y" : "ies");

    strncpy(out->filepath, filename, sizeof(out->filepath) - 1);
    out->filepath[sizeof(out->filepath) - 1] = '\0';
    return 1;
}

/*
 * Save the in-memory image. Writes to a temporary file in the same
 * directory and renames it over the original only after every write
 * succeeded, so a failure never leaves a truncated bank behind.
 */
static int bnk_save(SoundBank *b, char *err, size_t errlen)
{
    char target[PATH_MAX];
    char tmp[PATH_MAX + 16];
    struct stat st;
    int have_st, fd;
    size_t done = 0;

    if (!b->image) {
        snprintf(err, errlen, "No bank loaded");
        return 0;
    }

    /* Follow symlinks so the link itself is not replaced by a file. */
    if (!realpath(b->filepath, target)) {
        strncpy(target, b->filepath, sizeof(target) - 1);
        target[sizeof(target) - 1] = '\0';
    }

    have_st = (stat(target, &st) == 0);

    snprintf(tmp, sizeof(tmp), "%s.XXXXXX", target);
    fd = mkstemp(tmp);
    if (fd < 0) {
        snprintf(err, errlen, "Cannot create temporary file: %s", strerror(errno));
        return 0;
    }

    if (have_st)
        (void)fchmod(fd, st.st_mode & 07777);

    while (done < b->size) {
        ssize_t n = write(fd, b->image + done, b->size - done);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            snprintf(err, errlen, "Write failed: %s", strerror(errno));
            close(fd);
            unlink(tmp);
            return 0;
        }
        done += (size_t)n;
    }

    if (fsync(fd) != 0) {
        snprintf(err, errlen, "Write failed: %s", strerror(errno));
        close(fd);
        unlink(tmp);
        return 0;
    }
    if (close(fd) != 0) {
        snprintf(err, errlen, "Write failed: %s", strerror(errno));
        unlink(tmp);
        return 0;
    }
    if (rename(tmp, target) != 0) {
        snprintf(err, errlen, "Cannot replace '%s': %s", target, strerror(errno));
        unlink(tmp);
        return 0;
    }

    b->dirty = 0;
    return 1;
}

/* Write a new name (max 8 chars) into the entry, preserving index + flag. */
static void bnk_set_name(SoundBank *b, Instrument *ins, const char *name)
{
    uint8_t *p = b->image + ins->name_pos + 3;
    size_t n = strlen(name);

    if (n > BNK_NAME_VISIBLE_MAX)
        n = BNK_NAME_VISIBLE_MAX;

    memset(p, 0, BNK_NAME_FIELD);
    memcpy(p, name, n);
    inst_refresh_name(b, ins);
    b->dirty = 1;
}

/* ------------------------------------------------------------------ */
/* Preview via mop                                                    */
/* ------------------------------------------------------------------ */

static const char *inst_label(const Instrument *ins)
{
    return ins->name_disp[0] ? ins->name_disp : "<No Name>";
}

static int preview_check(int inst_idx)
{
    if (!bank_is_loaded()) {
        snprintf(g_status_msg, sizeof(g_status_msg),
                 "No bank is loaded. Press L to load a .bnk file.");
        return 0;
    }
    if (inst_idx < 0 || inst_idx >= (int)g_bank.count)
        return 0;
    if (!g_bank.inst[inst_idx].has_data) {
        snprintf(g_status_msg, sizeof(g_status_msg),
                 "Instrument #%03d has no data record - nothing to play.", inst_idx);
        return 0;
    }
    return 1;
}

static void inst_hex(const Instrument *ins, char *hex)
{
    int i;
    for (i = 0; i < BNK_INST_SIZE; i++)
        sprintf(hex + (i * 2), "%02X", (unsigned)ins->raw[i]);
    hex[BNK_INST_SIZE * 2] = '\0';
}

static void sound_preview(int inst_idx)
{
    char hex[BNK_INST_SIZE * 2 + 1];
    char cmd[256];

    if (!preview_check(inst_idx))
        return;

    inst_hex(&g_bank.inst[inst_idx], hex);
    snprintf(cmd, sizeof(cmd), "mop -r %s -n 60 -d 1000 > /dev/null 2>&1 &", hex);
    if (system(cmd) != 0) {
        snprintf(g_status_msg, sizeof(g_status_msg), "Could not start mop.");
        return;
    }

    snprintf(g_status_msg, sizeof(g_status_msg), "Note preview #%03d [%s] on OPL2...",
             inst_idx, inst_label(&g_bank.inst[inst_idx]));
}

static void sound_preview_chord(int inst_idx)
{
    char hex[BNK_INST_SIZE * 2 + 1];
    char cmd[256];

    if (!preview_check(inst_idx))
        return;

    inst_hex(&g_bank.inst[inst_idx], hex);
    snprintf(cmd, sizeof(cmd),
             "mop -r %s -n \"60,64,67\" -d 1500 > /dev/null 2>&1 &", hex);
    if (system(cmd) != 0) {
        snprintf(g_status_msg, sizeof(g_status_msg), "Could not start mop.");
        return;
    }

    snprintf(g_status_msg, sizeof(g_status_msg),
             "Chord preview #%03d [%s]: C4 E4 G4",
             inst_idx, inst_label(&g_bank.inst[inst_idx]));
}

/* ------------------------------------------------------------------ */
/* Selection / editing helpers                                        */
/* ------------------------------------------------------------------ */

static void clamp_selection(void)
{
    if (!bank_is_loaded()) {
        g_selected_inst = 0;
        g_scroll_offset = 0;
        if (g_mode != MODE_LIST)
            g_mode = MODE_LIST;
        return;
    }
    if (g_selected_inst >= (int)g_bank.count)
        g_selected_inst = (int)g_bank.count - 1;
    if (g_selected_inst < 0)
        g_selected_inst = 0;
    if (g_selected_param < 0)
        g_selected_param = 0;
    if (g_selected_param >= BNK_INST_SIZE)
        g_selected_param = BNK_INST_SIZE - 1;
}

static void adjust_param(int delta)
{
    Instrument *ins;
    uint8_t *v;

    if (!bank_is_loaded())
        return;
    ins = &g_bank.inst[g_selected_inst];
    if (!ins->has_data) {
        snprintf(g_status_msg, sizeof(g_status_msg),
                 "Instrument #%03d has no data record (read-only).", g_selected_inst);
        return;
    }

    v = &ins->raw[g_selected_param];
    if (delta > 0 && *v < g_param_desc[g_selected_param].max_val) {
        (*v)++;
        g_bank.dirty = 1;
    } else if (delta < 0 && *v > 0) {
        (*v)--;
        g_bank.dirty = 1;
    }
}

static void enter_inspector(void)
{
    if (!bank_is_loaded())
        return;
    if (!g_bank.inst[g_selected_inst].has_data) {
        snprintf(g_status_msg, sizeof(g_status_msg),
                 "Instrument #%03d refers to missing record %u.",
                 g_selected_inst, (unsigned)g_bank.inst[g_selected_inst].data_index);
        return;
    }
    g_mode = MODE_INSPECTOR;
    g_selected_param = 0;
}

static void load_status(const char *verb)
{
    snprintf(g_status_msg, sizeof(g_status_msg),
             "%s %u instrument%s from %s%s%s",
             verb,
             (unsigned)g_bank.count,
             g_bank.count == 1 ? "" : "s",
             g_bank.filepath,
             g_bank.note[0] ? " - " : "",
             g_bank.note);
}

/* ------------------------------------------------------------------ */
/* Drawing                                                            */
/* ------------------------------------------------------------------ */

/* Text in the GC's foreground colour, ending in "..." if it would pass `right` */
static void draw_text(Display *dpy, Drawable d, GC gc, const xbf_ctx *fs,
                      int x, int y, int right, const char *s)
{
    XGCValues v;

    XGetGCValues(dpy, gc, GCForeground, &v);   /* Xlib's own copy: no round trip */
    xbf_draw_fit((xbf_ctx *)fs, d, x, y, v.foreground, s, right - x, XBF_CUT_END);
}

static int line_height(const xbf_ctx *fs)
{
    return xbf_line_height(fs) + 4;
}

/* Baseline of the first list row (shared by drawing and hit-testing). */
static int list_first_row_y(const xbf_ctx *fs)
{
    return line_height(fs) * 2 + 16 + 6 + 14;
}

/* Baseline of the first inspector row. */
static int insp_first_row_y(const xbf_ctx *fs)
{
    return line_height(fs) * 2 + 16 + line_height(fs) + 6;
}

static unsigned long alloc_color(Display *dpy, int screen, const char *spec,
                                 unsigned long fallback)
{
    XColor c;
    Colormap cmap = DefaultColormap(dpy, screen);

    if (XParseColor(dpy, cmap, spec, &c) && XAllocColor(dpy, cmap, &c))
        return c.pixel;
    return fallback;
}

typedef struct {
    unsigned long bg, fg, hi, sel, dim;
} Palette;

static void draw_ui(Display *dpy, Pixmap buf, GC gc, xbf_ctx *fs,
                    int width, int height, const Palette *pal)
{
    int line_h = line_height(fs);
    char str[PATH_MAX + 64];
    int y_start;

    /* Clear Canvas */
    XSetForeground(dpy, gc, pal->bg);
    XFillRectangle(dpy, buf, gc, 0, 0, width, height);

    /* Header Bar */
    XSetForeground(dpy, gc, pal->sel);
    XFillRectangle(dpy, buf, gc, 0, 0, width, line_h * 2 + 6);

    snprintf(str, sizeof(str), " xadlbank - %s %s",
             bank_is_loaded() ? g_bank.filepath : "(no bank)",
             g_bank.dirty ? "[MODIFIED]" : "");
    /* a long path is shortened in the middle, so [MODIFIED] stays visible */
    xbf_draw_fit(fs, buf, 10, xbf_ascent(fs) + 4, pal->hi, str, width - 20, XBF_CUT_MIDDLE);

    XSetForeground(dpy, gc, pal->fg);
    snprintf(str, sizeof(str),
             " Instruments: %u  |  Used: %u  |  Version: %u.%u  |  Mode: %s",
             (unsigned)g_bank.count, (unsigned)g_bank.num_used,
             (unsigned)g_bank.ver_major, (unsigned)g_bank.ver_minor,
             g_mode == MODE_INSPECTOR ? "INSPECTOR" : "LIST");
    draw_text(dpy, buf, gc, fs, 10, line_h + xbf_ascent(fs) + 2, width - 10, str);

    y_start = line_h * 2 + 16;

    if (g_mode == MODE_LIST || g_mode == MODE_EDIT_NAME) {
        int first_y = list_first_row_y(fs);
        int visible_rows, r;
        /* ID column grows with the bank so large banks stay aligned */
        int id_w = g_bank.count > 10000 ? 5 : (g_bank.count > 1000 ? 4 : 3);

        /* Column Headers (columns line up with the row format below) */
        XSetForeground(dpy, gc, pal->dim);
        snprintf(str, sizeof(str),
                 "%-*sREC     NAME      MOD LV AT DC SU RL  CAR LV AT DC SU RL  FB",
                 id_w + 2, "ID");
        draw_text(dpy, buf, gc, fs, 10, y_start, width - 10, str);
        XDrawLine(dpy, buf, gc, 10, y_start + 6, width - 10, y_start + 6);

        if (!bank_is_loaded()) {
            XSetForeground(dpy, gc, pal->fg);
            draw_text(dpy, buf, gc, fs, 10, first_y, width - 10, "No bank loaded. Press L to load a .bnk file.");
        }

        visible_rows = (height - first_y - line_h * 2) / line_h;
        if (visible_rows < 1) visible_rows = 1;

        /* Auto-scroll clamp */
        if (g_selected_inst < g_scroll_offset) g_scroll_offset = g_selected_inst;
        if (g_selected_inst >= g_scroll_offset + visible_rows) g_scroll_offset = g_selected_inst - visible_rows + 1;
        if (g_scroll_offset < 0) g_scroll_offset = 0;

        for (r = 0; r < visible_rows && bank_is_loaded(); r++) {
            int idx = g_scroll_offset + r;
            int row_y = first_y + (r * line_h);
            Instrument *ins;
            const char *show_name;

            if (idx >= (int)g_bank.count) break;
            ins = &g_bank.inst[idx];

            /* Selection Box */
            if (idx == g_selected_inst) {
                XSetForeground(dpy, gc, pal->sel);
                XFillRectangle(dpy, buf, gc, 8, row_y - xbf_ascent(fs), width - 16, line_h);
                XSetForeground(dpy, gc, pal->hi);
            } else if (!ins->has_data || !ins->flags) {
                XSetForeground(dpy, gc, pal->dim);  /* missing or unused */
            } else {
                XSetForeground(dpy, gc, pal->fg);
            }

            if (g_mode == MODE_EDIT_NAME && idx == g_selected_inst)
                show_name = g_input_buf; /* Live preview of what user is typing */
            else
                show_name = inst_label(ins);

            if (ins->has_data) {
                snprintf(str, sizeof(str),
                         "#%0*d R:%-5u %-10s    "
                         "%02X %02X %02X %02X %02X      "
                         "%02X %02X %02X %02X %02X  "
                         "%02X",
                         id_w, idx, (unsigned)ins->data_index, show_name,
                         /* Mod: level, attack, decay, sustain, release */
                         ins->raw[10], ins->raw[5], ins->raw[8],
                         ins->raw[6],  ins->raw[9],
                         /* Carrier: level, attack, decay, sustain, release */
                         ins->raw[23], ins->raw[18], ins->raw[21],
                         ins->raw[19], ins->raw[22],
                         /* Feedback */
                         ins->raw[4]);
            } else {
                snprintf(str, sizeof(str),
                         "#%0*d R:%-5u %-10s    -- missing data record --",
                         id_w, idx, (unsigned)ins->data_index, show_name);
            }

            draw_text(dpy, buf, gc, fs, 10, row_y, width - 10, str);
        }

        /* Name Edit In-place Overlay */
        if (g_mode == MODE_EDIT_NAME && bank_is_loaded()) {
            int cw = xbf_char_width(fs);
            XSetForeground(dpy, gc, pal->hi);
            XDrawRectangle(dpy, buf, gc, 10 + cw * (id_w + 10) - 2,
                           first_y + (g_selected_inst - g_scroll_offset) * line_h - xbf_ascent(fs),
                           cw * 10 + 4, line_h);
        }

    } else if (g_mode == MODE_INSPECTOR && bank_is_loaded()) {
        /* Modal Instrument Inspector */
        Instrument *ins = &g_bank.inst[g_selected_inst];
        int col1_x = 30;
        int col2_x = width / 2 + 10;
        int row_count = 15;
        int first_y = insp_first_row_y(fs);
        int p;

        XSetForeground(dpy, gc, pal->hi);
        snprintf(str, sizeof(str),
                 "=== INSPECTOR: Instrument #%03d [%s] (Record: %u, Flag: %u) ===",
                 g_selected_inst, inst_label(ins),
                 (unsigned)ins->data_index, (unsigned)ins->flags);
        draw_text(dpy, buf, gc, fs, 20, y_start, width - 10, str);

        /* Draw 30 parameters in 2 columns */
        for (p = 0; p < BNK_INST_SIZE; p++) {
            int cx = (p < row_count) ? col1_x : col2_x;
            int cy = first_y + ((p % row_count) * line_h);

            if (p == g_selected_param) {
                XSetForeground(dpy, gc, pal->sel);
                XFillRectangle(dpy, buf, gc, cx - 4, cy - xbf_ascent(fs), (width / 2) - 30, line_h);
                XSetForeground(dpy, gc, pal->hi);
            } else {
                XSetForeground(dpy, gc, pal->fg);
            }

            snprintf(str, sizeof(str), "[%02d] %-32s : 0x%02X (%3u)",
                     p, g_param_desc[p].label, ins->raw[p], ins->raw[p]);
            draw_text(dpy, buf, gc, fs, cx, cy, cx - 4 + (width / 2) - 30, str);
        }
    }

    /* Footer / Status Bar */
    XSetForeground(dpy, gc, pal->dim);
    XDrawLine(dpy, buf, gc, 0, height - line_h - 8, width, height - line_h - 8);

    XSetForeground(dpy, gc, pal->hi);
    if (g_mode == MODE_EDIT_NAME) {
        snprintf(str, sizeof(str), "EDIT NAME: %s_  [ENTER] Commit  [ESC] Cancel", g_input_buf);
        draw_text(dpy, buf, gc, fs, 10, height - 30, width - 10, str);
    } else {
        draw_text(dpy, buf, gc, fs, 10, height - 30, width - 10, g_status_msg);
    }

    /* Bottom line: Permanent key shortcuts */
    XSetForeground(dpy, gc, pal->fg);
    if (g_mode == MODE_INSPECTOR)
        draw_text(dpy, buf, gc, fs, 10, height - 8, width - 10,
                  "INSPECTOR: [ARROWS] Select [+/-] Inc/Dec [p] Note [Shift+p] Chord [ESC] Back");
    else
        draw_text(dpy, buf, gc, fs, 10, height - 8, width - 10,
                  "[l] Load  [UP/DOWN] Select  [ENTER] Inspect  [p] Note [Shift+p] Chord [n] Rename [s] Save [Q] Quit");
}

static int load_bank_with_chooser(void)
{
    char err[PATH_MAX + 64];
    SoundBank nb;
    char *path;

    path = x11_filechooser(
        ".",                             // start_path (NULL => current dir)
        "Open File",                     // window title
        "Select an AdLib .bnk file",     // header text (shown above file list)
        ".bnk,.BNK",                     // extension filter (NULL or "" => all)
        FC_MODE_FILE,                    // FC_MODE_FILE or FC_MODE_DIR
        "[q/Esc]: Cancel [Enter]: Open"  // status/help line
    );

    if (path == NULL) {
        snprintf(g_status_msg, sizeof(g_status_msg), "Load cancelled.");
        return 0;
    }

    if (!bnk_load(path, &nb, err, sizeof(err))) {
        snprintf(g_status_msg, sizeof(g_status_msg), "ERROR: %s", err);
        free(path);
        return 0;
    }

    bnk_free(&g_bank);
    g_bank = nb;

    g_selected_inst = 0;
    g_scroll_offset = 0;
    g_selected_param = 0;
    g_mode = MODE_LIST;

    load_status("Loaded");
    free(path);
    return 1;
}

#ifndef XADLBANK_NO_MAIN
int main(int argc, char *argv[])
{
    char err[PATH_MAX + 64];
    Display *dpy;
    int screen;
    Window root, win;
    xbf_ctx *fs = NULL;
    Palette pal;
    GC gc;
    /* wide enough for the inspector's two columns of 50 characters
       (each highlight box is width / 2 - 30 pixels) */
    int win_w = 80 + 100 * TEXT_FONT.width > WIN_WIDTH ? 80 + 100 * TEXT_FONT.width : WIN_WIDTH;
    int win_h = WIN_HEIGHT;
    Pixmap backbuf;
    Atom wm_delete;
    int running = 1;

    memset(&g_bank, 0, sizeof(g_bank));

    if (argc >= 2) {
        if (!bnk_load(argv[1], &g_bank, err, sizeof(err))) {
            fprintf(stderr, "Error: Unable to load '%s': %s\n", argv[1], err);
            return 1;
        }
        if (g_bank.note[0])
            fprintf(stderr, "%s: %s\n", argv[1], g_bank.note);
        load_status("Loaded");
    } else {
        snprintf(g_status_msg, sizeof(g_status_msg),
                 "No bank is loaded. Press L to load a .bnk file.");
    }

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "Error: Cannot open X Display. Check $DISPLAY.\n");
        bnk_free(&g_bank);
        return 1;
    }

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);

    pal.bg  = BlackPixel(dpy, screen);
    pal.fg  = WhitePixel(dpy, screen);
    pal.hi  = alloc_color(dpy, screen, "#00FFFF", pal.fg);  /* Cyan */
    pal.sel = alloc_color(dpy, screen, "#003366", pal.bg);  /* Dark Blue */
    pal.dim = alloc_color(dpy, screen, "#888888", pal.fg);  /* Gray */

    win = XCreateSimpleWindow(dpy, root, 100, 100, win_w, win_h, 1, pal.dim, pal.bg);
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | StructureNotifyMask | ButtonPressMask);
    XStoreName(dpy, win, "xadlbank - OPL2 Soundbank Editor");
    XMapWindow(dpy, win);

    set_window_icon(dpy, win);

    gc = XCreateGC(dpy, win, 0, NULL);
    fs = xbf_create(dpy, win, &TEXT_FONT, 1);
    if (!fs) {
        fprintf(stderr, "Error: Could not upload the font to the X server - closing.\n");
        XCloseDisplay(dpy);
        bnk_free(&g_bank);
        return 1;
    }

    backbuf = XCreatePixmap(dpy, win, win_w, win_h, DefaultDepth(dpy, screen));

    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete, 1);

    while (running) {
        XEvent ev;
        int need_redraw = 0;

        XNextEvent(dpy, &ev);

        if (ev.type == ConfigureNotify) {
            if (ev.xconfigure.width != win_w || ev.xconfigure.height != win_h) {
                win_w = ev.xconfigure.width;
                win_h = ev.xconfigure.height;
                XFreePixmap(dpy, backbuf);
                backbuf = XCreatePixmap(dpy, win, win_w, win_h, DefaultDepth(dpy, screen));
            }
            need_redraw = 1;
        } else if (ev.type == Expose) {
            need_redraw = 1;
        } else if (ev.type == ClientMessage) {
            if ((Atom)ev.xclient.data.l[0] == wm_delete) running = 0;
        } else if (ev.type == ButtonPress && g_mode != MODE_EDIT_NAME) {
            int my = ev.xbutton.y;
            int mx = ev.xbutton.x;
            unsigned int btn = ev.xbutton.button;
            int line_h = line_height(fs);

            need_redraw = 1;

            if (!bank_is_loaded()) {
                /* nothing to select, scroll or play */
            } else if (btn == Button4) {          /* Wheel UP */
                if (g_mode == MODE_LIST)
                    g_selected_inst -= 3;
                else
                    adjust_param(+1);
            } else if (btn == Button5) {          /* Wheel DOWN */
                if (g_mode == MODE_LIST)
                    g_selected_inst += 3;
                else
                    adjust_param(-1);
            } else if (btn == Button1) {          /* Left Click */
                if (g_mode == MODE_LIST) {
                    /* Align with the top edge of the selection rectangle */
                    int y_table_top = list_first_row_y(fs) - xbf_ascent(fs);

                    if (my >= y_table_top && my < win_h - line_h * 2 - 8) {
                        int target_idx = g_scroll_offset + (my - y_table_top) / line_h;
                        if (target_idx >= 0 && target_idx < (int)g_bank.count)
                            g_selected_inst = target_idx;
                    }
                } else if (g_mode == MODE_INSPECTOR) {
                    int insp_y_top = insp_first_row_y(fs) - xbf_ascent(fs);

                    if (my >= insp_y_top) {
                        int row = (my - insp_y_top) / line_h;
                        if (row >= 0 && row < 15)
                            g_selected_param = row + ((mx < win_w / 2) ? 0 : 15);
                    }
                }
            } else if (btn == Button3) {          /* Right Click: Audio Preview */
                if (ev.xbutton.state & ShiftMask)
                    sound_preview_chord(g_selected_inst);
                else
                    sound_preview(g_selected_inst);
            }
        } else if (ev.type == KeyPress) {
            KeySym ks;
            char text[16];
            int len = XLookupString(&ev.xkey, text, sizeof(text) - 1, &ks, NULL);
            int shift_held = (ev.xkey.state & ShiftMask) != 0;

            if (len < 0) len = 0;
            text[len] = '\0';
            need_redraw = 1;

            if (g_mode == MODE_EDIT_NAME) {
                if (ks == XK_Return || ks == XK_KP_Enter) {
                    Instrument *ins = &g_bank.inst[g_selected_inst];
                    char prefill[BNK_NAME_VISIBLE_MAX + 1];

                    /* Compare with what was pre-filled, so pressing Enter on
                     * an untouched 9-char name does not truncate it. */
                    strncpy(prefill, ins->name_disp, BNK_NAME_VISIBLE_MAX);
                    prefill[BNK_NAME_VISIBLE_MAX] = '\0';

                    if (strcmp(prefill, g_input_buf) != 0) {
                        bnk_set_name(&g_bank, ins, g_input_buf);
                        snprintf(g_status_msg, sizeof(g_status_msg),
                                 "Renamed instrument #%03d to '%s'",
                                 g_selected_inst, ins->name_disp);
                    } else {
                        snprintf(g_status_msg, sizeof(g_status_msg), "Name unchanged.");
                    }
                    g_mode = MODE_LIST;
                } else if (ks == XK_Escape) {
                    /* Nothing was written yet, so the original name is intact. */
                    g_mode = MODE_LIST;
                    snprintf(g_status_msg, sizeof(g_status_msg),
                             "Rename cancelled (original name kept).");
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
                    case XK_Up:        g_selected_inst--;      break;
                    case XK_Down:      g_selected_inst++;      break;
                    case XK_Page_Up:   g_selected_inst -= 15;  break;
                    case XK_Page_Down: g_selected_inst += 15;  break;
                    case XK_Home:      g_selected_inst = 0;    break;
                    case XK_End:       g_selected_inst = (int)g_bank.count - 1; break;
                    case XK_Return:
                    case XK_KP_Enter:
                    case XK_Tab:
                        enter_inspector();
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
                        if (!bank_is_loaded()) {
                            snprintf(g_status_msg, sizeof(g_status_msg),
                                     "No bank is loaded. Press L to load a .bnk file.");
                            break;
                        }
                        g_mode = MODE_EDIT_NAME;
                        /* Pre-fill the editable input buffer (max 8 chars) */
                        strncpy(g_input_buf, g_bank.inst[g_selected_inst].name_disp,
                                BNK_NAME_VISIBLE_MAX);
                        g_input_buf[BNK_NAME_VISIBLE_MAX] = '\0';
                        g_input_len = (int)strlen(g_input_buf);
                        snprintf(g_status_msg, sizeof(g_status_msg),
                                 "Renaming instrument #%03d...", g_selected_inst);
                        break;
                    case XK_p:
                    case XK_P:
                    case XK_space:
                        if (shift_held && ks != XK_space)
                            sound_preview_chord(g_selected_inst);
                        else
                            sound_preview(g_selected_inst);
                        break;
                    case XK_s:
                    case XK_S:
                        if (!bank_is_loaded())
                            snprintf(g_status_msg, sizeof(g_status_msg),
                                     "No bank is loaded. Press L to load a .bnk file.");
                        else if (bnk_save(&g_bank, err, sizeof(err)))
                            snprintf(g_status_msg, sizeof(g_status_msg),
                                     "Successfully saved to '%s'", g_bank.filepath);
                        else
                            snprintf(g_status_msg, sizeof(g_status_msg),
                                     "ERROR: Save failed: %s", err);
                        break;
                    case XK_q:
                    case XK_Q:
                    case XK_Escape:
                        running = 0;
                        break;
                }
            } else if (g_mode == MODE_INSPECTOR) {
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
                    case XK_KP_Add:
                    case XK_bracketright:
                        adjust_param(+1);
                        break;
                    case XK_minus:
                    case XK_underscore:
                    case XK_KP_Subtract:
                    case XK_bracketleft:
                        adjust_param(-1);
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
                    case XK_KP_Enter:
                    case XK_Tab:
                    case XK_Escape:
                        g_mode = MODE_LIST;
                        snprintf(g_status_msg, sizeof(g_status_msg), "Back to list view.");
                        break;
                }
            }
        }

        clamp_selection();

        if (need_redraw && running) {
            draw_ui(dpy, backbuf, gc, fs, win_w, win_h, &pal);
            XCopyArea(dpy, backbuf, win, gc, 0, 0, win_w, win_h, 0, 0);
            XFlush(dpy);
        }
    }

    XFreePixmap(dpy, backbuf);
    XFreeGC(dpy, gc);
    xbf_free(fs);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    bnk_free(&g_bank);
    return 0;
}
#endif /* XADLBANK_NO_MAIN */
