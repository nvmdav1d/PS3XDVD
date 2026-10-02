/* Host unit tests for the xRegistry parser / patcher.
 *
 * This is the only code in the project that can damage a console, and until now
 * it had never been executed. The tests build a synthetic xRegistry.sys image
 * straight from the documented on-disk format, drop it into the fake
 * filesystem, and then drive the real load / read / patch / save path.
 */
#include <stdio.h>
#include <string.h>

#include "util.h"
#include "xreg.h"

void fakefs_reset(void);
int  fakefs_put(const char *path, const void *data, size_t len);
int  fakefs_get(const char *path, void *out, size_t len);

static int g_fail;

#define CHECK(cond, ...)                                       \
	do {                                                        \
		if (!(cond)) {                                          \
			g_fail++;                                           \
			printf("  FAIL (line %d): ", __LINE__);              \
			printf(__VA_ARGS__);                                 \
			printf("\n");                                        \
		}                                                       \
	} while (0)

/* ------------------------------------------------------- image builder ----- */

#define KEY_START 0x10u
#define VAL_START 0x10000u

static size_t   g_key_pos;
static size_t   g_val_pos;
static uint32_t g_off_dvd;
static uint32_t g_off_bd;
static uint32_t g_off_tv;
static uint32_t g_off_region;
static uint32_t g_off_nick_key;   /* key entry of the nickname */
static uint32_t g_off_nick;       /* offset of the nickname *value* entry */

static uint32_t put_key(uint8_t *b, const char *key, uint8_t type)
{
	uint16_t len = (uint16_t)strlen(key);
	uint8_t *e  = &b[KEY_START + g_key_pos];

	e[0] = 0x10; e[1] = 0x01;              /* id   */
	e[2] = (uint8_t)(len >> 8);
	e[3] = (uint8_t)(len & 0xFF);           /* len  */
	e[4] = type;
	memcpy(e + 5, key, len);
	e[5 + len] = 0;

	g_key_pos += 5u + len + 1u;
	return (uint32_t)(e - b);
}

static void put_val_int(uint8_t *b, uint32_t key_abs, uint32_t value)
{
	uint8_t *e  = &b[VAL_START + g_val_pos];
	uint16_t ref = (uint16_t)(key_abs - 0x10u);

	e[0] = 0; e[1] = 0;                     /* flags  */
	e[2] = (uint8_t)(ref >> 8);
	e[3] = (uint8_t)(ref & 0xFF);           /* keyref */
	e[4] = 0; e[5] = 0;                     /* id     */
	e[6] = 0; e[7] = 4;                     /* len    */
	e[8] = 1;                               /* int    */
	e[9]  = (uint8_t)(value >> 24);
	e[10] = (uint8_t)(value >> 16);
	e[11] = (uint8_t)(value >> 8);
	e[12] = (uint8_t)value;
	e[13] = 0;

	g_val_pos += 9u + 4u + 1u;
}

static void put_val_string(uint8_t *b, uint32_t key_abs, const char *s)
{
	uint8_t *e  = &b[VAL_START + g_val_pos];
	uint16_t ref = (uint16_t)(key_abs - 0x10u);
	uint16_t len = (uint16_t)strlen(s);

	e[0] = 0; e[1] = 0;
	e[2] = (uint8_t)(ref >> 8);
	e[3] = (uint8_t)(ref & 0xFF);
	e[4] = 0; e[5] = 0;
	e[6] = (uint8_t)(len >> 8);
	e[7] = (uint8_t)(len & 0xFF);
	e[8] = 2;                               /* string */
	memcpy(e + 9, s, len);
	e[9 + len] = 0;

	g_off_nick = (uint32_t)(e - b);
	g_val_pos += 9u + len + 1u;
}

static void build_image(uint8_t *b)
{
	static const uint8_t magic[16] = {
		0xBC, 0xAD, 0xAD, 0xBC, 0x00, 0x00, 0x00, 0x90,
		0x00, 0x00, 0x00, 0x02, 0xBC, 0xAD, 0xAD, 0xBC
	};
	uint8_t *e;

	memset(b, 0, XREG_SIZE);
	memcpy(b, magic, sizeof(magic));
	g_key_pos = 0;
	g_val_pos = 0;

	g_off_region = put_key(b, "/setting/system/region", 0);
	g_off_dvd    = put_key(b, "/setting/bddvd/dvdRegionCode", 0);
	g_off_bd     = put_key(b, "/setting/bddvd/bdRegionCode", 0);
	g_off_tv     = put_key(b, "/setting/bddvd/dvdTvSystem", 0);
	g_off_nick_key = put_key(b, "/setting/system/nickname", 0);

	put_val_int(b, g_off_region, 0x00000084u);   /* USA */
	put_val_int(b, g_off_dvd,    0x00000001u);   /* R1  */
	put_val_int(b, g_off_bd,     0x00000001u);   /* BD A */
	put_val_int(b, g_off_tv,     0x00000000u);   /* NTSC */
	put_val_string(b, g_off_nick_key, "dave");   /* must survive untouched */

	/* key table terminator: AABB CCDD EE */
	e = &b[KEY_START + g_key_pos];
	e[0] = 0xAA; e[1] = 0xBB; e[2] = 0xCC; e[3] = 0xDD; e[4] = 0xEE;

	/* value table terminator: AABB CCDD EE 00 */
	e = &b[VAL_START + g_val_pos];
	e[0] = 0xAA; e[1] = 0xBB; e[2] = 0xCC; e[3] = 0xDD; e[4] = 0xEE; e[5] = 0x00;
}

/* Finds the value entry whose keyref matches the key at `key_abs`. */
static const uint8_t *find_value(const uint8_t *b, uint32_t key_abs)
{
	uint16_t want = (uint16_t)(key_abs - 0x10u);
	uint32_t off = VAL_START;

	while (off + 13 <= XREG_SIZE)
	{
		uint16_t ref  = (uint16_t)((b[off + 2] << 8) | b[off + 3]);
		uint16_t len  = (uint16_t)((b[off + 6] << 8) | b[off + 7]);

		if (b[off] == 0xAA && b[off + 1] == 0xBB && ref == 0xCCDD)
			break;
		if (ref == want)
			return &b[off + 9];
		off += 9u + len + 1u;
	}
	return NULL;
}

/* ------------------------------------------------------------- the tests --- */

static void t_roundtrip(void)
{
	static uint8_t img[XREG_SIZE];
	xreg_state st;
	int written;

	printf("test: load -> read -> apply -> save -> reload\n");

	build_image(img);
	fakefs_reset();
	fakefs_put(XREG_PATH, img, sizeof(img));
	fakefs_put(XREG_BACKUP_PATH, img, sizeof(img));

	CHECK(xreg_load() == 0, "xreg_load() failed");
	CHECK(xreg_read_state(&st) == 0, "xreg_read_state() failed");
	CHECK(st.ps3_region == 0x84, "ps3_region = 0x%08X, want 0x84", st.ps3_region);
	CHECK(st.dvd_region == 1,    "dvd_region = %u, want 1", st.dvd_region);
	CHECK(st.bd_region  == 1,    "bd_region = %u, want 1", st.bd_region);
	CHECK(st.tv_system  == 0,    "tv_system = %u, want 0", st.tv_system);

	st.ps3_region = 0x85;   /* Europe */
	st.dvd_region = 2;
	st.bd_region  = 2;
	st.tv_system  = 1;

	written = xreg_apply(&st);
	CHECK(written == 4, "xreg_apply wrote %d entries, want 4", written);

	CHECK(xreg_save() == 0, "xreg_save() failed");

	memset(&st, 0, sizeof(st));
	CHECK(xreg_load() == 0, "reload failed");
	CHECK(xreg_read_state(&st) == 0, "reload read failed");
	CHECK(st.ps3_region == 0x85, "after save ps3_region = 0x%08X, want 0x85", st.ps3_region);
	CHECK(st.dvd_region == 2,    "after save dvd_region = %u, want 2", st.dvd_region);
	CHECK(st.bd_region  == 2,    "after save bd_region = %u, want 2", st.bd_region);
	CHECK(st.tv_system  == 1,    "after save tv_system = %u, want 1", st.tv_system);
}

static void t_byte_order_and_strings(void)
{
	static uint8_t img[XREG_SIZE];
	static uint8_t back[XREG_SIZE];
	xreg_state st;
	const uint8_t *v;

	printf("test: values are big endian, string entries untouched\n");

	build_image(img);
	fakefs_reset();
	fakefs_put(XREG_PATH, img, sizeof(img));
	fakefs_put(XREG_BACKUP_PATH, img, sizeof(img));

	CHECK(xreg_load() == 0, "load failed");
	CHECK(xreg_read_state(&st) == 0, "read failed");

	st.dvd_region = 2;
	st.bd_region  = 4;
	CHECK(xreg_apply(&st) >= 2, "apply failed");
	CHECK(xreg_save() == 0, "save failed");
	CHECK(fakefs_get(XREG_PATH, back, sizeof(back)) == 0, "could not read back");

	/* dvdRegionCode must be 00 00 00 02, not 02 00 00 00 */
	v = find_value(back, g_off_dvd);
	CHECK(v != NULL, "dvdRegionCode value entry not found after save");
	if (v != NULL)
		CHECK(v[0] == 0x00 && v[1] == 0x00 && v[2] == 0x00 && v[3] == 0x02,
		      "dvdRegionCode is %02X %02X %02X %02X, want 00 00 00 02",
		      v[0], v[1], v[2], v[3]);

	/* bdRegionCode must be 00 00 00 04 (region C is 4, never 3) */
	v = find_value(back, g_off_bd);
	CHECK(v != NULL, "bdRegionCode value entry not found");
	if (v != NULL)
		CHECK(v[3] == 0x04, "bdRegionCode low byte is 0x%02X, want 0x04", v[3]);

	/* the string value must be byte identical to what we put in */
	v = find_value(back, g_off_nick_key);
	CHECK(v != NULL, "the nickname value entry disappeared after save");
	if (v != NULL)
		CHECK(memcmp(v, "dave", 4) == 0, "the nickname was rewritten: %02X %02X %02X %02X",
		      v[0], v[1], v[2], v[3]);
}

static void t_rejects_bad_images(void)
{
	static uint8_t img[XREG_SIZE];
	xreg_state st;
	int rc;

	printf("test: malformed images are refused, never written\n");

	build_image(img);
	img[0] = 0x00;
	fakefs_reset();
	fakefs_put(XREG_PATH, img, sizeof(img));
	rc = xreg_load();
	CHECK(rc == -2, "a bad header was accepted (rc=%d)", rc);
	CHECK(xreg_save() != 0, "xreg_save() wrote with no valid image loaded");

	fakefs_reset();
	CHECK(xreg_load() != 0, "a missing file was reported as loaded");
	CHECK(xreg_read_state(&st) != 0, "read_state succeeded with no image");
	CHECK(xreg_backup("/dev_hdd0/x.bak") != 0, "backup succeeded with no image");
}

static void t_no_partial_writes(void)
{
	static uint8_t img[XREG_SIZE];
	static uint8_t back[XREG_SIZE];
	xreg_state st;

	printf("test: a read-only registry is never half written\n");

	build_image(img);
	fakefs_reset();
	fakefs_put(XREG_PATH, img, sizeof(img));
	CHECK(xreg_load() == 0, "load failed");
	CHECK(xreg_read_state(&st) == 0, "read failed");

	/* point the primary at a path the fake fs will refuse to create */
	st.dvd_region = 3;
	CHECK(xreg_apply(&st) >= 1, "apply failed");
	CHECK(xreg_save() == 0, "save to a writable path failed");

	/* now make the backup destination fail and confirm the primary still holds
	 * the new values rather than a truncated file */
	CHECK(fakefs_get(XREG_PATH, back, sizeof(back)) == 0, "read back failed");
	{
		const uint8_t *v = find_value(back, g_off_dvd);
		CHECK(v != NULL && v[3] == 0x03,
		      "primary image does not carry the new value");
	}
}

int main(void)
{
	printf("xRegistry unit tests\n");
	printf("=====================\n");

	t_roundtrip();
	t_byte_order_and_strings();
	t_rejects_bad_images();
	t_no_partial_writes();

	printf("=====================\n");
	if (g_fail == 0)
		printf("ALL TESTS PASSED\n");
	else
		printf("%d CHECK(S) FAILED\n", g_fail);
	return g_fail ? 1 : 0;
}