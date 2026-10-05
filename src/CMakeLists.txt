idf_component_register(
    SRCS
        "app_state.cpp"
        "board_screen.cpp"
        "canvas.cpp"
        "debug_console.cpp"
        "droras_client.cpp"
        "heat_sources.cpp"
        "http_client.cpp"
        "main.cpp"
        "network_task.cpp"
        "settings_screen.cpp"
        "settings_store.cpp"
        "text_renderer.cpp"
        "ui_common.cpp"
        "wifi_station.cpp"
    INCLUDE_DIRS "."
    # Display font, read in place from flash by text_renderer.cpp. Must also be listed in
    # board_build.embed_files in platformio.ini, which is what PlatformIO acts on.
    EMBED_FILES "../assets/fonts/ipaexg.ttf"
)
