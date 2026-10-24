/* SPDX-License-Identifier: Apache-2.0 */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "router_smb.h"
#include "router_smb_internal.h"

router_smb_client_t *router_smb_clients;
uint8_t *router_smb_buffer;
static struct smb2_server server;
static struct smb2_server_request_handlers handlers;
static struct ifreq ap_interface;
static SemaphoreHandle_t stopped;
static volatile bool active, bound;
static router_storage_config_t account;

static void bind_interface(struct smb2_server *s, fd_set *read, fd_set *write, int *maxfd)
{
    (void)read; (void)write; (void)maxfd;
    if (bound) return;
    if (setsockopt(s->fd, SOL_SOCKET, SO_BINDTODEVICE, &ap_interface, sizeof(ap_interface)) != 0) {
        s->stop_requested = 1;
        return;
    }
    bound = true;
}

static void connected(struct smb2_context *smb, void *data)
{
    (void)data;
    router_smb_client_t *client = NULL;
    for (unsigned i = 0; i < 2; ++i) if (!router_smb_clients[i].used) { client = &router_smb_clients[i]; break; }
    if (!client) { shutdown(smb2_get_fd(smb), SHUT_RDWR); return; }
    memset(client, 0, sizeof(*client));
    client->used = true;
    client->context = smb;
    smb2_set_opaque(smb, client);
    smb2_set_passthrough(smb, 1);
    smb2_set_version(smb, SMB2_VERSION_ANY3);
    smb2_set_security_mode(smb, SMB2_NEGOTIATE_SIGNING_ENABLED | SMB2_NEGOTIATE_SIGNING_REQUIRED);
    smb2_set_sign(smb, 1);
    smb2_set_timeout(smb, 30);
}

static int authorize(struct smb2_server *s, struct smb2_context *smb,
                     const char *user, const char *domain, const char *workstation)
{
    (void)s; (void)domain; (void)workstation;
    if (!smb2_get_opaque(smb) || !user || strcmp(user, account.username)) return -1;
    smb2_set_user(smb, account.username);
    smb2_set_password(smb, account.password);
    return 0;
}

static int destroy(struct smb2_server *s, struct smb2_context *smb)
{
    (void)s;
    router_smb_client_t *client = smb2_get_opaque(smb);
    if (!client) return 0;
    client->context = NULL;
    return router_smb_cleanup(client) == FR_OK ? 0 : -1;
}

static int tree_connect(struct smb2_server *s, struct smb2_context *smb,
                        struct smb2_tree_connect_request *req, struct smb2_tree_connect_reply *rep)
{
    (void)s;
    router_smb_client_t *client = smb2_get_opaque(smb);
    if (!req->path || (req->path_length & 1) || req->path_length > 2048)
        return router_smb_error(smb, SMB2_TREE_CONNECT, SMB2_STATUS_BAD_NETWORK_NAME);
    const char *path = smb2_utf16_to_utf8(req->path, req->path_length / 2);
    const char *share = path ? strrchr(path, '\\') : NULL;
    bool valid = client && share && !strcasecmp(share + 1, account.share);
    free((void *)path);
    if (!valid)
        return router_smb_error(smb, SMB2_TREE_CONNECT, SMB2_STATUS_BAD_NETWORK_NAME);
    client->tree = true;
    rep->share_type = SMB2_SHARE_TYPE_DISK;
    rep->maximal_access = 0x1f01ff;
    return 0;
}

static int tree_disconnect(struct smb2_server *s, struct smb2_context *smb, uint32_t id)
{
    (void)s; (void)id;
    router_smb_client_t *client = smb2_get_opaque(smb);
    if (!client) return -1;
    FRESULT result = router_smb_cleanup(client);
    client->used = true;
    return router_smb_result(smb, SMB2_TREE_DISCONNECT, result);
}

static int logoff(struct smb2_server *s, struct smb2_context *smb) { return tree_disconnect(s, smb, 0); }
static int echo(struct smb2_server *s, struct smb2_context *smb) { (void)s; (void)smb; return 0; }

static void serve(void *arg)
{
    (void)arg;
    if (smb2_serve_port(&server, 2, connected, NULL) < 0) ESP_LOGW("smb3", "SMB listener stopped after an error");
    for (unsigned i = 0; i < 2; ++i) {
        router_smb_clients[i].context = NULL;
        if (router_smb_cleanup(&router_smb_clients[i]) != FR_OK) ESP_LOGW("smb3", "File close failed; safe unmount must retry");
    }
    active = bound = false;
    xSemaphoreGive(stopped);
    vTaskDelete(NULL);
}

esp_err_t router_smb_init(esp_netif_t *ap)
{
    stopped = xSemaphoreCreateBinary();
    if (!stopped) return ESP_ERR_NO_MEM;
    xSemaphoreGive(stopped);
    return esp_netif_get_netif_impl_name(ap, ap_interface.ifr_name);
}

bool router_smb_running(void) { return active && bound && server.listener_ready; }
void router_smb_forget(void) { if (!active && router_smb_clients) memset(router_smb_clients, 0, 2 * sizeof(*router_smb_clients)); }

esp_err_t router_smb_stop(void)
{
    if (active) {
        server.stop_requested = 1;
        if (xSemaphoreTake(stopped, pdMS_TO_TICKS(10000)) != pdTRUE) return ESP_ERR_TIMEOUT;
        xSemaphoreGive(stopped);
    }
    if (router_smb_clients) for (unsigned i = 0; i < 2; ++i)
        if (router_smb_cleanup(&router_smb_clients[i]) != FR_OK) return ESP_FAIL;
    return ESP_OK;
}

esp_err_t router_smb_start(const router_storage_config_t *config)
{
    if (!stopped || active) return ESP_ERR_INVALID_STATE;
    if (!router_smb_clients) router_smb_clients = heap_caps_calloc(2, sizeof(*router_smb_clients), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!router_smb_buffer) router_smb_buffer = heap_caps_malloc(ROUTER_SMB_BUFFER, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!router_smb_clients || !router_smb_buffer) return ESP_ERR_NO_MEM;
    // Taking the idle token also excludes the interval before the old task signals completion.
    if (xSemaphoreTake(stopped, 0) != pdTRUE) return ESP_ERR_INVALID_STATE;
    account = *config;
    handlers = (struct smb2_server_request_handlers){.authorize_user = authorize, .destruction_event = destroy,
        .tree_connect_cmd = tree_connect, .tree_disconnect_cmd = tree_disconnect, .logoff_cmd = logoff,
        .session_established = echo, .echo_cmd = echo};
    router_smb_file_handlers(&handlers);
    router_smb_info_handlers(&handlers);
    router_smb_dir_handlers(&handlers);
    server = (struct smb2_server){.port = 445, .handlers = &handlers, .signing_enabled = 1,
        .max_transact_size = ROUTER_SMB_BUFFER, .max_read_size = ROUTER_SMB_BUFFER,
        .max_write_size = ROUTER_SMB_BUFFER, .extra_fdset = bind_interface};
    strcpy(server.hostname, "ESP32-ROUTER");
    esp_fill_random(server.guid, sizeof(server.guid));
    server.guid[0] |= 1;
    active = true;
    if (xTaskCreate(serve, "smb3", 16384, NULL, 3, NULL) != pdPASS) {
        active = false;
        xSemaphoreGive(stopped);
        return ESP_ERR_NO_MEM;
    }
    for (unsigned i = 0; i < 200 && active && !router_smb_running(); ++i) vTaskDelay(pdMS_TO_TICKS(10));
    if (router_smb_running()) return ESP_OK;
    router_smb_stop();
    return ESP_FAIL;
}
