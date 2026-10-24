/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "diskio.h"
#include "../../main/router_disk.h"
// Reuse the production authentication and connection callbacks as well as its file handlers.
#include "../../main/router_smb.c"

static FILE *image;
static bool fail_sync;
static int device_marker;
PARTITION VolToPart[FF_VOLUMES] = {{0, 0}};
void *ff_memalloc(UINT size) { return malloc(size); }
void ff_memfree(void *ptr) { free(ptr); }
DSTATUS disk_initialize(BYTE drive) { (void)drive; return 0; }
DSTATUS disk_status(BYTE drive) { (void)drive; return 0; }
DRESULT disk_read(BYTE drive, BYTE *data, LBA_t sector, UINT count)
{
    (void)drive;
    return fseeko(image, (off_t)sector * 512, SEEK_SET) || fread(data, 512, count, image) != count ? RES_ERROR : RES_OK;
}
DRESULT disk_write(BYTE drive, const BYTE *data, LBA_t sector, UINT count)
{
    (void)drive;
    return fseeko(image, (off_t)sector * 512, SEEK_SET) || fwrite(data, 512, count, image) != count ? RES_ERROR : RES_OK;
}
DRESULT disk_ioctl(BYTE drive, BYTE command, void *data)
{
    (void)drive;
    switch (command) {
    case GET_SECTOR_COUNT: *(LBA_t *)data = 131072; return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)data = 512; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)data = 1; return RES_OK;
    case CTRL_SYNC: return fail_sync || fflush(image) ? RES_ERROR : RES_OK;
    default: return RES_PARERR;
    }
}

int msc_host_install_device(uint8_t address, msc_host_device_handle_t *device)
{
    assert(address == 1);
    *device = &device_marker;
    return ESP_OK;
}
int msc_host_get_device_info(msc_host_device_handle_t device, msc_host_device_info_t *info)
{
    assert(device == &device_marker);
    *info = (msc_host_device_info_t){.sector_count = 131072, .sector_size = 512};
    return ESP_OK;
}
int msc_host_get_blockdev(msc_host_device_handle_t device, esp_blockdev_handle_t *blockdev)
{ assert(device == &device_marker); *blockdev = image; return ESP_OK; }
int msc_host_release_blockdev(esp_blockdev_handle_t handle) { assert(handle == image); return ESP_OK; }
int msc_host_uninstall_device(msc_host_device_handle_t device) { assert(device == &device_marker); return ESP_OK; }
int ff_diskio_register_bdl(unsigned char drive, esp_blockdev_handle_t handle) { assert(drive == 0 && handle == image); return ESP_OK; }
void ff_diskio_unregister(unsigned char drive) { assert(drive == 0); }
void ff_diskio_clear_pdrv_bdl(esp_blockdev_handle_t handle) { assert(handle == image); }

static void stop_server(int number) { (void)number; server.stop_requested = 1; }

int main(void)
{
    image = tmpfile();
    assert(image && ftruncate(fileno(image), 64 * 1024 * 1024) == 0);
    uint8_t work[4096];
    assert(router_disk_open(1) == ESP_OK);
    assert(router_disk_mount() == ESP_ERR_NOT_SUPPORTED);
    assert(disk_read(0, work, 0, 1) == RES_OK);
    for (unsigned i = 0; i < 512; ++i) assert(work[i] == 0); // Failed mounting never formats.
    assert(router_disk_format() == ESP_OK && router_disk_mounted());
    assert(router_disk_format() == ESP_ERR_INVALID_STATE);
    uint64_t total, available;
    assert(router_disk_space(&total, &available) == ESP_OK && total > 0 && available <= total);
    fail_sync = true;
    assert(router_disk_unmount() == ESP_FAIL && router_disk_mounted());
    fail_sync = false;
    assert(router_disk_unmount() == ESP_OK && !router_disk_mounted());
    assert(router_disk_mount() == ESP_OK);
    char path[64];
    assert(router_smb_path("folder\\file.txt", path, sizeof(path)) && !strcmp(path, "0:/folder/file.txt"));
    assert(!router_smb_path("../secret", path, sizeof(path)));
    assert(!router_smb_path("safe\\..\\secret", path, sizeof(path)));
    assert(!router_smb_path("C:\\secret", path, sizeof(path)));
    assert(!router_smb_path("bad. ", path, sizeof(path)));
    router_smb_clients = calloc(2, sizeof(*router_smb_clients));
    router_smb_buffer = malloc(ROUTER_SMB_BUFFER);
    assert(router_smb_clients && router_smb_buffer);
    account = (router_storage_config_t){.enabled = true, .share = "usb", .username = "testuser", .password = "test-password"};
    handlers = (struct smb2_server_request_handlers){.authorize_user = authorize, .destruction_event = destroy,
        .tree_connect_cmd = tree_connect, .tree_disconnect_cmd = tree_disconnect, .logoff_cmd = logoff,
        .session_established = echo, .echo_cmd = echo};
    router_smb_file_handlers(&handlers);
    router_smb_info_handlers(&handlers);
    router_smb_dir_handlers(&handlers);
    server = (struct smb2_server){.port = 1445, .handlers = &handlers, .signing_enabled = 1,
        .max_transact_size = ROUTER_SMB_BUFFER, .max_read_size = ROUTER_SMB_BUFFER, .max_write_size = ROUTER_SMB_BUFFER};
    strcpy(server.hostname, "ESP32-ROUTER");
    memcpy(server.guid, "router-host-test", 16);
    signal(SIGTERM, stop_server);
    signal(SIGINT, stop_server);
    int result = smb2_serve_port(&server, 2, connected, NULL);
    for (unsigned i = 0; i < 2; ++i) assert(router_smb_cleanup(&router_smb_clients[i]) == FR_OK);
    assert(router_disk_unmount() == ESP_OK);
    router_disk_close();
    fclose(image);
    free(router_smb_clients);
    free(router_smb_buffer);
    return result < 0 ? 1 : 0;
}
