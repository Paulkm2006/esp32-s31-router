/* SPDX-License-Identifier: Apache-2.0 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "router_smb_internal.h"

static bool matches(const char *pattern, const char *name)
{
    if (!strcmp(pattern, "*.*")) pattern = "*";
    const char *star = NULL, *retry = NULL;
    while (*name) {
        if (*pattern == '?' || (*pattern && tolower((unsigned char)*pattern) == tolower((unsigned char)*name))) {
            ++pattern; ++name;
        } else if (*pattern == '*') { star = pattern++; retry = name; }
        else if (star) { pattern = star + 1; name = ++retry; }
        else return false;
    }
    while (*pattern == '*') ++pattern;
    return !*pattern;
}

static int query_directory(struct smb2_server *s, struct smb2_context *smb,
                           struct smb2_query_directory_request *req, struct smb2_query_directory_reply *rep)
{
    (void)s;
    router_smb_file_t *f = router_smb_file(smb, req->file_id);
    if (!f || !f->directory) return router_smb_error(smb, SMB2_QUERY_DIRECTORY, SMB2_STATUS_NOT_A_DIRECTORY);
    unsigned base;
    switch (req->file_information_class) {
    case SMB2_FILE_DIRECTORY_INFORMATION: base = 64; break;
    case SMB2_FILE_FULL_DIRECTORY_INFORMATION: base = 68; break;
    case SMB2_FILE_BOTH_DIRECTORY_INFORMATION: base = 94; break;
    case SMB2_FILE_ID_BOTH_DIRECTORY_INFORMATION: base = 104; break;
    case SMB2_FILE_ID_FULL_DIRECTORY_INFORMATION: base = 80; break;
    case SMB2_FILE_NAMES_INFORMATION: base = 12; break;
    default: return router_smb_error(smb, SMB2_QUERY_DIRECTORY, SMB2_STATUS_INVALID_INFO_CLASS);
    }
    if (req->flags & (SMB2_RESTART_SCANS | SMB2_REOPEN)) {
        FRESULT result = f_readdir(&f->dir, NULL);
        if (result != FR_OK) return router_smb_result(smb, SMB2_QUERY_DIRECTORY, result);
        f->pending_entry = false;
    }
    if (req->name && req->name[0]) {
        if (strlen(req->name) >= sizeof(f->pattern))
            return router_smb_error(smb, SMB2_QUERY_DIRECTORY, SMB2_STATUS_OBJECT_NAME_INVALID);
        strcpy(f->pattern, req->name);
    }
    if (!f->pattern[0]) strcpy(f->pattern, "*");
    size_t room = req->output_buffer_length < ROUTER_SMB_BUFFER ? req->output_buffer_length : ROUTER_SMB_BUFFER;
    size_t used = 0, last = 0;
    memset(router_smb_buffer, 0, room);
    while (true) {
        if (!f->pending_entry) {
            FRESULT result = f_readdir(&f->dir, &f->entry);
            if (result != FR_OK) return router_smb_result(smb, SMB2_QUERY_DIRECTORY, result);
            if (!f->entry.fname[0]) break;
            if (!matches(f->pattern, f->entry.fname)) continue;
            f->pending_entry = true;
        }
        struct smb2_utf16 *name = smb2_utf8_to_utf16(f->entry.fname);
        if (!name) return router_smb_error(smb, SMB2_QUERY_DIRECTORY, SMB2_STATUS_INSUFFICIENT_RESOURCES);
        size_t bytes = name->len * 2, len = (base + bytes + 7) & ~7U;
        if (len > room - used) { free(name); break; }
        uint8_t *p = router_smb_buffer + used;
        if (used) router_smb_put(router_smb_buffer + last, used - last, 4);
        last = used;
        if (base > 12) {
            for (unsigned i = 0; i < 4; ++i) router_smb_put(p + 8 + i * 8, router_smb_timestamp(&f->entry), 8);
            router_smb_put(p + 40, f->entry.fsize, 8);
            router_smb_put(p + 48, ((uint64_t)f->entry.fsize + 32767) & ~32767ULL, 8);
            router_smb_put(p + 56, f->entry.fattrib & AM_DIR ? SMB2_FILE_ATTRIBUTE_DIRECTORY : SMB2_FILE_ATTRIBUTE_ARCHIVE, 4);
        }
        router_smb_put(p + (base == 12 ? 8 : 60), bytes, 4);
        if (base == 104 || base == 80) router_smb_put(p + base - 8, router_smb_file_number(f->path, f->entry.fname), 8);
        memcpy(p + base, name->val, bytes);
        free(name);
        used += len;
        f->pending_entry = false;
        if (req->flags & SMB2_RETURN_SINGLE_ENTRY) break;
    }
    if (!used) return router_smb_error(smb, SMB2_QUERY_DIRECTORY,
                                     f->pending_entry ? SMB2_STATUS_BUFFER_TOO_SMALL : SMB2_STATUS_NO_MORE_FILES);
    rep->output_buffer = router_smb_buffer;
    rep->output_buffer_length = used;
    return 0;
}

void router_smb_dir_handlers(struct smb2_server_request_handlers *handlers) { handlers->query_directory_cmd = query_directory; }
