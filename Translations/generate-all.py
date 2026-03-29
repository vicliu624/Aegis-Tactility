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
    # AppSettings app
    generate(
        "AppSettings.csv",
        "Tactility/Private/Tactility/app/appsettings/TextResources.h",
        "tt::app::appsettings::i18n",
        "Data/system/app/AppSettings/i18n"
    )
    # Development app
    generate(
        "Development.csv",
        "Tactility/Private/Tactility/app/development/TextResources.h",
        "tt::app::development::i18n",
        "Data/system/app/Development/i18n"
    )
    # Display app
    generate(
        "Display.csv",
        "Tactility/Private/Tactility/app/display/TextResources.h",
        "tt::app::display::i18n",
        "Data/system/app/Display/i18n"
    )
    # Files app
    generate(
        "Files.csv",
        "Tactility/Private/Tactility/app/files/TextResources.h",
        "tt::app::files::i18n",
        "Data/system/app/Files/i18n"
    )
    # GPS settings app
    generate(
        "GpsSettings.csv",
        "Tactility/Private/Tactility/app/gpssettings/TextResources.h",
        "tt::app::gpssettings::i18n",
        "Data/system/app/GpsSettings/i18n"
    )
    # Keyboard settings app
    generate(
        "KeyboardSettings.csv",
        "Tactility/Private/Tactility/app/keyboard/TextResources.h",
        "tt::app::keyboardsettings::i18n",
        "Data/system/app/KeyboardSettings/i18n"
    )
    # LocaleSettings app
    generate(
        "LocaleSettings.csv",
        "Tactility/Private/Tactility/app/localesettings/TextResources.h",
        "tt::app::localesettings::i18n",
        "Data/system/app/LocaleSettings/i18n"
    )
    # Power app
    generate(
        "Power.csv",
        "Tactility/Private/Tactility/app/power/TextResources.h",
        "tt::app::power::i18n",
        "Data/system/app/Power/i18n"
    )
    # Settings app
    generate(
        "Settings.csv",
        "Tactility/Private/Tactility/app/settings/TextResources.h",
        "tt::app::settings::i18n",
        "Data/system/app/Settings/i18n"
    )
    # Time and date settings app
    generate(
        "TimeDateSettings.csv",
        "Tactility/Private/Tactility/app/timedatesettings/TextResources.h",
        "tt::app::timedatesettings::i18n",
        "Data/system/app/TimeDateSettings/i18n"
    )
    # Trackball settings app
    generate(
        "TrackballSettings.csv",
        "Tactility/Private/Tactility/app/trackball/TextResources.h",
        "tt::app::trackballsettings::i18n",
        "Data/system/app/TrackballSettings/i18n"
    )
    # USB settings app
    generate(
        "UsbSettings.csv",
        "Tactility/Private/Tactility/app/usbsettings/TextResources.h",
        "tt::app::usbsettings::i18n",
        "Data/system/app/UsbSettings/i18n"
    )
    # Wi-Fi manage app
    generate(
        "WifiManage.csv",
        "Tactility/Private/Tactility/app/wifimanage/TextResources.h",
        "tt::app::wifimanage::i18n",
        "Data/system/app/WifiManage/i18n"
    )
