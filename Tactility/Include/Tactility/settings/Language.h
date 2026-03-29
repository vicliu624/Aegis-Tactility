#pragma once

#include <string>
#include <vector>

namespace tt::settings {

enum class Language {
    en_GB,
    en_US,
    fr_FR,
    nl_BE,
    nl_NL,
    zh_CN,
    count
};

void setLanguage(Language language);

Language getLanguage();

std::string toString(Language language);

bool fromString(const std::string& text, Language& language);

bool isLanguageSupported(Language language);

std::vector<Language> getSupportedLanguages();

}
