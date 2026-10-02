/* main.c - DVD Region Tools
 *
 * Three jobs, one screenful apart:
 *   1. change the DVD / Blu-ray / PS3 region stored in xRegistry.sys
 *   2. read the loaded DVD (1:1 sector dump or a VIDEO_TS folder copy)
 *   3. optionally clear the prohibited-region byte so the dump plays anywhere
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <lv2/sysfs.h>

#include "disc.h"
#include "gfx.h"
#include "input.h"
#include "rip.h"
#include "util.h"
#include "xreg.h"

/* ------------------------------------------------------------------ layout -- */

#define MARGIN      40
#define HEADER_H    84
#define PANEL_Y     (HEADER_H + 24)
#define FOOTER_H    56

#define APP_BACKUP_DIR "/dev_hdd0/" APP_TITLE_ID
#define BACKUP_FILE    APP_BACKUP_DIR "/xRegistry.sys.bak"

/* ------------------------------------------------------------------- state -- */

typedef enum
{
	SCR_HOME = 0,
	SCR_REGION,
	SCR_DISC,
	SCR_REGISTRY,
	SCR_HELP,
	SCR_COUNT
} screen_id;

typedef enum
{
	DLG_NONE = 0,
	DLG_INFO,
	DLG_WARN,
	DLG_ERROR,
	DLG_YESNO
} dialog_kind;

/* What a DLG_YESNO should actually do when the user confirms. */
typedef enum
{
	DLGACT_NONE = 0,
	DLGACT_RESTORE,
	DLGACT_QUIT
} dialog_action;

static screen_id   g_screen = SCR_HOME;
static int         g_home_sel;
static int         g_region_tab;        /* 0 = preset, 1 = manual */
static int         g_region_sel;
static xreg_state  g_reg;
static int         g_reg_loaded;       /* at least the DVD region key was found */
static int         g_reg_readable;     /* xRegistry.sys itself could be read    */

static disc_info   g_disc;
static int         g_disc_sel;
static int         g_dump_mode;         /* 0 = 1:1 ISO, 1 = VIDEO_TS copy */
static int         g_patch_region;
static int         g_dest;

/* Cached disc geometry: measuring talks to the drive, so it is done once per
 * disc / mode change instead of once per frame. */
static uint64_t    g_disc_bytes;        /* total bytes on the disc, 0 unknown */
static uint64_t    g_ts_bytes;          /* total bytes in VIDEO_TS          */
static char        g_size_how[48];
static int         g_size_valid;
static char        g_basename[160];

static char        g_dev[9][24];
static int         g_dev_count;

static int         g_registry_sel;
static dialog_kind  g_dlg;
static dialog_action g_dlg_act;
static char        g_dlg_title[64];
static char        g_dlg_body[512];
static int         g_running = 1;
static int         g_abort_rip;
static rip_status  g_rip;

/* progress timing, reset for every job */
static uint32_t    g_rip_start_ms;
static uint32_t    g_rip_last_draw;
static int         g_rip_started;

/* --------------------------------------------------------------- utilities -- */

static void build_devices(void)
{
	int i;

	g_dev_count = 0;
	ustrlcpy(g_dev[g_dev_count++], "/dev_hdd0", sizeof(g_dev[0]));

	for (i = 0; i < 8 && g_dev_count < 9; i++)
	{
		char p[24];
		snprintf(p, sizeof(p), "/dev_usb%03d", i);
		if (fs_exists(p))
			ustrlcpy(g_dev[g_dev_count++], p, sizeof(g_dev[0]));
	}
}

static uint64_t device_free(const char *dev)
{
	char p[32];
	uint64_t free_bytes = 0;

	snprintf(p, sizeof(p), "%s/", dev);
	if (fs_free_space(p, &free_bytes) != 0)
		return 0;
	return free_bytes;
}

static void out_dir_for(char *out, size_t n, const char *dev, const char *base)
{
	snprintf(out, n, "%s/DVDISO/%s", dev, base);
}

static void out_file_for(char *out, size_t n, const char *dev, const char *base)
{
	snprintf(out, n, "%s/DVDISO/%s.iso", dev, base);
}

/* ------------------------------------------------------------------ dialogs -- */

static void show_dialog(dialog_kind kind, const char *title, const char *fmt, ...)
{
	va_list ap;

	ustrlcpy(g_dlg_title, title, sizeof(g_dlg_title));
	va_start(ap, fmt);
	vsnprintf(g_dlg_body, sizeof(g_dlg_body), fmt, ap);
	va_end(ap);

	g_dlg = kind;
	if (kind != DLG_YESNO)
		g_dlg_act = DLGACT_NONE;
}

/* ----------------------------------------------------------------- drawing -- */

static void draw_header(void)
{
	static const char *titles[SCR_COUNT] = {
		"DVD REGION TOOLS", "REGION SETTINGS", "DISC TOOLS",
		"REGISTRY BACKUP", "HELP"
	};
	static const char *tabs[SCR_COUNT] = {
		"Home", "Region", "Disc", "Backup", "Help"
	};
	int x = MARGIN;
	int i;

	gfx_fill(0, 0, GFX_W, HEADER_H, COL_PANEL);
	gfx_hline(0, HEADER_H, GFX_W, COL_LINE);
	gfx_hline(0, HEADER_H + 1, GFX_W, COL_ACCENT);
	gfx_text(MARGIN, 18, COL_TEXT, titles[g_screen], 4);

	for (i = 0; i < SCR_COUNT; i++)
	{
		int w = gfx_text_w(tabs[i], 2) + 24;
		if (i == (int)g_screen)
		{
			gfx_fill(x - 6, HEADER_H - 30, w, 24, COL_SEL);
			gfx_text(x, HEADER_H - 24, COL_TEXT, tabs[i], 2);
		}
		else
		{
			gfx_text(x, HEADER_H - 24, COL_DIM, tabs[i], 2);
		}
		x += w;
	}
}

static void draw_footer(const char *hint)
{
	gfx_fill(0, GFX_H - FOOTER_H, GFX_W, FOOTER_H, COL_PANEL);
	gfx_hline(0, GFX_H - FOOTER_H, GFX_W, COL_LINE);
	gfx_text(MARGIN, GFX_H - FOOTER_H + 18, COL_DIM, hint, 2);
}

static void draw_list(int x, int y, int w, int items, int sel, int row_h)
{
	int i;

	for (i = 0; i < items; i++)
	{
		int ry = y + i * row_h;
		if (i == sel)
			gfx_fill(x - 12, ry - 6, w + 24, row_h - 4, COL_SEL);
		else
			gfx_hline(x - 12, ry + row_h - 12, w + 24, COL_LINE);
	}
}

static void kv(int x, int y, int label_w, const char *k, const char *v, uint32_t col)
{
	gfx_text(x, y, COL_DIM, k, 2);
	gfx_text(x + label_w, y, col, v, 2);
}

/* Wraps `src` on spaces so a long message never runs off the dialog. Words
 * longer than one line are hard split rather than allowed to overflow. */
static void draw_wrapped(int x, int y, int w, uint32_t col, const char *src,
                         int scale, int max_lines)
{
	char line[256];
	int max_chars;
	int lines = 0;
	int cx = 0;
	int cy = y;

	if (scale < 1)
		scale = 1;

	/* characters that fit on one line, never more than the buffer can hold */
	max_chars = w / (6 * scale);
	if (max_chars > (int)sizeof(line) - 1)
		max_chars = (int)sizeof(line) - 1;
	if (max_chars < 1)
		max_chars = 1;

	while (*src != '\0' && lines < max_lines)
	{
		const char *sp;
		int word;

		if (*src == '\n')
		{
			line[cx] = '\0';
			gfx_text(x, cy, col, line, scale);
			cx = 0;
			cy += gfx_text_h(scale) + 4;
			lines++;
			src++;
			continue;
		}

		sp   = strchr(src, ' ');
		word = sp ? (int)(sp - src) : (int)strlen(src);
		if (word > max_chars)
			word = max_chars;
		if (word < 1)
			word = 1;

		/* Always leave room for the separating space and the terminator, so
		 * after this block cx + word <= max_chars - 1 <= sizeof(line) - 2. */
		if (cx + word + 1 > max_chars)
		{
			if (cx > 0)
			{
				line[cx] = '\0';
				gfx_text(x, cy, col, line, scale);
				cx = 0;
				cy += gfx_text_h(scale) + 4;
				lines++;
				continue;
			}
			word = max_chars - 1;   /* single word longer than a line */
			if (word < 1)
				word = 1;
		}

		if (cx > 0)
			line[cx++] = ' ';
		{
			int i;
			for (i = 0; i < word && cx < (int)sizeof(line) - 1; i++)
				line[cx++] = src[i];
		}
		src += (size_t)word;
		if (sp != NULL)
			src++;
	}

	if (cx > 0 && lines < max_lines)
	{
		line[cx] = '\0';
		gfx_text(x, cy, col, line, scale);
	}
}

static void draw_dialog(void)
{
	int w = 900;
	int h = 380;
	int x = (GFX_W - w) / 2;
	int y = (GFX_H - h) / 2;
	uint32_t accent = (g_dlg == DLG_ERROR) ? COL_ERR :
	                  (g_dlg == DLG_WARN)  ? COL_WARN : COL_ACCENT;

	gfx_fill(x + 8, y + 10, w, h, 0xFF101418u);        /* drop shadow */
	gfx_fill(x, y, w, h, COL_PANEL2);
	gfx_frame(x, y, w, h, accent);

	gfx_text(x + 24, y + 20, COL_TEXT, g_dlg_title, 3);
	gfx_hline(x + 24, y + 56, w - 48, COL_LINE);

	draw_wrapped(x + 24, y + 76, w - 48, COL_TEXT, g_dlg_body, 2, 12);

	gfx_text_box(x + 24, y + h - 52, w - 48, 28, COL_DIM,
	             (g_dlg == DLG_YESNO)
	             ? "X = Yes      O = No"
	             : "Press X or O to continue",
	             2, ALIGN_CENTER);
}

/* ------------------------------------------------------------------ screens - */

static void draw_home(void)
{
	char buf[192];
	int x = MARGIN;
	int y = PANEL_Y + 20;

	gfx_fill(x, y, 600, 286, COL_PANEL);
	gfx_frame(x, y, 600, 286, COL_LINE);
	gfx_text(x + 20, y + 16, COL_ACCENT, "CONSOLE", 2);
	gfx_hline(x + 20, y + 42, 560, COL_LINE);

	y += 58;
	if (g_reg_loaded)
	{
		kv(x + 20, y,      190, "PS3 region",     ps3_region_name(g_reg.ps3_region), COL_TEXT);
		kv(x + 20, y + 28, 190, "DVD region",     dvd_region_name(g_reg.dvd_region), COL_TEXT);
		kv(x + 20, y + 56, 190, "Blu-ray region", bd_region_name(g_reg.bd_region),  COL_TEXT);
		kv(x + 20, y + 84, 190, "TV system",      tv_system_name(g_reg.tv_system),   COL_TEXT);
		gfx_text(x + 20, y + 124, COL_DIM,
		         "Loaded straight from xRegistry.sys", 2);
	}
	else if (g_reg_readable)
	{
		gfx_text(x + 20, y,      COL_WARN, "Registry read, region keys missing", 2);
		gfx_text(x + 20, y + 28, COL_DIM,
		         "/setting/bddvd/dvdRegionCode was not found in the file.", 2);
	}
	else
	{
		gfx_text(x + 20, y,      COL_ERR, "xRegistry.sys is not readable", 2);
		gfx_text(x + 20, y + 28, COL_DIM, "Run this app from CFW, HEN or HAN", 2);
	}

	/* Pad diagnostics: on a real console this is noise, but it is the only way
	 * to tell a dead pad from a dead UI when running under an emulator. */
	{
		char dbg[128];

		snprintf(dbg, sizeof(dbg), "pad init 0x%08X  connected %d  ports 0x%02X",
		         (uint32_t)g_ioPadInitRet, g_pads_connected, g_pad_ports_seen);
		gfx_text(x + 20, y + 126,
		         (g_ioPadInitRet == 0) ? COL_DIM : COL_ERR, dbg, 2);

		snprintf(dbg, sizeof(dbg), "len %u  word 0x%04X  mask 0x%02X",
		         g_pad_last_len, g_pad_last_word, input_mask());
		gfx_text(x + 20, y + 148,
		         g_pad_last_len ? COL_OK : COL_DIM, dbg, 2);
	}

	x = MARGIN + 640;
	y = PANEL_Y + 20;
	gfx_fill(x, y, 560, 286, COL_PANEL);
	gfx_frame(x, y, 560, 286, COL_LINE);
	gfx_text(x + 20, y + 16, COL_ACCENT, "DISC", 2);
	gfx_hline(x + 20, y + 42, 520, COL_LINE);

	y += 58;
	if (g_disc.present && g_disc.ifo_ok)
	{
		snprintf(buf, sizeof(buf), "Provider : %s", g_disc.provider);
		gfx_text_box(x + 20, y, 520, 20, COL_TEXT, buf, 2, ALIGN_LEFT);
		y += 26;
		snprintf(buf, sizeof(buf), "Regions  : %s", g_disc.region_list);
		gfx_text_box(x + 20, y, 520, 20,
		             g_disc.region_free ? COL_OK :
		             g_disc.rce_suspected ? COL_WARN : COL_TEXT,
		             buf, 2, ALIGN_LEFT);
		y += 26;
		snprintf(buf, sizeof(buf), "Mask     : 0x%02X 0x%02X",
		         g_disc.region_mask, g_disc.region_mask2);
		gfx_text_box(x + 20, y, 520, 20, COL_DIM, buf, 2, ALIGN_LEFT);
		y += 26;
		snprintf(buf, sizeof(buf), "Titles   : %u", g_disc.num_titles);
		gfx_text_box(x + 20, y, 520, 20, COL_DIM, buf, 2, ALIGN_LEFT);
	}
	else if (g_disc.present)
	{
		gfx_text(x + 20, y,      COL_WARN, "Disc loaded, but it is not DVD-Video", 2);
		gfx_text(x + 20, y + 28, COL_DIM,  "No VIDEO_TS directory was found", 2);
	}
	else
	{
		gfx_text(x + 20, y, COL_DIM, "No disc in the drive", 2);
		gfx_text(x + 20, y + 28, COL_DIM, "TRIANGLE to rescan", 2);
	}

	y = PANEL_Y + 330;
	draw_list(MARGIN, y, 1180, 5, g_home_sel, 32);

	{
		static const char *titles[5] = {
			"Region settings", "Disc tools", "Registry backup", "Help / about", "Quit"
		};
		static const char *subs[5] = {
			"change DVD / Blu-ray / PS3 region",
			"dump the loaded DVD, optionally region free",
			"save or restore xRegistry.sys",
			"what this can and cannot do",
			"back to the XMB"
		};
		int i;
		for (i = 0; i < 5; i++)
		{
			int ry = y + i * 32;
			if (i == g_home_sel)
				gfx_text(MARGIN + 10, ry, COL_TEXT, ">", 2);
			gfx_text(MARGIN + 40, ry, COL_TEXT, titles[i], 2);
			gfx_text(MARGIN + 700, ry, COL_DIM, subs[i], 2);
		}
	}
}

/* ------------------------------------------------------------------ region - */

static const uint32_t MANUAL_BD[4] = { 0, 1, 2, 4 };

static void draw_region(void)
{
	int x = MARGIN;
	int y = PANEL_Y + 20;
	char buf[160];

	gfx_fill(x, y, 1180, 34, COL_PANEL2);
	gfx_text(x + 20,  y + 8, g_region_tab == 0 ? COL_TEXT : COL_DIM,
	         "<  Preset", 2);
	gfx_text(x + 400, y + 8, g_region_tab == 1 ? COL_TEXT : COL_DIM,
	         "Manual  >", 2);
	gfx_vline(x + 380, y + 4, 26, COL_LINE);

	y += 58;

	if (g_region_tab == 0)
	{
		int i;
		int first = g_region_sel - 5;
		int last;

		if (first < 0) first = 0;
		last = first + 11;
		if (last > PS3_REGION_COUNT) last = PS3_REGION_COUNT;
		if (last - first < 10) first = last - 10;
		if (first < 0) first = 0;

		for (i = first; i < last; i++)
		{
			int ry  = y + (i - first) * 28;
			int cur = g_reg_loaded && g_reg.ps3_region == PS3_REGIONS[i].ps3_region;

			if (i == g_region_sel)
			{
				gfx_fill(x - 10, ry - 4, 1200, 26, COL_SEL);
				gfx_text(x + 8, ry, COL_TEXT, ">", 2);
			}
			snprintf(buf, sizeof(buf), "%-18s  DVD %-10s  Blu-ray %-10s  code 0x%02X",
			         PS3_REGIONS[i].name,
			         dvd_region_name(PS3_REGIONS[i].dvd_region),
			         bd_region_name(PS3_REGIONS[i].bd_region),
			         PS3_REGIONS[i].ps3_region);
			gfx_text(x + 40, ry, cur ? COL_OK : COL_TEXT, buf, 2);
		}
	}
	else
	{
		static const char *labels[3] = {
			"DVD region", "Blu-ray region", "TV system"
		};
		const char *values[3];
		int i;

		values[0] = dvd_region_name(g_reg.dvd_region);
		values[1] = bd_region_name(g_reg.bd_region);
		values[2] = tv_system_name(g_reg.tv_system);

		for (i = 0; i < 3; i++)
		{
			int ry = y + i * 40;
			if (i == g_region_sel)
			{
				gfx_fill(x - 10, ry - 6, 720, 36, COL_SEL);
				gfx_text(x + 8, ry, COL_TEXT, ">", 2);
			}
			gfx_text(x + 40,  ry, COL_DIM,  labels[i], 2);
			gfx_text(x + 340, ry, COL_TEXT, values[i], 2);
			if (i == g_region_sel)
				gfx_text(x + 560, ry, COL_ACCENT, "X to cycle", 2);
		}

		gfx_text(x + 40, y + 140, COL_DIM,
		         "The preset tab keeps the PS3 region and both disc codes", 2);
		gfx_text(x + 40, y + 162, COL_DIM,
		         "consistent with each other, which is what you normally want.", 2);
	}

	y += 372;
	gfx_fill(MARGIN, y, 1180, 92, COL_PANEL);
	gfx_frame(MARGIN, y, 1180, 92, COL_LINE);
	gfx_text(MARGIN + 20, y + 14, COL_ACCENT, "BEFORE YOU APPLY", 2);
	draw_wrapped(MARGIN + 20, y + 38, 1140, COL_DIM,
	             "xRegistry.sys is copied to " APP_BACKUP_DIR " on the HDD first. "
	             "Restart the console afterwards or the change is ignored. "
	             "RPC-2 / RCE discs and drive level locks are not affected by this.",
	             2, 2);
}

/* -------------------------------------------------------------------- disc - */

static void draw_disc(void)
{
	int x = MARGIN;
	int y = PANEL_Y + 20;
	char buf[256];
	const char *dev = g_dev[g_dest];
	uint64_t need = (g_dump_mode == 0) ? g_disc_bytes : g_ts_bytes;
	uint64_t have = device_free(dev);

	gfx_fill(x, y, 600, 330, COL_PANEL);
	gfx_frame(x, y, 600, 330, COL_LINE);
	gfx_text(x + 20, y + 16, COL_ACCENT, "DISC", 2);
	gfx_hline(x + 20, y + 42, 560, COL_LINE);

	y += 58;
	if (!g_disc.present || !g_disc.ifo_ok)
	{
		gfx_text(x + 20, y,      COL_WARN, "No DVD-Video disc loaded", 2);
		gfx_text(x + 20, y + 28, COL_DIM,  "Insert one and press TRIANGLE", 2);
	}
	else
	{
		int yy = y;
		snprintf(buf, sizeof(buf), "Provider : %s", g_disc.provider);
		gfx_text_box(x + 20, yy, 560, 20, COL_TEXT, buf, 2, ALIGN_LEFT);
		yy += 26;
		snprintf(buf, sizeof(buf), "Regions  : %s", g_disc.region_list);
		gfx_text_box(x + 20, yy, 560, 20,
		             g_disc.region_free ? COL_OK : COL_TEXT, buf, 2, ALIGN_LEFT);
		yy += 26;
		snprintf(buf, sizeof(buf), "Mask     : 0x%02X 0x%02X",
		         g_disc.region_mask, g_disc.region_mask2);
		gfx_text_box(x + 20, yy, 560, 20, COL_DIM, buf, 2, ALIGN_LEFT);
		yy += 26;
		snprintf(buf, sizeof(buf), "Titles   : %u", g_disc.num_titles);
		gfx_text_box(x + 20, yy, 560, 20, COL_DIM, buf, 2, ALIGN_LEFT);
		yy += 26;
		snprintf(buf, sizeof(buf), "VMG end  : LBA %u", g_disc.vmg_last_sector);
		gfx_text_box(x + 20, yy, 560, 20, COL_DIM, buf, 2, ALIGN_LEFT);

		if (g_disc.rce_suspected)
			gfx_text_box(x + 20, yy + 34, 560, 20, COL_WARN,
			             "0xFF mask: RPC-2 / RCE disc, mask patch may not help",
			             2, ALIGN_LEFT);
	}

	/* options */
	x = MARGIN + 640;
	y = PANEL_Y + 20;
	{
		static const char *labels[4] = {
			"Dump mode", "Region patch", "Destination", "Start"
		};
		int ry;

		draw_list(x, y, 600, 4, g_disc_sel, 40);
		for (ry = 0; ry < 4; ry++)
		{
			int ty = y + ry * 40;
			if (ry == g_disc_sel)
			{
				gfx_fill(x - 12, ty - 6, 624, 36, COL_SEL);
				gfx_text(x, ty, COL_TEXT, ">", 2);
			}
			gfx_text(x + 30, ty, COL_DIM, labels[ry], 2);
		}

		gfx_text(x + 330, y + 0 * 40, COL_TEXT,
		         g_dump_mode == 0 ? "1:1 ISO image" : "VIDEO_TS folder", 2);
		gfx_text(x + 330, y + 1 * 40, COL_TEXT,
		         g_patch_region ? "Yes - clear the mask" : "No - keep as is", 2);
		gfx_text(x + 330, y + 2 * 40, COL_TEXT, dev, 2);
		if (g_disc_sel == 3)
			gfx_text(x + 330, y + 3 * 40, COL_ACCENT, "X to begin", 2);
	}

	/* destination details */
	y = PANEL_Y + 200;
	gfx_fill(x, y, 600, 150, COL_PANEL);
	gfx_frame(x, y, 600, 150, COL_LINE);

	if (g_disc.present && g_disc.ifo_ok)
	{
		char a[32], b[32];

		if (g_dump_mode == 0)
			snprintf(buf, sizeof(buf), "Output : %s/DVDISO/%s.iso", dev, g_basename);
		else
			snprintf(buf, sizeof(buf), "Output : %s/DVDISO/%s/", dev, g_basename);
		gfx_text_box(x + 20, y + 14, 560, 20, COL_TEXT, buf, 2, ALIGN_LEFT);

		format_size(need, a, sizeof(a));
		format_size(have, b, sizeof(b));
		snprintf(buf, sizeof(buf), "Need %s   Free %s%s", a, b,
		         fs_is_fat(dev) ? "  (FAT32, will split at 4 GB)" : "");
		gfx_text_box(x + 20, y + 40, 560, 20,
		             (need > 0 && have < need) ? COL_ERR : COL_DIM, buf, 2, ALIGN_LEFT);

		if (g_dump_mode == 0 && !g_size_valid)
		{
			snprintf(buf, sizeof(buf), "Drive did not report a size (%s)", g_size_how);
			gfx_text_box(x + 20, y + 66, 560, 20, COL_WARN, buf, 2, ALIGN_LEFT);
		}
		else if (g_dump_mode == 0)
			gfx_text_box(x + 20, y + 66, 560, 20, COL_DIM,
			             g_size_how, 2, ALIGN_LEFT);
		else
			gfx_text_box(x + 20, y + 66, 560, 20, COL_DIM,
			             "Folder copy needs roughly the disc size", 2, ALIGN_LEFT);
	}

	gfx_text(MARGIN, PANEL_Y + 368, COL_DIM,
	         "1:1 ISO needs CFW/HEN for raw drive access. VIDEO_TS copy works on any firmware.", 2);
	gfx_text(MARGIN, PANEL_Y + 390, COL_DIM,
	         "The patch only clears the prohibited-region byte in VIDEO_TS.IFO.", 2);
}

/* ---------------------------------------------------------------- registry - */

static void draw_registry(void)
{
	int x = MARGIN;
	int y = PANEL_Y + 20;
	char b[600];

	gfx_fill(x, y, 1180, 300, COL_PANEL);
	gfx_frame(x, y, 1180, 300, COL_LINE);
	gfx_text(x + 20, y + 16, COL_ACCENT, "xRegistry.sys", 2);
	gfx_hline(x + 20, y + 42, 1140, COL_LINE);

	kv(x + 20, y + 62, 140, "Flash copy", XREG_PATH, COL_TEXT);
	kv(x + 20, y + 90, 140, "Mirror",     XREG_BACKUP_PATH, COL_TEXT);
	kv(x + 20, y + 118, 140, "Backup dir", APP_BACKUP_DIR, COL_TEXT);
	kv(x + 20, y + 146, 140, "Status",
	   g_reg_loaded  ? "readable, region keys found" :
	   g_reg_readable ? "readable, region keys missing" : "NOT readable",
	   g_reg_loaded ? COL_OK : (g_reg_readable ? COL_WARN : COL_ERR));

	snprintf(b, sizeof(b), "%s", BACKUP_FILE);
	kv(x + 20, y + 182, 140, "HDD backup", b,
	   fs_exists(BACKUP_FILE) ? COL_OK : COL_DIM);
	gfx_text(x + 160, y + 208,
	         fs_exists(BACKUP_FILE) ? COL_OK : COL_DIM,
	         fs_exists(BACKUP_FILE) ? "present" : "not created yet", 2);

	y += 330;
	draw_list(x, y, 1180, 3, g_registry_sel, 40);
	gfx_text(x + 30, y + 0 * 40, COL_TEXT, "Create backup now", 2);
	gfx_text(x + 30, y + 1 * 40, COL_TEXT, "Restore from backup", 2);
	gfx_text(x + 30, y + 2 * 40, COL_TEXT, "Restart the console", 2);
	gfx_text(x + 560, y + 0 * 40, COL_DIM, "writes the untouched image to the HDD", 2);
	gfx_text(x + 560, y + 1 * 40, COL_DIM, "copies it back over both flash copies", 2);
	gfx_text(x + 560, y + 2 * 40, COL_DIM, "needed after a region change", 2);
}

/* -------------------------------------------------------------------- help - */

static void draw_help(void)
{
	static const char *body =
		"What this app does\n"
		"  1. Rewrites the region keys in /dev_flash2/etc/xRegistry.sys, the same\n"
		"     file webMAN and the classic xRegistry editors touch.\n"
		"  2. Dumps the DVD in the console's own drive to /dev_hdd0/DVDISO.\n"
		"  3. Optionally clears the prohibited-region byte in VIDEO_TS.IFO so the\n"
		"     copy plays on any player.\n"
		"\n"
		"Requirements\n"
		"  Region change and the 1:1 dump need a console that can write flash and\n"
		"  open the raw drive: CFW, HEN or HAN. The VIDEO_TS folder copy and the\n"
		"  region patch work on stock firmware as well.\n"
		"\n"
		"Limits worth knowing\n"
		"  A DVD carrying RPC-2 / RCE checks PGC bytecode while it plays.\n"
		"  Clearing the mask does not defeat that, so those discs will still\n"
		"  refuse to play outside their own region.\n"
		"  Writing the registry does not reprogram the drive EEPROM or the\n"
		"  console target ID, so some units ignore the DVD code altogether.\n"
		"  The console must be restarted before a new region takes effect.\n"
		"\n"
		"Playing the result\n"
		"  ISOs land in /dev_hdd0/DVDISO and are picked up by Showtime and\n"
		"  MultiMAN automatically. A copied VIDEO_TS folder can be mounted from a\n"
		"  file manager as a DVD folder.\n"
		"\n"
		"Safety\n"
		"  The registry is copied to the HDD before any change. If the console\n"
		"  misbehaves afterwards, restore it from the Backup screen or copy the\n"
		"  saved file back to /dev_flash2/etc with FTP.";

	draw_wrapped(MARGIN, PANEL_Y + 16, 1180, COL_TEXT, body, 2, 30);
}

/* ---------------------------------------------------------------- progress - */

static int rip_progress_cb(void *ctx, const rip_status *st)
{
	uint32_t now = input_millis();
	uint32_t elapsed_s, rate_bps, eta_s;
	float frac = 0.0f;
	char buf[192];
	char a[32], b[32], c[32], d[32];
	int x = 160;
	int y = 250;
	int w = GFX_W - 320;
	int redraw;

	(void)ctx;
	if (!g_rip_started)
	{
		g_rip_started = 1;
		g_rip_start_ms = now;
	}

	/* Redrawing a full frame per 64 KB would throttle the dump, so the
	 * screen is refreshed at ~12 Hz while the transfer keeps running. */
	redraw = (now - g_rip_last_draw >= 80);
	if (redraw)
		g_rip_last_draw = now;

	elapsed_s = (now - g_rip_start_ms) / 1000;
	rate_bps = elapsed_s ? (uint32_t)(st->done / elapsed_s) : 0;

	if (st->total > 0)
	{
		frac = (float)((double)st->done / (double)st->total);
		if (frac > 1.0f)
			frac = 1.0f;
	}
	eta_s = (rate_bps > 0 && st->done < st->total)
	      ? (uint32_t)((st->total - st->done) / rate_bps) : 0;

	if (redraw)
	{
		gfx_begin();
		gfx_clear(COL_BG);
		draw_header();

		gfx_text_box(x, y - 96, w, 40, COL_TEXT,
		             (g_dump_mode == 0) ? "Dumping 1:1 image" : "Copying VIDEO_TS",
		             3, ALIGN_CENTER);

		gfx_bar(x, y, w, 34, frac, COL_PANEL2, COL_ACCENT);

		format_size(st->done, a, sizeof(a));
		format_size(st->total, b, sizeof(b));
		format_size((uint64_t)rate_bps, c, sizeof(c));
		format_duration(eta_s, d, sizeof(d));

		snprintf(buf, sizeof(buf), "%d%%    %s / %s",
		         (int)(frac * 100.0f), a, b);
		gfx_text_box(x, y + 46, w, 24, COL_TEXT, buf, 2, ALIGN_CENTER);

		snprintf(buf, sizeof(buf), "speed %s/s    remaining %s", c, d);
		gfx_text_box(x, y + 76, w, 24, COL_DIM, buf, 2, ALIGN_CENTER);

		gfx_text_box(x, y + 124, w, 24, COL_ACCENT, st->message, 2, ALIGN_CENTER);
		gfx_text_box(x, y + 168, w, 24, COL_WARN, "Press X to cancel",
		             2, ALIGN_CENTER);

		gfx_end();
	}

	input_poll();
	if (input_pressed(B_CROSS))
	{
		g_abort_rip = 1;
		return 1;
	}
	return 0;
}

/* ----------------------------------------------------------------- actions -- */

static void refresh_registry(void)
{
	g_reg_readable = 0;
	g_reg_loaded   = 0;

	if (xreg_load() != 0)
		return;

	g_reg_readable = 1;
	if (xreg_read_state(&g_reg) == 0)
		g_reg_loaded = 1;
}

/* Measuring the disc touches the drive, so it happens on rescan / mode change
 * and never inside the draw loop. */
static void measure_disc(void)
{
	g_size_valid = 0;
	g_disc_bytes = 0;
	g_ts_bytes   = 0;
	ustrlcpy(g_size_how, "unknown", sizeof(g_size_how));

	if (!g_disc.present || !g_disc.ifo_ok)
		return;

	disc_make_basename(&g_disc, g_basename, sizeof(g_basename));

	if (disc_video_ts_size(&g_ts_bytes) != 0)
		g_ts_bytes = 0;

	if (disc_sector_count(&g_disc_bytes, g_size_how, sizeof(g_size_how)) == 0 &&
	    g_disc_bytes > 0)
	{
		g_size_valid = 1;
	}
	else
	{
		/* fall back to what the filesystem says the disc holds */
		g_disc_bytes = g_ts_bytes;
		ustrlcpy(g_size_how, "estimated from VIDEO_TS", sizeof(g_size_how));
	}
}

static void refresh_disc(void)
{
	disc_probe(&g_disc);
	measure_disc();
}

static void make_backup(void)
{
	if (fs_exists(BACKUP_FILE))
	{
		show_dialog(DLG_INFO, "Backup already exists",
		            "%s\n\nA pristine copy is already there and was left alone.",
		            BACKUP_FILE);
		return;
	}

	if (xreg_load() != 0)
	{
		show_dialog(DLG_ERROR, "Cannot read the registry",
		            "%s could not be read.\n\nRun this app from CFW, HEN or HAN.",
		            XREG_PATH);
		return;
	}

	fs_mkdir_p(APP_BACKUP_DIR, 0777);
	if (xreg_backup(BACKUP_FILE) != 0)
	{
		show_dialog(DLG_ERROR, "Backup failed",
		            "Could not write:\n%s", BACKUP_FILE);
		return;
	}

	refresh_registry();
	show_dialog(DLG_INFO, "Backup created", "Saved to:\n%s", BACKUP_FILE);
}

static void ask_restore(void)
{
	static const uint8_t magic[16] = {
		0xBC, 0xAD, 0xAD, 0xBC, 0x00, 0x00, 0x00, 0x90,
		0x00, 0x00, 0x00, 0x02, 0xBC, 0xAD, 0xAD, 0xBC
	};
	uint8_t head[16];
	s32 fd = -1;

	if (!fs_exists(BACKUP_FILE))
	{
		show_dialog(DLG_ERROR, "No backup", "Nothing to restore.\n\n%s",
		            BACKUP_FILE);
		return;
	}

	if (sysFsOpen(BACKUP_FILE, SYS_O_RDONLY, &fd, NULL, 0) != 0 || fd < 0)
	{
		show_dialog(DLG_ERROR, "No backup", "Could not open:\n%s", BACKUP_FILE);
		return;
	}
	if (io_read_all(fd, head, sizeof(head)) != 0 ||
	    memcmp(head, magic, sizeof(magic)) != 0)
	{
		sysFsClose(fd);
		show_dialog(DLG_ERROR, "Backup is damaged",
		            "The header does not match a PS3 registry.\nRestore aborted.");
		return;
	}
	sysFsClose(fd);

	show_dialog(DLG_YESNO, "Overwrite the registry?",
	            "The current settings in %s will be replaced by the copy on the "
	            "HDD. Restart the console afterwards.", XREG_PATH);
	g_dlg_act = DLGACT_RESTORE;
}

static void do_restore(void)
{
	if (xreg_load_from(BACKUP_FILE) != 0)
	{
		show_dialog(DLG_ERROR, "Restore failed",
		            "Could not read the backup image.");
		return;
	}

	if (xreg_save() != 0)
	{
		show_dialog(DLG_ERROR, "Restore failed",
		            "Writing %s failed.\n\nFlash is probably read only here.",
		            XREG_PATH);
		refresh_registry();
		return;
	}

	refresh_registry();
	show_dialog(DLG_INFO, "Registry restored",
	            "The backup was written back to flash.\nRestart the console.");
}

static void apply_region(void)
{
	int written;
	char path[600];

	if (xreg_load() != 0)
	{
		show_dialog(DLG_ERROR, "Cannot read the registry", "%s", XREG_PATH);
		return;
	}

	snprintf(path, sizeof(path), "%s", BACKUP_FILE);
	fs_mkdir_p(APP_BACKUP_DIR, 0777);
	if (!fs_exists(path))
		xreg_backup(path);

	written = xreg_apply(&g_reg);
	if (written <= 0)
	{
		show_dialog(DLG_ERROR, "Nothing written",
		            "None of the region keys were found in\n%s\n\nIt was left untouched.",
		            XREG_PATH);
		refresh_registry();
		return;
	}

	if (xreg_save() != 0)
	{
		show_dialog(DLG_ERROR, "Write failed",
		            "Could not write %s.\n\nFlash is probably read only here.",
		            XREG_PATH);
		refresh_registry();
		return;
	}

	refresh_registry();
	show_dialog(DLG_INFO, "Region applied",
	            "%d setting(s) written to flash.\n\nRestart the console for the "
	            "change to take effect.", written);
}

static void start_rip(void)
{
	char dir[700];
	char file[740];
	const char *dev = g_dev[g_dest];

	if (!g_disc.present || !g_disc.ifo_ok)
	{
		show_dialog(DLG_ERROR, "No disc",
		            "Insert a DVD-Video disc and press TRIANGLE to rescan.");
		return;
	}

	disc_make_basename(&g_disc, g_basename, sizeof(g_basename));
	out_dir_for(dir, sizeof(dir), dev, g_basename);
	fs_mkdir_p(dir, 0777);

	g_abort_rip    = 0;
	g_rip_started  = 0;
	g_rip_start_ms = 0;
	g_rip_last_draw = input_millis();

	if (g_dump_mode == 0)
	{
		char a[32], b[32];
		uint64_t free_bytes = device_free(dev);

		if (!g_size_valid || g_disc_bytes == 0)
		{
			show_dialog(DLG_ERROR, "Drive not accessible",
			            "The raw drive gave no usable size (%s).\n"
			            "Use the VIDEO_TS folder copy instead.", g_size_how);
			return;
		}
		if (free_bytes < g_disc_bytes)
		{
			format_size(g_disc_bytes, a, sizeof(a));
			format_size(free_bytes, b, sizeof(b));
			show_dialog(DLG_ERROR, "Not enough space",
			            "Need %s, %s has %s free.", a, dev, b);
			return;
		}

		out_file_for(file, sizeof(file), dev, g_basename);
		rip_raw_iso(file, g_disc_bytes / 2048ULL, g_patch_region,
		            rip_progress_cb, NULL, &g_rip);
	}
	else
	{
		uint64_t free_bytes = device_free(dev);

		if (g_ts_bytes > 0 && free_bytes < g_ts_bytes)
		{
			char a[32], b[32];
			format_size(g_ts_bytes, a, sizeof(a));
			format_size(free_bytes, b, sizeof(b));
			show_dialog(DLG_ERROR, "Not enough space",
			            "Need about %s, %s has %s free.", a, dev, b);
			return;
		}

		rip_copy_video_ts(dir, g_patch_region, rip_progress_cb, NULL, &g_rip);
	}

	if (g_abort_rip)
	{
		show_dialog(DLG_WARN, "Cancelled",
		            "The partial output is still on disk:\n%s", g_rip.out_path);
	}
	else if (g_rip.ok)
	{
		show_dialog(DLG_INFO, "Dump finished",
		            "%s\n\n%s\n\n%s written", g_rip.out_path, g_rip.message,
		            g_rip.patched ? "Region mask cleared." : "Region kept as is.");
	}
	else
	{
		show_dialog(DLG_ERROR, "Dump failed", "%s\n\nOutput was:\n%s",
		            g_rip.message, g_rip.out_path);
	}
}

/* --------------------------------------------------------------- navigation - */

static void handle_region(void)
{
	unsigned long rep = 0;
	int count;

	if (input_pressed(B_LEFT) || input_pressed(B_RIGHT))
		g_region_tab ^= 1;

	count = (g_region_tab == 0) ? PS3_REGION_COUNT : 3;
	if (g_region_sel >= count)
		g_region_sel = count - 1;

	if (input_repeat(B_UP | B_DOWN, &rep, INPUT_REPEAT_DELAY_MS, INPUT_REPEAT_RATE_MS))
	{
		g_region_sel += input_held(B_DOWN) ? 1 : -1;
		if (g_region_sel < 0)        g_region_sel = count - 1;
		if (g_region_sel >= count)   g_region_sel = 0;
	}

	if (input_pressed(B_CROSS))
	{
		if (g_region_tab == 0)
		{
			const ps3_region_entry *e = &PS3_REGIONS[g_region_sel];
			g_reg.ps3_region = e->ps3_region;
			g_reg.dvd_region = e->dvd_region;
			g_reg.bd_region  = e->bd_region;
		}
		else if (g_region_sel == 0)
		{
			g_reg.dvd_region = (g_reg.dvd_region + 1) % 7;
		}
		else if (g_region_sel == 1)
		{
			int idx = 0, i;
			for (i = 0; i < 4; i++)
				if (MANUAL_BD[i] == g_reg.bd_region)
					idx = i;
			g_reg.bd_region = MANUAL_BD[(idx + 1) % 4];
		}
		else
		{
			g_reg.tv_system = (g_reg.tv_system + 1) % 4;
		}
	}

	if (input_pressed(B_TRIANGLE))
		make_backup();

	if (input_pressed(B_START))
		apply_region();
}

static void handle_disc(void)
{
	unsigned long rep = 0;
	int d;

	if (input_repeat(B_UP | B_DOWN, &rep, INPUT_REPEAT_DELAY_MS, INPUT_REPEAT_RATE_MS))
	{
		g_disc_sel += input_held(B_DOWN) ? 1 : -1;
		if (g_disc_sel < 0)  g_disc_sel = 3;
		if (g_disc_sel > 3)  g_disc_sel = 0;
	}

	d = input_held(B_RIGHT) ? 1 : -1;

	/* LEFT / RIGHT and X both operate on the highlighted row. */
	if (input_pressed(B_LEFT) || input_pressed(B_RIGHT) || input_pressed(B_CROSS))
	{
		switch (g_disc_sel)
		{
		case 0:
			g_dump_mode ^= 1;
			break;
		case 1:
			g_patch_region ^= 1;
			break;
		case 2:
			if (g_dev_count > 0)
				g_dest = (g_dest + d + g_dev_count) % g_dev_count;
			break;
		default:
			if (input_pressed(B_CROSS))
				start_rip();
			break;
		}
	}

	if (input_pressed(B_TRIANGLE) || input_pressed(B_START))
	{
		refresh_disc();
		show_dialog(DLG_INFO, "Rescanned",
		            (g_disc.present && g_disc.ifo_ok) ? "Disc found: %s"
		                                             : "No DVD-Video disc found",
		            g_disc.provider);
	}
}

static void handle_registry(void)
{
	if (input_pressed(B_UP) || input_pressed(B_DOWN))
	{
		g_registry_sel = (g_registry_sel + (input_held(B_DOWN) ? 1 : 2)) % 3;
		return;
	}

	if (input_pressed(B_CROSS))
	{
		if (g_registry_sel == 0)
			make_backup();
		else if (g_registry_sel == 1)
			ask_restore();
		else
		{
			/* Honours the request only on a modified hypervisor; if the
			 * console is still alive afterwards it was refused. */
			input_restart_console();
			show_dialog(DLG_INFO, "Restart requested",
			            "If this app is still running, your firmware refused the\n"
			            "request: power the console off and on again instead.");
		}
	}
}

static void handle_home(void)
{
	unsigned long rep = 0;

	if (input_repeat(B_UP | B_DOWN, &rep, INPUT_REPEAT_DELAY_MS, INPUT_REPEAT_RATE_MS))
		g_home_sel = (g_home_sel + (input_held(B_DOWN) ? 1 : 4)) % 5;

	if (input_pressed(B_CROSS))
	{
		switch (g_home_sel)
		{
		case 0: g_screen = SCR_REGION;   g_region_tab = 0; break;
		case 1: g_screen = SCR_DISC;     g_disc_sel = 3;    break;
		case 2: g_screen = SCR_REGISTRY; g_registry_sel = 0; break;
		case 3: g_screen = SCR_HELP;     break;
		default:
			/* Confirmation, because an accidental exit on a five item list
			 * used to look like a crash. */
			show_dialog(DLG_YESNO, "Quit to the XMB?",
			            "Close DVD Region Tools and return to the console menu.");
			g_dlg_act = DLGACT_QUIT;
			break;
		}
	}

	if (input_pressed(B_TRIANGLE) || input_pressed(B_START))
	{
		refresh_disc();
		refresh_registry();
	}
}

static int handle_global(void)
{
	/* L1 / R1 move between pages: LEFT and RIGHT belong to the individual
	 * screens, which use them for in-place adjustments. */
	if (input_pressed(B_L1) || input_pressed(B_R1))
	{
		int d = input_held(B_R1) ? 1 : -1;
		int next = (int)g_screen + d;
		if (next < 0)            next = SCR_COUNT - 1;
		if (next >= SCR_COUNT)   next = 0;
		g_screen = (screen_id)next;
		return 1;
	}

	if (input_pressed(B_CIRCLE))
	{
		if (g_screen != SCR_HOME)
			g_screen = SCR_HOME;
		else
			g_running = 0;
	}
	return 0;
}

/* --------------------------------------------------------------------- main - */

static void draw(void)
{
	gfx_begin();
	gfx_clear(COL_BG);
	draw_header();

	switch (g_screen)
	{
	case SCR_HOME:
		draw_home();
		draw_footer("X select    L1/R1 page    TRIANGLE rescan    O quit");
		break;
	case SCR_REGION:
		draw_region();
		draw_footer("UP/DOWN move    LEFT/RIGHT tab    X change    START apply    TRIANGLE backup    O back");
		break;
	case SCR_DISC:
		draw_disc();
		draw_footer("UP/DOWN move    LEFT/RIGHT or X change    TRIANGLE rescan    O back");
		break;
	case SCR_REGISTRY:
		draw_registry();
		draw_footer("UP/DOWN move    X run    O back");
		break;
	case SCR_HELP:
		draw_help();
		draw_footer("L1/R1 page    O back");
		break;
	default:
		break;
	}

	if (g_dlg != DLG_NONE)
		draw_dialog();

	gfx_end();
}

static void handle_dialog(void)
{
	if (g_dlg == DLG_YESNO)
	{
		if (input_pressed(B_CROSS))
		{
			dialog_action act = g_dlg_act;
			g_dlg     = DLG_NONE;
			g_dlg_act = DLGACT_NONE;

			if (act == DLGACT_RESTORE)
				do_restore();
			else if (act == DLGACT_QUIT)
				g_running = 0;
			return;
		}
		if (input_pressed(B_CIRCLE))
		{
			g_dlg     = DLG_NONE;
			g_dlg_act = DLGACT_NONE;
		}
		return;
	}

	if (input_pressed(B_CROSS) || input_pressed(B_CIRCLE))
	{
		g_dlg     = DLG_NONE;
		g_dlg_act = DLGACT_NONE;
	}
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	if (gfx_init() != 0)
		return 1;

	input_init();
	build_devices();
	refresh_registry();
	refresh_disc();

	while (g_running)
	{
		input_poll();

		if (g_dlg != DLG_NONE)
			handle_dialog();
		else if (!handle_global())
		{
			switch (g_screen)
			{
			case SCR_HOME:     handle_home();     break;
			case SCR_REGION:   handle_region();   break;
			case SCR_DISC:     handle_disc();     break;
			case SCR_REGISTRY: handle_registry(); break;
			default: break;
			}
		}

		draw();
		usleep(16666);
	}

	/*
	 * Tear down with sys_ppu_exit rather than returning from main: a PSL1GHT
	 * SELF has no C runtime unwinding to fall back on, and letting SDL_Quit()
	 * run first is what made the old quit path look like a crash. Returning to
	 * the XMB is the process exit here.
	 */
	input_exit_app();

	return 0;
}
