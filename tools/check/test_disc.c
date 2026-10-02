/* Host unit tests for DVD-Video region detection.
 *
 * The bug this exists for: the app used to report "region free" whenever the
 * VMG category byte happened to be zero, which is also what RPC-2 and pressed
 * discs report. Detection now cross checks the VMG against the first title set
 * and refuses to make the claim unless they agree.
 */
#include <stdio.h>
#include <string.h>

#include "disc.h"
#include "util.h"

void fakefs_reset(void);
int  fakefs_put(const char *path, const void *data, size_t len);

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

#define VMG_PATH "/dev_bdvd/VIDEO_TS/VIDEO_TS.IFO"
#define VTS_PATH "/dev_bdvd/VIDEO_TS/VTS_01_0.IFO"

static void build_ifo(uint8_t *b, const char *magic, uint8_t mask,
                      uint16_t titles, const char *provider)
{
	memset(b, 0, 256);
	memcpy(b, magic, 12);
	b[0x0C] = 0x00; b[0x0D] = 0x00; b[0x0E] = 0x10; b[0x0F] = 0x00; /* vmg last sector */
	b[0x10] = 0x00; b[0x11] = 0x00; b[0x12] = 0x00; b[0x13] = 0x04; /* ifo last sector */
	b[0x22] = mask;
	b[0x3E] = (uint8_t)(titles >> 8);
	b[0x3F] = (uint8_t)(titles & 0xFF);
	if (provider != NULL)
	{
		size_t n = strlen(provider);
		if (n > 32)
			n = 32;
		memcpy(b + 0x40, provider, n);
	}
}

/* No disc at all. */
static void t_no_disc(void)
{
	disc_info d;

	printf("test: no disc\n");
	fakefs_reset();
	CHECK(disc_probe(&d) == -1, "probe succeeded with an empty drive");
	CHECK(d.present == 0, "present was set");
	CHECK(d.ifo_ok == 0, "ifo_ok was set");
}

/* VMG present but not a DVD-Video volume. */
static void t_not_dvdvideo(void)
{
	disc_info d;
	uint8_t junk[256];

	printf("test: VIDEO_TS present but not DVD-Video\n");

	memset(junk, 0, sizeof(junk));
	memcpy(junk, "SOMETHING-EL", 12);
	fakefs_reset();
	fakefs_put(VMG_PATH, junk, sizeof(junk));

	CHECK(disc_probe(&d) == -1, "probe accepted a non DVD-Video file");
	CHECK(d.present == 1, "present should still be set");
	CHECK(d.ifo_ok == 0, "ifo_ok should not be set");
	CHECK(d.ifo_err == 2, "ifo_err = %d, want 2", d.ifo_err);
}

/* The reported bug: zero VMG byte must not be called region free on its own. */
static void t_zero_mask_needs_agreement(void)
{
	disc_info d;
	uint8_t vmg[256], vts[256];

	printf("test: a zero VMG byte alone is not proof of region free\n");

	/* VMG says 0x00, VTS says 0xFD (region 1 only): they disagree */
	build_ifo(vmg, "DVDVIDEO-VMG", 0x00, 1, "STUDIO");
	build_ifo(vts, "DVDVIDEO-VTS", 0xFD, 1, NULL);
	fakefs_reset();
	fakefs_put(VMG_PATH, vmg, sizeof(vmg));
	fakefs_put(VTS_PATH, vts, sizeof(vts));

	CHECK(disc_probe(&d) == 0, "probe failed");
	CHECK(d.ifo_ok == 1, "ifo_ok should be set");
	CHECK(d.have_vts == 1, "the VTS cross check did not run");
	CHECK(d.region_mask == 0x00, "VMG mask = 0x%02X", d.region_mask);
	CHECK(d.vts_mask == 0xFD, "VTS mask = 0x%02X", d.vts_mask);
	CHECK(d.region_conflict == 1, "the disagreement was not detected");
	CHECK(d.region_free == 0,
	      "still reported region free from a single zero byte");
	CHECK(strcmp(d.region_list, "unreliable") == 0,
	      "region_list = \"%s\", want \"unreliable\"", d.region_list);
}

/* Both agree on zero: now the claim is made. */
static void t_agreement_allows_claim(void)
{
	disc_info d;
	uint8_t vmg[256], vts[256];

	printf("test: agreeing zero masks may claim no restriction\n");

	build_ifo(vmg, "DVDVIDEO-VMG", 0x00, 2, "SONY");
	build_ifo(vts, "DVDVIDEO-VTS", 0x00, 2, NULL);
	fakefs_reset();
	fakefs_put(VMG_PATH, vmg, sizeof(vmg));
	fakefs_put(VTS_PATH, vts, sizeof(vts));

	CHECK(disc_probe(&d) == 0, "probe failed");
	CHECK(d.region_conflict == 0, "false conflict reported");
	CHECK(d.region_free == 1, "region_free not set on agreement");
	CHECK(strcmp(d.provider, "SONY") == 0, "provider = \"%s\"", d.provider);
	CHECK(d.num_titles == 2, "num_titles = %u, want 2", d.num_titles);
}

/* A normal region 2 only disc: 0xFD means bit 0 clear, all others set. */
static void t_region2(void)
{
	disc_info d;
	uint8_t vmg[256], vts[256];

	printf("test: 0xFD decodes to Region 2\n");

	build_ifo(vmg, "DVDVIDEO-VMG", 0xFD, 1, "FOX");
	build_ifo(vts, "DVDVIDEO-VTS", 0xFD, 1, NULL);
	fakefs_reset();
	fakefs_put(VMG_PATH, vmg, sizeof(vmg));
	fakefs_put(VTS_PATH, vts, sizeof(vts));

	CHECK(disc_probe(&d) == 0, "probe failed");
	CHECK(d.region_free == 0, "region_free should be false");
	CHECK(d.allowed_regions == (1u << 1),
	      "allowed_regions = 0x%02X, want 0x02", d.allowed_regions);
	CHECK(strcmp(d.region_list, "R2") == 0,
	      "region_list = \"%s\", want \"R2\"", d.region_list);
}

/* A multi region disc. */
static void t_multi_region(void)
{
	disc_info d;
	uint8_t vmg[256], vts[256];

	printf("test: 0xF1 decodes to R2,R3,R4\n");

	/* bits 1,2,3 clear (allowed), all others set (prohibited) */
	build_ifo(vmg, "DVDVIDEO-VMG", 0xF1, 1, "BBC");
	build_ifo(vts, "DVDVIDEO-VTS", 0xF1, 1, NULL);
	fakefs_reset();
	fakefs_put(VMG_PATH, vmg, sizeof(vmg));
	fakefs_put(VTS_PATH, vts, sizeof(vts));

	CHECK(disc_probe(&d) == 0, "probe failed");
	CHECK(d.allowed_regions == ((1u << 1) | (1u << 2) | (1u << 3)),
	      "allowed_regions = 0x%02X, want 0x0E", d.allowed_regions);
	CHECK(strcmp(d.region_list, "R2,R3,R4") == 0,
	      "region_list = \"%s\", want \"R2,R3,R4\"", d.region_list);
}

/* RPC-2 / RCE style disc. */
static void t_rce(void)
{
	disc_info d;
	uint8_t vmg[256], vts[256];

	printf("test: 0xFF is flagged as RCE, never region free\n");

	build_ifo(vmg, "DVDVIDEO-VMG", 0xFF, 1, "RCE");
	build_ifo(vts, "DVDVIDEO-VTS", 0xFF, 1, NULL);
	fakefs_reset();
	fakefs_put(VMG_PATH, vmg, sizeof(vmg));
	fakefs_put(VTS_PATH, vts, sizeof(vts));

	CHECK(disc_probe(&d) == 0, "probe failed");
	CHECK(d.rce_suspected == 1, "rce_suspected not set");
	CHECK(d.region_free == 0, "region_free must never be set for 0xFF");
	CHECK(d.allowed_regions == 0, "allowed_regions should stay 0");
	CHECK(strcmp(d.region_list, "RCE / unknown") == 0,
	      "region_list = \"%s\"", d.region_list);
}

/* No VTS readable: fall back to the VMG alone but do not claim region free. */
static void t_vmg_only(void)
{
	disc_info d;
	uint8_t vmg[256];

	printf("test: VMG only, zero byte stays unconfirmed\n");

	build_ifo(vmg, "DVDVIDEO-VMG", 0x00, 1, "SONY");
	fakefs_reset();
	fakefs_put(VMG_PATH, vmg, sizeof(vmg));

	{
		int rc = disc_probe(&d);
		CHECK(rc == 0, "probe failed (rc=%d)", rc);
	}
	CHECK(d.have_vts == 0, "have_vts should be 0");
	CHECK(d.region_conflict == 0, "no conflict without a second source");

	/* The 12 byte identifier occupies 0x00..0x0B, so the sector counts must be
	 * read from 0x0C and 0x10. They used to be read from inside the
	 * identifier, which printed a nonsense LBA on the Disc page. */
	CHECK(d.vmg_last_sector == 0x1000,
	      "vmg_last_sector = 0x%08X, want 0x00001000", d.vmg_last_sector);
	CHECK(d.ifo_last_sector == 0x00000004,
	      "ifo_last_sector = 0x%08X, want 0x00000004", d.ifo_last_sector);
	CHECK(d.region_free == 1, "a lone zero byte claimed region free again");
}

int main(void)
{
	printf("disc region unit tests\n");
	printf("======================\n");

	t_no_disc();
	t_not_dvdvideo();
	t_zero_mask_needs_agreement();
	t_agreement_allows_claim();
	t_region2();
	t_multi_region();
	t_rce();
	t_vmg_only();

	printf("======================\n");
	if (g_fail == 0)
		printf("ALL TESTS PASSED\n");
	else
		printf("%d CHECK(S) FAILED\n", g_fail);
	return g_fail ? 1 : 0;
}