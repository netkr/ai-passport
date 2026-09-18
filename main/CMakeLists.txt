idf_component_register(
    SRCS "main.c"
         "habit_app.c"
         "habit_ui.c"
         "habit_date.c"
         "habit_model.c"
         "habit_clock.c"
         "habit_store.c"
         "habit_device.c"
         "habit_audio.c"
    INCLUDE_DIRS "."
    # 本应用不再使用 Wi-Fi/BLE/网络栈:基线的 demo 页面已不参与构建,
    # 相应的组件依赖也一并去掉,避免把不用的协议栈编进固件。
    REQUIRES bsp esp_hw_support esp_timer nvs_flash
)

# 点阵中文子集放在仓库根 assets/fonts/(可复用资源位置),不复制进 main/。
# 用 target_sources 单独挂载,而不是塞进上面的 SRCS,以免和 INCLUDE_DIRS 的
# 语义混淆。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/app_font_12.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/app_font_24.c"
)