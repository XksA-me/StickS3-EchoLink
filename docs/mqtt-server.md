# MQTT 服务器部署

仓库提供 Mosquitto Broker 和 Python Recorder 的 Docker Compose 方案。Broker 负责实时转发，Recorder 订阅同一 Topic，将完整语音重组为 WAV。

## 架构

```text
StickS3 A -> Wi-Fi -> MQTT Broker -> StickS3 B
                              \----> Recorder -> WAV + index.jsonl
```

MQTT 消息使用 QoS 0 且不 retained。Broker 不保存聊天历史；长期数据由 Recorder 文件目录保存。

## 服务器要求

- 一台可被设备访问的 Linux 主机
- Docker Engine 与 Docker Compose Plugin
- 开放 TCP `1883`（测试）或 `8883`（推荐 TLS）
- 建议至少 1 GB 内存和可监控的磁盘空间

## 1. 上传项目

```sh
git clone https://github.com/XksA-me/StickS3-EchoLink.git
cd StickS3-EchoLink
```

## 2. 创建环境文件

```sh
cp .env.example .env
```

编辑 `.env`：

```dotenv
MQTT_USER=echolink
MQTT_PASSWORD=replace-with-a-long-random-password
MQTT_TOPIC=echolink/v1/voice
```

`.env` 已被 Git 忽略，不要提交真实密码。

## 3. 创建 Mosquitto 密码文件

```sh
mkdir -p server/recordings
docker run --rm -it \
  -v "$PWD/server:/work" \
  eclipse-mosquitto:2 \
  mosquitto_passwd -c /work/passwords echolink
```

交互输入的密码必须与 `.env` 中的 `MQTT_PASSWORD` 一致。`server/passwords` 已被 Git 忽略。

## 4. 启动

```sh
docker compose up -d --build
docker compose ps
```

正常情况下会看到：

- `echolink-mqtt`
- `echolink-recorder`

查看日志：

```sh
docker logs -f echolink-mqtt
docker logs -f echolink-recorder
```

## 5. 防火墙

Ubuntu UFW 示例：

```sh
sudo ufw allow 1883/tcp
sudo ufw status
```

云厂商安全组也必须允许相同端口。只开放给可信 IP 或 VPN 会更安全。

## 6. 配置 StickS3

在每台设备的配置网页填写：

| 字段 | 内容 |
| --- | --- |
| MQTT host | 服务器域名或 IP，不要包含 `mqtt://` |
| MQTT port | `1883` |
| MQTT user | `.env` 中的用户 |
| MQTT password | Mosquitto 密码文件中对应密码 |
| Topic | 默认 `echolink/v1/voice` |
| Transport | `WIFI` |

两台设备可连接不同 Wi-Fi，但 Broker、Topic 和固件 Group ID 必须一致。

## 录音存储

```text
server/recordings/YYYY/MM/DD/*.wav
server/recordings/index.jsonl
```

`index.jsonl` 每行包含文件名、UTC 时间、Group ID、Sender ID、Session ID、时长和字节数。Recorder 只保存分片完整且收到 `VoiceEnd` 的消息。

查看最近记录：

```sh
find server/recordings -type f -name '*.wav' | tail
tail -n 20 server/recordings/index.jsonl
```

## 更新与备份

```sh
git pull
docker compose up -d --build
```

备份示例：

```sh
tar -czf echolink-recordings-$(date +%F).tar.gz server/recordings
```

停止服务：

```sh
docker compose down
```

不要随意使用 `down -v`，它会删除 Mosquitto 命名卷。

## 安全说明

仓库默认配置的 `1883` 只有用户名/密码认证，没有 TLS。MQTT 凭据和音频内容可能被链路监听，适合内网、VPN 或开发测试，不适合直接长期暴露公网。

正式部署建议：

1. 使用域名和可信 CA 证书。
2. Mosquitto 监听 `8883` 并启用 TLS。
3. 固件改用 `WiFiClientSecure` 并校验证书。
4. 按 Group/设备限制 Topic ACL。
5. 关闭公网 `1883`。
6. 增加磁盘配额、保留周期、监控和自动备份。

当前固件尚未实现 TLS 客户端，升级 TLS 需要同步修改固件网络层。
