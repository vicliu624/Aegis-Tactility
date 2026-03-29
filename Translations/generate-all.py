import os
from pathlib import Path

def get_project_root():
    return Path(__file__).parent.parent.resolve()

def generate(csv_file, header_file, header_namespace, data_path):
    csv_file_path = f"{get_project_root()}/Translations/{csv_file}"
    if os.path.isfile(csv_file_path):
        print(f"Processing {csv_file}")
        script_path = f"{get_project_root()}/Translations/generate.py"
        os.system(f"python {script_path} {csv_file} {header_file} {header_namespace} {data_path}")
    else:
        print(f"Skipping {csv_file} (not found)")

if __name__ == "__main__":
    # Core translations
    generate(
        "Core.csv",
        "Tactility/Include/Tactility/i18n/CoreTextResources.h",
        "tt::i18n::core",
        "Data/system/i18n/core"
    )
    # Launcher app
    generate(
        "Launcher.csv",
        "Tactility/Private/Tactility/app/launcher/TextResources.h",
        "tt::app::launcher::i18n",
        "Data/system/app/Launcher/i18n"
    )
    # AppList app
    generate(
        "AppList.csv",
        "Tactility/Private/Tactility/app/applist/TextResources.h",
        "tt::app::applist::i18n",
        "Data/system/app/AppList/i18n"
    )
    # Files app
    generate(
        "Files.csv",
        "Tactility/Private/Tactility/app/files/TextResources.h",
        "tt::app::files::i18n",
        "Data/system/app/Files/i18n"
    )
    # LocaleSettings app
    generate(
        "LocaleSettings.csv",
        "Tactility/Private/Tactility/app/localesettings/TextResources.h",
        "tt::app::localesettings::i18n",
        "Data/system/app/LocaleSettings/i18n"
    )
    # Settings app
    generate(
        "Settings.csv",
        "Tactility/Private/Tactility/app/settings/TextResources.h",
        "tt::app::settings::i18n",
        "Data/system/app/Settings/i18n"
    )
