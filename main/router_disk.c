/* SPDX-License-Identifier: Apache-2.0 */
#include <stdlib.h>
#include "diskio_impl.h"
#include "diskio_bdl.h"
#include "router_disk.h"

_Static_assert(FF_VOLUMES == 1, "USB storage owns FatFs volume 0");
static msc_host_device_handle_t device;
static esp_blockdev_handle_t blockdev;
static FATFS filesystem;
static bool mounted;
static uint64_t capacity;

bool router_disk_mounted(void) { return mounted; }
bool router_disk_is(msc_host_device_handle_t handle) { return handle == device; }
uint64_t router_disk_capacity(void) { return capacity; }

esp_err_t router_disk_unmount(void)
{
    if (!mounted) {
        return ESP_OK;
    }
    // All SMB handles must be closed before changing the FatFs volume.
    if (disk_ioctl(0, CTRL_SYNC, NULL) != RES_OK || f_mount(NULL, ROUTER_DISK, 0) != FR_OK) {
        return ESP_FAIL;
    }
    mounted = false;
    return ESP_OK;
}

void router_disk_close(void)
{
    f_mount(NULL, ROUTER_DISK, 0);
    mounted = false;
    if (blockdev) {
        ff_diskio_unregister(0);
        ff_diskio_clear_pdrv_bdl(blockdev);
        msc_host_release_blockdev(blockdev);
        blockdev = NULL;
    }
    if (device) {
        msc_host_uninstall_device(device);
        device = NULL;
    }
    capacity = 0;
}

esp_err_t router_disk_open(uint8_t address)
{
    if (device) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = msc_host_install_device(address, &device);
    msc_host_device_info_t info;
    if (err == ESP_OK) {
        err = msc_host_get_device_info(device, &info);
    }
    if (err == ESP_OK) {
        capacity = (uint64_t)info.sector_count * info.sector_size;
        err = msc_host_get_blockdev(device, &blockdev);
    }
    if (err == ESP_OK) {
        err = ff_diskio_register_bdl(0, blockdev);
    }
    if (err != ESP_OK) {
        router_disk_close();
    }
    return err;
}

esp_err_t router_disk_mount(void)
{
    if (!blockdev || mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    FRESULT result = f_mount(&filesystem, ROUTER_DISK, 1);
    if (result == FR_OK && filesystem.fs_type == FS_FAT32) {
        mounted = true;
        return ESP_OK;
    }
    f_mount(NULL, ROUTER_DISK, 0);
    return result == FR_OK || result == FR_NO_FILESYSTEM ? ESP_ERR_NOT_SUPPORTED : ESP_FAIL;
}

esp_err_t router_disk_format(void)
{
    if (!blockdev || mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    void *work = malloc(4096);
    if (!work) {
        return ESP_ERR_NO_MEM;
    }
    // Formatting explicitly replaces the partition table with one FAT32 partition.
    const LBA_t partitions[] = {100, 0, 0, 0};
    FRESULT result = f_fdisk(0, partitions, work);
    const MKFS_PARM options = {.fmt = FM_FAT32, .n_fat = 2};
    if (result == FR_OK) {
        result = f_mkfs(ROUTER_DISK, &options, work, 4096);
    }
    free(work);
    return result == FR_OK ? router_disk_mount() : ESP_FAIL;
}

esp_err_t router_disk_space(uint64_t *total, uint64_t *free_bytes)
{
    FATFS *fs;
    DWORD clusters;
    if (!mounted || f_getfree(ROUTER_DISK, &clusters, &fs) != FR_OK) {
        return ESP_FAIL;
    }
    uint64_t cluster_bytes = (uint64_t)fs->csize * fs->ssize;
    *total = (fs->n_fatent - 2) * cluster_bytes;
    *free_bytes = clusters * cluster_bytes;
    return ESP_OK;
}
