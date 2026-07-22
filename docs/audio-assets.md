# Audio assets

The LifeTime wooden-fish feedback uses one embedded short sample:

- Source: [Woodblock-soft.wav by hollandm](https://freesound.org/people/hollandm/sounds/692828/)
- License: [Creative Commons Zero 1.0](https://creativecommons.org/publicdomain/zero/1.0/)
- Source description: a single soft wooden stick hit against a woodblock
- Firmware format: signed 8-bit PCM, mono, 16 kHz
- Firmware size: 8,000 bytes

The repository contains the converted file at `assets/audio/woodblock-soft-cc0.raw` and the generated include at `include/generated/woodblock_soft_cc0.h`. If a more authentic temple wooden-fish recording is added later, keep the replacement public-domain or CC0 and update this file with the exact source and conversion command.

Conversion command used for the current asset:

```sh
ffmpeg -i source.wav -ac 1 -ar 16000 -f s8 assets/audio/woodblock-soft-cc0.raw
xxd -i assets/audio/woodblock-soft-cc0.raw > include/generated/woodblock_soft_cc0.h
```
