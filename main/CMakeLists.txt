idf_component_register(SRCS "router_main.c" "router_ap.c"
                       PRIV_REQUIRES esp_netif esp_eth esp_event esp_wifi nvs_flash
                       INCLUDE_DIRS ".")
