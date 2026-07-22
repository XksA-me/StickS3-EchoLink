# Contributing

感谢参与 StickS3 EchoLink。提交改动前请先说明问题、硬件版本和使用的传输模式，避免不同 StickS3 批次或网络条件造成误判。

## 开发流程

1. Fork 仓库并从 `master` 创建分支。
2. 保持改动聚焦，不提交 `.pio/`、`lib/`、密码、Wi-Fi 信息或录音。
3. 更新相关文档和测试。
4. 运行主机测试和固件构建。
5. 提交 Pull Request，写清实机验证结果。

```sh
./scripts/run_host_sim.sh
pio run -e sticks3
```

涉及音频、显示、IMU、ESP-NOW 或电源管理的改动必须说明是否在 M5StickS3 实机测试。主机模拟不能替代硬件验证。

## 代码约定

- 固件使用 C++11/Arduino 兼容写法。
- 避免在 Arduino 主循环执行可能长期阻塞的网络操作。
- 大型音频和画布缓冲优先使用 PSRAM。
- UI 动画需要限制刷新率，静态页面按脏标记更新。
- 修改 Voice Protocol 时同步更新 C++、Python Recorder、主机模拟和文档。

## 安全

不要在 Issue、PR、日志或截图中提交真实 Wi-Fi、MQTT、SSH 凭据、服务器地址或私人录音。安全问题请优先邮件联系 `pythonbrief@163.com`，不要先公开利用细节。
