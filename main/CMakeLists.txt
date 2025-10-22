idf_component_register(
    SRCS 
        "main.c"
        "hid_device.c"
        "ble_hid.c"
        "mouse_report_builder.c"
        "transport_uart.c"
        "transport_ws.c"
        "ws_ascii.c"
        "wifi_credentials.c"
        "wifi_manager.c"
        "nvs_keystore.c"
        "http_server.c"
        "dns_server.c"
        "hid_keymap.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_wifi
        esp_netif
        esp_http_server
        bt
        driver
        json
    EMBED_FILES
        "web/index.html"
)

idf_build_get_component(mdns_component mdns)
if(mdns_component)
    target_link_libraries(${COMPONENT_LIB} PUBLIC ${mdns_component})
endif()
