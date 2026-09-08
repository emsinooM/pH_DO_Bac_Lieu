set(COMPONENT_INCLUDE_DIRS
    inc
    ../config
)

idf_component_register(SRCS "src/cs1237.c"
                            "src/do_sensor.c"
                            "src/ds3231.c"
                            "src/filter.c"
                            "src/large_font.c"
                            "src/main.c"
                            "src/ph_temp.c"
                            "src/screen_disp.c"
                            "src/screen_menu.c"
                            "src/user_azure.c"
                            "src/user_crc32.cpp"
                            "src/user_fram.c"
                            "src/user_http_server.c"
                            "src/user_ota.c"
                            "src/user_output.c"
                            "src/user_storage.c"
                            "src/user_system.c"
                            "src/user_time.c"
                            "src/web_portal.c"
                            "src/wifi_config_manager.c"
                    INCLUDE_DIRS "." ${COMPONENT_INCLUDE_DIRS}
                    REQUIRES    coreMQTT
                                azure-sdk-for-c 
                                azure-iot-middleware-freertos 
                                sample-azure-iot 
                                driver 
                                nvs_flash
                                esp_netif
                                esp_wifi
                                esp_https_ota
                                app_update
                                esp_http_server
                                mdns
                    )
