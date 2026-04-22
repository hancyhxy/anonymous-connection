# Firmware

设备端代码，目标板 **Waveshare ESP32-C6-LCD-1.3**。

---

## 目录结构

```
firmware/
├── README.md                   # 本文件：环境搭建 + 烧录步骤
└── smoke_test/
    └── smoke_test.ino          # 第一个 sketch：Hello + 每秒跳动计数器
```

未来会加入：
```
firmware/
├── avatar_display/             # 把 avatar-studio 导出的位图画在屏上
├── qrcode_pairing/             # QR 码 + WebSocket 配对逻辑
└── main/                       # 整合业务 sketch
```

---

## 1. 第一次设置（一次性工作）

### 1.1 安装 Arduino-ESP32 Core（**必须 3.0.1**）

1. Arduino IDE → **Preferences** → **Additional Boards Manager URLs**
   加入：
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
2. **Tools → Board → Boards Manager**
3. 搜 `esp32`，找到 "esp32 by Espressif Systems"
4. 在版本下拉选 **3.0.1**（不是 latest）
5. 点 Install，等大约 200MB 下载完

**为什么是 3.0.1：** Waveshare 官方文档指定这个版本。3.0.0 没有 C6 支持；3.0.3+ 偶有 flash 分区 bug 报告。别追新。

### 1.2 下载 Waveshare Demo zip 并抽库

1. 下载：`https://files.waveshare.net/wiki/ESP32-C6-LCD-1.3/ESP32-C6-LCD-1.3_Demo.zip`
2. 解压后找到 `Arduino/libraries/` 目录
3. **把这个目录下所有文件夹**复制到：
   - macOS：`~/Documents/Arduino/libraries/`
4. 复制后你的 libraries 目录里应该多出这些：
   - `Arduino_GFX` (v1.4.9)
   - `Adafruit_GFX_Library` (v1.11.9)
   - `lvgl` (v8.3.10)
   - `Arduino_DriveBus`
   - `ArduinoJson`, `FastLED`, `JPEGDEC`, `PNGdec`, `Time`, `TJpg_Decoder`
5. **重启 Arduino IDE**（不重启的话新库不会被扫到）

**为什么不从 Library Manager 装：** Arduino_DriveBus 只存在于 Waveshare demo zip；其他库在 Library Manager 上有更新版本但和官方 demo 的版本组合不兼容。

### 1.3 配置板子和串口

连接板子后：

1. **Tools → Board → esp32 → "ESP32C6 Dev Module"**（没有 1.3 专用条目，这是最接近的）
2. **Tools → Port** → 选 `/dev/cu.usbmodem*`（Mac）
3. **Tools → USB CDC On Boot** → **Enabled**（不然 `Serial` 输出看不到）
4. **Tools → Flash Size** → **4MB (32Mb)**
5. 其他项保持默认

---

## 2. 烧录 smoke_test

1. Arduino IDE → **File → Open** → 选 `smoke_test/smoke_test.ino`
2. 按 ✓（Verify）编译一次。**第一次编译会慢（~2 分钟）**，因为要编译整个 ESP32 框架
3. 编译无 error 后按 → (Upload) 烧录
4. 烧录完成后屏幕应该显示：
   - 顶部一行 `Hello, I'm XinYi`
   - 中间一个大数字，每秒 +1
5. 打开 Serial Monitor（115200 baud）应该看到 `tick: 0, 1, 2, ...`

### 2.1 验收标准

三件事同时满足即通过：

- [ ] 屏幕显示 Hello + 大数字
- [ ] 数字每秒跳动
- [ ] Serial Monitor 能看到对应的 log

三件同时满足 = **Arduino 环境 + C6 烧录链路 + Arduino_GFX 绘图 + 主循环刷新**全部通过 → 可以开始写业务代码。

### 2.2 如果显示不对

smoke_test.ino 顶部有 10 行集中的 `#define`，是 LCD 初始化参数。出厂 demo 用的正确参数在 demo zip 的源码里——我写的默认值基于 Arduino_GFX 的 C6 通用示例，有可能和 Waveshare 板子实际参数有 1–2 处偏差。

**对齐方法：**

1. 解压 Waveshare Demo zip
2. 打开 `Arduino/examples/01_LVGL_Arduino/` 下的主 .ino 文件
3. 找类似这段的代码：
   ```cpp
   Arduino_DataBus *bus = new Arduino_ESP32SPI(7 /* DC */, 14 /* CS */, 6 /* SCK */, 5 /* MOSI */, ...);
   Arduino_GFX *gfx = new Arduino_ST7789(bus, 21 /* RST */, 0 /* rotation */, true /* IPS */, 240, 240, 0, 0, 0, 0);
   ```
4. 把里面的 GPIO 号（DC/CS/SCK/MOSI/RST）复制到 smoke_test.ino 顶部对应的 `#define`
5. 重新编译烧录

### 2.3 常见问题

- **编译报 "esp32c6 not found"** → Core 版本不对，回 §1.1 确认装的是 3.0.1
- **编译通过但烧不进去** → 按住 BOOT 键不放，同时按一下 RST，松开 RST 两秒后松 BOOT，再烧录
- **屏幕全白/全黑** → LCD 初始化参数不对，按 §2.2 对齐
- **显示颜色反了（红变蓝）** → 找 `#define RGB_ORDER`，改 BGR ↔ RGB
- **显示上下颠倒** → 找 `#define ROTATION`，0/1/2/3 循环试

---

## 3. 下一步

smoke_test 通过后，下一个 sketch 建议做：

1. **按钮输入测试**：接一个按钮到 IO12，按下时屏幕变色——验证 `pinMode(INPUT_PULLUP)` + 中断
2. **WiFi 连接测试**：硬编码 SSID/密码，连上后 Serial 打印 IP——验证射频业务栈
3. **WebSocket client**：连一个公开 echo server，发送/接收——验证 Phase 1 主通信链路

这三步都通过后，再上业务代码（画 avatar、QR 码、配对动画）。
