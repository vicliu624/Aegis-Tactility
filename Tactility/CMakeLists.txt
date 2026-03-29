cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../Buildscripts/module.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../Buildscripts/properties.cmake")

file(GLOB_RECURSE SOURCE_FILES Source/*.c*)

list(APPEND REQUIRES_LIST
    TactilityKernel
    TactilityCore
    TactilityFreeRtos
    hal-device-module
    lvgl-module
    lv_screenshot
    minitar
    minmea
)

if (DEFINED ENV{ESP_IDF_VERSION})

    list(APPEND REQUIRES_LIST
        platform-esp32
        driver
        elf_loader
        QRCode
        esp_http_server
        esp_http_client
        esp-tls
        esp_wifi
        json # Effectively cJSON
        nvs_flash
        spiffs
        vfs
        fatfs
        lwip
        spi_flash
    )

    if ("${IDF_TARGET}" STREQUAL "esp32s3")
        list(APPEND REQUIRES_LIST esp_tinyusb)
    endif ()

else ()

    list(APPEND REQUIRES_LIST
        platform-posix
        freertos_kernel
        cJSON
        lvgl
    )

endif ()

tactility_add_module(Tactility
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS Include/
    PRIV_INCLUDE_DIRS Private/
    REQUIRES ${REQUIRES_LIST}
)

if (DEFINED ENV{ESP_IDF_VERSION})
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${COMPONENT_LIB} PUBLIC -Wno-unused-variable)
    endif ()

    if (NOT DEFINED TACTILITY_SKIP_SPIFFS)
        if (NOT DEFINED PYTHON)
            set(PYTHON python)
        endif ()

        GET_PROPERTY_FILE_CONTENT("${CMAKE_CURRENT_SOURCE_DIR}/../sdkconfig" sdkconfig_text)
        GET_PROPERTY_VALUE_OR_DEFAULT(sdkconfig_text "CONFIG_TT_RUNTIME_CJK_FONT" "n" tt_runtime_cjk_font_enabled)
        GET_PROPERTY_VALUE_OR_DEFAULT(sdkconfig_text "CONFIG_LV_USE_TINY_TTF" "n" lv_use_tiny_ttf_enabled)

        set(enable_runtime_cjk_font FALSE)
        if (tt_runtime_cjk_font_enabled STREQUAL "y" AND lv_use_tiny_ttf_enabled STREQUAL "y")
            set(enable_runtime_cjk_font TRUE)
            message("Runtime CJK font packaging enabled for this device")
        else ()
            message("Runtime CJK font packaging disabled for this device")
        endif ()

        set(DATA_STAGING_DIR "${CMAKE_BINARY_DIR}/fatfs-data-root")
        set(DATA_FONT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../Libraries/lvgl/tests/src/test_files/fonts/noto/NotoSansSC-Regular.ttf")
        set(DATA_FONT_SUBSET_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../Buildscripts/font-subset/subset_runtime_cjk_font.py")
        set(DATA_FONT_CHARSET "${CMAKE_CURRENT_SOURCE_DIR}/../Buildscripts/font-subset/runtime-cjk-charset-common-sc.txt")
        set(DATA_FONT_RUNTIME_TEXT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../Data/system")
        set(DATA_FONT_TARGET_DIR "${DATA_STAGING_DIR}/fonts")
        set(DATA_FONT_TARGET "${DATA_FONT_TARGET_DIR}/NotoSansSC-Aegis-CommonSC.ttf")

        set(AEGIS_STAGE_DATA_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${DATA_STAGING_DIR}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${DATA_STAGING_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/../Data/data" "${DATA_STAGING_DIR}"
        )

        if (enable_runtime_cjk_font)
            list(APPEND AEGIS_STAGE_DATA_COMMANDS
                COMMAND ${CMAKE_COMMAND} -E make_directory "${DATA_FONT_TARGET_DIR}"
                COMMAND "${PYTHON}" "${DATA_FONT_SUBSET_SCRIPT}"
                    --source-font "${DATA_FONT_SOURCE}"
                    --output-font "${DATA_FONT_TARGET}"
                    --charset "${DATA_FONT_CHARSET}"
                    --runtime-text-root "${DATA_FONT_RUNTIME_TEXT_ROOT}"
            )
        endif ()

        add_custom_target(aegis_stage_data_assets ALL
            ${AEGIS_STAGE_DATA_COMMANDS}
            VERBATIM
        )

        # Read-only
        fatfs_create_rawflash_image(system "${CMAKE_CURRENT_SOURCE_DIR}/../Data/system" FLASH_IN_PROJECT PRESERVE_TIME)
        # Read-write
        fatfs_create_spiflash_image(data "${DATA_STAGING_DIR}" FLASH_IN_PROJECT PRESERVE_TIME)
        add_dependencies(fatfs_data_bin aegis_stage_data_assets)
    endif ()

endif ()

