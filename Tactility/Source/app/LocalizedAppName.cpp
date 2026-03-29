#include <Tactility/app/LocalizedAppName.h>

#include <Tactility/RecursiveMutex.h>
#include <Tactility/i18n/TextResources.h>
#include <Tactility/settings/Language.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace tt::app {

namespace {

struct LocalizedAppNameCacheEntry {
    std::unique_ptr<i18n::TextResources> textResources;
    std::string loadedLocale;
    std::string appName;
};

RecursiveMutex cacheMutex;
std::unordered_map<std::string, LocalizedAppNameCacheEntry> cacheEntries;

}

std::string getLocalizedAppNameFromPath(const char* textResourcePath) {
    if (textResourcePath == nullptr || textResourcePath[0] == '\0') {
        return {};
    }

    auto lock = cacheMutex.asScopedLock();
    if (!lock.lock()) {
        return {};
    }

    auto& cacheEntry = cacheEntries[textResourcePath];
    if (!cacheEntry.textResources) {
        cacheEntry.textResources = std::make_unique<i18n::TextResources>(textResourcePath);
    }

    const auto currentLocale = settings::toString(settings::getLanguage());
    if (cacheEntry.loadedLocale != currentLocale) {
        if (!cacheEntry.textResources->load()) {
            return {};
        }

        cacheEntry.loadedLocale = currentLocale;
        cacheEntry.appName = (*cacheEntry.textResources)[0];
    }

    return cacheEntry.appName;
}

}
