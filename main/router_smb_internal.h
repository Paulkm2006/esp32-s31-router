/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "ff.h"
#include "smb2/smb2.h"
#include "smb2/libsmb2.h"
#include "smb2/libsmb2-raw.h"

#define ROUTER_SMB_HANDLES 8
#define ROUTER_SMB_BUFFER 32768
#define ROUTER_SMB_PATH 1024
#define ROUTER_SMB_FILE_TOO_LARGE 0xc0000904U
typedef struct {
    bool used, directory, writable, readable, delete_pending;
    uint32_t access, sharing;
    smb2_file_id id;
    char path[ROUTER_SMB_PATH];
    char pattern[ROUTER_SMB_PATH];
    FIL file;
    FF_DIR dir;
    FILINFO entry;
    bool pending_entry;
} router_smb_file_t;
typedef struct {
    bool used, tree;
    struct smb2_context *context;
    router_smb_file_t files[ROUTER_SMB_HANDLES];
    router_smb_file_t *last;
} router_smb_client_t;
extern router_smb_client_t *router_smb_clients;
extern uint8_t *router_smb_buffer;
bool router_smb_path(const char *name, char *out, size_t size);
router_smb_file_t *router_smb_file(struct smb2_context *smb, const smb2_file_id id);
int router_smb_error(struct smb2_context *smb, uint16_t command, uint32_t status);
int router_smb_result(struct smb2_context *smb, uint16_t command, FRESULT result);
FRESULT router_smb_stat(router_smb_file_t *file, FILINFO *info);
FRESULT router_smb_cleanup(router_smb_client_t *client);
void router_smb_file_handlers(struct smb2_server_request_handlers *handlers);
void router_smb_info_handlers(struct smb2_server_request_handlers *handlers);
void router_smb_dir_handlers(struct smb2_server_request_handlers *handlers);
uint64_t router_smb_get(const void *data, unsigned bytes);
void router_smb_put(void *data, uint64_t value, unsigned bytes);
uint64_t router_smb_timestamp(const FILINFO *info);
