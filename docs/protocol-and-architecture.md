# 协议与架构

## 固件结构

StickS3 只能运行一个主固件，因此 EchoLink 与 LifeTime 作为两个前台应用编译到同一个程序：

```text
M5Unified hardware
  |- App launcher
  |- EchoLink UI + audio state machine
  |- LifeTime canvas UI + timers + IMU
  |- Transport abstraction
       |- ESP-NOW
       `- Wi-Fi + MQTT worker task
```

应用切换只改变前台界面。MQTT、接收队列、待发语音和 LifeTime 定时器继续在后台运行。

## 并发模型

- Arduino `loop()`：按键、UI、录音状态、播放、ESP-NOW 队列和 LifeTime。
- MQTT FreeRTOS Task：连接、Keepalive、订阅、接收和发布。
- FreeRTOS Queue：在线程之间传递固定大小的网络帧和发布结果。
- PSRAM：发送语音 5 槽、接收语音 5 槽、录制/重组缓冲和 LifeTime 全屏 Canvas。

MQTT Socket 阻塞不会直接卡住 UI。录音和播放期间暂停发送语音分片，结束后自动继续。

## Voice Protocol v2

包头为 22 字节、小端、紧凑布局：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| magic | `uint16` | 固定 `0xEC71` |
| version | `uint8` | 当前 `2` |
| type | `uint8` | 1 Start、2 Audio、3 End、4 Nudge |
| groupId | `uint32` | 设备组过滤标识 |
| senderId | `uint32` | ESP32 eFuse MAC 派生 ID |
| sessionId | `uint16` | 单条语音或互动会话 ID |
| sequence | `uint16` | 音频分片序号 |
| totalBytes | `uint32` | 完整语音字节数 |
| payloadBytes | `uint16` | 当前 Payload 长度 |

语音序列：

```text
VoiceStart(totalBytes)
Audio(sequence=0, payload=...)
Audio(sequence=1, payload=...)
...
VoiceEnd(totalBytes)
```

接收端允许 Audio 乱序到达并忽略重复分片。只有全部字节到齐并收到 `VoiceEnd` 才加入未读队列或写入 WAV。

`Nudge` 只有一个字节 Payload，用于 LifeTime `TOGETHER` 互动，不占用语音会话缓冲。

## 两种传输

### ESP-NOW

- 广播发送，近距离、无需路由器。
- Group ID 和 Sender ID 在应用层过滤。
- 两台设备必须使用同一 Wi-Fi channel。
- 当前没有应用层加密和确认重传。

### MQTT

- 所有协议帧作为 MQTT Binary Payload 发布到同一 Topic。
- 两台设备订阅同一 Topic，并忽略自身 Sender ID。
- 当前 QoS 0；网络断开会重发整条待发语音，但不会逐分片 ACK。
- Recorder 订阅同一 Topic，不参与设备之间的实时转发逻辑。

## 主机测试

```sh
./scripts/run_host_sim.sh
```

`host/voice_protocol_sim.cpp` 验证协议和双设备行为，`host/recorder_sim.py` 验证 Python Recorder 可以生成 8 秒 WAV。硬件麦克风、扬声器、屏幕、IMU 和无线射频仍需实机测试。
