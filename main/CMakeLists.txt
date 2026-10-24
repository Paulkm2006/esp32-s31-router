idf_component_register(SRCS "router_main.c" "router_ap.c" "router_config.c"
                            "router_auth.c" "router_http.c" "router_status.c" "router_ipv6.c"
                       PRIV_REQUIRES esp_netif esp_eth esp_event esp_wifi nvs_flash lwip
                                     esp_http_server esp_timer
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES "router_web.html")
