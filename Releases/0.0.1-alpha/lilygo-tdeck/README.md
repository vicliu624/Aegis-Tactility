# Aegis 0.0.1-alpha for LilyGO T-Deck

- Aegis version: `0.0.1-alpha`
- Upstream base: `Tactility v0.7.0-dev`
- Target board: `lilygo-tdeck`
- ESP-IDF used for this build: `5.5.4`

## Included Files

- `bootloader/bootloader.bin`
- `partition_table/partition-table.bin`
- `Tactility.bin`
- `system.bin`
- `data.bin`
- `flash_args`
- `SHA256SUMS.txt`

## Flashing

### With esptool

From this directory, run:

```powershell
python -m esptool --chip esp32s3 --port COM7 --baud 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB @flash_args
```

Adjust `COM7` to match your serial port.

### Entering bootloader mode on T-Deck

1. Press the trackball and the reset button at the same time.
2. Release the reset button first.
3. Release the trackball.
4. After flashing, press `RST` once if the device does not reboot cleanly.

## Notes

- `zh-CN` is included in this build through PSRAM-backed runtime CJK font loading.
- This bundle is intended for the LilyGO T-Deck only.