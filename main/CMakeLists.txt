idf_component_register(SRCS "router_main.c" "network/router_ap.c" "config/router_config.c"
                            "web/router_auth.c" "web/router_http.c" "network/router_status.c" "network/router_ipv6.c" "system/router_log.c"
                            "storage/router_storage_config.c" "storage/router_disk.c" "storage/smb/router_smb.c"
                            "storage/smb/router_smb_files.c" "storage/smb/router_smb_info.c" "storage/smb/router_smb_dir.c" "storage/router_storage.c"
                       PRIV_REQUIRES esp_netif esp_eth esp_event esp_wifi nvs_flash lwip
                                     esp_http_server esp_timer smb3 fatfs esp_psram
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES "web/ui/router_web.html")
idf_component_get_property(fatfs_lib fatfs COMPONENT_LIB)
target_compile_options(${fatfs_lib} PUBLIC "$<$<COMPILE_LANGUAGE:C,CXX>:-include>"
    "$<$<COMPILE_LANGUAGE:C,CXX>:${CMAKE_CURRENT_LIST_DIR}/storage/router_fatfs_config.h>")
