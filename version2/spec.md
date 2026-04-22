# Spec — Anonymous Avatar Matching Device (v2)

Last updated: 2026-04-21
Scope: Phase 1 MVP (2 devices, button-triggered pairing)

---

## 0. 设计目标

- **两周内交付 2 台设备的高保真 demo**，跑通体验链：扫码 → 设置 avatar + 标签 → 屏幕显示 → 按下按钮 → 两台设备同步播放连接动画
- NFC 留到 Phase 2，本 spec 只覆盖 Phase 1
- 一句话架构：**一个后端服务同时托管静态网页和 WebSocket，两台设备通过 WebSocket 连到后端，后端把配对事件在两台设备之间转发**

---

## 1. 端（三个）

```
┌─────────────┐   HTTP (scan QR)     ┌────────────────┐    WebSocket    ┌──────────────┐
│   Phone     │ ───────────────────▶ │   Backend      │ ◀─────────────▶ │   Device A   │
│ (web page)  │ ◀─────────────────── │  (Node/Python) │                 │  ESP32-C6    │
│             │   POST avatar+tags   │                │ ◀─────────────▶ │              │
└─────────────┘                      │                │    WebSocket    ┌──────────────┐
                                     │                │ ◀─────────────▶ │   Device B   │
                                     └────────────────┘                 │  ESP32-C6    │
                                                                        └──────────────┘
```

### 1.1 Web 端（手机浏览器）

**职责：**
- 扫 QR 码进入，URL 里带 `?device=<device_id>`（二维码由设备屏幕自己生成）
- 让用户挑一个 avatar（从固定候选集里选，先不做自定义上传）
- 让用户勾选兴趣标签（固定候选集，多选）
- 提交到后端：`POST /api/device/<device_id>/profile` with `{ avatar_id, tags[] }`
- 提交成功后显示"设置完成，去找人吧"，页面任务结束

**不做的事：**
- 不做账号 / 登录 / 长期存储
- 不做 avatar 图片上传（Phase 1 只选预设）
- 不接入后端的配对事件流（手机不参与配对）

### 1.2 设备端（ESP32-C6-LCD-1.3）

**职责：**
- 上电后连 Wi-Fi（SSID 硬编码或走 SmartConfig，见 §4）
- 向后端注册 `device_id`（用 MAC 后 4 位），拿到一个配对页面 URL
- 屏幕上画二维码（链接到 Web 端，带 `?device=<device_id>`）
- 通过 WebSocket 监听后端的 profile 更新 → 切到"展示态"：渲染 hello-stranger 风格 avatar + quote 气泡
- 监听按钮 GPIO 中断 → 发送 `{type:"pair_request", from:<device_id>}` 到后端
- 监听后端推来的 `{type:"pair_event"}` → 播放连接动画

**屏幕三种态：**
1. `WAITING_PROFILE` — 显示 QR 码 + 提示文字
2. `SHOWING_PROFILE` — 显示 avatar + quote 气泡
3. `PAIRING` — 播放动画（avatar 互相靠近、合并、停留）

**Avatar 渲染方式（复刻 hello-stranger）：**
- 用 `spriteKey = ${sex}_${color}_${num}${smile?"smile":""}` 从 `sprites.json` 取 18×18 字符网格
- 遍历网格，每个字符查 **8×10 单色位图表**（本项目新建，替代原作浏览器字体渲染）
- 每字符调用 `drawBitmap(x, y, glyph, 8, 10, mainColor)`，其中 `mainColor = darkenColor(bg_hex)`（派生色算法照抄 hello-stranger）
- 动画：每 166 ms 切 `currentFrame = (currentFrame+1) % 3`，只重绘 avatar 区域
- 底色：`fillRect(0, 0, 240, 240, lightenColor(bg_hex))`；`bg_hex = colors.energy[profile.bg_level]`
- Quote 气泡：`drawFastHLine / drawFastVLine` 画 `┌─┐│└┘▼` 边框，`drawString()` 写 quote 文字
- 完整渲染规范见 [design.md §2 + §6](./design.md)

### 1.3 后端（Node 或 Python，静态 + API + WebSocket 三合一）

**职责：**
- 托管 `/`（静态 Web 端资源）
- `POST /api/device/:id/profile` — 接收 Web 端提交的 avatar+tags，存内存 map，通过 WS 推给 `:id` 对应的设备
- `GET /api/device/:id/profile` — 可选，设备可轮询拉取（兜底）
- WebSocket endpoint `/ws?device=:id` — 设备长连接；后端转发两类消息：
  - 收到任一设备的 `pair_request` → 记录"该设备举手中"
  - 两台设备都在举手 → 同时给两边推 `pair_event`，带对方的 profile
- 全部数据放内存，不持久化；重启即清空（MVP 够用）

**可部署到：** 本地 Mac 开发机 + ngrok，或直接跑局域网 IP（demo 场地两台设备和电脑同网即可）

---

## 2. 数据模型（最小集）

```jsonc
// Device profile (in backend memory)
{
  "device_id": "A1B2",              // MAC 后 4 位，hex
  "avatar": {                        // 详细机制见 design.md §2
    "sex":      "male",             // "male" | "female"
    "color":    2,                  // 身型/配色档位 0..4（由 Q_A 驱动）
    "num":      1,                  // 动画子变体 0..2（后端分配时 roll 一次）
    "smile":    false,              // 微笑变体开关
    "bg_level": 3                   // 背景能量档位 0..5（由 Q_B 驱动）
  },
  "quote": "need coffee",           // 自由 ASCII，0-20 字符，可随时改，新增字段
  "ws_connected": true,
  "pair_armed": false,              // 按钮按下后为 true，配对成功或超时清零
  "pair_armed_at": 1714000000       // unix ts
}
```

**Avatar 字段语义**：这五个字段复刻自 hello-stranger 的 `person.svelte` 组件，完整机制见 [design.md](./design.md)。设备端拿到后，用 `spriteKey = ${sex}_${color}_${num}${smile?"smile":""}` 从 sprites.json 索引字符网格；`bg_level` 单独查我们的 `colors.json.energy[bg_level]` 得到底色 hex。

**quote 字段**：本项目相对 hello-stranger 的原创扩展，见 [design.md §5](./design.md#5-新增内容二quote-文字气泡)。

**配对触发规则（Phase 1 简化版）：**
- 任一设备按下按钮 → `pair_armed = true`，10 秒内有效
- 若在 10 秒窗口内 **另一台设备也 armed**，后端推 `pair_event` 给两边
- 超时未配上 → 自动清零，屏幕回到 `SHOWING_PROFILE` 态
- Phase 2 NFC 会用真实的"对方 ID"替代这个时间窗口启发式

---

## 3. Avatar 和 Quote 设计（MVP）

**本节是 design.md 的摘要。完整机制、映射关系、hello-stranger 源码溯源见 [design.md](./design.md)。**

### 3.1 视觉方案：复刻 hello-stranger

本项目**直接复用** The Pudding 的 [Hello Stranger](https://pudding.cool/2025/06/hello-stranger/) 项目（MIT License，原作源码见 [github.com/the-pudding/hello-stranger](https://github.com/the-pudding/hello-stranger)）的：

- **sprite 骨架**（`sprites.json`，10 个 ASCII 角色 × 3 帧动画，完整嵌入）
- **配色 palette**（`colors.json` 的 hex 值，仅 key 名换为我们的新语义）
- **派生色算法**（`darkenColor / lightenColor / darkestColor`，逐行照抄）
- **渲染技术**（手机端 `<pre>` + 等宽字体 + 负字距；设备端用 8×10 位图查表替代）

**不复用**的是 hello-stranger 的数据驱动逻辑——原作用 1431 个真实受访者的 demographic（race/age/politics/edu）决定每个 avatar 的样子，我们用**用户当场回答的选择题**替代。

### 3.2 映射机制

用户通过手机网页回答 3 道题，答案驱动 avatar 视觉：

| 题目 | 档位 | 驱动变量 | hello-stranger 对应维度 |
|---|---|---|---|
| **Q_S** — Pick a body | 2（male/female） | `avatar.sex` | 原 sex 字段 |
| **Q_A** — How do you feel? | 5（chill/curious/playful/tired/glowing） | `avatar.color` + 前景色 | 原 race 驱动的 color + 前景色 |
| **Q_B** — Energy level? | 6（empty→electric） | `avatar.bg_level` → 背景色 | 原 age 驱动的 backgroundColor |

外加两个开关：
- `smile` — 单个 checkbox，选上切 sprite 微笑变体
- `quote` — 自由 ASCII 文本（0-20 字符），**可随时重新编辑**

**题目文案设计原则**：全部是"此刻状态"而非"固有属性"，鼓励用户多次编辑。完整题目定义与每档位继承的 hex 值见 [design.md §4](./design.md#4-新增内容一我们自己设计的选择题)。

### 3.3 组合空间

```
sex (2) × color (5) × num (3) × bg_level (6) × smile (部分 key) ≈ 270 种结构化组合
```

加上自由文本 `quote`，实际独一无二感无限。50 人教室撞款期望 ~4.5 对（仅看结构化字段），加上 quote 后几乎零撞款。

### 3.4 Quote 文字气泡（本项目原创扩展）

hello-stranger 原作没有 quote 层，avatar 是被数据静态定义的。我们新增 quote 作为**用户主动广播**的通道——装置唯一的高频编辑入口。完整规格见 [design.md §5](./design.md#5-新增内容二quote-文字气泡)。

简要：
- 0-20 字符 ASCII，0 即隐藏气泡
- 位置：avatar 上方，用 `┌─┐│└┘▼` 画气泡边框
- 更新：设备端只重绘气泡区，不整屏重画（SRAM 省用）

---

## 4. Wi-Fi 和网络（Phase 1 简化策略）

**默认走"同一 Wi-Fi + 固定后端地址"：**
- 设备固件里硬编码 Wi-Fi SSID/密码和后端地址 `ws://<demo-laptop-ip>:3000/ws`
- demo 时你带自己的路由器或者用手机热点
- 不做 captive portal / SmartConfig（Phase 2 再考虑）

**这意味着：** demo 前要在固件里确认 IP 和 SSID。不是漂亮方案，但两周 MVP 够用。

---

## 5. 屏幕硬件前置验证

**状态：已验证（2026-04-21）。** 板子出厂预烧 demo，插 USB 即亮，显示 Flash 4 MB + Wi-Fi/BLE 扫描 OK。硬件栈（MCU + 屏 + 射频）全部正常。

**完整的硬件规格、引脚定义、Arduino 环境搭建、业务代码烧录步骤、已知坑位见 → [hardware.md](./hardware.md)**

### 5.1 屏幕功能清单（业务需要用到的）

- [ ] 纯色填充 + 清屏（`fillScreen()`）
- [ ] 绘制 8×10 单色位图（`drawBitmap()`，18×18=324 次叠加画 avatar 字符网格）
- [ ] 绘制 ASCII 文本（`drawString()` / `print()`，显示标签和提示文字）
- [ ] 绘制二维码（WAITING_PROFILE 态用 `qrcode` 库）
- [ ] 播放简单帧动画 / 位移（PAIRING 态，每帧擦旧画新位置）

前四项用 Arduino_GFX 都能直接覆盖；动画若过于吃力，可降级为静态成功画面。

---

## 6. 开发顺序（推荐）

严格按这个顺序，前一步不通过不做下一步：

1. **屏幕点亮**（§5.2）— 独立验证，与软件无关
2. **后端骨架**：Node/Express 起一个静态服务 + WS echo，两台设备能连上
3. **Web 端最小可用**：扫 QR → 选 avatar + 选标签 → POST → 看到"成功"
4. **设备显示 profile**：后端 push 后设备屏上画出 avatar + 标签（静态就行）
5. **按钮触发 + 跨设备事件**：两台设备 10 秒内都按 → 后端推 pair_event → 设备切 PAIRING 态（先用一个静态"Matched!"画面）
6. **配对动画**：把 PAIRING 态的静态画面换成真动画
7. （可选）美化、增加更多 avatar/tag、降低延迟

---

## 7. 不在本 spec 范围内（明确排除）

- NFC 识别对方身份（Phase 2）
- 自定义 avatar 上传
- 用户身份持久化
- 多于 2 台设备的配对逻辑（后端代码应该不难扩展到 N，但 demo 场景只做 2）
- 配对历史、好友列表
- 掉线重连的鲁棒性打磨（尽力而为，demo 前测几遍）

---

## 8. 待定项（需要决策后更新本 spec）

- [ ] 后端用 Node 还是 Python —— 建议 Node (Express + ws)，前端 HTML 同源托管更顺
- [ ] demo 环境的网络方案 —— 自带路由器还是手机热点

**已拍板：**
- Avatar 方案：复刻 hello-stranger（完整机制见 [design.md](./design.md)）
- Avatar 素材：直接嵌入 hello-stranger 原作的 `src/data/sprites.json`（MIT License），在本 repo 中位于 `version2/web/sprites.json`
- 题目：3 道（Q_S 身型 / Q_A 心情 / Q_B 能量），见 [design.md §4](./design.md#4-新增内容一我们自己设计的选择题)
- 新增 quote 自由文本字段，见 [design.md §5](./design.md#5-新增内容二quote-文字气泡)
- avatar-studio/ 已降级到 Phase 2（MVP 不做 32 部件手绘）
- 按钮接 IO12（[hardware.md §3](./hardware.md#3-本项目引脚分配)）

---

## 参考

- [ESP32-C6-LCD-1.3 产品页](https://www.waveshare.com/esp32-c6-lcd-1.3.htm)
- [Working with Arduino — Waveshare Docs](https://docs.waveshare.com/ESP32-C6-LCD-1.3/Development-Environment-Setup-Arduino)
- [Waveshare Wiki — ESP32-C6-LCD-1.47（同系列，结构类似，可参照）](https://www.waveshare.com/wiki/ESP32-C6-LCD-1.47)
- [Getting Started 教程（Medium，1.47 变体，步骤可迁移）](https://medium.com/@androidcrypto/getting-started-with-an-esp32-c6-waveshare-lcd-device-with-1-47-inch-st7789-tft-display-07804fdc589a)
