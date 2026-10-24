/* SPDX-License-Identifier: Apache-2.0 */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "router_disk.h"
#include "router_smb_internal.h"

bool router_smb_path(const char *name, char *out, size_t size)
{
    if (!name || size < 4) return false;
    strcpy(out, ROUTER_DISK "/");
    size_t used = 3;
    while (*name) {
        while (*name == '/' || *name == '\\') ++name;
        const char *start = name;
        while (*name && *name != '/' && *name != '\\') {
            if ((unsigned char)*name < 32 || strchr(":*?<>|\"", *name)) return false;
            ++name;
        }
        size_t n = name - start;
        if (!n || (n == 1 && *start == '.')) continue;
        if ((n == 2 && !memcmp(start, "..", 2)) || start[n - 1] == '.' ||
            start[n - 1] == ' ' || used + n + 1 >= size) return false;
        if (used > 3) out[used++] = '/';
        memcpy(out + used, start, n);
        used += n;
        out[used] = 0;
    }
    return true;
}

int router_smb_error(struct smb2_context *smb, uint16_t command, uint32_t status)
{
    struct smb2_error_reply error = {0};
    struct smb2_pdu *pdu = smb2_cmd_error_reply_async(smb, &error, command, status, NULL, NULL);
    if (!pdu) return -1;
    smb2_set_pdu_message_id(smb, pdu, smb2_get_last_request_message_id(smb));
    smb2_queue_pdu(smb, pdu);
    return 1;
}

int router_smb_result(struct smb2_context *smb, uint16_t command, FRESULT result)
{
    if (result == FR_OK) return 0;
    uint32_t status = SMB2_STATUS_IO_DEVICE_ERROR;
    switch (result) {
    case FR_NO_FILE: status = SMB2_STATUS_OBJECT_NAME_NOT_FOUND; break;
    case FR_NO_PATH: status = SMB2_STATUS_OBJECT_PATH_NOT_FOUND; break;
    case FR_EXIST: status = SMB2_STATUS_OBJECT_NAME_COLLISION; break;
    case FR_INVALID_NAME: status = SMB2_STATUS_OBJECT_NAME_INVALID; break;
    case FR_DENIED: case FR_WRITE_PROTECTED: status = SMB2_STATUS_ACCESS_DENIED; break;
    case FR_LOCKED: status = SMB2_STATUS_SHARING_VIOLATION; break;
    case FR_TOO_MANY_OPEN_FILES: status = SMB2_STATUS_TOO_MANY_OPENED_FILES; break;
    case FR_NOT_READY: status = SMB2_STATUS_DEVICE_NOT_READY; break;
    case FR_NOT_ENOUGH_CORE: status = SMB2_STATUS_INSUFFICIENT_RESOURCES; break;
    default: break;
    }
    return router_smb_error(smb, command, status);
}

router_smb_file_t *router_smb_file(struct smb2_context *smb, const smb2_file_id id)
{
    router_smb_client_t *client = smb2_get_opaque(smb);
    if (!client || !client->tree) return NULL;
    static const uint8_t related[16] = {[0 ... 15] = 255};
    if (!memcmp(id, related, 16)) return client->last && client->last->used ? client->last : NULL;
    for (unsigned i = 0; i < ROUTER_SMB_HANDLES; ++i) {
        if (client->files[i].used && !memcmp(client->files[i].id, id, 16)) return &client->files[i];
    }
    return NULL;
}

FRESULT router_smb_stat(router_smb_file_t *file, FILINFO *info)
{
    memset(info, 0, sizeof(*info));
    if (!strcmp(file->path, ROUTER_DISK "/")) {
        info->fattrib = AM_DIR;
        return FR_OK;
    }
    FRESULT result = f_stat(file->path, info);
    if (result == FR_OK && !file->directory) info->fsize = f_size(&file->file);
    return result;
}

static bool share_conflict(const char *path, uint32_t access, uint32_t sharing)
{
    for (unsigned c = 0; c < 2; ++c) for (unsigned i = 0; i < ROUTER_SMB_HANDLES; ++i) {
        router_smb_file_t *f = &router_smb_clients[c].files[i];
        if (!f->used || strcasecmp(f->path, path)) continue;
        if (f->delete_pending || (access & ~f->sharing) || (f->access & ~sharing)) return true;
    }
    return false;
}

static int create_file(struct smb2_server *server, struct smb2_context *smb,
                       struct smb2_create_request *req, struct smb2_create_reply *rep)
{
    (void)server;
    router_smb_client_t *client = smb2_get_opaque(smb);
    router_smb_file_t *file = NULL;
    if (!client || !client->tree) return router_smb_error(smb, SMB2_CREATE, SMB2_STATUS_ACCESS_DENIED);
    for (unsigned i = 0; i < ROUTER_SMB_HANDLES; ++i) if (!client->files[i].used) { file = &client->files[i]; break; }
    if (!file) return router_smb_error(smb, SMB2_CREATE, SMB2_STATUS_TOO_MANY_OPENED_FILES);
    memset(file, 0, sizeof(*file));
    if (!router_smb_path(req->name ? req->name : "", file->path, sizeof(file->path)))
        return router_smb_error(smb, SMB2_CREATE, SMB2_STATUS_OBJECT_NAME_INVALID);
    file->readable = (req->desired_access & 0x90000001U) != 0;
    file->writable = (req->desired_access & 0x50000006U) != 0;
    file->access = (file->readable ? 1 : 0) | (file->writable ? 2 : 0) |
                   ((req->desired_access & 0x10010000U) ? 4 : 0);
    file->sharing = req->share_access & 7;
    if (share_conflict(file->path, file->access, file->sharing))
        return router_smb_error(smb, SMB2_CREATE, SMB2_STATUS_SHARING_VIOLATION);
    FILINFO info = {.fattrib = AM_DIR};
    FRESULT result = !strcmp(file->path, ROUTER_DISK "/") ? FR_OK : f_stat(file->path, &info);
    bool exists = result == FR_OK;
    if (!exists && result != FR_NO_FILE && result != FR_NO_PATH) return router_smb_result(smb, SMB2_CREATE, result);
    bool directory = exists ? (info.fattrib & AM_DIR) != 0 : (req->create_options & SMB2_FILE_DIRECTORY_FILE) != 0;
    if ((directory && (req->create_options & SMB2_FILE_NON_DIRECTORY_FILE)) ||
        (!directory && (req->create_options & SMB2_FILE_DIRECTORY_FILE)))
        return router_smb_error(smb, SMB2_CREATE, directory ? SMB2_STATUS_FILE_IS_A_DIRECTORY : SMB2_STATUS_NOT_A_DIRECTORY);
    if (req->create_disposition > 5 || (req->create_disposition == 2 && exists))
        return router_smb_error(smb, SMB2_CREATE, SMB2_STATUS_OBJECT_NAME_COLLISION);
    if (!exists && (req->create_disposition == 1 || req->create_disposition == 4))
        return router_smb_error(smb, SMB2_CREATE, SMB2_STATUS_OBJECT_NAME_NOT_FOUND);
    bool truncate = req->create_disposition == 0 || req->create_disposition >= 4;
    if ((!exists || truncate) && !file->writable && !directory)
        return router_smb_error(smb, SMB2_CREATE, SMB2_STATUS_ACCESS_DENIED);
    file->delete_pending = (req->create_options & SMB2_FILE_DELETE_ON_CLOSE) != 0;
    if (file->delete_pending && (!(file->access & 4) || !strcmp(file->path, ROUTER_DISK "/")))
        return router_smb_error(smb, SMB2_CREATE, SMB2_STATUS_ACCESS_DENIED);
    if (directory) {
        result = exists ? FR_OK : f_mkdir(file->path);
        if (result == FR_OK) result = f_opendir(&file->dir, file->path);
    } else {
        BYTE mode = FA_READ | (file->writable ? FA_WRITE : 0) |
                    (truncate ? FA_CREATE_ALWAYS : exists ? FA_OPEN_EXISTING : FA_CREATE_NEW);
        result = f_open(&file->file, file->path, mode);
    }
    if (result != FR_OK) return router_smb_result(smb, SMB2_CREATE, result);
    file->used = true;
    file->directory = directory;
    static uint64_t counter;
    uint64_t id = ++counter;
    memcpy(file->id, &id, sizeof(id));
    client->last = file;
    memcpy(rep->file_id, file->id, sizeof(rep->file_id));
    rep->create_action = !exists ? 2 : truncate && !directory ? 3 : 1;
    rep->file_attributes = directory ? SMB2_FILE_ATTRIBUTE_DIRECTORY : SMB2_FILE_ATTRIBUTE_ARCHIVE;
    rep->end_of_file = directory ? 0 : f_size(&file->file);
    rep->allocation_size = (rep->end_of_file + 4095) & ~4095ULL;
    return 0;
}
