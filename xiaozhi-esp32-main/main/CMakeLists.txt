# ============================================================
#   XIAOZHI ASSISTANT – CUSTOM CMakeLists.txt FOR PUPPY ROBOT
#   SAFE VERSION – NO CAMERA, NO VIDEO, NO IMAGE EFFECTS
# ============================================================

idf_build_get_property(project_dir PROJECT_DIR)
idf_build_get_property(project_name PROJECT_NAME)
get_filename_component(PROJECT_DIR_ABS ${PROJECT_DIR} ABSOLUTE)

# ============================================================
#   SOURCE LIST (ĐÃ XOÁ CAMERA/VIDEO/IOCTL/IMAGE EFFECTS)
# ============================================================

set(SOURCES 
    "audio/audio_codec.cc"
    "audio/audio_service.cc"
    "audio/codecs/no_audio_codec.cc"
    "audio/codecs/box_audio_codec.cc"
    "audio/codecs/es8311_audio_codec.cc"
    "audio/codecs/es8374_audio_codec.cc"
    "audio/codecs/es8388_audio_codec.cc"
    "audio/codecs/es8389_audio_codec.cc"
    "audio/codecs/dummy_audio_codec.cc"
    "audio/processors/audio_debugger.cc"

    "led/single_led.cc"
    "led/circular_strip.cc"
    "led/gpio_led.cc"

    "display/display.cc"
    "display/lcd_display.cc"
    "display/oled_display.cc"
    "display/lvgl_display/lvgl_display.cc"
    "display/emote_display.cc"

    "display/lvgl_display/emoji_collection.cc"
    "display/lvgl_display/lvgl_theme.cc"
    "display/lvgl_display/lvgl_font.cc"
    "display/lvgl_display/lvgl_image.cc"

    "display/lvgl_display/gif/lvgl_gif.cc"
    "display/lvgl_display/gif/gifdec.c"

    "protocols/protocol.cc"
    "protocols/mqtt_protocol.cc"
    "protocols/websocket_protocol.cc"
    "mcp_server.cc"

    "system_info.cc"
    "application.cc"
    "ota.cc"
    "settings.cc"
    "device_state_event.cc"
    "assets.cc"
    "main.cc"
)

set(INCLUDE_DIRS 
    "." 
    "display" 
    "display/lvgl_display" 
    "display/lvgl_display/jpg" 
    "audio" 
    "protocols"
)

# ============================================================
#   THÊM BOARD COMMON FILES
# ============================================================

file(GLOB BOARD_COMMON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/boards/common/*.cc)
list(APPEND SOURCES ${BOARD_COMMON_SOURCES})
list(APPEND INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/boards/common)

# ============================================================
#   FUNCTION TÌM COMPONENT (KHÔNG ĐỤNG)
# ============================================================

idf_build_get_property(build_components BUILD_COMPONENTS)

function(find_component_by_pattern PATTERN COMPONENT_VAR PATH_VAR)
    foreach(COMPONENT ${build_components})
        if(COMPONENT MATCHES "${PATTERN}")
            set(${COMPONENT_VAR} ${COMPONENT} PARENT_SCOPE)
            idf_component_get_property(COMPONENT_PATH ${COMPONENT} COMPONENT_DIR)
            set(${PATH_VAR} "${COMPONENT_PATH}" PARENT_SCOPE)
            break()
        endif()
    endforeach()
endfunction()

# ============================================================
#   SET FONT DEFAULT
# ============================================================

set(BUILTIN_TEXT_FONT font_puhui_16_4)
set(BUILTIN_ICON_FONT font_awesome_16_4)

# ============================================================
#   BOARD SELECTION (CHỌN PUPPY ROBOT)
# ============================================================

if(CONFIG_BOARD_TYPE_PUPPY_ROBOT)
    set(BOARD_TYPE "puppy-robot")
endif()

# ============================================================
#   IMPORT FILES CỦA BOARD PUPPY ROBOT
# ============================================================

file(GLOB BOARD_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD_TYPE}/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD_TYPE}/*.c
)
list(APPEND SOURCES ${BOARD_SOURCES})

# ============================================================
#   AUDIO PROCESSOR
# ============================================================

if(CONFIG_USE_AUDIO_PROCESSOR)
    list(APPEND SOURCES "audio/processors/afe_audio_processor.cc")
else()
    list(APPEND SOURCES "audio/processors/no_audio_processor.cc")
endif()

# Wake words
if(CONFIG_IDF_TARGET_ESP32S3)
    list(APPEND SOURCES 
        "audio/wake_words/afe_wake_word.cc"
        "audio/wake_words/custom_wake_word.cc"
    )
endif()

# ============================================================
#   LANGUAGE SELECTION
# ============================================================

if(CONFIG_LANGUAGE_VI_VN)
    set(LANG_DIR "vi-VN")
elseif(CONFIG_LANGUAGE_EN_US)
    set(LANG_DIR "en-US")
endif()

set(LANG_JSON "${CMAKE_CURRENT_SOURCE_DIR}/assets/locales/${LANG_DIR}/language.json")
set(LANG_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/assets/lang_config.h")

file(GLOB LANG_SOUNDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/locales/${LANG_DIR}/*.ogg)
file(GLOB COMMON_SOUNDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/common/*.ogg)

# ============================================================
#   REGISTER COMPONENT
# ============================================================

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS ${INCLUDE_DIRS}
    EMBED_FILES ${LANG_SOUNDS} ${COMMON_SOUNDS}
    WHOLE_ARCHIVE
)

# ============================================================
#   DEFINE BOARD
# ============================================================

if(NOT BOARD_NAME)
    set(BOARD_NAME ${BOARD_TYPE})
endif()

target_compile_definitions(${COMPONENT_LIB}
    PRIVATE BOARD_TYPE=\"${BOARD_TYPE}\"
    PRIVATE BOARD_NAME=\"${BOARD_NAME}\"
    PRIVATE BUILTIN_TEXT_FONT=${BUILTIN_TEXT_FONT}
    PRIVATE BUILTIN_ICON_FONT=${BUILTIN_ICON_FONT}
)

# ============================================================
#   LANGUAGE GENERATION
# ============================================================

add_custom_command(
    OUTPUT ${LANG_HEADER}
    COMMAND python ${PROJECT_DIR}/scripts/gen_lang.py
            --language "${LANG_DIR}"
            --output "${LANG_HEADER}"
    DEPENDS
        ${LANG_JSON}
        ${PROJECT_DIR}/scripts/gen_lang.py
    COMMENT "Generating language config: ${LANG_DIR}"
)

add_custom_target(lang_header ALL
    DEPENDS ${LANG_HEADER}
)

# ============================================================
#   ASSET BUILDER (KHÔNG ĐỤNG)
# ============================================================

find_component_by_pattern("espressif__esp-sr" ESP_SR_COMPONENT ESP_SR_COMPONENT_PATH)
if(ESP_SR_COMPONENT_PATH)
    set(ESP_SR_MODEL_PATH "${ESP_SR_COMPONENT_PATH}/model")
endif()

find_component_by_pattern("xiaozhi-fonts" XIAOZHI_FONTS_COMPONENT XIAOZHI_FONTS_COMPONENT_PATH)
if(XIAOZHI_FONTS_COMPONENT_PATH)
    set(XIAOZHI_FONTS_PATH "${XIAOZHI_FONTS_COMPONENT_PATH}")
endif()

# ---- BỎ PHẦN CAMERA / VIDEO / IMAGE EFFECTS HOÀN TOÀN ----
# Không cần xóa gì thêm ở đây. 
# ============================================================

message(STATUS "Puppy Robot CMake loaded successfully. No camera, no video, no esp_image_effects.")
