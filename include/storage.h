/* storage.h - LV2 block storage syscalls (sys_storage_*)
 *
 * PSL1GHT ships the syscall numbers but no wrappers, so they live here.
 * Note that sys_storage_open (600) and sys_storage_get_device_info (609)
 * are root-only on stock firmware: raw disc access needs a modified
 * hypervisor.  Everything that goes through sysFs* works everywhere.
 */
#ifndef DVDREGION_STORAGE_H
#define DVDREGION_STORAGE_H

#include <stdint.h>

#define STORAGE_OPEN                  600
#define STORAGE_CLOSE                 601
#define STORAGE_READ                  602
#define STORAGE_GET_DEVICE_INFO       609
#define STORAGE_SEND_DEVICE_COMMAND   604
#define STORAGE_EXECUTE_DEVICE_COMMAND 616

#define BDVD_DEVICE_ID 0x101000000000006ULL

#define LV2_STORAGE_SEND_ATAPI_COMMAND 1

#define GPCMD_READ_TOC_PMA_ATIP 0x43

typedef struct
{
	char     label[32];
	uint32_t pad_20;
	uint32_t pad_24;
	uint64_t sector_count;
	uint32_t sector_size;
	uint32_t one;
	uint8_t  flags[8];
} storage_device_info;

typedef struct
{
	uint8_t  pkt[0x20];
	uint32_t pktlen;
	uint32_t blocks;
	uint32_t block_size;
	uint32_t proto;
	uint32_t in_out;
	uint32_t unknown;
} atapi_cmd_block;

int storage_open(uint64_t device_id, uint32_t *handle);
int storage_close(uint32_t handle);

/* Reads `sectors` 2048-byte sectors starting at `lba`. */
int storage_read(uint32_t handle, uint64_t lba, uint32_t sectors, void *buf,
                 uint32_t *sectors_read);

int storage_get_info(uint64_t device_id, storage_device_info *info);

/* Sends a raw ATAPI command. `cmd`/`cmd_len` is the filled packet, `buf` is
 * the transfer buffer.  Returns the LV2 status. */
int storage_atapi(uint32_t handle, const uint8_t *cmd, uint32_t cmd_len,
                  uint8_t *buf, uint32_t buf_len, uint32_t *transferred);

/* Convenience: lead-out LBA of the loaded disc via READ TOC. */
int storage_read_leadout(uint32_t handle, uint32_t *leadout_lba);

#endif /* DVDREGION_STORAGE_H */
