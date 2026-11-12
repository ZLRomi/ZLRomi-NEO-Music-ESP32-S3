idf_component_register(
    SRCS         "src/main.c"
                 "src/gfx.c"
                 "src/input.c"
                 "src/theme.c"
                 "src/settings.c"
                 "src/wifi_mgr.c"
                 "src/netease.c"
                 "src/player.c"
                 "src/jpeg.c"
                 "src/icon.c"
                 "src/cover.c"
                 "src/pinyin.c"
                 "src/music_state.c"
                 "src/ui_core.c"
                 "src/screen_home.c"
                 "src/screen_songs.c"
                 "src/screen_player.c"
                 "src/screen_search.c"
                 "src/screen_settings.c"
                 "src/screen_restart.c"
                 "src/font_data.c"
                 "src/font_index.c"
    INCLUDE_DIRS "include"

    # 中文字库直接嵌进固件：GB2312 全字库，运行时按需取字形，不做动态加载
    # 大字 20x24 给标题，小字 16x16 给列表与副信息（scripts/gen_font16.py 生成）
    EMBED_FILES  "font_cn.bin"
                 "font_ascii.bin"
                 "font_cn16.bin"
                 "font_ascii16.bin"

    REQUIRES     esp_wifi
                 esp_netif
                 esp_event
                 nvs_flash
                 esp_http_client
                 esp_http_server
                 mbedtls
                 cjson
                 lwip
                 esp_timer
                 esp_lcd
                 driver
                 esp_driver_ledc
                 esp_driver_i2s
                 esp_app_format
                 esp_system
)
