# T-Deck LoRa Backend 实现说明与分层约束

这份文档不是临时想法记录，而是 `lilygo-tdeck` LoRa 后端的实施边界。
后续只要继续改 T-Deck LoRa，就必须先对照这里，确保代码落点和职责没有漂移。

## 目标

`lilygo-tdeck` 上的 LoRa 不是可选功能，而是系统默认通信能力的一部分。
因此目标不是“把 SX1262 点亮”，而是把它正确接入当前工程结构。

本次实现必须同时满足这些要求：

- T-Deck 开机后，LoRa 默认处于启用路径，而不是等待用户进设置页手动打开。
- 板级细节必须留在板级，不得把 GPIO、SPI、IRQ、BUSY、RESET 细节抬到 Reticulum 或 GUI。
- HAL 必须暴露稳定的 LoRa 能力对象，而不是让上层继续直接碰板子引脚。
- Reticulum `LoRaInterface` 只负责接口适配，不负责板级驱动。
- 旧的 UART/KISS 路径不能被硬删，而是作为另一种 backend 保留下来。
- 服务启动日志必须真实反映启动成功还是失败，不能再出现失败了仍然打印 `Started` 的情况。

## 当前工程分层

### 1. 板级层

位置：

- `Devices/lilygo-tdeck/Source/*`

职责：

- 描述 T-Deck 自己的硬件现实。
- 拥有 T-Deck 专属的 GPIO、SPI、CS、RST、BUSY、IRQ 绑定。
- 创建 T-Deck 这块板子上的具体 SX1262 设备对象。
- 处理这块板子的上电、总线接入、射频芯片 bring-up。

不该做的事：

- 不写 Reticulum 包语义。
- 不写 LoRa 接口管理策略。
- 不把 GUI、Boot app、Settings 逻辑塞进板级设备。

### 2. HAL 能力层

位置：

- `Tactility/Include/Tactility/hal/radio/LoRaDevice.h`
- `Tactility/Source/hal/radio/LoRaDevice.cpp`
- `Modules/hal-device-module/include/tactility/hal/Device.h`
- `Modules/hal-device-module/include/tactility/drivers/hal_device.h`
- `Modules/hal-device-module/source/drivers/hal_device.cpp`

职责：

- 把板级 LoRa 硬件收敛成统一能力接口。
- 向上暴露 `start / stop / poll / send / metrics / detail` 这组稳定契约。
- 在 HAL 类型系统里正式引入 `Radio`，避免把无线设备继续塞进 `Other`。

不该做的事：

- 不关心 Reticulum 协议细节。
- 不处理 UI 生命周期。
- 不处理 LoRa 设置页保存逻辑。

### 3. Backend 层

位置：

- `Tactility/Private/Tactility/service/reticulum/interfaces/backends/LoRaBackend.h`
- `Tactility/Private/Tactility/service/reticulum/interfaces/backends/NativeLoRaBackend.h`
- `Tactility/Private/Tactility/service/reticulum/interfaces/backends/UartKissLoRaBackend.h`
- `Tactility/Source/service/reticulum/interfaces/backends/NativeLoRaBackend.cpp`
- `Tactility/Source/service/reticulum/interfaces/backends/UartKissLoRaBackend.cpp`

职责：

- 对 Reticulum 提供“后端”视角的统一接口。
- 承接两种实现来源：
- 原生 HAL LoRa 设备 backend。
- 旧的 UART/RNode KISS modem backend。

这里是关键分界线：

- `LoRaBackend` 是后端契约。
- `NativeLoRaBackend` 适配 HAL `LoRaDevice`。
- `UartKissLoRaBackend` 承接原有 RNode/KISS 串口路径。

这样做的意义是：

- `LoRaInterface` 不再知道 T-Deck 的 GPIO 和 SX1262 命令细节。
- 原生后端与串口后端可以并列存在，而不是互相污染。

### 4. Reticulum Interface 层

位置：

- `Tactility/Private/Tactility/service/reticulum/interfaces/LoRaInterface.h`
- `Tactility/Source/service/reticulum/interfaces/LoRaInterface.cpp`

职责：

- 把 backend 适配成 Reticulum `Interface`。
- 管理收包回调、发送入口、线程轮询和 metrics 读取。
- 在运行时优先选择 native backend，没有 native LoRa 设备时再退回 UART/KISS。

不该做的事：

- 不直接操作 SX1262 SPI 命令。
- 不直接写 T-Deck 引脚逻辑。
- 不把板级 bring-up 混进 interface 启动路径里。

### 5. 服务层

位置：

- `Tactility/Source/service/reticulum/ReticulumService.cpp`
- `Tactility/Source/service/ServiceRegistration.cpp`
- `Tactility/Source/settings/ReticulumSettings.cpp`

职责：

- 管理 Reticulum 生命周期。
- 管理 LoRa 配置加载。
- 决定 LoRa 服务是否注册、是否启动。
- 保证启动结果的日志和状态一致。

不该做的事：

- 不知道 T-Deck 的射频引脚定义。
- 不拥有 SX1262 驱动实现。

### 6. UI 和设置层

位置：

- `Tactility/Source/app/reticulumsettings/ReticulumSettings.cpp`

职责：

- 展示 LoRa 配置。
- 保存 LoRa 配置。
- 在配置改变后重启 Reticulum 服务。

不该做的事：

- 不做驱动初始化。
- 不做 backend 探测。
- 不承担“开机必须先把 LoRa 拉起来”的职责。

## 本次实现已经落地的内容

### 1. HAL 正式引入 LoRa 能力对象

新增：

- `tt::hal::radio::LoRaDevice`
- `tt::hal::radio::LoRaConfiguration`
- `tt::hal::radio::LoRaMetrics`
- `findLoRaDevices()`
- `findLoRaDevice()`

同时：

- `tt::hal::Device::Type` 新增 `Radio`
- `hal_device` C 接口同步支持 `HAL_DEVICE_TYPE_RADIO`

这意味着：

- T-Deck 原生 LoRa 不再是“板子私货”，而是 HAL 的正式能力。
- 以后再接别的 LoRa 板，不需要重写 Reticulum 这一层。

### 2. T-Deck 板级实现了原生 SX1262 设备

新增：

- `Devices/lilygo-tdeck/Source/devices/TdeckSx1262LoRaDevice.h`
- `Devices/lilygo-tdeck/Source/devices/TdeckSx1262LoRaDevice.cpp`

`Devices/lilygo-tdeck/Source/Configuration.cpp` 现在会注册这个设备。

当前板级实现拥有这些职责：

- 解析并接入 `spi0`
- 配置 SX1262 的 `CS / RST / BUSY / IRQ`
- 执行复位、探测、初始化、进入 RX
- 处理收发状态切换
- 在发包后回到接收模式
- 提供 RSSI、SNR、hardware MTU 等指标

当前 T-Deck 板级引脚定义：

- `CS = GPIO_NUM_9`
- `RST = GPIO_NUM_17`
- `BUSY = GPIO_NUM_13`
- `IRQ = GPIO_NUM_45`
- SPI 复用 `spi0`

### 3. Reticulum 侧不再把 UART/KISS 写死在 `LoRaInterface`

`LoRaInterface` 已经重构为“选 backend 的接口适配层”。

它现在只做这些事：

- 校验 LoRa settings
- 创建 backend
- 启动 backend
- 启动 LoRa 线程轮询 backend
- 把收到的 payload 适配成 Reticulum frame
- 发送 frame 到当前 backend

它现在不再做这些事：

- 不再自己持有 KISS 协议状态机
- 不再直接绑定 `uart1`
- 不再承担原生 SX1262 驱动职责

### 4. 旧 UART/KISS 路径保留为独立 backend

旧的 RNode/KISS 串口逻辑没有被删掉。
它被下沉成：

- `UartKissLoRaBackend`

这样一来：

- 没有 native LoRa 设备的板子仍然可以走旧路径
- T-Deck 可以优先走原生 SX1262 backend
- 两种后端不必在同一个类里互相打架

### 5. T-Deck 默认启用 LoRa

`Tactility/Source/settings/ReticulumSettings.cpp` 现在对 `lilygo-tdeck` 返回默认 `enabled = true`。

这满足了这块板子的核心需求：

- LoRa 是系统启动路径的一部分
- 不需要等用户进设置页再手动打开

### 6. 服务启动日志改为真实状态

`Tactility/Source/service/ServiceRegistration.cpp` 现在只有在 `onStart()` 成功时才打印 `Started <service>`。

这解决了之前的结构性问题：

- 启动失败不会再被日志掩盖
- 后续排查 LoRa bring-up 时，服务状态更可信

### 7. `LoRaInterface` backend 生命周期已做并发收口

`LoRaInterface` 现在使用共享持有 backend，而不是把 backend 裸指针抛出锁外。

这样做的目的：

- 避免服务停止和发包并发时出现 backend 生命周期悬空窗口
- 保证 `getMetrics()`、轮询线程、发送路径看到的是同一份有效 backend 引用

## 启动路径现在应该是什么样

T-Deck 的正确启动路径应当是：

1. 板级配置注册 `TdeckSx1262LoRaDevice`
2. HAL 持有这个 LoRa 设备，并把它暴露成 `Radio` 能力
3. Reticulum 启动时加载 LoRa settings
4. `LoRaInterface` 优先发现 native HAL LoRa 设备
5. `NativeLoRaBackend` 启动板级 SX1262
6. LoRa 线程进入轮询和收包状态
7. UI 只负责展示设置和控制服务重启

这里最重要的原则是：

- LoRa bring-up 属于系统服务路径
- 不属于 BootSplash
- 不属于某个菜单页面
- 更不属于某个临时 workaround

## 明确禁止的越界做法

后续继续开发时，下面这些都算越界：

- 把 SX1262 的 SPI、GPIO、BUSY、IRQ 逻辑塞进 `LoRaInterface`
- 把 T-Deck 专属硬件细节塞进 `ReticulumService`
- 把 native LoRa bring-up 塞进 `GuiService`、`Loader`、`Boot` app 或设置页
- 把“引脚定义补齐了”误当成“backend 完成了”
- 把“日志里打印了 started”误当成“硬件已经 online”
- 把 UI 卡顿、串口监听问题、LoRa bring-up 问题混成一个补丁乱修

## 当前验证状态

当前已经完成：

- 架构落位
- 代码实现
- `lilygo-tdeck` 编译通过

当前还没有在这次会话里完成的事情：

- 实板 flash 验证
- 实包收发验证
- 长时间运行稳定性验证

这意味着当前结论是：

- 代码结构已经按目标落好
- 编译层面已经打通
- 硬件联调仍然需要按清单确认

## 硬件验证清单

刷机后重点看这些日志：

- `Registering LoRa Reticulum interface`
- `Using native LoRa device backend SX1262 LoRa`
- `SX1262 online on spi0 ...`
- `Registered interface lora ... started=true, available=true/false`

启动后至少检查这些行为：

- 系统 UI 能正常进入 Launcher 和 Settings，不出现明显卡死
- LoRa 默认已启用，而不是等待手动开关
- 发送后能回到 RX 模式
- 没有因为 backend bring-up 导致串口持续掉线或反复重启

## 后续迭代原则

如果后面还要继续增强 T-Deck LoRa，只允许沿着这条线推进：

- 板级优化放在 `TdeckSx1262LoRaDevice`
- HAL 能力增强放在 `tt::hal::radio::LoRaDevice`
- backend 策略增强放在 `LoRaBackend` 体系
- Reticulum 适配增强放在 `LoRaInterface`
- 设置展示增强放在 UI

不要再回到“先找个地方塞进去跑起来”的写法。
只要边界守住，后面无论是调 IRQ、调 TX/RX 切换、补 metrics，还是给别的板子接原生 LoRa，都不会重新把工程写坏。
