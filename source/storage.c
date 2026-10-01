/* storage.c */
#include <string.h>

#include <ppu-lv2.h>

#include "storage.h"
#include "util.h"

int storage_open(uint64_t device_id, uint32_t *handle)
{
	lv2syscall4(STORAGE_OPEN, device_id, 0, (u64)(uint32_t)handle, 0);
	return (int)p1;
}

int storage_close(uint32_t handle)
{
	lv2syscall1(STORAGE_CLOSE, (u64)handle);
	return (int)p1;
}

int storage_read(uint32_t handle, uint64_t lba, uint32_t sectors, void *buf,
                 uint32_t *sectors_read)
{
	lv2syscall7(STORAGE_READ, (u64)handle, 0, lba, (u64)sectors,
	            (u64)(uint32_t)buf, (u64)(uint32_t)sectors_read, 0);
	return (int)p1;
}

int storage_get_info(uint64_t device_id, storage_device_info *info)
{
	lv2syscall3(STORAGE_GET_DEVICE_INFO, device_id,
	            (u64)(uint32_t)info, (u64)sizeof(*info));
	return (int)p1;
}

int storage_atapi(uint32_t handle, const uint8_t *cmd, uint32_t cmd_len,
                  uint8_t *buf, uint32_t buf_len, uint32_t *transferred)
{
	atapi_cmd_block blk;
	uint64_t tag = 0;
	int ret;

	(void)transferred;

	memset(&blk, 0, sizeof(blk));
	memcpy(blk.pkt, cmd, cmd_len < sizeof(blk.pkt) ? cmd_len : sizeof(blk.pkt));
	blk.pktlen     = 12;
	blk.blocks     = 1;
	blk.block_size = buf_len;
	blk.proto      = 1; /* PIO data-in */
	blk.in_out     = 1; /* read        */

	/* Sony changed which of the two device-command syscalls carries ATAPI
	 * across firmware revisions, so try both and let the caller validate the
	 * reply. */
	lv2syscall7(STORAGE_EXECUTE_DEVICE_COMMAND, (u64)handle,
	            LV2_STORAGE_SEND_ATAPI_COMMAND,
	            (u64)(uint32_t)&blk, (u64)sizeof(blk),
	            (u64)(uint32_t)buf, (u64)buf_len, (u64)(uint32_t)&tag);
	ret = (int)p1;

	if (ret != 0)
	{
		memset(&blk, 0, sizeof(blk));
		memcpy(blk.pkt, cmd, cmd_len < sizeof(blk.pkt) ? cmd_len : sizeof(blk.pkt));
		blk.pktlen     = 12;
		blk.blocks     = 1;
		blk.block_size = buf_len;
		blk.proto      = 1;
		blk.in_out     = 1;

		lv2syscall7(STORAGE_SEND_DEVICE_COMMAND, (u64)handle,
		            LV2_STORAGE_SEND_ATAPI_COMMAND,
		            (u64)(uint32_t)&blk, (u64)sizeof(blk),
		            (u64)(uint32_t)buf, (u64)buf_len, (u64)(uint32_t)&tag);
		ret = (int)p1;
	}

	return ret;
}

int storage_read_leadout(uint32_t handle, uint32_t *leadout_lba)
{
	uint8_t cmd[12];
	uint8_t resp[32];
	uint32_t leadout;
	int ret;

	memset(cmd, 0, sizeof(cmd));
	cmd[0] = GPCMD_READ_TOC_PMA_ATIP;   /* READ TOC / PMA / ATIP */
	cmd[1] = 0x00;                      /* format 0            */
	cmd[2] = 0x00;                      /* no extended TOC     */
	cmd[6] = 0x00;                      /* track number = 0 (TOC) */
	cmd[7] = 0x00;
	cmd[8] = 0x0C;                      /* allocation length 12 */

	memset(resp, 0, sizeof(resp));

	ret = storage_atapi(handle, cmd, 12, resp, sizeof(resp), NULL);
	if (ret != 0)
		return ret;

	/* READ TOC format 0:
	 *   0-1 data length, 2 first track, 3 last track,
	 *   4-7 lead-out address (big endian), 8-11 LBA of track 1          */
	if (resp[2] < 1 || resp[2] > 99 || resp[3] < resp[2] || resp[3] > 99)
		return -1;

	leadout = be32(resp + 4);
	if (leadout < 0x1000u || leadout > 0x200000u)
		return -1;   /* not a DVD sized medium */

	*leadout_lba = leadout;
	return 0;
}
