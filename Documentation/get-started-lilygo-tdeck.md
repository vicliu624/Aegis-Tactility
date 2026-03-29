# Get Started: LilyGO T-Deck

这份文档面向第一次接触本项目的新手，目标是把下面三件事一次做完：

1. 在 Windows 上装好 ESP-IDF 5.5.4 构建环境
2. 编译出 `lilygo-tdeck` 可用的固件
3. 把固件烧录到 LilyGO T-Deck 或 T-Deck Plus

这套流程已经按本仓库当前状态实际验证通过，验证环境为：
- Windows PowerShell
- ESP-IDF `5.5.4`
- 设备目标 `lilygo-tdeck`
- 芯片目标 `esp32s3`

注意：
- `T-Deck` 和 `T-Deck Plus` 在本仓库里使用同一个设备目标：`lilygo-tdeck`
- 本仓库当前要求 `ESP-IDF 5.5`，不要用 `5.4`
- 本文里的命令都显式指定了 `IDF_TARGET=esp32s3`，这样更不容易撞上 `esp32` / `esp32s3` 目标冲突

## 1. 你需要准备什么
- 一台 Windows 电脑
- 一块 LilyGO T-Deck 或 T-Deck Plus
- 一根支持数据传输的 USB-C 线
- 已安装并加入 `PATH` 的 `Git`
- 已安装并加入 `PATH` 的 `python`

先确认工具可用：

```powershell
git --version
python --version
```

如果任意一条命令报错，先把对应工具装好，再继续。

## 2. 克隆仓库

推荐直接连子模块一起克隆：

```powershell
git clone --recursive <your-repo-url>
cd Aegis-Tactility
```

如果你已经克隆过仓库，但没有拉子模块，执行：

```powershell
git submodule update --init --recursive
```

## 3. 安装 ESP-IDF 5.5.4

下面这套命令会把 ESP-IDF 安装到 `C:\ProgramData\Espressif`。

### 3.1 克隆 ESP-IDF

```powershell
git clone --recursive --branch v5.5.4 https://github.com/espressif/esp-idf.git C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4
```

### 3.2 安装 ESP32-S3 工具链

这一部推荐使用 `install.bat`，因为有些 Windows / PowerShell 环境会拦截 `.ps1` 脚本。

```powershell
cmd /c "set IDF_TOOLS_PATH=C:\ProgramData\Espressif&& C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4\install.bat esp32s3"
```

这一部会自动安装：
- ESP32-S3 编译器
- CMake
- Ninja
- `esptool`
- ESP-IDF 使用的 Python 虚拟环境

### 3.3 验证 ESP-IDF 安装成功

```powershell
cmd /c "set IDF_TOOLS_PATH=C:\ProgramData\Espressif&& call C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4\export.bat && idf.py --version"
```

期望看到类似输出：

```text
ESP-IDF v5.5.x
```

## 4. 为 T-Deck 生成 `sdkconfig`

在仓库根目录执行：

```powershell
python device.py lilygo-tdeck
```

这一步会读取 `Devices/lilygo-tdeck/device.properties`，并生成当前设备需要的 `sdkconfig`。

对于 T-Deck，这里最关键的配置是：
- 芯片目标：`ESP32S3`
- 设备 ID：`lilygo-tdeck`
- Flash 容量：`16MB`
- PSRAM：启用

## 5. 编译固件

推荐把构建输出放到单独的 `build-tdeck` 目录：

```powershell
cmd /c "set IDF_TOOLS_PATH=C:\ProgramData\Espressif&& set IDF_TARGET=esp32s3&& call C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4\export.bat && python device.py lilygo-tdeck && idf.py -B build-tdeck build"
```

如果编译成功，主要产物会在 `build-tdeck` 下面：
- `build-tdeck\Tactility.bin`
- `build-tdeck\bootloader\bootloader.bin`
- `build-tdeck\partition_table\partition-table.bin`
- `build-tdeck\system.bin`
- `build-tdeck\data.bin`

另外还会生成两个很重要的刷机参数文件：
- `build-tdeck\flash_args`
- `build-tdeck\flasher_args.json`

## 6. 找到开发板串口号

接上 T-Deck 后，在 PowerShell 里查看当前串口：

```powershell
[System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
```

比如输出：

```text
COM7
```

如果你想看更详细的设备信息，可以执行：

```powershell
Get-PnpDevice -PresentOnly | Where-Object { $_.Class -eq 'Ports' -or $_.FriendlyName -match 'COM[0-9]+' } | Format-List Status,Class,FriendlyName,InstanceId
```

## 7. 让 T-Deck 进入下载模式

如果烧录时连不上板子，手动让它进入下载模式：

1. 按下轨迹球中键
2. 保持按住轨迹球，同时按一下 `RST`
3. 先松开 `RST`
4. 再松开轨迹球

如果烧录完成后屏幕没有马上亮起来，再按一次 `RST`。

## 8. 烧录固件

把下面命令中的 `COM7` 替换成你自己的串口号：

```powershell
cmd /c "set IDF_TOOLS_PATH=C:\ProgramData\Espressif&& set IDF_TARGET=esp32s3&& call C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4\export.bat && python device.py lilygo-tdeck && idf.py -B build-tdeck -p COM7 flash"
```

这条命令会把整套镜像都写进去，不只是主程序：
- bootloader
- partition table
- app
- system data
- user data

这里特意把 `python device.py lilygo-tdeck` 放进了烧录命令里。这样即使你中途重开过终端，也不会因为丢了设备配置而让 `idf.py` 回退到默认 `esp32` 目标。

## 9. 打开串口监视器

烧录完成后，可以直接看启动日志：

```powershell
cmd /c "set IDF_TOOLS_PATH=C:\ProgramData\Espressif&& set IDF_TARGET=esp32s3&& call C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4\export.bat && idf.py -B build-tdeck -p COM7 monitor"
```

退出监视器的方法是按 `Ctrl+]`。

## 10. 新手最常用的一整套命令

如果你只想按顺序把环境搭好、编译、烧录、看日志，下面这组命令最实用：

```powershell
git clone --recursive <your-repo-url>
cd Aegis-Tactility

git clone --recursive --branch v5.5.4 https://github.com/espressif/esp-idf.git C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4
cmd /c "set IDF_TOOLS_PATH=C:\ProgramData\Espressif&& C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4\install.bat esp32s3"

cmd /c "set IDF_TOOLS_PATH=C:\ProgramData\Espressif&& set IDF_TARGET=esp32s3&& call C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4\export.bat && python device.py lilygo-tdeck && idf.py -B build-tdeck build"

[System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object

cmd /c "set IDF_TOOLS_PATH=C:\ProgramData\Espressif&& set IDF_TARGET=esp32s3&& call C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4\export.bat && python device.py lilygo-tdeck && idf.py -B build-tdeck -p COM7 flash"
cmd /c "set IDF_TOOLS_PATH=C:\ProgramData\Espressif&& set IDF_TARGET=esp32s3&& call C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4\export.bat && idf.py -B build-tdeck -p COM7 monitor"
```

## 11. 常见问题

### `idf.py` 提示找不到

说明当前终端还没有进入 ESP-IDF 环境。先执行：

```powershell
cmd /c "set IDF_TOOLS_PATH=C:\ProgramData\Espressif&& call C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4\export.bat && idf.py --version"
```

### `install.ps1` 因执行策略失败

不要改系统策略，直接用 `install.bat`：

```powershell
cmd /c "set IDF_TOOLS_PATH=C:\ProgramData\Espressif&& C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4\install.bat esp32s3"
```

### `Could not open COMx`

优先检查这几项：
- USB 线是不是数据线
- 串口号是不是选对了
- 有没有别的串口工具占用了这个口
- 板子有没有进入下载模式

### `Target settings are not consistent: 'esp32' in the environment, 'esp32s3' in CMakeCache.txt`

这是最容易在重开终端后遇到的问题。它的意思是：
- 当前命令用了默认目标 `esp32`
- 但 `build-tdeck` 这个构建目录之前是按 `esp32s3` 生成的

对 T-Deck，优先用下面这条命令重新生成设备配置并重新构建：

```powershell
cmd /c "set IDF_TOOLS_PATH=C:\ProgramData\Espressif&& set IDF_TARGET=esp32s3&& call C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4\export.bat && python device.py lilygo-tdeck && idf.py -B build-tdeck build"
```

如果还是报同样的错，再做一次干净重编译：

```powershell
cmd /c "set IDF_TOOLS_PATH=C:\ProgramData\Espressif&& set IDF_TARGET=esp32s3&& call C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4\export.bat && python device.py lilygo-tdeck && idf.py -B build-tdeck fullclean && idf.py -B build-tdeck build"
```

重新编译完成后，再执行烧录命令。

### `Property file not found: sdkconfig`

说明你还没有为当前仓库生成设备配置，或者 `sdkconfig` 被删掉了。执行：

```powershell
python device.py lilygo-tdeck
```

然后再重新执行编译或烧录命令。

### 编译时报告子模块缺失

执行：

```powershell
git submodule update --init --recursive
```

### 误用了 ESP-IDF 5.4

这个仓库当前要求 `ESP-IDF 5.5`。用下面命令确认：

```powershell
cmd /c "set IDF_TOOLS_PATH=C:\ProgramData\Espressif&& call C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4\export.bat && idf.py --version"
```

### 想做一次干净重编译

先清理：

```powershell
cmd /c "set IDF_TOOLS_PATH=C:\ProgramData\Espressif&& set IDF_TARGET=esp32s3&& call C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4\export.bat && python device.py lilygo-tdeck && idf.py -B build-tdeck fullclean"
```

再重新编译：

```powershell
cmd /c "set IDF_TOOLS_PATH=C:\ProgramData\Espressif&& set IDF_TARGET=esp32s3&& call C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4\export.bat && python device.py lilygo-tdeck && idf.py -B build-tdeck build"
```