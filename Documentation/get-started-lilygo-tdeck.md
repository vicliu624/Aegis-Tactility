# Get Started: LilyGO T-Deck

这份文档面向第一次接触本项目的新手，目标是把下面三件事一次做完：

1. 在 Windows 上准备好 ESP-IDF 5.5.4 构建环境
2. 编译出 `lilygo-tdeck` 可用的固件
3. 把固件烧录到 LilyGO T-Deck 或 T-Deck Plus

这份文档已经按当前仓库和当前机器状态重新整理过，重点解决这类机器上最常见的问题：

- 同时装了多个 ESP-IDF 版本，例如 `5.4` 和 `5.5`
- VS Code 扩展里选的是一个环境，终端里实际跑的是另一个环境
- `IDF_PYTHON_ENV_PATH` 被旧的 shell 污染，导致 `export.bat` 拉起了错误的 Python 虚拟环境
- 直接运行裸 `idf.py` 时，被系统的 `.py` 文件关联或旧 PATH 干扰

## 核心原则

从现在开始，不要再直接使用这种命令：

```powershell
cmd /c "... export.bat && idf.py ..."
```

在多 ESP-IDF / 多 Python 的 Windows 机器上，这种写法非常容易串环境。

本仓库现在提供了一个专门的执行器：

- [idf55_runner.py](C:/Users/VicLi/Documents/Projects/Aegis-Tactility/Buildscripts/idf55_runner.py)

它会做这些事情：

- 固定使用 `ESP-IDF 5.5.4`
- 自动寻找本机可用的 `idf5.5_py*_env`
- 清理旧的 `IDF_PATH`、`IDF_PYTHON_ENV_PATH`、`ESP_IDF_VERSION` 等环境污染
- 调用正确的 `device.py`
- 调用正确的 `python.exe + idf.py`
- 不依赖当前 PowerShell / VS Code shell 的状态

后面的编译和烧录步骤，都统一走这个脚本。

## 1. 你需要准备什么

- 一台 Windows 电脑
- 一块 LilyGO T-Deck 或 T-Deck Plus
- 一根支持数据传输的 USB-C 线
- 已安装并可用的 `Git`
- 已安装并可用的 `Python`

先确认基础工具可用：

```powershell
git --version
python --version
```

如果任意一条命令报错，先把对应工具装好，再继续。

## 2. 获取仓库

推荐连子模块一起克隆：

```powershell
git clone --recursive <your-repo-url>
cd Aegis-Tactility
```

如果你已经克隆过仓库，但没拉子模块，执行：

```powershell
git submodule update --init --recursive
```

## 3. 安装 ESP-IDF 5.5.4

本仓库当前要求 `ESP-IDF 5.5.x`，不要用 `5.4`。

### 3.1 克隆 ESP-IDF 5.5.4

```powershell
git clone --recursive --branch v5.5.4 https://github.com/espressif/esp-idf.git C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4
```

### 3.2 安装 ESP32-S3 工具链和 Python 环境

推荐使用官方 `install.bat`：

```powershell
cmd /c "set IDF_TOOLS_PATH=C:\ProgramData\Espressif&& C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4\install.bat esp32s3"
```

这一步会在 `C:\ProgramData\Espressif\python_env` 下创建一个 `idf5.5_py*_env`，具体可能是：

- `idf5.5_py3.11_env`
- `idf5.5_py3.12_env`
- `idf5.5_py3.13_env`

这取决于你当时用什么 Python 跑的安装脚本。

仓库提供的执行器会自动识别可用的 `idf5.5_py*_env`，所以你不需要手工死记某一个版本号。

## 4. 验证 5.5.4 环境是否可用

在仓库根目录执行：

```powershell
python Buildscripts\idf55_runner.py doctor --device lilygo-tdeck
```

如果环境正常，你应该看到类似：

```text
ESP-IDF v5.5.4
```

如果这里失败，不要继续往后跑编译和烧录。优先检查：

- `C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4` 是否存在
- `C:\ProgramData\Espressif\python_env` 下是否存在 `idf5.5_py*_env`
- `install.bat esp32s3` 是否已经跑完

## 5. 为 T-Deck 编译固件

在仓库根目录执行：

```powershell
python Buildscripts\idf55_runner.py build --device lilygo-tdeck --build-dir build-tdeck
```

这条命令会自动完成：

- 选择 `ESP-IDF 5.5.4`
- 清理旧 shell 里的 ESP-IDF 环境变量
- 调用 `python device.py lilygo-tdeck`
- 使用 `esp32s3` 目标进行构建
- 把构建输出放到 `build-tdeck`

如果编译成功，主要产物会在：

- `build-tdeck\Tactility.bin`
- `build-tdeck\bootloader\bootloader.bin`
- `build-tdeck\partition_table\partition-table.bin`
- `build-tdeck\system.bin`
- `build-tdeck\data.bin`

## 6. 查找串口号

接上 T-Deck 之后，在 PowerShell 中查看当前串口：

```powershell
[System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
```

例如可能看到：

```text
COM7
```

如果你想看更详细的设备信息，可以执行：

```powershell
Get-PnpDevice -PresentOnly | Where-Object { $_.Class -eq 'Ports' -or $_.FriendlyName -match 'COM[0-9]+' } | Format-List Status,Class,FriendlyName,InstanceId
```

## 7. 让 T-Deck 进入下载模式

如果烧录时连不上板子，可以手动让它进入下载模式：

1. 按住轨迹球中键
2. 保持按住轨迹球，同时按一下 `RST`
3. 先松开 `RST`
4. 再松开轨迹球

如果烧录完成后屏幕没有马上亮起来，再按一次 `RST`。

## 8. 烧录固件

把下面命令中的 `COM7` 改成你自己的串口号：

```powershell
python Buildscripts\idf55_runner.py flash --device lilygo-tdeck --build-dir build-tdeck --port COM7
```

这条命令会写入整套镜像：

- bootloader
- partition table
- app
- system data
- user data

这条命令已经在当前仓库和当前机器上实测通过。

## 9. 打开串口日志

烧录完成后，如果你想看启动日志：

```powershell
python Buildscripts\idf55_runner.py monitor --device lilygo-tdeck --build-dir build-tdeck --port COM7
```

退出监视器使用：

```text
Ctrl+]
```

## 10. 新手最常用的一整套命令

如果你只想按顺序搭环境、编译、烧录，最推荐使用下面这一组：

```powershell
git clone --recursive <your-repo-url>
cd Aegis-Tactility

git submodule update --init --recursive

git clone --recursive --branch v5.5.4 https://github.com/espressif/esp-idf.git C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4
cmd /c "set IDF_TOOLS_PATH=C:\ProgramData\Espressif&& C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4\install.bat esp32s3"

python Buildscripts\idf55_runner.py doctor --device lilygo-tdeck
python Buildscripts\idf55_runner.py build --device lilygo-tdeck --build-dir build-tdeck

[System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object

python Buildscripts\idf55_runner.py flash --device lilygo-tdeck --build-dir build-tdeck --port COM7
python Buildscripts\idf55_runner.py monitor --device lilygo-tdeck --build-dir build-tdeck --port COM7
```

## 11. 为什么这套命令比旧文档稳

你之前遇到的问题，核心是下面这几类环境污染：

- `IDF_PYTHON_ENV_PATH` 还指向旧的 `idf5.4` 环境
- 裸 `idf.py` 命令走到了错误的 PATH 或错误的 `.py` 文件关联
- VS Code 扩展中选的是 `5.5.4`，但终端里继承的是 `5.4.x`
- 根目录 `sdkconfig` 被不同 target 反复覆盖后，命令本身又没有把 target 锁死

`idf55_runner.py` 的作用就是把这些问题都挡在命令之外。

## 12. 常见问题

### `doctor` 提示找不到 `idf5.5_py*_env`

说明 `ESP-IDF 5.5.4` 的 Python 虚拟环境还没有准备好。执行：

```powershell
cmd /c "set IDF_TOOLS_PATH=C:\ProgramData\Espressif&& C:\ProgramData\Espressif\frameworks\esp-idf-v5.5.4\install.bat esp32s3"
```

然后再执行：

```powershell
python Buildscripts\idf55_runner.py doctor --device lilygo-tdeck
```

### `Could not open COMx`

优先检查：

- USB 线是否是数据线
- 串口号是否选对了
- 是否有其他串口工具占用了该端口
- 板子是否进入了下载模式

### 板子在线，但烧录时 `Connecting...` 卡住

按前面的下载模式步骤操作：

1. 按住轨迹球
2. 按一下 `RST`
3. 先松 `RST`
4. 再松轨迹球

然后重新执行 `flash` 命令。

### 想重新生成设备配置

执行：

```powershell
python Buildscripts\idf55_runner.py build --device lilygo-tdeck --build-dir build-tdeck
```

这条命令内部会自动重新执行：

```text
python device.py lilygo-tdeck
```

通常不需要再手工单独跑一次。

### 想做一次干净重编译

先清理：

```powershell
python Buildscripts\idf55_runner.py fullclean --device lilygo-tdeck --build-dir build-tdeck
```

再重新构建：

```powershell
python Buildscripts\idf55_runner.py build --device lilygo-tdeck --build-dir build-tdeck
```

### 为什么不再推荐直接运行 `export.bat && idf.py`

因为它在单一环境机器上经常没问题，但在已经装过多个 ESP-IDF 版本、或者开着 VS Code ESP-IDF 扩展 shell 的机器上，非常容易混入旧环境变量。

这个仓库现在推荐统一走：

- [idf55_runner.py](C:/Users/VicLi/Documents/Projects/Aegis-Tactility/Buildscripts/idf55_runner.py)

它是为当前仓库的多设备、多环境场景专门整理出来的稳定入口。
