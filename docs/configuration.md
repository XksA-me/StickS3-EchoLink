# 固件配置

运行时 Wi-Fi、MQTT 与消息检查频率通过设备网页配置。只有无线分组、显示功耗、音频上限和 LifeTime 参数需要修改源码后重新烧录。

## EchoLink 配置

文件：`include/Config.h`

### Group ID

```cpp
#define ECHOLINK_GROUP_ID 0x4D3A91C7UL
```

同一组设备必须使用相同值。建议生成一个随机 32 位值：

```sh
python3 -c 'import secrets; print(f"0x{secrets.randbits(32):08X}UL")'
```

Group ID 用于过滤其他 EchoLink 数据，不是密码，也不等于加密密钥。公开场景仍可能被监听或伪造。

### ESP-NOW channel

```cpp
#define ECHOLINK_ESPNOW_CHANNEL 1
```

两台设备必须一致。若本地 2.4 GHz 干扰严重，可共同改为 1、6、11 中的另一个信道后重新烧录。

### 音频

```cpp
#define ECHOLINK_SAMPLE_RATE 16000
#define ECHOLINK_MAX_RECORD_SECONDS 8
#define ECHOLINK_AUDIO_CHUNK_BYTES 200
```

不建议只修改一台设备。增加最大录音时间会线性增加每个 PSRAM 队列槽的内存占用和发送时间；当前五条发送队列与五条接收队列按 8 秒设计。

### 显示功耗

```cpp
#define ECHOLINK_DISPLAY_BRIGHTNESS 80
#define ECHOLINK_DISPLAY_DIM_BRIGHTNESS 16
#define ECHOLINK_DISPLAY_DIM_SECONDS 15
#define ECHOLINK_DISPLAY_SLEEP_SECONDS 30
```

亮度范围为 0-255。睡眠时间必须大于或等于降亮时间。

## LifeTime 配置

文件：`include/LifeTimeConfig.h`

### 方向检测

```cpp
static constexpr uint32_t LIFETIME_ORIENTATION_STABLE_MS = 1000;
static constexpr uint32_t LIFETIME_IMU_SAMPLE_MS = 100;
```

第一个值控制设备保持某个方向多久后启动 Hourglass；第二个值控制沙漏与 Together 前台的 IMU 采样周期。减小采样周期会提高灵敏度，也会增加功耗。

### 六面时长

```cpp
static constexpr uint32_t LIFETIME_FACE_DURATION_SECONDS[6] = {
    60, 120, 300, 600, 900, 1800};
```

顺序依次为：屏幕向上、屏幕向下、右侧、左侧、顶部、底部。

### 续航估算

`LIFETIME_LOCAL_RUNTIME_MINUTES`、`LIFETIME_WIFI_RUNTIME_MINUTES` 和 `LIFETIME_MONITOR_RUNTIME_MINUTES` 只用于电量页显示粗略剩余时间。它们不是电量计实测结果，应根据自己的亮度、信号与电池老化情况校准。

## Wi-Fi 与 MQTT

这些信息不应写进源码：

- Wi-Fi SSID 和密码
- MQTT Host、端口、用户名和密码
- MQTT Topic
- MQTT 消息检查频率：20ms、250ms、1s 或 5s

在 EchoLink 页面长按 `B` 2.5 秒，连接临时热点并访问 `http://192.168.4.1` 配置。信息保存在设备 NVS 中，最多保存 5 个 Wi-Fi Profile。

检查频率只降低 MQTT Task 的空闲唤醒次数。Socket 保持连接，待发语音会立即唤醒任务，Socket 中已有的语音分片也会连续排空。5 秒档的新消息提示可能延迟约 5 秒；当前 QoS 0 协议不支持断开 Wi-Fi 数分钟后补拉历史消息。

两台远程设备必须满足：

- Group ID 相同
- 都选择 `WIFI`
- Broker 地址、Topic 一致
- MQTT 账号拥有该 Topic 的发布和订阅权限

## 配置修改后的验证

```sh
./scripts/run_host_sim.sh
pio run -e sticks3
```

涉及协议字段、采样率或分片大小时，还要同步修改 `server/recorder/protocol.py` 和测试代码，否则 Recorder 可能无法解析新固件。
