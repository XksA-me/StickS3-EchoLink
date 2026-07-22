# LifeTime 功能说明

LifeTime 是 EchoLink 固件中的本地功能应用。按 `B` 在 `BATTERY`、`HOURGLASS`、`TOGETHER` 三个页面之间切换，按 `A` 执行当前页面的操作。

## 木鱼

在 `BATTERY` 页面短按 `A`，会增加持久化功德计数，播放一段木块敲击声，并显示短暂上浮的 `功德 +1` 字效。动画结束后页面恢复安静状态，不会持续刷新屏幕。

当前内置音频是 [hollandm 制作的 Woodblock-soft.wav](https://freesound.org/people/hollandm/sounds/692828/)。原页面说明这是木棒敲击木块的一声短音，并标记为 **Creative Commons 0（CC0 1.0）**。转换后的原始采样位于 `assets/audio/woodblock-soft-cc0.raw`，生成的 C 头文件位于 `include/generated/woodblock_soft_cc0.h`。

## 沙漏

屏幕中的沙漏固定为一个竖直方向，不会随着设备旋转而改变 UI 方向。设备首次在某个面稳定放置 1 秒后，该面成为初始面；翻到相对面后进入回流模式：

| 设备状态 | 模式 | 含义 |
| --- | --- | --- |
| 初始面 | `TIME FLOW` | 从 2 分钟开始倒计时 |
| 相对面 | `TIME REWIND` | 从 0 开始正计时，沙子向上回流 |

按 `A` 开始或暂停。运行中翻到相对面会保留当前时间比例，并改变时间流动方向。左右晃动时，海洋色沙层会产生横向摆动和波峰高光。

默认时长由 [LifeTimeConfig.h](../include/LifeTimeConfig.h) 中的 `LIFETIME_FACE_DURATION_SECONDS[0]` 配置，目前为 120 秒。其他五个旧方向配置仍保留，以兼容旧配置，但不再用于旋转 UI 或改变时长。

## 刷新与功耗

沙漏运行或晃动时最多每 450 ms 提交一帧；暂停且设备静止时不自动刷新。沙漏运行时 IMU 每 100 ms 采样，静止时降为每 320 ms；`TOGETHER` 页面使用 180 ms 采样。电池页每 60 秒读取一次电池数据，木鱼字效只在点击后短时间刷新。

更完整的功耗说明见 [功耗设计](power-management.md)。
