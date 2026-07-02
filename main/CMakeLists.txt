idf_component_register(SRCS "main.cpp" 
                            "motor_control.cpp"
                            "web_server.cpp"
                            "wifi_manager.cpp"
                            "system_utils.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash esp_wifi esp_http_server json driver esp_netif mdns)