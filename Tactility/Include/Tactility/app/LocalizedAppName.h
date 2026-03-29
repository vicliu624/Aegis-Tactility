#pragma once

#include <string>

namespace tt::app {

/**
 * Resolve a localized app name from an app-local i18n directory.
 *
 * The first entry in the app's generated TextResources must be APP_NAME.
 * When the current locale is unavailable, TextResources falls back to en-US.
 */
std::string getLocalizedAppNameFromPath(const char* textResourcePath);

}
