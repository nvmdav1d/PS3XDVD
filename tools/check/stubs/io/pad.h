/* Stub of <io/pad.h> for the host compile check.
 *
 * The real padData embeds a bitfield struct inside an anonymous union. Only the
 * members this project touches are reproduced here, and they are plain
 * bitfields so a host C compiler can parse them. */
#ifndef STUB_IO_PAD_H
#define STUB_IO_PAD_H

#include <ppu-types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_PADS      (127)
#define MAX_PAD_CODES (64)
#define MAX_PORT_NUM  (7)

#define PAD_OK                  0
#define PAD_ERROR_FATAL         0x80121101
#define PAD_ERROR_INVALID_PARAMETER 0x80121102

typedef struct _pad_info
{
	u32 max;
	u32 connected;
	u32 info;
	u16 vendor_id[MAX_PADS];
	u16 product_id[MAX_PADS];
	u8  status[MAX_PADS];
} padInfo;

typedef struct _pad_data
{
	s32 len;

	unsigned int BTN_LEFT      : 1;
	unsigned int BTN_DOWN      : 1;
	unsigned int BTN_RIGHT     : 1;
	unsigned int BTN_UP        : 1;
	unsigned int BTN_START     : 1;
	unsigned int BTN_R3        : 1;
	unsigned int BTN_L3        : 1;
	unsigned int BTN_SELECT    : 1;

	unsigned int BTN_SQUARE    : 1;
	unsigned int BTN_CROSS     : 1;
	unsigned int BTN_CIRCLE    : 1;
	unsigned int BTN_TRIANGLE  : 1;
	unsigned int BTN_R1        : 1;
	unsigned int BTN_L1        : 1;
	unsigned int BTN_R2        : 1;
	unsigned int BTN_L2        : 1;

	unsigned int ANA_R_H : 16;
	unsigned int ANA_R_V : 16;
	unsigned int ANA_L_H : 16;
	unsigned int ANA_L_V : 16;

	unsigned int BTN_BDLEN  : 16;
	unsigned int BTN_BDCODE : 16;
} padData;

typedef enum _io_pad_bd_code
{
	BTN_BD_1       = 0x00,
	BTN_BD_2       = 0x01,
	BTN_BD_3       = 0x02,
	BTN_BD_4       = 0x03,
	BTN_BD_5       = 0x04,
	BTN_BD_6       = 0x05,
	BTN_BD_7       = 0x06,
	BTN_BD_8       = 0x07,
	BTN_BD_9       = 0x08,
	BTN_BD_0       = 0x09,
	BTN_BD_ENTER   = 0x0b,
	BTN_BD_RETURN  = 0x0e,
	BTN_BD_CLEAR   = 0x0f,
	BTN_BD_EJECT   = 0x16,
	BTN_BD_TOPMENU = 0x1a,
	BTN_BD_TIME    = 0x28,
	BTN_BD_PREV    = 0x30,
	BTN_BD_NEXT    = 0x31,
	BTN_BD_PLAY    = 0x32,
	BTN_BD_SCAN_REV  = 0x33,
	BTN_BD_SCAN_FWD  = 0x34,
	BTN_BD_STOP    = 0x38,
	BTN_BD_PAUSE   = 0x39,
	BTN_BD_POPUP_MENU = 0x40,
	BTN_BD_SELECT  = 0x50,
	BTN_BD_L3      = 0x51,
	BTN_BD_R3      = 0x52,
	BTN_BD_START   = 0x53,
	BTN_BD_UP      = 0x54,
	BTN_BD_RIGHT   = 0x55,
	BTN_BD_DOWN    = 0x56,
	BTN_BD_LEFT    = 0x57,
	BTN_BD_L2      = 0x58,
	BTN_BD_R2      = 0x59,
	BTN_BD_L1      = 0x5a,
	BTN_BD_R1      = 0x5b,
	BTN_BD_TRIANGLE = 0x5c,
	BTN_BD_CIRCLE  = 0x5d,
	BTN_BD_CROSS   = 0x5e,
	BTN_BD_SQUARE  = 0x5f,
	BTN_BD_SLOW_REV = 0x60,
	BTN_BD_SLOW_FWD = 0x61,
	BTN_BD_SUBTITLE = 0x63,
	BTN_BD_AUDIO   = 0x64,
	BTN_BD_ANGLE   = 0x65,
	BTN_BD_DISPLAY = 0x70,
	BTN_BD_BLUE    = 0x80,
	BTN_BD_RED     = 0x81,
	BTN_BD_GREEN   = 0x82,
	BTN_BD_YELLOW  = 0x83,
	BTN_BD_RELEASE = 0xff
} ioPadBdCode;

s32 ioPadInit(u32 max);
s32 ioPadEnd(void);
s32 ioPadGetInfo(padInfo *info);
s32 ioPadGetInfo2(void *info);
s32 ioPadClearBuf(u32 port);
s32 ioPadGetCapabilityInfo(u32 port, void *capabilities);
s32 ioPadGetData(u32 port, padData *data);
s32 ioPadGetDataExtra(u32 port, u32 *type, padData *data);
s32 ioPadSetPressMode(u32 port, u32 mode);
u32 ioPadSetActDirect(u32 port, void *actParam);

#ifdef __cplusplus
}
#endif

#endif