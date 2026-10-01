/* input.c */
#include <string.h>
#include <sys/time.h>

#include <io/pad.h>
#include <ppu-lv2.h>

#include "input.h"

#define SYSCALL_PPU_EXIT        82
#define SYSCALL_SS_RTC_SET_RTC 1201

static uint32_t g_cur;
static uint32_t g_prev;
static int      g_ready;
static struct timeval g_t0;

int input_init(void)
{
	memset(&g_cur, 0, sizeof(g_cur));
	memset(&g_prev, 0, sizeof(g_prev));
	g_ready = (ioPadInit(7) == PAD_OK) ? 1 : 0;
	gettimeofday(&g_t0, NULL);
	return g_ready;
}

uint32_t input_millis(void)
{
	struct timeval now;
	gettimeofday(&now, NULL);
	return (uint32_t)((now.tv_sec - g_t0.tv_sec) * 1000 +
	                  (now.tv_usec - g_t0.tv_usec) / 1000);
}

static uint32_t build_mask(const padData *d)
{
	uint32_t m = 0;

	if (d->BTN_CROSS    != 0) m |= B_CROSS;
	if (d->BTN_CIRCLE   != 0) m |= B_CIRCLE;
	if (d->BTN_TRIANGLE != 0) m |= B_TRIANGLE;
	if (d->BTN_SQUARE   != 0) m |= B_SQUARE;
	if (d->BTN_UP       != 0) m |= B_UP;
	if (d->BTN_DOWN     != 0) m |= B_DOWN;
	if (d->BTN_LEFT     != 0) m |= B_LEFT;
	if (d->BTN_RIGHT    != 0) m |= B_RIGHT;
	if (d->BTN_START    != 0) m |= B_START;
	if (d->BTN_SELECT   != 0) m |= B_SELECT;
	if (d->BTN_L1       != 0) m |= B_L1;
	if (d->BTN_R1       != 0) m |= B_R1;
	if (d->BTN_L2       != 0) m |= B_L2;
	if (d->BTN_R2       != 0) m |= B_R2;

	/* The BD remote carries its own code word; map the useful keys onto the
	 * same buttons so the whole UI works with either device. */
	if (d->BTN_BDCODE != 0)
	{
		switch (d->BTN_BDCODE)
		{
		case BTN_BD_CROSS:    m |= B_CROSS;    break;
		case BTN_BD_CIRCLE:   m |= B_CIRCLE;   break;
		case BTN_BD_TRIANGLE: m |= B_TRIANGLE; break;
		case BTN_BD_SQUARE:   m |= B_SQUARE;   break;
		case BTN_BD_UP:       m |= B_UP;       break;
		case BTN_BD_DOWN:     m |= B_DOWN;     break;
		case BTN_BD_LEFT:     m |= B_LEFT;     break;
		case BTN_BD_RIGHT:    m |= B_RIGHT;    break;
		case BTN_BD_ENTER:    m |= B_CROSS;    break;
		case BTN_BD_RETURN:   m |= B_CIRCLE;   break;
		case BTN_BD_EJECT:    m |= B_TRIANGLE; break;
		case BTN_BD_SELECT:   m |= B_SELECT;   break;
		case BTN_BD_START:    m |= B_START;    break;
		default: break;
		}
	}

	return m;
}

void input_poll(void)
{
	padData data;
	uint32_t mask = 0;

	memset(&data, 0, sizeof(data));
	if (g_ready)
		ioPadGetData(0, &data);

	if (data.len > 0)
		mask = build_mask(&data);

	g_prev = g_cur;
	g_cur  = mask;
}

int input_pressed(uint32_t mask)
{
	return ((g_cur & mask) && !(g_prev & mask)) ? 1 : 0;
}

int input_held(uint32_t mask)
{
	return (g_cur & mask) ? 1 : 0;
}

/* Fires immediately on press, then after `delay_ms`, then every `rate_ms`.
 * `state` must be a zero-initialised unsigned long owned by the caller. */
int input_repeat(uint32_t mask, unsigned long *state, uint32_t delay_ms, uint32_t rate_ms)
{
	uint32_t now;

	if (state == NULL)
		return input_pressed(mask);

	if (!input_held(mask))
	{
		*state = 0;
		return 0;
	}

	now = input_millis();
	if (*state == 0)
	{
		*state = (unsigned long)now + delay_ms + 1;
		return 1;
	}

	/* (int) on a wrapped unsigned difference, otherwise the sign is lost on
	 * a 64 bit long. */
	if ((int)(now - (uint32_t)*state) >= 0)
	{
		*state = (unsigned long)now + rate_ms;
		return 1;
	}
	return 0;
}

void input_exit_app(void)
{
	lv2syscall1(SYSCALL_PPU_EXIT, 0);
}

/* ------------------------------------------------------------------ restart --
 * Homebrew cannot reboot GameOS cleanly, but the RTC syscall with the
 * "restart" opcode (0x3001) is honoured by custom firmwares / HAN payloads
 * and is a no-op on stock firmware.  main() reports the outcome. */
void input_restart_console(void)
{
	lv2syscall2(SYSCALL_SS_RTC_SET_RTC, 0x3001ULL, 0x00000001ULL);
}