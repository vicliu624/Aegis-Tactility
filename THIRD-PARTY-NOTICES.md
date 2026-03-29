# Third-Party Notices

This repository contains Aegis, a derivative firmware based on Tactility.

The notices below summarize important third-party and upstream components used by the project.
This file should be updated whenever new third-party code, fonts, assets, or build dependencies
are added to Aegis.

### Tactility (upstream base)

Aegis is based on the Tactility project.

Website: https://github.com/TactilityProject/Tactility

License: Mixed by subproject. See [LICENSE.md](LICENSE.md) for the repository-level overview.

### ESP-IDF

This project uses ESP-IDF to compile the ESP32 firmware.

Website: https://www.espressif.com/

License: [Apache License v2.0](https://github.com/espressif/esp-idf/blob/master/LICENSE)

### Flipper Zero Firmware

Some of the code in inside the Tactility or TactilityCore project has originally been adapted
from the Flipper Zero firmware it was changed to fit the Tactility project.

Website: https://github.com/flipperdevices/flipperzero-firmware/

License: [GPL v3.0](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/LICENSE)

### Google Fonts & Material Design Icons

Websites:

- https://fonts.google.com/icons
- https://github.com/google/material-design-icons

License: [Apache License, version 2.0](https://fonts.google.com/attribution)

### Google Material Design & Icons

Website: https://fonts.google.com/icons

License: Multiple (SIL Open Font License, Apache License, Ubuntu Font License): https://developers.google.com/fonts/faq 

### Noto Sans SC

The runtime CJK text fallback shipped with Aegis for Simplified Chinese rendering is based on
Noto Sans SC.

The font image flashed onto the device is generated at build time as a subset:

- `build-*/fatfs-data-root/fonts/NotoSansSC-Aegis-CommonSC.ttf`
- `Buildscripts/font-subset/subset_runtime_cjk_font.py`
- `Buildscripts/font-subset/runtime-cjk-charset-common-sc.txt`

Source font in this repository:

- `Libraries/lvgl/tests/src/test_files/fonts/noto/NotoSansSC-Regular.ttf`
- `Libraries/lvgl/tests/src/test_files/fonts/noto/OFL.txt`

Website: https://fonts.google.com/noto/specimen/Noto+Sans+SC

License: [SIL Open Font License 1.1](https://openfontlicense.org/)

### Timezones CSV

Website: https://github.com/nayarsystems/posix_tz_db

License: [MIT](https://github.com/nayarsystems/posix_tz_db/blob/master/LICENSE)

### Minmea

Website: https://github.com/kosma/minmea

License: [WTFPL](https://github.com/kosma/minmea/blob/master/LICENSE.grants), [LGPL 3.0](https://github.com/kosma/minmea/blob/master/LICENSE.LGPL-3.0), [MIT](https://github.com/kosma/minmea/blob/master/LICENSE.MIT)

### Meshtastic Firmware

Parts of the Meshtastic firmware were copied and modified for Tactility.

Website: https://github.com/meshtastic/firmware

License: [GPL v3.0](https://github.com/meshtastic/firmware/blob/master/LICENSE)

### BQ27220 Driver

Website: https://github.com/Xinyuan-LilyGO/T-Echo/blob/main/LICENSE

License: [MIT](https://github.com/Xinyuan-LilyGO/T-Echo/blob/main/LICENSE)

### esp32s3-gc9a01-lvgl

Website: https://github.com/UsefulElectronics/esp32s3-gc9a01-lvgl

License: [Explicitly granted by author](https://github.com/TactilityProject/Tactility/pull/295#discussion_r2226215423)

### Other Dependencies

Some dependencies contain their own license. For example: the subprojects in `Libraries/`
