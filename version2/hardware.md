# Hardware — ESP32-C6-LCD-1.3

Last updated: 2026-04-21
Source of truth for `pip/version2`. Referenced by `spec.md`.

---

## 1. 板子事实

| 项 | 事实 |
|---|---|
| 板子 | Waveshare ESP32-C6-LCD-1.3（1.3 寸板屏一体，加焊排针版） |
| 主控 | **ESP32-C6FH4** |
| 架构 | RISC-V 32 位单核 |
| 主频 | 160 MHz |
| 内存 | **320 KB SRAM（无 PSRAM）**，4 MB 叠封 Flash |
| 屏幕 | **ST7789V2**，4 线 SPI，240×240，262K 色，IPS |
| 无线 | Wi-Fi 6 + BLE 5 + Zigbee（本项目只用 Wi-Fi） |
| 供电 | Type-C USB 5V，板载 3.3V LDO |
| 板载外设 | BOOT 键、RST 键、Type-C、MicroSD 卡槽、陶瓷天线 |
| 尺寸 | 26.25 × 29.42 × 8.95 mm |

**重要硬约束：无 PSRAM。** 240×240 RGB565 framebuffer 约 112 KB，占 SRAM 1/3。大图缓存和帧序列动画要量力而行；Arduino_GFX 可分区域绘制，不强依赖整屏 buffer。

---

## 2. 引脚定义（对外 15 pin）

LCD 的 SPI 在板内固定布线，**不占对外引脚**。外部可用引脚按物理布局：

### 左侧 9 pin（Type-C 朝上，板子左边）

| # | 标注 | 可用功能 | 备注 |
|---|---|---|---|
| 1 | 5V | 电源输入/输出 | |
| 2 | GND | 地 | |
| 3 | 3V3(OUT) | 3.3V 输出 | 给外设供电 |
| 4 | IO1 | UART / PWM / I2S / **ADC** / I2C / **SPI** | 唯一能做 ADC 和 SPI 的区段 |
| 5 | IO2 | UART / PWM / I2S / **ADC** / I2C / **SPI** | 同上 |
| 6 | IO3 | UART / PWM / I2S / **ADC** / I2C / **SPI** | 同上 |

### 右侧 6 pin

| # | 标注 | 可用功能 | 备注 |
|---|---|---|---|
| 1 | IO12 | I2C / I2S / PWM / UART | 数字 IO，适合按钮 |
| 2 | IO13 | I2C / I2S / PWM / UART | 同上 |
| 3 | IO16 | I2C / I2S / PWM / UART | 同上 |
| 4 | IO17 | I2C / I2S / PWM / UART | 同上 |
| 5 | IO20 | I2C / I2S / PWM / UART | 同上 |
| 6 | IO23 | I2C / I2S / PWM / UART | 同上 |

**策略：** 左侧 IO1–IO3 稀缺（只有这三根带 SPI/ADC），**留给未来 NFC**；按钮这种数字输入放右侧。

---

## 3. 本项目引脚分配

### 3.1 LCD 内部 pin（板内固定，不在对外 header 上）

**来源：** Waveshare demo zip `Arduino/examples/LVGL_Arduino/Display_ST7789.h`

| 信号 | GPIO |
|---|---|
| SCLK | **7** |
| MOSI | **6** |
| MISO | 5（屏幕只写不读，通常忽略）|
| CS | **14** |
| DC | **15** |
| RST | **21** |
| BL | **22**（可 PWM 调光，ledcAttach）|

**⚠️ 易错点：** SCLK=7 / MOSI=6 与直觉顺序相反（低号给 MOSI 不是 SCLK）。我初版 sketch 曾写反这两个，导致屏幕全黑无报错。**始终以 Waveshare 源码为准**。

### 3.2 外部可用 pin（业务使用）

| 用途 | GPIO | 阶段 | 备注 |
|---|---|---|---|
| 配对按钮 | **IO12**（推荐） | Phase 1 | 上拉到 3V3，按下接 GND，固件 `pinMode(INPUT_PULLUP)` |
| NFC SDA（I2C）| IO1 | Phase 2 | PN532 走 I2C 模式 |
| NFC SCL（I2C）| IO2 | Phase 2 | 同上 |

未用引脚保持开路。

---

## 4. 开发环境

**Arduino IDE 路线**（本项目选定）。

### 4.1 Arduino-ESP32 Core

- 安装方式：Arduino IDE → Boards Manager → 搜索 `esp32` by Espressif Systems
- **指定版本：3.0.1**（官方验证过的版本）
- 偏离 3.0.1 可能遇到 C6 编译或烧录问题；不要图新追 3.x 最新版

### 4.2 必装库

多数从 Waveshare 官方 demo zip 随包提供，**但 Arduino_GFX 必须升级**（见下）。

| 库 | 版本 | 来源 | 用途 |
|---|---|---|---|
| Arduino_GFX | **≥1.6.2**（本项目用 1.6.5） | **GitHub 最新**，不是 demo zip | 图形核心（本项目主力） |
| Adafruit_GFX_Library | 1.11.9 | Waveshare demo zip | 部分官方 demo 依赖 |
| LVGL | 8.3.10 | Waveshare demo zip | 官方 demo 01 依赖；**本项目业务代码不用 LVGL** |
| ArduinoJson | 6.21.2 | Waveshare demo zip | |
| FastLED | 3.10.3 | Waveshare demo zip | |
| JPEGDEC | 1.6.1 | Waveshare demo zip | |
| PNGdec | 1.0.2 | Waveshare demo zip | |
| Time | 1.6.1 | Waveshare demo zip | |
| TJpg_Decoder | 1.0.8 | Waveshare demo zip | |

**⚠️ 重要坑位：Arduino_GFX 1.4.9 不支持 ESP32-C6**

Waveshare demo zip 里打包的 Arduino_GFX 是 **1.4.9**——这个版本不支持 C6（编译会报 `fatal error: soc/dport_reg.h: No such file or directory`，因为 C6 的 ESP-IDF v5.1 已删除该头文件）。

Waveshare demo 本身能跑，是因为它走 `Arduino_DriveBus`（Waveshare 自研的 C6 友好总线封装），绕开了 Arduino_GFX 旧版的 C6 不兼容问题。但 **Arduino_GFX 官方在 v1.6.0 加入了 C6 原生支持**，v1.6.2 加入了 C6 开发板 device declaration——对我们不用 LVGL 的业务代码来说，**直接升级 Arduino_GFX 到最新版**比走 DriveBus 路线更简洁。

**正确装法：**

```bash
cd ~/Documents/Arduino/libraries/
# 1. 先从 Waveshare demo zip 装全部 10 个库（里面的 Arduino_GFX 1.4.9 会被覆盖）
cp -R <zip-extracted>/Arduino/libraries/* ~/Documents/Arduino/libraries/
# 2. 用 GitHub 最新版替换 Arduino_GFX
rm -rf ~/Documents/Arduino/libraries/Arduino_GFX
git clone --depth 1 https://github.com/moononournation/Arduino_GFX.git
```

**⚠️ 第二个坑：Arduino_GFX 1.6.5 的 QSPI 文件与 Arduino-ESP32 3.0.4 的 IDF 版本错配**

升级到 1.6.5 后编译 sketch 时，会遇到：

```
Arduino_GFX/src/databus/Arduino_ESP32QSPI.cpp:58:23:
  error: 'ESP_INTR_CPU_AFFINITY_AUTO' was not declared in this scope
```

**根因：** Arduino_GFX 1.6.5 用 `ESP_ARDUINO_VERSION_MAJOR >= 3` 判断是否启用 `.isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO`，但这个条件过于宽松——**`ESP_INTR_CPU_AFFINITY_AUTO` 是 ESP-IDF v5.3+ 引入的，而 Arduino-ESP32 3.0.4 底层是 IDF v5.1**。C6 平台上符号根本不存在。不是 C6 特有，是整个 Arduino-ESP32 3.0.x 线都会中招，只是其他变体的 sketch 可能没触发这个编译单元。

**修复：** 在 `~/Documents/Arduino/libraries/Arduino_GFX/src/databus/Arduino_ESP32QSPI.cpp` 第 55 行附近，把：

```cpp
#if (!defined(ESP_ARDUINO_VERSION_MAJOR)) || (ESP_ARDUINO_VERSION_MAJOR < 3)
    // skip this
#else
        .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
#endif
```

改为：

```cpp
#if (!defined(ESP_ARDUINO_VERSION_MAJOR)) || (ESP_ARDUINO_VERSION_MAJOR < 3) || (ESP_IDF_VERSION_MAJOR < 5) || (ESP_IDF_VERSION_MAJOR == 5 && ESP_IDF_VERSION_MINOR < 3)
    // skip this — .isr_cpu_id unavailable in IDF <5.3
#else
        .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
#endif
```

**这个 patch 会在下次更新 Arduino_GFX 时被覆盖**，所以每次更新后要重新打。建议：
1. 在 patch 行旁用 `// Patch (YYYY-MM-DD)` 注释标记
2. 如果 upstream 采纳了更严格的版本守卫（可能在 1.6.6+），可以移除本地 patch
3. 或者考虑把 `Arduino_GFX/` 用 git submodule + 自己的 fork 管理

**⚠️ 第三个坑：Arduino_GFX 1.6.5 的 U8g2 字体分支有个 typo**

只在启用 U8g2 字库支持（sketch 里 `#include <U8g2lib.h>` 之后）时才会触发：

```
Arduino_GFX/src/Arduino_GFX.cpp:1952:56:
  error: 'u' was not declared in this scope
```

**根因：** `Arduino_GFX.cpp:1952` 上游 typo —— `(u >= _min_text_y)` 里的 `u` 应该是 `y`（上一行 1949 只 declare 了 `x` / `y`，根本没 `u`）。这段代码被 `#if defined(U8G2_FONT_SUPPORT)` 守卫，只有 U8g2 激活时才编译，所以 moononournation 自己的 CI 可能没覆盖到。

**修复：** 把 `Arduino_GFX.cpp:1952` 的：

```cpp
if ((x <= _max_text_x) && (y <= _max_text_y) && (u >= _min_text_y))
```

改为：

```cpp
if ((x <= _max_text_x) && (y <= _max_text_y) && (y >= _min_text_y)) // Patch (2026-04-21): upstream typo u -> y
```

和第二个坑同理，升级库后会被覆盖，重新打即可。

**⚠️ 第四个坑：U8g2 字库符号需要 include 顺序正确才能识别**

编译报 `'u8g2_font_unifont_h_chinese4' was not declared in this scope` 或 `class Arduino_GFX has no member named 'setUTF8Print'`。

**根因：** `Arduino_GFX.h:20` 用 `#if __has_include(<U8g2lib.h>)` 检查 U8g2 是否可用，触发时才 define `U8G2_FONT_SUPPORT` 宏并 include 一堆 `u8g2_font_*.h` 字库。Arduino IDE 只有在 sketch 里看到某个库的 include 之后才会把那个库的目录加到 `-I` 搜索路径 —— 所以 **`#include <U8g2lib.h>` 必须在 `#include <Arduino_GFX_Library.h>` 之前**，否则 `__has_include` 查找时 U8g2 还不在搜索路径里。

**修复：** sketch 顶部的 include 顺序：

```cpp
#define U8G2_USE_LARGE_FONTS     // 启用大号 Unicode BMP 字库（必须在任何 include 之前）
#include <U8g2lib.h>             // 必须先 —— 让 Arduino IDE 把 U8g2 目录加入 include path
#include <Arduino_GFX_Library.h> // 然后 —— 里面的 __has_include 才能查到 U8g2
#include <ArduinoJson.h>
```

**⚠️ 第五个坑：C 的 `\xHH` 转义贪婪吞并后续 hex 字符（最隐蔽的一个）**

这是 `scripts/sprites_to_header.py` 里埋的编译期 bug —— 表现为设备上 sprite 身体大片"不渲染"，但**实际是 sprite 数据在编译时就被 GCC 截断了**。

**症状：** 某些 sprite frame 数据在设备端 PROGMEM 里**在某个字节后全是 0x00**（正常应该继续几百字节）。例如 `female_4_0 frame 0` 从 row 2 开始 Serial log 显示 `p=57 next_byte=0x00` 就停止。

**根因：** C11 §6.4.4.4 规定 `\xHH...` 转义是**贪婪**的 —— 一路吃下所有十六进制字符。所以这段生成的字符串：

```c
static const char spr[] PROGMEM = "...\xe2\x97\x86" "00" "...";
//                                 ↑ 正确：用 "" 断开
// 如果写成：
static const char spr[] PROGMEM = "...\xe2\x97\x8600...";
//                                 ↑ BUG：\x8600 → 34304 → truncated 到 0x00，string 被截断
```

sprites.json 里的字符 `◆`（U+25C6 = `\xe2\x97\x86`）+ 紧接的 ASCII `0` 在编译期会被合并成一个数字极大的 `\x8600` 转义 → 截断到 0x00 → 后面所有字节都在源码里但 C 字符串逻辑长度在此终止。

**修复：** 在 `sprites_to_header.py` 的 `c_escape_utf8()` 里，每次 `\xNN` 后面紧跟 ASCII 十六进制字符（0-9, a-f, A-F）时插入字符串分隔 `" "`：

```python
if last_was_hex_escape and ch and ch in HEX_CHARS:
    out.append('" "')  # force compiler to end the escape
```

`"\xe2\x97\x86" "00"` 和 `"\xe2\x97\x8600"` 在**运行时值完全相同**（adjacent string literals 会 concat），但编译器不会再把 `00` 当作转义的一部分。

**教训：** 任何时候生成 C 字符串字面量，**如果非 printable 字节用 `\xHH` 编码 + 紧跟 ASCII 可打印字符**，就必须检查 "下一个字符是不是 hex" 并插入分隔。这不是 Python、不是 GCC bug —— 是 C 标准定义的行为。最坑的是**从 `.bin` 或 `hexdump` 看源码似乎是对的**，问题只在 preprocessor 解析那一瞬间发生。调试这类问题需要**让设备把 PROGMEM 原始字节 dump 出来**，而不是只看渲染结果。

**LVGL 版本锁死的硬道理：** LVGL v8 和 v9 的驱动 API 不兼容，官方 demo 基于 v8.3.10 写死；你若随手升 v9 会编译失败。整个库表按这个版本号装就能稳定复现。

### 4.3 Demo zip

- 下载：`https://files.waveshare.net/wiki/ESP32-C6-LCD-1.3/ESP32-C6-LCD-1.3_Demo.zip`
- 三个 demo：
  - `01_LVGL_Arduino` — 硬件信息屏（Flash/SD/WiFi/BLE 扫描），**用于冒烟测试**
  - `02_LVGL_WeatherClock` — 天气时钟，演示 LVGL 流畅性
  - `03_Video_demo` — SD 卡视频播放

---

## 5. 点屏冒烟测试（写任何业务代码前必须先跑）

**风险等级：~~高~~ 已验证（2026-04-21）。** 板子出厂预烧了 `01_LVGL_Arduino` demo，插 USB 即可看到屏幕显示 Onboard parameter 信息（SD Card / Flash Size / Wireless scan W/B 结果）。验收全部通过：屏幕显色正常、Flash 4MB 正确、Wi-Fi + BLE 扫描 OK。

**这意味着硬件栈（MCU + 屏 + 射频 + 出厂固件）全绿灯**。下面的步骤只在需要重新烧录业务代码时再走。

### 5.0 零成本验证（推荐先做）

**只需一根 USB-C 线**：

1. 插 USB-C 到电脑或充电器
2. 屏幕应该亮起，显示 Waveshare 出厂 demo（Onboard parameter 页面）
3. 能看到 Flash Size 4 MB 和 Wireless scan 有结果 → 硬件全正常

**如果这步失败**（屏幕不亮/不显示内容），再考虑是线的问题还是板子的问题，不要急着装 Arduino IDE。

### 5.1 步骤（重新烧录业务代码时走这里）

1. 装 Arduino IDE，加入 ESP32 board URL：`https://espressif.github.io/arduino-esp32/package_esp32_index.json`
2. Boards Manager → 搜 `esp32` → 装 **3.0.1**
3. 解压 Waveshare demo zip
4. 把 zip 里 `Arduino/libraries/` 下的所有文件夹复制到 `~/Documents/Arduino/libraries/`
5. Arduino IDE 打开 `01_LVGL_Arduino.ino`
6. 选板：在 Boards Manager 的 ESP32 板列表里选 `ESP32C6 Dev Module`（官方版本无 1.3 专用条目）
7. Port：插 Type-C → 选对应 `/dev/cu.usbmodem*`
8. 编译 → 烧录
9. 屏幕应该显示 Flash/SD/WiFi/BLE 扫描信息

### 5.2 验收标准

- [ ] 屏幕亮、颜色对、不闪、不偏色
- [ ] 编译无 error、烧录成功、serial monitor 有输出
- [ ] Demo 的所有信息都显示出来

任一项不通过，**先解决再写业务代码**。

### 5.3 已知坑位

- **v1 的 ST7789 驱动不要复用** — `pip/version1/Arduino_ST7789.cpp` 和 `Adafruit-GFX-Library-master/` 是为 Xtensa ESP32 DEVKIT V1 + 裸屏写的，C6（RISC-V）+ 官方总线封装完全不兼容
- **Core 版本敏感** — 3.0.0 及更早没有 C6 支持；3.0.1 是官方验证过的；3.0.3+ 偶有 flash 分区 bug 报告
- **Arduino_DriveBus 不在 Library Manager 里** — 只能从 demo zip 抽出来手动放
- **官方教程截图用的是 ESP32-S3-Zero**，不是 C6-LCD-1.3。引脚图别照抄截图
- **USB CDC on boot** — 如果 serial monitor 没输出，检查 Tools → USB CDC On Boot 设置

---

## 6. 参考

- [产品页（英文）](https://www.waveshare.com/esp32-c6-lcd-1.3.htm)
- [官方文档平台 - ESP32-C6-LCD-1.3](https://docs.waveshare.net/ESP32-C6-LCD-1.3/)
- [Working with Arduino 子页](https://docs.waveshare.net/ESP32-C6-LCD-1.3/Development-Environment-Setup-Arduino)
- [Demo zip 下载](https://files.waveshare.net/wiki/ESP32-C6-LCD-1.3/ESP32-C6-LCD-1.3_Demo.zip)
- [同系列 1.47 Wiki（结构可参照）](https://www.waveshare.com/wiki/ESP32-C6-LCD-1.47)
