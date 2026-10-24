idf_component_register(SRCS "router_main.c" "router_ap.c" "router_config.c"
                            "router_auth.c" "router_http.c" "router_status.c" "router_ipv6.c"
                            "router_storage_config.c" "router_disk.c" "router_smb.c"
                            "router_smb_files.c" "router_smb_info.c" "router_smb_dir.c" "router_storage.c"
                       PRIV_REQUIRES esp_netif esp_eth esp_event esp_wifi nvs_flash lwip
                                     esp_http_server esp_timer smb3 fatfs esp_psram
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES "router_web.html")
idf_component_get_property(fatfs_lib fatfs COMPONENT_LIB)
target_compile_options(${fatfs_lib} PUBLIC "$<$<COMPILE_LANGUAGE:C,CXX>:-include>"
    "$<$<COMPILE_LANGUAGE:C,CXX>:${CMAKE_CURRENT_LIST_DIR}/router_fatfs_config.h>")
