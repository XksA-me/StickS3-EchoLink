# 音频资源与许可

LifeTime 的木鱼反馈使用一段内置短音频：

- 来源：[hollandm 的 Woodblock-soft.wav](https://freesound.org/people/hollandm/sounds/692828/)
- 许可：[Creative Commons Zero 1.0（CC0）](https://creativecommons.org/publicdomain/zero/1.0/)
- 原始说明：木棒敲击木块的一声短音
- 固件格式：有符号 8 位 PCM、单声道、16 kHz
- 固件占用：8000 字节

仓库中保存了转换后的 `assets/audio/woodblock-soft-cc0.raw`，以及编译时使用的 `include/generated/woodblock_soft_cc0.h`。如果以后替换为更接近寺庙木鱼的录音，仍应选择公共领域或 CC0 资源，并在本文档中记录准确的来源和转换方式。

当前资源的转换命令：

```sh
ffmpeg -i source.wav -ac 1 -ar 16000 -f s8 assets/audio/woodblock-soft-cc0.raw
xxd -i assets/audio/woodblock-soft-cc0.raw > include/generated/woodblock_soft_cc0.h
```
