#include <Tactility/Logger.h>
#include <Tactility/settings/Language.h>
#include <Tactility/settings/SystemSettings.h>

#include <utility>
#include <vector>

namespace tt::settings {

static const auto LOGGER = Logger("Language");

namespace {

constexpr bool RUNTIME_CJK_LANGUAGE_ENABLED =
#if defined(CONFIG_TT_RUNTIME_CJK_FONT) && CONFIG_TT_RUNTIME_CJK_FONT
    true;
#else
    false;
#endif

Language normalizeLanguage(Language language) {
    return isLanguageSupported(language) ? language : Language::en_US;
}

} // namespace

void setLanguage(Language newLanguage) {
    SystemSettings properties;
    if (!loadSystemSettings(properties)) {
        return;
    }

    properties.language = normalizeLanguage(newLanguage);
    saveSystemSettings(properties);
}

Language getLanguage() {
    SystemSettings properties;
    if (!loadSystemSettings(properties)) {
        return Language::en_US;
    } else {
        return normalizeLanguage(properties.language);
    }
}

std::string toString(Language language) {
    switch (language) {
        case Language::en_GB:
            return "en-GB";
        case Language::en_US:
            return "en-US";
        case Language::fr_FR:
            return "fr-FR";
        case Language::nl_BE:
            return "nl-BE";
        case Language::nl_NL:
            return "nl-NL";
        case Language::zh_CN:
            return "zh-CN";
        default:
            LOGGER.error("Missing serialization for language {}", static_cast<int>(language));
            std::unreachable();
    }
}

bool fromString(const std::string& text, Language& language) {
    if (text == "en-GB") {
        language = Language::en_GB;
    } else if (text == "en-US") {
        language = Language::en_US;
    } else if (text == "fr-FR") {
        language = Language::fr_FR;
    } else if (text == "nl-BE") {
        language = Language::nl_BE;
    } else if (text == "nl-NL") {
        language = Language::nl_NL;
    } else if (text == "zh-CN" && RUNTIME_CJK_LANGUAGE_ENABLED) {
        language = Language::zh_CN;
    } else {
        return false;
    }

    return true;
}

bool isLanguageSupported(Language language) {
    switch (language) {
        case Language::en_GB:
        case Language::en_US:
        case Language::fr_FR:
        case Language::nl_BE:
        case Language::nl_NL:
            return true;
        case Language::zh_CN:
            return RUNTIME_CJK_LANGUAGE_ENABLED;
        case Language::count:
            return false;
        default:
            LOGGER.error("Missing support rule for language {}", static_cast<int>(language));
            return false;
    }
}

std::vector<Language> getSupportedLanguages() {
    std::vector<Language> languages = {
        Language::en_GB,
        Language::en_US,
        Language::fr_FR,
        Language::nl_BE,
        Language::nl_NL,
    };

    if (RUNTIME_CJK_LANGUAGE_ENABLED) {
        languages.push_back(Language::zh_CN);
    }

    return languages;
}

}
