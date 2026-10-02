/* input.h - libpad wrapper with press edges and auto-repeat */
#ifndef DVDREGION_INPUT_H
#define DVDREGION_INPUT_H

#include <stdint.h>

#define B_UP       (1u << 0)
#define B_DOWN     (1u << 1)
#define B_LEFT     (1u << 2)
#define B_RIGHT    (1u << 3)
#define B_CROSS    (1u << 4)
#define B_CIRCLE   (1u << 5)
#define B_TRIANGLE (1u << 6)
#define B_SQUARE   (1u << 7)
#define B_START    (1u << 8)
#define B_SELECT   (1u << 9)
#define B_L1       (1u << 10)
#define B_R1       (1u << 11)
#define B_L2       (1u << 12)
#define B_R2       (1u << 13)

int  input_init(void);
void input_poll(void);

/* 1 only on the frame the button went down */
int  input_pressed(uint32_t mask);
/* 1 for as long as the button is down */
int  input_held(uint32_t mask);

/* The raw button mask from the most recent poll. */
uint32_t input_mask(void);

/* Auto-repeat helper for list navigation.
 * Fires immediately on press, then after `delay_ms`, then every `rate_ms`.
 * `state` must be a zero-initialised unsigned long owned by the caller.
 *
 * The defaults below are deliberately unhurried: these lists are five items
 * long and overshooting a selection is worse than having to wait.
 */
#define INPUT_REPEAT_DELAY_MS 500u
#define INPUT_REPEAT_RATE_MS  220u

int  input_repeat(uint32_t mask, unsigned long *state, uint32_t delay_ms,
                  uint32_t rate_ms);

/* Milliseconds since input_init(). */
uint32_t input_millis(void);

/* Diagnostics for the HOME screen, so a dead pad can be told apart from a
 * dead UI without attaching a debugger. */
extern int      g_ioPadInitRet;
extern int      g_pads_connected;
extern uint32_t g_pad_last_len;
extern uint32_t g_pad_last_word;
extern int      g_pad_ports_seen;

/* Requests a console restart. If the console honours it this call never
 * returns; otherwise it returns immediately and the user has to restart by
 * hand. */
void input_restart_console(void);

void input_exit_app(void);        /* sys_ppu_exit */

#endif /* DVDREGION_INPUT_H */
