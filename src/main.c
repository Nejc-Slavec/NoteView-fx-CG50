/* NoteView: viewer for .img files made by convert.py (on the PC).
   List (folders first) -> EXE opens -> UP/DOWN scroll, LEFT/RIGHT browse, +/- zoom, arrows pan, F6 info, EXIT back.
   F6 in the list: settings (colour, continuous reading, info bar, key help). */
#include <gint/display.h>
#include <gint/keyboard.h>
#include <gint/gint.h>
#include <gint/bfile.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "view.h"

#define MAX_IMAGES 200
#define MAX_FOLDERS 32

#define C_HINT C_RGB(12, 12, 12)

/* bar colour and the light tint of it for the selection, by settings colour */
static struct { char const *name; color_t head, tint; } const palette[N_COLOURS] = {
	{"Blue",   C_RGB(4, 10, 20),  C_RGB(20, 26, 31)},
	{"Green",  C_RGB(3, 15, 7),   C_RGB(21, 30, 22)},
	{"Red",    C_RGB(21, 4, 4),   C_RGB(31, 22, 22)},
	{"Purple", C_RGB(14, 6, 19),  C_RGB(27, 23, 31)},
	{"Orange", C_RGB(26, 12, 1),  C_RGB(31, 26, 18)},
	{"Teal",   C_RGB(2, 14, 15),  C_RGB(19, 29, 29)},
	{"Gray",   C_RGB(11, 11, 11), C_RGB(26, 26, 26)},
	{"Black",  C_RGB(2, 2, 2),    C_RGB(24, 24, 24)},
};

/* Where the user is; written to NoteView.cfg on EXIT (quit), MENU and when leaving Settings */
static state_t st;
#define C_HEAD (palette[st.set.colour].head)
#define C_SEL  (palette[st.set.colour].tint)

static char folders[MAX_FOLDERS][NAME_LEN];   /* folders with .img files (root only) */
static int nfolders;
static char folder[NAME_LEN];                 /* the folder shown, "" = root */
static char names[MAX_IMAGES][NAME_LEN];      /* .img files in it */
static int count;
static int nfold;                             /* list entries before the images: folders (root only) */
static uint16_t *frame;      /* the picture currently on screen, without the info bars */
static uint8_t *file;        /* the current .img, read whole */
static int line_h;

/* \\fls0\[folder\]name as a path */
static void make_path(uint16_t *path, char const *dir, char const *name)
{
	static char const prefix[] = "\\\\fls0\\";
	int n = 0;
	for(char const *s = prefix; *s; s++) path[n++] = *s;
	if(*dir) {
		for(char const *s = dir; *s; s++) path[n++] = (unsigned char)*s;
		path[n++] = '\\';
	}
	for(char const *s = name; *s; s++) path[n++] = (unsigned char)*s;
	path[n] = 0;
}

/* Name found by a search -> ASCII that fits. -1 if it does not (such a file is skipped). */
static int ascii_name(char *dst, uint16_t const *found)
{
	int i = 0;
	for(; found[i]; i++) {
		if(i >= NAME_LEN - 1 || found[i] < 32 || found[i] > 126) return -1;
		dst[i] = found[i];
	}
	dst[i] = 0;
	return 0;
}

static int has_images(char const *dir)
{
	uint16_t path[2 * NAME_LEN + 16], found[256];
	struct BFile_FileInfo info;
	int sh;
	make_path(path, dir, "*.img");
	if(BFile_FindFirst(path, &sh, found, &info) < 0) return 0;
	BFile_FindClose(sh);
	return 1;
}

/* Only folders that hold at least one .img: the root has many others (@MainMem, ...) */
static int find_folders(void)
{
	uint16_t path[2 * NAME_LEN + 16], found[256];
	struct BFile_FileInfo info;
	int sh;
	make_path(path, "", "*");
	int rc = BFile_FindFirst(path, &sh, found, &info);
	if(rc < 0) return 0;
	while(rc == 0 && nfolders < MAX_FOLDERS) {
		if(info.type == BFile_Type_Directory && !ascii_name(folders[nfolders], found) && has_images(folders[nfolders]))
			nfolders++;
		rc = BFile_FindNext(sh, found, &info);
	}
	BFile_FindClose(sh);
	return 0;
}

static int find_images(void)
{
	uint16_t path[2 * NAME_LEN + 16], found[256];
	struct BFile_FileInfo info;
	make_path(path, folder, "*.img");
	int sh, rc = BFile_FindFirst(path, &sh, found, &info);
	if(rc < 0) return 0;
	while(rc == 0 && count < MAX_IMAGES) {
		if(!ascii_name(names[count], found)) count++;
		rc = BFile_FindNext(sh, found, &info);
	}
	BFile_FindClose(sh);
	return 0;
}

static int cmp(void const *a, void const *b)
{
	return strcmp(a, b);
}

/* Makes f ("" = root) the folder shown: lists and sorts its images */
static void load_folder(char const *f)
{
	strcpy(folder, f);
	strcpy(st.folder, f);
	count = 0;
	gint_world_switch(GINT_CALL(find_images));
	qsort(names, count, NAME_LEN, cmp);
	nfold = *f ? 0 : nfolders;
}

/* The list entry as shown and saved: folders get a '>' in front */
static void entry_label(char *dst, int e)
{
	if(e < 0 || e >= nfold + count) *dst = 0;
	else if(e < nfold) sprintf(dst, ">%s", folders[e]);
	else strcpy(dst, names[e - nfold]);
}

static int find_entry(char const *label)
{
	char l[NAME_LEN + 1];
	for(int e = 0; e < nfold + count; e++) {
		entry_label(l, e);
		if(!strcmp(l, label)) return e;
	}
	return -1;
}

/* Reads image i into file. Returns its size, or < 0 on error. */
static int read_file(int i)
{
	uint16_t path[2 * NAME_LEN + 16];
	make_path(path, folder, names[i]);
	int fd = BFile_Open(path, BFile_ReadOnly);
	if(fd < 0) return fd;
	int size = BFile_Size(fd);
	if(size > MAX_FILE || BFile_Read(fd, file, size, 0) != size) size = -1;
	BFile_Close(fd);
	return size;
}

static uint16_t const CFG[] = u"\\\\fls0\\NoteView.cfg";
static char saved[STATE_MAX];
static int saved_len;

static int save_state(void)
{
	char buf[STATE_MAX];
	int n = state_format(&st, buf);
	if(n == saved_len && !memcmp(buf, saved, n)) return 0;   /* unchanged: spare the flash */
	BFile_Remove(CFG);
	int size = n;
	if(BFile_Create(CFG, BFile_File, &size) < 0) return -1;
	int fd = BFile_Open(CFG, BFile_WriteOnly);
	if(fd < 0) return fd;
	BFile_Write(fd, buf, n);
	BFile_Close(fd);
	memcpy(saved, buf, n);
	saved_len = n;
	return 0;
}

static int load_state(void)
{
	int fd = BFile_Open(CFG, BFile_ReadOnly);
	if(fd < 0) return fd;
	int n = BFile_Size(fd);
	if(n > 0 && n < STATE_MAX && BFile_Read(fd, saved, n, 0) == n) saved_len = n;
	BFile_Close(fd);
	return 0;
}

/* getkey(), but MENU saves the position first (another app may be opened from the menu) */
static int key(void)
{
	int k = getkey_opt(GETKEY_DEFAULT & ~GETKEY_MENU, NULL).key;
	if(k != KEY_MENU) return k;
	gint_world_switch(GINT_CALL(save_state));
	gint_osmenu();
	return 0;
}

static void message(char const *l1, char const *l2)
{
	dclear(C_WHITE);
	dtext(6, 40, C_BLACK, l1);
	dtext(6, 40 + line_h + 4, C_BLACK, l2);
	dtext(6, DHEIGHT - line_h - 3, C_HINT, "EXIT: back");
	dupdate();
	while(getkey().key != KEY_EXIT) {}
}

static void bar(int y, char const *left, char const *right)
{
	drect(0, y, DWIDTH - 1, y + line_h + 5, C_HEAD);
	dtext(6, y + 3, C_WHITE, left);
	if(right) dtext_opt(DWIDTH - 6, y + 3, C_WHITE, C_NONE, DTEXT_RIGHT, DTEXT_TOP, right);
}

static void hint(char const *text)
{
	dline(0, DHEIGHT - line_h - 6, DWIDTH - 1, DHEIGHT - line_h - 6, C_HINT);
	dtext(6, DHEIGHT - line_h - 3, C_HINT, text);
}

/* What every key does; lines that start with a letter are headings */
static char const *const help_lines[] = {
	"List",
	"  UP/DOWN  select",
	"  EXE or RIGHT  open image / folder",
	"  F6  settings",
	"  EXIT  quit (in a folder: back)",
	"Photo",
	"  UP/DOWN  scroll (tall images)",
	"  UP/DOWN at the end  next/previous",
	"    image, if Continuous reading is on",
	"  LEFT/RIGHT  previous/next image",
	"  x  jump to top    div  jump to bottom",
	"  +  zoom in (2x, 4x)    -  zoom out",
	"  F6  info bar on/off",
	"  EXIT or F1  back to the list",
	"Zoomed",
	"  arrows  move around",
	"  x / div  top / bottom",
	"  EXIT  back to 1x",
	"Settings",
	"  UP/DOWN  select",
	"  LEFT/RIGHT or EXE  change",
	"  EXIT  back (saves)",
};
#define N_HELP ((int)(sizeof help_lines / sizeof *help_lines))

static void help(void)
{
	int top = line_h + 10, lh = line_h + 3, first = 0;
	int rows = (DHEIGHT - top - line_h - 8) / lh;
	while(1) {
		dclear(C_WHITE);
		bar(0, "Keys", NULL);
		for(int i = 0; i < rows && first + i < N_HELP; i++) {
			char const *s = help_lines[first + i];
			dtext(6, top + i * lh, *s == ' ' ? C_BLACK : C_HEAD, s);
		}
		hint(N_HELP > rows ? "UP/DOWN: scroll   EXIT: back" : "EXIT: back");
		dupdate();
		int k = key();
		if(k == KEY_EXIT) return;
		if(k == KEY_DOWN && first + rows < N_HELP) first++;
		if(k == KEY_UP && first > 0) first--;
	}
}

static void settings(void)
{
	char const *const label[4] = {"Colour", "Continuous reading", "Info bar", "Keys"};
	int sel = 0, top = line_h + 12, item_h = line_h + 10;
	while(1) {
		dclear(C_WHITE);
		bar(0, "Settings", NULL);
		for(int i = 0; i < 4; i++) {
			int y = top + i * item_h;
			if(i == sel) drect(0, y - 2, DWIDTH - 1, y + item_h - 5, C_SEL);
			dtext(10, y, C_BLACK, label[i]);
			if(i == 0) {
				drect(230, y, 230 + line_h + 2, y + line_h, C_HEAD);
				dtext(252, y, C_BLACK, palette[st.set.colour].name);
			}
			else dtext(230, y, C_BLACK, i == 1 ? (st.set.continuous ? "On" : "Off")
				: i == 2 ? (st.set.info ? "On" : "Off") : "EXE: show");
		}
		hint("UP/DOWN: select  LEFT/RIGHT: change  EXIT: back");
		dupdate();

		int k = key();
		int d = k == KEY_RIGHT ? 1 : k == KEY_LEFT ? -1 : 0;
		if(k == KEY_EXIT) {
			gint_world_switch(GINT_CALL(save_state));
			return;
		}
		if(k == KEY_UP && sel > 0) sel--;
		if(k == KEY_DOWN && sel < 3) sel++;
		if(k == KEY_EXE) d = 1;
		if(sel == 0 && d) st.set.colour = (st.set.colour + d + N_COLOURS) % N_COLOURS;
		if(sel == 1 && d) st.set.continuous = !st.set.continuous;
		if(sel == 2 && d) st.set.info = !st.set.info;
		if(sel == 3 && k == KEY_EXE) help();
	}
}

/* Shows image i (at view *start if not NULL) and lets the user browse.
   Returns the image shown last when EXIT goes back. */
static int viewer(int i, view_t const *start)
{
	static img_t im;
	view_t v, shown = {0, 0, 0}, carry;
	int ok = -1, jump = 0;

	while(1) {
		if(ok < 0) {
			int size = gint_world_switch(GINT_CALL(read_file, i));
			ok = size >= 0 && img_open(&im, file, size) == 0;
			view_reset(&v);
			if(ok && start) {
				v = *start;
				view_pan(&im, &v, 0, 0);   /* clamps a saved position to this image */
				if(jump) view_jump(&im, &v, jump);
			}
			start = NULL;
			jump = 0;
			shown.zoom = 0;
		}
		if(ok && memcmp(&v, &shown, sizeof v)) {   /* decode only when the view changed */
			ok = view_render(&im, &v, frame) == 0;
			shown = v;
		}
		if(!ok) {
			message(names[i], "Not a NoteView image. Convert it with convert.py.");
			return i;
		}
		memcpy(gint_vram, frame, FIT_BYTES);
		if(st.set.info) {
			int top = line_h + 6, bottom = DHEIGHT - line_h - 6;
			char right[32];
			sprintf(right, "%d/%d  %dx", i + 1, count, v.zoom);
			bar(0, names[i], right);
			bar(bottom, v.zoom == 1 ? "</>: image  ^v: scroll  +/-: zoom  x/div: jump"
				: "arrows: move  +/-: zoom  x/div: jump  F6", NULL);
			if(im.h > SH && v.zoom == 1) {   /* scroll bar */
				int track = bottom - top, y = top + track * (v.cy - SH / 2) / im.h;
				drect(DWIDTH - 4, y, DWIDTH - 1, y + track * SH / im.h - 1, C_WHITE);
			}
		}
		dupdate();

		strcpy(st.name, names[i]);
		st.viewing = 1;
		st.v = v;
		int k = key();
		if(k == KEY_F6) st.set.info = !st.set.info;
		else if(k == KEY_EXIT && v.zoom > 1) while(v.zoom > 1) view_zoom(&im, &v, -1);
		else if(k == KEY_EXIT || k == KEY_F1) return i;
		else if(k == KEY_PLUS) view_zoom(&im, &v, +1);
		else if(k == KEY_MINUS) view_zoom(&im, &v, -1);
		else if(k == KEY_MUL) view_jump(&im, &v, -1);
		else if(k == KEY_DIV) view_jump(&im, &v, +1);
		else if(k == KEY_UP || k == KEY_DOWN) {
			int d = view_scroll(&im, &v, k == KEY_DOWN ? 1 : -1, st.set.continuous);
			if(d && i + d >= 0 && i + d < count) {   /* continuous reading: next image at its top, previous at its bottom */
				carry = v;
				start = &carry;
				jump = -d;
				i += d;
				ok = -1;
			}
		}
		else if(v.zoom > 1 && k == KEY_LEFT) view_pan(&im, &v, -1, 0);
		else if(v.zoom > 1 && k == KEY_RIGHT) view_pan(&im, &v, +1, 0);
		else if(k == KEY_LEFT || k == KEY_RIGHT) {
			i = (i + (k == KEY_RIGHT ? 1 : count - 1)) % count;
			ok = -1;
		}
	}
}

int main(void)
{
	/* Without this, leaving with EXIT makes the OS "resume" the add-in next time,
	   which exits again at once: the app could not be reopened. */
	gint_setrestart(1);
	dsize("A", NULL, NULL, &line_h);
	frame = malloc(FIT_BYTES);
	file = malloc(MAX_FILE);
	if(!frame || !file) {
		message("Not enough memory.", "");
		return 1;
	}
	state_init(&st);
	gint_world_switch(GINT_CALL(find_folders));
	qsort(folders, nfolders, NAME_LEN, cmp);
	load_folder("");
	if(count == 0 && nfolders == 0) {
		message("No .img files found.", "Make them with convert.py, copy to the root.");
		return 1;
	}

	/* back to where the user was last time, if that image still exists */
	int sel = 0, first = 0;
	state_t s;
	int first_start = gint_world_switch(GINT_CALL(load_state)) < 0;
	if(saved_len && state_parse(&s, saved, saved_len) == 0) {
		st.set = s.set;
		for(int f = 0; s.folder[0] && !folder[0] && f < nfolders; f++)
			if(!strcmp(folders[f], s.folder)) load_folder(s.folder);
		if(!s.folder[0] || folder[0]) {
			int e = find_entry(s.name);
			if(e >= 0) sel = e;
			if(e >= nfold && s.viewing) sel = viewer(e - nfold, &s.v) + nfold;
		}
	}
	else if(first_start) help();   /* no NoteView.cfg: explain the keys once */

	int top = line_h + 10, item_h = line_h + 6;
	int rows = (DHEIGHT - top - line_h - 8) / item_h;
	while(1) {
		int total = nfold + count;
		if(sel < first) first = sel;
		if(sel >= first + rows) first = sel - rows + 1;
		char n[16], label[NAME_LEN + 1];
		sprintf(n, "%d", total);
		dclear(C_WHITE);
		bar(0, folder[0] ? folder : "NoteView", n);
		for(int i = 0; i < rows && first + i < total; i++) {
			int y = top + i * item_h;
			entry_label(label, first + i);
			if(first + i == sel) drect(0, y - 2, DWIDTH - 1, y + item_h - 3, C_SEL);
			dtext(10, y, first + i < nfold ? C_HEAD : C_BLACK, label);
		}
		hint(folder[0] ? "^v: select  EXE: open  F6: settings  EXIT: back"
			: "^v: select  EXE: open  F6: settings  EXIT: quit");
		dupdate();

		entry_label(st.name, sel);
		st.viewing = 0;
		int k = key();
		if(k == KEY_EXIT && folder[0]) {   /* back to the root, folder selected */
			char f[NAME_LEN];
			strcpy(f, folder);
			load_folder("");
			sel = find_entry(strcat(strcpy(label, ">"), f));
			if(sel < 0) sel = 0;
			continue;
		}
		if(k == KEY_EXIT) {
			gint_world_switch(GINT_CALL(save_state));
			return 0;
		}
		if(k == KEY_F6) settings();
		if((k == KEY_EXE || k == KEY_RIGHT) && sel < nfold) {
			load_folder(folders[sel]);
			sel = first = 0;
		}
		else if((k == KEY_EXE || k == KEY_RIGHT) && sel < total) sel = viewer(sel - nfold, NULL) + nfold;
		if(k == KEY_DOWN && sel < total - 1) sel++;
		if(k == KEY_UP && sel > 0) sel--;
	}
}
