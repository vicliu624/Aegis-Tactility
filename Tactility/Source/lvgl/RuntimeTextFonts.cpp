#include "RuntimeTextFonts.h"

#include <Tactility/Logger.h>
#include <Tactility/MountPoints.h>
#include <Tactility/Paths.h>
#include <Tactility/file/File.h>
#include <Tactility/lvgl/Lvgl.h>

#include <tactility/lvgl_fonts.h>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

#include <cstdio>
#include <format>
#include <string>

#include <lvgl.h>

namespace tt::lvgl {

static const auto LOGGER = Logger("RuntimeFonts");

namespace {

constexpr auto* FONT_FILE_NAME = "NotoSansSC-Aegis-CommonSC.ttf";
constexpr auto* INTERNAL_FONT_DIRECTORY = "/data/fonts";
constexpr auto* SD_FONT_DIRECTORY = "aegis/fonts";
constexpr size_t TINY_TTF_CACHE_SIZE_SMALL = 48;
constexpr size_t TINY_TTF_CACHE_SIZE_DEFAULT = 192;
constexpr size_t TINY_TTF_CACHE_SIZE_LARGE = 128;

struct RuntimeTextFontState {
    lv_font_t* small = nullptr;
    lv_font_t* normal = nullptr;
    lv_font_t* large = nullptr;
    uint8_t* fontData = nullptr;
    size_t fontDataSize = 0;
    bool loaded = false;
    std::string activeFontPath;
    std::string activeSource;
};

RuntimeTextFontState runtime_fonts;

constexpr bool RUNTIME_CJK_FONT_ENABLED =
#if defined(CONFIG_TT_RUNTIME_CJK_FONT) && CONFIG_TT_RUNTIME_CJK_FONT
    true;
#else
    false;
#endif

constexpr bool IS_DEFAULT_THEME_DARK =
#ifdef CONFIG_LV_THEME_DEFAULT_DARK
    true;
#else
    false;
#endif

std::string toLvglPath(const std::string& path) {
    return std::format("{}{}", PATH_PREFIX, path);
}

std::string getInternalFontPath() {
    return file::getChildPath(INTERNAL_FONT_DIRECTORY, FONT_FILE_NAME);
}

std::string getSdFontPath() {
    std::string sd_root;
    if (!findFirstMountedSdCardPath(sd_root)) return {};
    return file::getChildPath(file::getChildPath(sd_root, SD_FONT_DIRECTORY), FONT_FILE_NAME);
}

std::string resolveFontPath() {
    const auto internal_font_path = getInternalFontPath();
    if (file::isFile(internal_font_path)) return internal_font_path;

    const auto sd_font_path = getSdFontPath();
    if (!sd_font_path.empty() && file::isFile(sd_font_path)) return sd_font_path;

    return {};
}

lv_font_t* createRuntimeFont(const std::string& lvgl_path, enum LvglFontSize font_size) {
    size_t cache_size = TINY_TTF_CACHE_SIZE_DEFAULT;
    switch (font_size) {
        case FONT_SIZE_SMALL:
            cache_size = TINY_TTF_CACHE_SIZE_SMALL;
            break;
        case FONT_SIZE_DEFAULT:
            cache_size = TINY_TTF_CACHE_SIZE_DEFAULT;
            break;
        case FONT_SIZE_LARGE:
            cache_size = TINY_TTF_CACHE_SIZE_LARGE;
            break;
    }

#if LV_USE_TINY_TTF && LV_TINY_TTF_FILE_SUPPORT
    return lv_tiny_ttf_create_file_ex(
        lvgl_path.c_str(),
        static_cast<int32_t>(lvgl_get_text_font_height(font_size)),
        LV_FONT_KERNING_NORMAL,
        cache_size
    );
#else
    LV_UNUSED(lvgl_path);
    LV_UNUSED(font_size);
    return nullptr;
#endif
}

lv_font_t* createRuntimeFont(const void* data, size_t data_size, enum LvglFontSize font_size) {
#if LV_USE_TINY_TTF
    size_t cache_size = TINY_TTF_CACHE_SIZE_DEFAULT;
    switch (font_size) {
        case FONT_SIZE_SMALL:
            cache_size = TINY_TTF_CACHE_SIZE_SMALL;
            break;
        case FONT_SIZE_DEFAULT:
            cache_size = TINY_TTF_CACHE_SIZE_DEFAULT;
            break;
        case FONT_SIZE_LARGE:
            cache_size = TINY_TTF_CACHE_SIZE_LARGE;
            break;
    }

    return lv_tiny_ttf_create_data_ex(
        data,
        data_size,
        static_cast<int32_t>(lvgl_get_text_font_height(font_size)),
        LV_FONT_KERNING_NORMAL,
        cache_size
    );
#else
    LV_UNUSED(data);
    LV_UNUSED(data_size);
    LV_UNUSED(font_size);
    return nullptr;
#endif
}

#ifdef ESP_PLATFORM
void freeRuntimeFontData() {
    if (runtime_fonts.fontData != nullptr) {
        heap_caps_free(runtime_fonts.fontData);
        runtime_fonts.fontData = nullptr;
        runtime_fonts.fontDataSize = 0;
    }
}

bool loadFontIntoPsram(const std::string& path) {
    auto lock = file::getLock(path)->asScopedLock();
    if (!lock.lock()) {
        LOGGER.warn("Failed to lock runtime font file {}", path);
        return false;
    }

    FILE* file = fopen(path.c_str(), "rb");
    if (file == nullptr) {
        LOGGER.warn("Failed to open runtime font file {}", path);
        return false;
    }

    const auto close_file = [&]() {
        fclose(file);
        file = nullptr;
    };

    const auto file_size = file::getSize(file);
    if (file_size <= 0) {
        LOGGER.warn("Runtime font file has invalid size {}: {}", file_size, path);
        close_file();
        return false;
    }

    auto* buffer = static_cast<uint8_t*>(heap_caps_malloc(
        static_cast<size_t>(file_size),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    ));
    if (buffer == nullptr) {
        LOGGER.warn(
            "Failed to allocate {} bytes for runtime font in PSRAM (free={}, largest={})",
            file_size,
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
            heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)
        );
        close_file();
        return false;
    }

    size_t total_read = 0;
    while (total_read < static_cast<size_t>(file_size)) {
        const auto bytes_read = fread(buffer + total_read, 1, static_cast<size_t>(file_size) - total_read, file);
        if (bytes_read == 0) break;
        total_read += bytes_read;
    }

    close_file();

    if (total_read != static_cast<size_t>(file_size)) {
        LOGGER.warn("Failed to read runtime font file completely: {} (read {} / {})", path, total_read, file_size);
        heap_caps_free(buffer);
        return false;
    }

    runtime_fonts.fontData = buffer;
    runtime_fonts.fontDataSize = static_cast<size_t>(file_size);
    return true;
}
#else
void freeRuntimeFontData() {}
bool loadFontIntoPsram(const std::string& path) {
    LV_UNUSED(path);
    return false;
}
#endif

void destroyFont(lv_font_t*& font) {
#if LV_USE_TINY_TTF
    if (font != nullptr) {
        lv_tiny_ttf_destroy(font);
        font = nullptr;
    }
#else
    LV_UNUSED(font);
#endif
}

void destroyRuntimeFonts() {
    destroyFont(runtime_fonts.small);
    destroyFont(runtime_fonts.normal);
    destroyFont(runtime_fonts.large);
    freeRuntimeFontData();
    runtime_fonts.loaded = false;
    runtime_fonts.activeFontPath.clear();
    runtime_fonts.activeSource.clear();
}

void refreshDisplayThemes() {
#if LV_USE_THEME_DEFAULT
    for (auto* display = lv_display_get_next(nullptr); display != nullptr; display = lv_display_get_next(display)) {
        lv_color_t primary = lv_palette_main(LV_PALETTE_BLUE);
        lv_color_t secondary = lv_palette_main(LV_PALETTE_RED);

        auto* screen = lv_display_get_screen_active(display);
        if (screen != nullptr && lv_display_get_theme(display) != nullptr) {
            primary = lv_theme_get_color_primary(screen);
            secondary = lv_theme_get_color_secondary(screen);
        }

        auto* theme = lv_theme_default_init(
            display,
            primary,
            secondary,
            IS_DEFAULT_THEME_DARK,
            lvgl_get_text_font(FONT_SIZE_DEFAULT)
        );
        lv_display_set_theme(display, theme);
    }
#endif
}

} // namespace

bool loadRuntimeTextFonts() {
    if (runtime_fonts.loaded) return true;

    if (!RUNTIME_CJK_FONT_ENABLED) {
        return true;
    }

#if !LV_USE_TINY_TTF
    LOGGER.warn("tiny_ttf is not enabled in the current LVGL configuration");
    return false;
#else
    const auto font_path = resolveFontPath();
    if (font_path.empty()) {
        LOGGER.warn("Runtime CJK font not found in /data/fonts or SD card");
        return false;
    }

    bool using_psram_copy = false;

#ifdef ESP_PLATFORM
    const auto psram_free_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const auto psram_largest_before = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
#endif

    if (loadFontIntoPsram(font_path)) {
        runtime_fonts.small = createRuntimeFont(runtime_fonts.fontData, runtime_fonts.fontDataSize, FONT_SIZE_SMALL);
        runtime_fonts.normal = createRuntimeFont(runtime_fonts.fontData, runtime_fonts.fontDataSize, FONT_SIZE_DEFAULT);
        runtime_fonts.large = createRuntimeFont(runtime_fonts.fontData, runtime_fonts.fontDataSize, FONT_SIZE_LARGE);
        using_psram_copy = runtime_fonts.small != nullptr && runtime_fonts.normal != nullptr && runtime_fonts.large != nullptr;

        if (!using_psram_copy) {
            LOGGER.warn("Failed to create runtime fonts from PSRAM copy, falling back to file-backed tiny_ttf");
            destroyFont(runtime_fonts.small);
            destroyFont(runtime_fonts.normal);
            destroyFont(runtime_fonts.large);
            freeRuntimeFontData();
        }
    }

    if (!using_psram_copy) {
#if LV_TINY_TTF_FILE_SUPPORT
        const auto lvgl_font_path = toLvglPath(font_path);
        runtime_fonts.small = createRuntimeFont(lvgl_font_path, FONT_SIZE_SMALL);
        runtime_fonts.normal = createRuntimeFont(lvgl_font_path, FONT_SIZE_DEFAULT);
        runtime_fonts.large = createRuntimeFont(lvgl_font_path, FONT_SIZE_LARGE);
#else
        LOGGER.warn("tiny_ttf file loading is not enabled and PSRAM preload failed");
#endif
    }

    if (runtime_fonts.small == nullptr || runtime_fonts.normal == nullptr || runtime_fonts.large == nullptr) {
        LOGGER.error("Failed to load runtime CJK fonts from {}", font_path);
        lvgl_reset_text_font_fallbacks();
        destroyRuntimeFonts();
        return false;
    }

    lvgl_set_text_font_fallback(FONT_SIZE_SMALL, runtime_fonts.small);
    lvgl_set_text_font_fallback(FONT_SIZE_DEFAULT, runtime_fonts.normal);
    lvgl_set_text_font_fallback(FONT_SIZE_LARGE, runtime_fonts.large);
    refreshDisplayThemes();

    runtime_fonts.loaded = true;
    runtime_fonts.activeFontPath = font_path;
    runtime_fonts.activeSource = using_psram_copy ? "psram" : "file";
#ifdef ESP_PLATFORM
    LOGGER.info(
        "Loaded runtime CJK fonts from {} via {} (font_bytes={}, psram_free_before={}, psram_free_after={}, psram_largest_before={}, psram_largest_after={}, cache small={}, default={}, large={})",
        font_path,
        runtime_fonts.activeSource,
        runtime_fonts.fontDataSize,
        psram_free_before,
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        psram_largest_before,
        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
        TINY_TTF_CACHE_SIZE_SMALL,
        TINY_TTF_CACHE_SIZE_DEFAULT,
        TINY_TTF_CACHE_SIZE_LARGE
    );
#else
    LOGGER.info(
        "Loaded runtime CJK fonts from {} via {} (cache small={}, default={}, large={})",
        font_path,
        runtime_fonts.activeSource,
        TINY_TTF_CACHE_SIZE_SMALL,
        TINY_TTF_CACHE_SIZE_DEFAULT,
        TINY_TTF_CACHE_SIZE_LARGE
    );
#endif
    return true;
#endif
}

void unloadRuntimeTextFonts() {
    if (!RUNTIME_CJK_FONT_ENABLED) {
        return;
    }

    if (!runtime_fonts.loaded) return;

    LOGGER.info("Unloading runtime CJK fonts");
    lvgl_reset_text_font_fallbacks();
    destroyRuntimeFonts();
}

} // namespace tt::lvgl
