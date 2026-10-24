/* SPDX-License-Identifier: Apache-2.0 */
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "router_disk.h"
#include "router_smb_internal.h"

uint64_t router_smb_get(const void *data, unsigned bytes)
{
    const uint8_t *p = data;
    uint64_t n = 0;
    for (unsigned i = 0; i < bytes; ++i) n |= (uint64_t)p[i] << (8 * i);
    return n;
}

void router_smb_put(void *data, uint64_t value, unsigned bytes)
{
    uint8_t *p = data;
    for (unsigned i = 0; i < bytes; ++i) p[i] = value >> (8 * i);
}

uint64_t router_smb_timestamp(const FILINFO *info)
{
    struct tm tm = {.tm_year = info->fdate ? (info->fdate >> 9) + 80 : 80,
                    .tm_mon = info->fdate ? ((info->fdate >> 5) & 15) - 1 : 0,
                    .tm_mday = info->fdate ? info->fdate & 31 : 1,
                    .tm_hour = info->ftime >> 11, .tm_min = (info->ftime >> 5) & 63,
                    .tm_sec = (info->ftime & 31) * 2};
    return ((uint64_t)mktime(&tm) + 11644473600ULL) * 10000000ULL;
}

static size_t utf16(uint8_t *out, const char *name)
{
    struct smb2_utf16 *u = smb2_utf8_to_utf16(name);
    if (!u) return 0;
    size_t bytes = u->len * 2;
    memcpy(out, u->val, bytes);
    free(u);
    return bytes;
}

static void basic_info(uint8_t *out, const FILINFO *info)
{
    for (unsigned i = 0; i < 4; ++i) router_smb_put(out + 8 * i, router_smb_timestamp(info), 8);
    router_smb_put(out + 32, info->fattrib & AM_DIR ? SMB2_FILE_ATTRIBUTE_DIRECTORY :
                   (info->fattrib & AM_RDO ? SMB2_FILE_ATTRIBUTE_READONLY : SMB2_FILE_ATTRIBUTE_ARCHIVE), 4);
}

static void standard_info(uint8_t *out, const FILINFO *info, bool pending)
{
    router_smb_put(out, ((uint64_t)info->fsize + 32767) & ~32767ULL, 8);
    router_smb_put(out + 8, info->fsize, 8);
    router_smb_put(out + 16, 1, 4);
    out[20] = pending;
    out[21] = (info->fattrib & AM_DIR) != 0;
}

static int query_info(struct smb2_server *server, struct smb2_context *smb,
                      struct smb2_query_info_request *req, struct smb2_query_info_reply *unused)
{
    (void)server; (void)unused;
    router_smb_file_t *file = router_smb_file(smb, req->file_id);
    if (!file) return router_smb_error(smb, SMB2_QUERY_INFO, SMB2_STATUS_FILE_CLOSED);
    uint8_t *p = router_smb_buffer;
    memset(p, 0, ROUTER_SMB_BUFFER);
    size_t len = 0;
    if (req->info_type == SMB2_0_INFO_FILE) {
        FILINFO info;
        FRESULT result = router_smb_stat(file, &info);
        if (result != FR_OK) return router_smb_result(smb, SMB2_QUERY_INFO, result);
        switch (req->file_info_class) {
        case SMB2_FILE_BASIC_INFORMATION: basic_info(p, &info); len = 40; break;
        case SMB2_FILE_STANDARD_INFORMATION: standard_info(p, &info, file->delete_pending); len = 24; break;
        case SMB2_FILE_INTERNAL_INFORMATION: memcpy(p, file->id, 8); len = 8; break;
        case SMB2_FILE_EA_INFORMATION: len = 4; break;
        case SMB2_FILE_ACCESS_INFORMATION: router_smb_put(p, 0x1f01ff, 4); len = 4; break;
        case SMB2_FILE_POSITION_INFORMATION: router_smb_put(p, file->directory ? 0 : f_tell(&file->file), 8); len = 8; break;
        case SMB2_FILE_MODE_INFORMATION: case SMB2_FILE_ALIGNMENT_INFORMATION: len = 4; break;
        case SMB2_FILE_ATTRIBUTE_TAG_INFORMATION: basic_info(p, &info); memmove(p, p + 32, 4); memset(p + 4, 0, 4); len = 8; break;
        case SMB2_FILE_ALL_INFORMATION:
            basic_info(p, &info); standard_info(p + 40, &info, file->delete_pending);
            memcpy(p + 64, file->id, 8); router_smb_put(p + 76, 0x1f01ff, 4);
            router_smb_put(p + 80, file->directory ? 0 : f_tell(&file->file), 8);
            len = utf16(p + 100, file->path + 2); router_smb_put(p + 96, len, 4); len += 100; break;
        case SMB2_FILE_NAME_INFORMATION: case SMB2_FILE_NORMALIZED_NAME_INFORMATION:
            len = utf16(p + 4, file->path + 2); router_smb_put(p, len, 4); len += 4; break;
        case SMB2_FILE_NETWORK_OPEN_INFORMATION:
            basic_info(p, &info); router_smb_put(p + 32, ((uint64_t)info.fsize + 32767) & ~32767ULL, 8);
            router_smb_put(p + 40, info.fsize, 8);
            router_smb_put(p + 48, info.fattrib & AM_DIR ? SMB2_FILE_ATTRIBUTE_DIRECTORY : SMB2_FILE_ATTRIBUTE_ARCHIVE, 4); len = 56; break;
        case SMB2_FILE_STREAM_INFORMATION:
            router_smb_put(p + 8, info.fsize, 8); router_smb_put(p + 16, info.fsize, 8);
            len = utf16(p + 24, "::$DATA"); router_smb_put(p + 4, len, 4); len += 24; break;
        default: break;
        }
    } else if (req->info_type == SMB2_0_INFO_FILESYSTEM) {
        uint64_t total, free_bytes;
        if (router_disk_space(&total, &free_bytes) != ESP_OK)
            return router_smb_error(smb, SMB2_QUERY_INFO, SMB2_STATUS_IO_DEVICE_ERROR);
        switch (req->file_info_class) {
        case SMB2_FILE_FS_VOLUME_INFORMATION:
            router_smb_put(p, 119600064000000000ULL, 8);
            router_smb_put(p + 8, 0x533331, 4);
            len = utf16(p + 18, "USB"); router_smb_put(p + 12, len, 4); len += 18; break;
        case SMB2_FILE_FS_SIZE_INFORMATION: case SMB2_FILE_FS_FULL_SIZE_INFORMATION:
            router_smb_put(p, total / 512, 8); router_smb_put(p + 8, free_bytes / 512, 8);
            len = 16;
            if (req->file_info_class == SMB2_FILE_FS_FULL_SIZE_INFORMATION) { router_smb_put(p + len, free_bytes / 512, 8); len += 8; }
            router_smb_put(p + len, 1, 4); router_smb_put(p + len + 4, 512, 4); len += 8; break;
        case SMB2_FILE_FS_DEVICE_INFORMATION: router_smb_put(p, FILE_DEVICE_DISK, 4); router_smb_put(p + 4, FILE_REMOVABLE_MEDIA, 4); len = 8; break;
        case SMB2_FILE_FS_ATTRIBUTE_INFORMATION:
            router_smb_put(p, 6, 4); router_smb_put(p + 4, FF_MAX_LFN, 4);
            len = utf16(p + 12, "FAT32"); router_smb_put(p + 8, len, 4); len += 12; break;
        case SMB2_FILE_FS_SECTOR_SIZE_INFORMATION:
            for (unsigned i = 0; i < 4; ++i) router_smb_put(p + 4 * i, 512, 4);
            len = 28; break;
        case SMB2_FILE_FS_CONTROL_INFORMATION: len = 48; break;
        default: break;
        }
    }
    if (!len) return router_smb_error(smb, SMB2_QUERY_INFO, SMB2_STATUS_NOT_SUPPORTED);
    if (len > req->output_buffer_length) return router_smb_error(smb, SMB2_QUERY_INFO, SMB2_STATUS_BUFFER_TOO_SMALL);
    // An unhandled class uses libsmb2's passthrough encoder for the wire-format blob.
    struct smb2_query_info_request raw = *req;
    raw.info_type = SMB2_0_INFO_FILE;
    raw.file_info_class = 255;
    struct smb2_query_info_reply rep = {.output_buffer = p, .output_buffer_length = len};
    struct smb2_pdu *pdu = smb2_cmd_query_info_reply_async(smb, &raw, &rep, NULL, NULL);
    if (!pdu) return -1;
    smb2_set_pdu_message_id(smb, pdu, smb2_get_last_request_message_id(smb));
    smb2_queue_pdu(smb, pdu);
    return 1;
}

void router_smb_info_handlers(struct smb2_server_request_handlers *handlers) { handlers->query_info_cmd = query_info; }
