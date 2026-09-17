idf_component_register(
    SRCS "main.c"
         "demo_navigation.c"
         "ui_pixel.c"
         "ui_pixel_math.c"
         "demo_display.c"
         "demo_button.c"
         "demo_audio.c"
         "demo_battery.c"
         "demo_radio.c"
         "demo_wifi.c"
         "demo_ble.c"
         "demo_low_power.c"
         "habit_date.c"
         "habit_model.c"
         "habit_clock.c"
         "habit_store.c"
         "habit_device.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_event esp_hw_support esp_netif esp_timer esp_wifi nvs_flash
)

# 点阵中文子集放在仓库根 assets/fonts/(可复用资源位置),不复制进 main/。
# 用 target_sources 单独挂载,而不是塞进上面的 SRCS,以免和 INCLUDE_DIRS 的
# 语义混淆。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/app_font_12.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/app_font_24.c"
)
