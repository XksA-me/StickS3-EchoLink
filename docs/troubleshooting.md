# 故障排查

## 电脑只看到一台设备

1. 确认两条 USB-C 线都支持数据，不是充电线。
2. 分别单独连接，执行 `pio device list`，记录 USB 序列号。
3. 更换 Hub 端口或直接连接电脑。
4. macOS 查看 `ls /dev/cu.usbmodem*`。

两个 StickS3 应出现两个不同的 `usbmodem` 端口。

## 上传连接失败

- 关闭占用串口的 Monitor、Arduino IDE 或其他终端。
- 降低 `upload_speed` 到 `460800` 或 `115200`。
- 重新插拔设备并再次运行上传命令。
- 确认日志识别为 `ESP32-S3-PICO-1`、8 MB Flash 和 8 MB PSRAM。
- 自动进入下载模式失败时，按住设备 A 再重新连接 USB，然后重试；不同硬件批次的按键时序可能不同。

## M5GFX 下载失败或反复换源

项目默认使用：

```ini
https://github.com/m5stack/M5GFX.git#0.2.25
```

这会直接 Git clone，不需要关闭 MD5 校验。第一次可能较慢。检查 GitHub 访问：

```sh
git ls-remote https://github.com/m5stack/M5GFX.git refs/tags/0.2.25
```

清理后重试：

```sh
pio run -e sticks3 -t clean
pio pkg install -e sticks3
pio run -e sticks3
```

## 屏幕显示 `WiFi SSID missing`

- 长按 EchoLink 页面 `B` 2.5 秒进入配置门户。
- 手机连接临时热点后打开 `http://192.168.4.1`。
- StickS3 只支持 2.4 GHz；确认路由器没有只开 5 GHz。
- 扫描不到时可手动输入 SSID，注意大小写和空格。
- 每台设备都需要单独配置。

## `WiFi not ready` 或 MQTT 反复重连

- 先确认设备获得 IP，查看串口中的 `WiFi status=3`。
- MQTT Host 只填写域名/IP，不加 `mqtt://`。
- 检查端口、防火墙、安全组、用户名和密码。
- 两台设备 Topic 必须一致。
- 查看服务器 `docker compose ps` 和容器日志。
- 公网信号不稳定时固件会三次快速重试，再暂停约 15 秒，不会锁死 UI。

## 按 A 后 `mic data timeout` 或没有录音

- 确认硬件为 StickS3，而不是其他 M5Stick 型号。
- 串口应显示 `PTT recording started`，松开后显示字节数。
- 完全断电重启，排除 Codec 状态残留。
- 使用本项目固定的 M5Unified/M5GFX 版本重新编译。

## 录音显示已发送但另一台没有提示

- 两台必须在同一模式：都为 `LOCAL` 或都为 `WIFI`。
- LOCAL：Group ID 和 ESP-NOW channel 必须一致。
- WIFI：Group ID、Broker 和 Topic 必须一致。
- 当前协议是半双工且 QoS 0，避免两台同时讲话。
- 查看发送端和接收端串口日志中的丢包计数。

## LifeTime 摇一摇没有反应

- 进入 `TOGETHER` 页面后连续做两次明确但不过度猛烈的摇动。
- 使用 `B` 单击作为可靠测试入口。
- 确认启动日志为 `IMU=1`。
- 两台设备传输模式与 Group ID 必须一致。

## UI 闪烁或设备发热

- 使用最新固件，LifeTime 应显示 `canvas=1`。
- 确认 PSRAM 配置为 N8R8，启动日志没有 `PSRAM alloc failed`。
- Wi-Fi 信号差导致持续重连会增加发热，先在路由器附近测试。
- 屏幕会在 15 秒后变暗、30 秒后关闭；后台网络仍保持工作。
