# Third-party notices

- NOTE4C pin mapping and native SSD2683 command sequence are derived from the MIT-licensed manufacturer-linked reference, [LazyYoun/youn-ink-fourcolor-firmware](https://github.com/LazyYoun/youn-ink-fourcolor-firmware/tree/2bp), branch `2bp`, commit `8907e59300fde3b77a385dba9f3070b0de17b4c8`. Root copyright: 2026 macheng2017. Firmware-subtree notices: 2025 Shenzhen Xinzhi Future Technology Co., Ltd. and Project Contributors. Both complete license texts are in `third_party/`.
- ESP-IDF 5.5.1, Espressif, primarily Apache-2.0. The binary is linked with the pinned IDF release and its component licenses. Source: https://github.com/espressif/esp-idf/tree/v5.5.1 .
- `joltwallet/littlefs` 1.20.2, a LittleFS ESP-IDF wrapper, MIT; bundled LittleFS, BSD-3-Clause. Source: https://github.com/joltwallet/esp_littlefs . Locked by `dependencies.lock`.
- DejaVu Sans and DejaVu Sans Mono: bitmap glyphs generated from installed DejaVu fonts; complete font notice in `third_party/DejaVu-LICENSE.txt`.
- Noto Sans CJK: basic CJK bitmap glyphs generated from Noto Sans CJK Regular (Simplified Chinese face), SIL Open Font License 1.1. Complete notice in `third_party/Noto-CJK-LICENSE.txt`. The raster subset is embedded without modifying or redistributing the original font file.
- PCF8563 register behavior is implemented from the [NXP PCF8563 data sheet](https://www.nxp.com/docs/en/data-sheet/PCF8563.pdf), including burst access, voltage-low detection and calendar validation.

- SSD2683 fast waveform and setup from ZECTRIX Lab’s MIT-licensed NOTE4 reference demo, commit `ca285c98ed0641f86780edb1f5ec77b0335fe649`. License in `third_party/ZECTRIX-NOTE4-DEMO-LICENSE.txt`.
