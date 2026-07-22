# 固件编译与烧录

本文说明如何从源码准备依赖、编译、烧录和查看串口日志。项目只维护一个 `sticks3` 环境，ESP-NOW 与 Wi-Fi MQTT 在运行时切换，不需要分别构建两个固件。

## 环境要求

- Git
- Python 3.9 或更新版本
- PlatformIO Core 6.x
- 支持数据传输的 USB Type-C 线
- M5StickS3（ESP32-S3-PICO-1-N8R8）

安装 PlatformIO：

```sh
python3 -m pip install --user platformio
```

macOS/Linux 若找不到命令：

```sh
~/.local/bin/pio --version
```

也可以把 `~/.local/bin` 加入 shell 的 `PATH`。

## 获取源码

```sh
git clone git@github.com:XksA-me/StickS3-EchoLink.git
cd StickS3-EchoLink
```

没有 GitHub SSH Key 时可使用 HTTPS：

```sh
git clone https://github.com/XksA-me/StickS3-EchoLink.git
```

## 依赖策略

依赖在 `platformio.ini` 中固定：

- Espressif32 Platform `6.12.0`
- Arduino-ESP32 `2.0.17`（由 Platform 决定）
- M5GFX `0.2.25`，直接从官方 GitHub 源码拉取
- M5Unified `0.2.18`
- PubSubClient `2.8`

M5GFX 使用 Git 依赖是为了避开部分 PlatformIO Package Mirror 返回截断压缩包的问题。不要关闭 MD5 或文件大小校验；校验失败通常说明下载内容确实不完整。第一次 Git clone 较慢属于正常情况。

预先安装依赖：

```sh
pio pkg install -e sticks3
```

## 编译

```sh
pio run -e sticks3
```

成功后固件位于：

```text
.pio/build/sticks3/firmware.bin
```

## 查找设备端口

```sh
pio device list
```

或按系统查看：

```sh
# macOS
ls /dev/cu.usbmodem*

# Linux
ls /dev/ttyACM*
```

Windows 使用设备管理器中的 `COMx`。

两台设备同时连接时会出现两个不同端口和 USB 序列号。请逐台烧录，避免选错。

## 烧录

macOS 示例：

```sh
pio run -e sticks3 -t upload --upload-port /dev/cu.usbmodem14201
```

Linux 示例：

```sh
pio run -e sticks3 -t upload --upload-port /dev/ttyACM0
```

Windows 示例：

```powershell
pio run -e sticks3 -t upload --upload-port COM3
```

看到 `Hash of data verified` 和 `[SUCCESS]` 才代表烧录完整。普通烧录不会清空 NVS；执行 `erase` 才会清除网络配置和计数。

## 串口监控

```sh
pio device monitor --port /dev/cu.usbmodem14201 --baud 115200
```

正常日志包含：

```text
LifeTime ready; IMU=1 canvas=1
EchoLink device XXXXXXXX ready in LOCAL / ESP-NOW mode
HEARTBEAT ... state=READY ...
```

退出监控使用 `Ctrl+C`。

## M5Burner 与 PlatformIO

- **M5Burner** 面向已经生成好的固件，适合普通用户一键烧录。
- **PlatformIO** 负责编译源码、安装依赖、修改配置、调试和上传，适合开发者。

本仓库当前以 PlatformIO 源码发布为主。后续可在 GitHub Releases 附带 `firmware.bin`，供 M5Burner 或 `esptool.py` 用户使用。

## 清理重建

```sh
pio run -e sticks3 -t clean
pio run -e sticks3
```

不要把 `.pio/`、本地 `lib/` 或构建产物提交到 Git，它们已在 `.gitignore` 中排除。

遇到端口、依赖或下载模式问题，继续查看 [故障排查](troubleshooting.md)。
