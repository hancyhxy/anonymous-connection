# Design — Avatar 渲染机制

Last updated: 2026-04-21
Scope: Avatar 的视觉设计与生成机制,是 [spec.md §3](./spec.md#3-avatar-和-tag-预设集mvp) 的权威附录
Status: 本文件确立本项目 **Phase 1 MVP** 的 avatar 渲染方案

---

## 0. 这篇文档回答三个问题

1. **我们怎么生成每一个独一无二的 avatar?**(匹配/生成机制)
2. **用户的选择题答案如何驱动视觉?**(映射关系)
3. **相对 hello-stranger 原作,我们新增了什么?**(本项目的原创部分)

整个设计建立在一个核心决策上:**直接复用 hello-stranger 的视觉机制与素材,只替换它的输入源**——原作靠受访者真实 demographic 数据驱动配色,我们靠用户答题当场驱动配色。视觉产出和 pudding.cool 的页面应当**肉眼无法区分**,但语义完全转向"匿名线下社交"。

---

## 1. 参考来源与 Attribution

**Hello Stranger** — The Pudding (Alvin Chang), 2025
- 项目页:https://pudding.cool/2025/06/hello-stranger/
- 源码:https://github.com/the-pudding/hello-stranger
- 许可:**MIT License**, Copyright (c) 2022 The Pudding
- 开发时的本地 reference checkout 未随 repo 公开;如需参考原作源码,请直接访问上方 GitHub 链接

**本项目直接复用的资产:**

| 文件 | 用途 | 是否修改 |
|---|---|---|
| `src/data/sprites.json`(85 KB) | 10 个骨架 × 3 帧的 ASCII 字符网格 | 不修改,原样嵌入 |
| `src/data/colors.json`(857 B) | hex 颜色表 | 结构保留,**值会换**(见 §4) |
| `HelloStranger.person.svelte` 的渲染逻辑 | `<pre>` + CSS 字体压扁方案 | 照抄 CSS,JS 重写为我们的输入源 |
| `darkenColor` / `lightenColor` / `darkestColor` 三个派生色函数 | 从一个 hex 派生出 mainColor / bgColor / darkerColor | 逐字节照抄 |

**合规说明:**依 MIT 许可,我们在 design.md(本文件)、手机网页 footer、设备开机画面都保留 "Sprites by The Pudding, MIT License" 归属声明。这不是礼貌性标注,是许可条款的硬性要求。

---

## 2. 匹配机制(生成一个 avatar 需要的全部输入)

### 2.1 一个 avatar 的数据模型

```jsonc
{
  "sex":       "male" | "female",   // 身型大类,2 选
  "color":     0 | 1 | 2 | 3 | 4,   // 身型/配色档位,5 档(见 §4 Q_A)
  "num":       0 | 1 | 2,           // 动画起始帧 + 同档位子变体,3 选
  "smile":     true | false,        // 是否启用微笑变体
  "bg_level":  0..5,                // 背景色档位,6 档(见 §4 Q_B)
  "quote":     string(0..20)        // 自由文字(可空),本项目新增
}
```

这六个字段完全决定一个 avatar 的视觉输出。前五个照搬原作字段语义;`quote` 是新增字段,见 §5。

### 2.2 spriteKey 生成规则

照抄 `HelloStranger.person.svelte` 第 153 行 + 第 162 行:

```js
spriteKey = `${sex}_${color}_${num}${smile ? "smile" : ""}`
```

例:
- `male_2_1`          → 男性身型 档位 2 的第 1 帧
- `female_0_0smile`   → 女性身型 档位 0 第 0 帧微笑版

拿到 spriteKey 后查 `sprites.json[spriteKey]` 得到 18×18 字符网格。sprites.json 的 36 个 key 完整覆盖了 `2 sex × 5 color × 3 frame + smile` 的所有有效组合 + `end1 / end2` 两个片尾帧(本项目不用)。

### 2.3 派生色计算

照抄 `HelloStranger.person.svelte` 第 47-108 行的三个函数。**关键一点:所有字符颜色和底色都从一个 hex 派生,不需要单独选前景色。**

```js
mainColor   = darkenColor(backgroundColor, 0.3)    // 主字符颜色
bgColor     = lightenColor(backgroundColor, 1.3)   // 屏幕底色
darkerColor = darkestColor(backgroundColor, 0.27)  // 阴影/边框色
```

`darkenColor` 实现(见原作源码 47-66 行):把 hex 转 RGB,找到 R/G/B 里最大的那一路,给主色路多一点亮度留存(boost=0.2),让整体暗化后仍保持色相。

### 2.4 渲染(DOM 级别)

手机网页端照抄原作渲染:

```html
<div class="asciiContainer" style="color: {mainColor}">
  <pre>{sprites[spriteKey][currentFrame]}</pre>
</div>

<style>
  .asciiContainer {
    font-family: "Lucida Console", Monaco, monospace;
    font-size: 2.9em;
    line-height: 0.8rem;      /* 关键:行高 < 字号,压扁行距 */
    letter-spacing: -0.1em;    /* 关键:负字距,字符靠紧 */
    font-weight: bold;
    color: var(--main-color);
  }
</style>
```

设备端(ESP32-C6)**不能**走字体渲染路线——屏幕端不支持 Unicode block 字符的默认字体。设备端方案见 §6。

### 2.5 动画

照抄 `HelloStranger.person.svelte` 第 192-203 行:

```js
const frameRate = 6;          // 6 fps,即 ~166 ms/帧
setInterval(() => {
  frameCount = (frameCount + 1) % sprites[spriteKey].length;
  currentAsciiArt = sprites[spriteKey][frameCount];
}, 1000 / frameRate);
```

每个 spriteKey 数组有 3 帧(或 smile 有 1 帧特例),循环切换 = 会动的头像。

---

## 3. 映射关系(hello-stranger 的变量 → 我们的用户输入)

这是本项目相对原作的**核心改动**:我们不用 1431 人的真实 demographic 数据,用用户当场答题来驱动这些变量。

### 3.1 变量对照表

| 原作变量 | 原作数据源 | 我们的输入源 | 保留 or 换源 |
|---|---|---|---|
| `sex` | `people.json[id].sex` | **用户选择题 Q_S**(see §4) | 换源 |
| `color` (0-4) | `personData.race` 查表 | **用户选择题 Q_A**(see §4) | 换源 |
| `num` (0-2) | `personKey` 字符串哈希 | **随机分配**(由后端在 profile 写入时 roll 一次,此后固定) | 换源(简化) |
| `smile` | 特定对话场景触发 | **用户显式勾选**(一个开关) | 换源(语义简化) |
| `backgroundColor` | `colors.json.age[personData.age]` 查表 | **用户选择题 Q_B**(see §4) | 换源 |
| `mainColor` | `darkenColor(backgroundColor)` | **同原作**,从 backgroundColor 派生 | 保留 |
| `bgColor` | `lightenColor(backgroundColor)` | **同原作**,从 backgroundColor 派生 | 保留 |
| `darkerColor` | `darkestColor(backgroundColor)` | **同原作**,从 backgroundColor 派生 | 保留 |

### 3.2 重要关系澄清

读原作源码的一个关键发现:**race 和 backgroundColor 在原作里语义耦合**。`colors.json` 里 `race.white` 给的是一个 hex 值,这个 hex 被当作 backgroundColor 传给 person 组件,person 组件再用 `darkenColor()` 派生出字符颜色。**race 同时**:
1. 通过 145-146 行的表达式决定 sprite 身型档位(`color` 变量,0-4)
2. 通过 `colors.json.race[race]` 决定 backgroundColor(进而决定字符色、底色)

**我们的处理**:保持这个语义耦合——用户对 Q_A 的答案**同时**决定身型档位 + 前景字符色系。一次答题,两条线路同步改变。这和原作机制一致,也让 UI 更简单(用户不会有"我为什么要选 5 个不同颜色"的困惑)。

### 3.3 colors.json 的结构保留但值要换

原作 `colors.json` 按 lens 组织:

```json
{
  "race":     { "white": "#c9c1af", "aapi": "#d173bb", ... },
  "age":      { "0-19": "#d62d87", ... },
  "politics": { "1": "#d94a64", ... },
  "edu":      { ... },
  ...
}
```

我们的新 `colors.json` 按**我们的 lens 重命名**,但**颜色值全部沿用原作**(因为这些颜色本身就是设计师精心挑过的柔和 palette,换了反而破坏视觉一致性):

```json
{
  "mood":   { "chill": "#c9c1af", "curious": "#ff997a", "playful": "#d173bb",
              "tired": "#95c6ed", "glowing": "#9fe0cf" },
  "energy": { "0": "#d62d87", "1": "#e06cd5", "2": "#e9a5e2",
              "3": "#b0877b", "4": "#d0caa4", "5": "#d6be76" }
}
```

**为什么照搬原作颜色值**:这些颜色是 The Pudding 的 designer 挑过的、彼此协调的、能在浅/深底色下都保持可读的柔和色。自己换颜色的风险远大于收益,而 MIT 允许我们这么做。

---

## 4. 新增内容一:我们自己设计的选择题

这是本项目对 hello-stranger 的核心**语义替换**。原作的 age/politics/race/edu 在 UTS 课堂场景要么不适用要么冒犯;我们用下面三道题驱动同一套视觉机制。

### 4.1 Q_S — 身型选择题(决定 sex 变量,2 档)

> **"Pick a body — which one feels like you today?"**
>
> (下方并排展示 male/female 两个预览缩略图,用户点哪个算哪个)

**映射**:直接决定 `sex` 变量。用户点击 = 选定;不是"你的生理性别",是"你**今天**想呈现哪个身型"。**每次编辑 profile 都可以重选**,这是匿名社交装置的一层灵活性。

**UI 呈现**:两个预览卡,各自显示 `male_0_0` 和 `female_0_0` 的默认渲染缩略。

### 4.2 Q_A — 心情选择题(决定 color + backgroundColor,5 档)

> **"How do you feel right now?"**

| 档位 | 关键词 | 继承原作 hex | 继承原作位置 |
|---|---|---|---|
| 0 | `chill` | `#c9c1af` | race.white |
| 1 | `curious` | `#ff997a` | race.other/mixed |
| 2 | `playful` | `#d173bb` | race.aapi |
| 3 | `tired` | `#95c6ed` | race.black_or_african_american |
| 4 | `glowing` | `#9fe0cf` | race.hispanic_or_latino |

**映射逻辑**:用户选档位 N →
- `color = N`(sprite 身型档位)
- `backgroundColor = hex 查表`(派生出 mainColor/bgColor/darkerColor)

**文案设计原则**:5 个词都是**"此刻状态"**,不是**"固有属性"**——用户应该有合理的冲动明天重新选一个。这是 avatar 生成机制成为**持续互动**而不是**一次性捏脸**的语义基础。

### 4.3 Q_B — 能量选择题(决定 bg_level → 最终背景底色,6 档)

> **"What's your energy level?"**
>
> (从左到右是一条 6 档滑块:empty → low → medium → steady → high → electric)

| 档位 | 关键词 | 继承原作 hex | 继承原作位置 |
|---|---|---|---|
| 0 | `empty` | `#d62d87` | age.0-19 |
| 1 | `low` | `#e06cd5` | age.20-29 |
| 2 | `medium` | `#e9a5e2` | age.30-39 |
| 3 | `steady` | `#b0877b` | age.40-49 |
| 4 | `high` | `#d0caa4` | age.50-59 |
| 5 | `electric` | `#d6be76` | age.60+ |

**问题**:上面 Q_A 已经决定了 backgroundColor,为什么这里还要一个 bg_level?

**答案**:在原作里,`backgroundColor` 这个 CSS 变量**不同视角下取值不同**——当页面 lens 是 "race" 时 background 用 race 颜色,lens 切到 "age" 时就换成 age 颜色。**我们在 Phase 1 不做多 lens 切换**(用户就在一个视角看自己),因此 Q_A 和 Q_B 同时生效,具体合成方式:

- **Q_A 的 hex → sprite 颜色层**(mainColor = darkenColor(Q_A.hex))
- **Q_B 的 hex → 屏幕外层底色**(相当于原作的 bgColor 那一层,lightenColor 派生)

这是对原作机制的一个小改装——原作里字符色和底色都源自**同一个** backgroundColor,我们把这两路**解耦**,让用户能独立控制。视觉上依然和谐(因为配色源都是原作 palette),但组合空间扩大了。

### 4.4 可选维度:smile toggle

> **"Feeling good about it?"** [ ☐ smile ]

一个复选框,勾上 → `smile=true` → spriteKey 后缀加 `smile`。

如果当前 `${sex}_${color}_${num}smile` 在 sprites.json 里**不存在**(原作只有少数 key 有 smile 变体),就**降级为 `false`**,静默。后端/前端都要做这个 fallback 检查。

### 4.5 组合空间

```
sex (2) × color (5) × num (3) × smile (1.5, 打折因为不是所有 key 都有 smile)
  × bg_level (6)
= 2 × 5 × 3 × 1.5 × 6
≈ 270 种纯结构化组合
```

加上 §5 的自由文本 quote(20 字符 ASCII),**实际独一无二感无限**。

50 人教室撞款期望(只看结构化 270 种,忽略 quote):
`C(50, 2) / 270 ≈ 4.5 对`
——4-5 对人身型+心情+能量完全相同的概率;但他们的 quote 文本不同,依然视觉上可区分。

---

## 5. 新增内容二:Quote 文字气泡

**这是本项目对 hello-stranger 的原创扩展,不是复刻。**

### 5.1 为什么加这个

hello-stranger 的 avatar 是**被数据定义的**——一个人的 race/age/politics 不会因为他改主意而变化。那是可视化的正确姿态。

但我们做的是**线下匿名社交装置**,avatar 必须是**可广播、可改写**的——"我此刻想说什么"。`quote` 就是这个广播层。整个 profile 里其他字段偶尔改(换心情时),**quote 可以每几分钟改一次**,这是产品唯一的高频主动输入。

### 5.2 规格

- 类型:任意 ASCII 字符串
- 长度:0 – 20 字符(0 即隐藏,不显示气泡区)
- 编码:纯 ASCII(原因:ESP32 默认字体仅支持 ASCII,若允许 emoji/中文需要额外字体工程,Phase 1 放弃)
- 预设灵感(手机页可提供快捷按钮):`need coffee` / `say hi` / `want quiet` / `open to chat` / `new here` / `tired` / `daydreaming` / `same vibe?`

### 5.3 视觉位置

```
     ┌─────────────────┐
     │ "need coffee"   │    ← ASCII bubble 边框,
     └────────┬────────┘      位于 avatar 上方
              ▼
         [avatar]             ← 原作一样的 ascii 渲染
```

气泡用 ASCII 字符画边框:`┌─┐│└┘` + 指向 avatar 的 `▼`。边框色 = mainColor,文字色 = mainColor。

### 5.4 传输和更新

手机 → 后端 → 设备的 JSON 片段:

```json
{ "quote": "need coffee" }
```

设备端收到后**只重绘 quote 气泡区**,不重绘整屏——ESP32-C6 的 SRAM 紧张,全屏重画会看得到闪。

### 5.5 关系:quote vs interests?

上几轮讨论我们想过加一组 "interests" emoji 标签,但**在这个"复刻 hello-stranger"定位下暂时不加**。原因:

- hello-stranger 原作没有 interests 这一层
- interests 的视觉位置需要一个额外显示区,会让 avatar 旁边变"贴满标签"——破坏原作那种"一个干净的发光 sprite"的极简美
- quote 的自由文本已经可以承载兴趣表达(`"love coffee"` / `"film nerd"`)

**interests 降级为 Phase 2 扩展**。如果用户反馈"写不出 quote",再把预设短语 + 图标加回来。

---

## 6. 设备端渲染(ESP32-C6)

手机端照抄 hello-stranger 的 `<pre>` + CSS 方案,设备端不行——ST7789V2 屏 + Arduino_GFX 默认字体只支持 ASCII,不支持 `▰ □ △ ▮ ○ ◊` 等 block 字符。

### 6.1 方案:把 Unicode 字符映射成 8×10 位图查表

原作使用的字符集统计(从 sprites.json 扫出来):`▰ □ △ ▮ ○ ◊ | ` ` _ -` 加空格,**约 10 个字符**。每个字符做一张 8×10 单色位图:

```c
// Auto-generated from sprites.json character set
const uint8_t glyph_filled_block[10]  = { 0xFF, 0xFF, 0xFF, ... }; // ▰
const uint8_t glyph_hollow_square[10] = { 0xFF, 0x81, 0x81, ... }; // □
const uint8_t glyph_triangle[10]      = { 0x00, 0x10, 0x28, ... }; // △
// ... 共 10 个
```

总资源 ~100 字节 flash,对 4 MB 毫无压力。

渲染流程:遍历 18×18 字符网格 → 每字符查表取对应位图 → `drawBitmap(x, y, glyph, 8, 10, mainColor)`。18 行 × 18 列 × 8×10 px ≈ 144 × 180 px,对 240 × 240 屏刚好居中。

### 6.2 派生色在设备端怎么算

`darkenColor/lightenColor/darkestColor` 三个函数在 Arduino 端用 C 实现(也就是把 JS 版本逐行翻译到 C)。输入是一个 RGB565 值(或 `#hex` 字符串),输出是 RGB565。运算量极小。

### 6.3 动画

设备端主循环里一个 `millis()` 判断,每 166 ms 切 `currentFrame = (currentFrame + 1) % 3`,然后只重绘 avatar 区域(不整屏 clear,用背景色 fillRect 擦掉就好)。

### 6.4 quote 气泡渲染

用 `drawFastHLine / drawFastVLine` 画 ASCII 边框,用内置字体 `drawString()` 写 quote 文字。整个气泡区 ~60×20 px,重绘很快。

---

## 7. 本项目相对原作的变化总表

| 项目 | hello-stranger 原作 | 本项目 |
|---|---|---|
| 主要叙事 | "同一群 1431 人,切不同 lens 看" | "50 人线下,每个人配一个独一无二的自己" |
| 数据源 | 真实问卷 demographic(race/age/politics/edu/affect) | 用户当场回答的选择题(mood/energy) |
| 配色表 | 原作 palette | **完全继承原作 palette,只换 key 名** |
| sprite 骨架 | 10 个(male/female × 0-4) | **完全继承,一张不改** |
| 派生色算法 | darken/lighten/darkest 三函数 | **完全继承** |
| 渲染技术 | `<pre>` + 等宽字体 + 负字距 | 手机端继承;设备端改为 8×10 位图查表 |
| 多 lens 切换 | 核心交互 | **Phase 1 不做**,单一视角 |
| quote 文字气泡 | 固定台词,数据驱动 | **自由编辑,用户驱动(新增)** |
| smile 变体 | 对话触发 | 用户显式开关(换源) |
| num 子变体 | personKey 哈希 | 后端分配时 roll 一次(换源) |
| interests 标签 | 无 | Phase 2,MVP 不做 |
| 设备端 | 无(纯网页) | ESP32-C6 板屏一体(**全新**) |

---

## 8. 写这份 design.md 的动机

**为什么我们要写这个,而不是直接 fork hello-stranger 改两行:**

1. 我们要部署到**硬件**,硬件没有浏览器,没有 Svelte runtime——必须把原作 web-only 的实现**逐条翻译到 Arduino C++**。这份 design.md 是翻译的底本。
2. 我们要把原作**"可视化历史对话"**的语义,改造成**"当场匿名社交"**的语义。这两种语义表面视觉相似、机制相同,但题目设计、用户流程、数据模型都不同。这份 design.md 固定我们的新语义。
3. 两周 MVP 交付期里,下面几个新成员/新阶段需要一份**权威参考**:手机网页前端、Node 后端、ESP32 固件、avatar 素材扩展。各端一致要看同一份规范。

---

## 9. 交叉引用

- [concept.md](./concept.md) — 项目概念与产品定位
- [spec.md](./spec.md) — Phase 1 MVP 的技术 spec;本 design.md 是 spec §3 的附录详解
- [hardware.md](./hardware.md) — ESP32-C6-LCD-1.3 硬件事实和引脚定义
- The Pudding 原作源码:[github.com/the-pudding/hello-stranger](https://github.com/the-pudding/hello-stranger) — MIT License,供学习与素材复用

---

## 10. 所有可拆维度完整档案(Dimension Registry)

**本节是一张穷尽的清单**,列出 hello-stranger 原作数据 + 代码里**所有影响 avatar 视觉的可拆维度**。本 MVP 不一定每个都暴露给用户,但**每一个都是未来扩展的入口**——以后想加新选项,来这里查。

### 10.1 维度分类总览

hello-stranger 的视觉由三层数据驱动,我们分层清点:

```
Layer A:  sprite 骨架维度      — 来自 sprites.json 的 key 解构
Layer B:  渲染视觉参数          — person.svelte 的 props + CSS 变量
Layer C:  语义 lens 维度        — colors.json 的 5 个色板 + people.json 的 15 个字段
```

### 10.2 Layer A — Sprite 骨架维度(来自 sprites.json)

36 个 key 严格符合一个语法:`{sex}_{color}_{num}[smile]` + 2 个 `end*` 片尾帧。从中可切出以下独立维度:

```typescript
type SpriteAxes = {
  sex:        "male" | "female";              // 2 values
  color:      0 | 1 | 2 | 3 | 4;               // 5 values (身型/发型大类)
  num:        0 | 1 | 2;                       // 3 values (同 sex+color 下的变体姿态)
  smile:      boolean;                         // 2 values (但仅 4 个 key 存在 smile 变体)
  frame:      number;                          // 每个 sprite 有 2-5 帧,动画用
};

// Smile 变体精确清单(sprites.json 里真实存在的):
const SMILE_AVAILABLE = [
  { sex: "male",   color: 0, num: 2 },         // male_0_2smile
  { sex: "female", color: 1, num: 0 },         // female_1_0smile
  { sex: "female", color: 3, num: 1 },         // female_3_1smile
  { sex: "female", color: 3, num: 2 },         // female_3_2smile
];

// 帧数分布(非每个 sprite 都有 3 帧):
const FRAME_COUNTS = {
  "male_0_0":  5, "male_0_1":  4, "male_0_2":  4, "male_0_2smile":  3,
  "male_1_0":  4, "male_1_1":  5, "male_1_2":  5,
  "male_2_0":  5, "male_2_1":  5, "male_2_2":  5,
  "male_3_0":  5, "male_3_1":  5, "male_3_2":  5,
  "male_4_0":  5, "male_4_1":  5, "male_4_2":  5,
  "female_0_0":  4, "female_0_1":  4, "female_0_2":  4,
  "female_1_0":  5, "female_1_0smile":  3, "female_1_1":  5, "female_1_2":  5,
  "female_2_0":  5, "female_2_1":  5, "female_2_2":  5,
  "female_3_0":  5, "female_3_1":  5, "female_3_1smile":  3,
                    "female_3_2":  5, "female_3_2smile":  2,
  "female_4_0":  5, "female_4_1":  5, "female_4_2":  5,
  // special (我们不用):
  "end1":  8, "end2":  8,
};

// 原作把 num 和 frame 混用:
//   num=0..2 是不同"静态姿态"(每个都可当 sprite key 起始)
//   每个 sprite 有自己内部 2-5 frames 的动画循环
// 我们做设计决策时可分开:让用户选 num(姿态),动画自动在 frames 内循环。
```

**粒度说明(重要)**:
hello-stranger 的 sprite **是整张 18×18 手画**,**不分层**。我们调查过:每个 sprite 内部的"头发/眼睛/嘴/身体/衣服"**物理上粘在一起**,不能在运行时独立替换。所以:

- ❌ 无法独立选择 hair / eye / face / body / outfit
- ❌ 无法做 hair 配一个色 + body 配另一个色
- ✅ 只能选 sprite 整体(通过 sex + color + num + smile 组合)

要做"分部件可组合 avatar",需要我们**自己画 32 个 16×16 部件**(即 `avatar-studio/` 原设计)——这是 Phase 2 大工程,不在现有数据覆盖范围内。

### 10.3 Layer B — 渲染视觉参数(来自 person.svelte)

从 `$props()` 声明 + 模板的 `style=` 绑定 + CSS `class:` 切换,穷尽如下:

```typescript
type RenderParams = {
  // —— 颜色派生(全部从 backgroundColor 一个 hex 出来)——
  backgroundColor:  string;    // hex, 唯一色源,下面三个都是它的派生
  mainColor:        string;    // = darkenColor(backgroundColor, 0.3)   → 字符色
  bgColor:          string;    // = lightenColor(backgroundColor, 1.3)  → 外框底色
  darkerColor:      string;    // = darkestColor(backgroundColor, 0.27) → 边框/阴影

  // —— 几何变换(原作叙事动画用,我们单屏展示可能只用其中 1-2 个)——
  transform:        string;    // CSS transform string,原作用于"把 avatar 飞到另一位置"
  scale:            string;    // CSS scale string
  w:                number;    // 容器宽度(px),原作用 scale({w/60}) 让字体自动缩放
  h:                number;    // 容器高度(px)
  opacity:          number;    // 0-1,原作淡入淡出
  visible:          boolean;   // 隐藏/显示开关
  prefersReducedMotion: boolean;  // a11y 开关,关闭动画

  // —— 交互状态(影响边框/背景色)——
  selected:         boolean;   // 选中时边框高亮(`var(--person-hl-color)`)
  quoteText:        string;    // 有 quote 时边框变另一色(`var(--quote-hl-color)`)
  loaded:           boolean;   // 加载状态 class
  fadeOut:          boolean;   // 淡出状态 class

  // —— 左右翻转彩蛋(person2 的 CSS 规则)——
  // CSS: .person2 .asciiContainer pre { transform: scaleX(-1); }
  // 原作按 avatar 编号的奇偶翻转,实现"两人面对面对话"
  // 对我们可作为免费的 mirror 维度(任何 sprite ×2)
  mirror:           boolean;   // 我们自己加的派生维度

  // —— 交互类 props(跟视觉无关,叙事用)——
  onClick:          Function;  // 点击回调
  personKey:        string;    // 用于 hash num 的源
  personData:       object;    // 完整 1431 人的 demographic 数据
  convoState:       object;    // 对话场景状态
  personState:      object;    // 单人场景状态
  sortMode:         string;    // 排列模式
  convoId:          string;    // 对话 ID
  zoomPerson:       string;    // 当前放大的 person
  talking:          boolean;   // 是否在"说话"
  value:            number;    // 当前 scrolly 进度
  currentTime, nextTime: number;  // 对话计时
  instant:          string;    // "instant" 标志,跳过动画
  var_to_show:      string;    // 当前 lens
  data, metric:     ...        // 其他数据驱动状态
};
```

**对我们有意义的子集**(可以做成 UI 选项的):
- `backgroundColor` — 整套配色源,**已暴露为 energy**
- `mirror` — 免费的 ×2 视觉扩展,**未暴露,可随时加**
- `opacity` — 可做"淡入淡出"特效,**未暴露,未来加**
- `transform` / `scale` — 可做 avatar 飞动、放大缩小,**未暴露,配对动画时可用**

### 10.4 Layer C — 语义 Lens 维度(来自 colors.json + people.json)

原作有 **5 个独立的 lens 色板**,每个都是一套完整的配色方案:

```typescript
type ColorsJSON = {
  politics: {
    "0": "#c2b0b6",   "1": "#d94a64",   "2": "#fab1bf",
    "3": "#e4caed",   "4": "#a3d0e3",   "6": "#48afdb",
  };                                                    // 6 档
  race: {
    "white":                            "#c9c1af",
    "asian" | "native_..._islander":    "#d173bb",
    "black_or_african_american":        "#95c6ed",
    "hispanic_or_latino":               "#9fe0cf",
    "mixed" | "other" | "native_..." | "prefer_not_to_say":
                                        "#ff997a",
  };                                                    // 5 个独立 hex
  edu: {
    "some_high_school" | "completed_high_school":       "#bf93cf",
    "some_college":                                     "#e35db2",
    "associate_degree":                                 "#d1882e",
    "bachelors_degree":                                 "#e8d26f",
    "masters_degree" | "doctoral_degree" | "professional_degree":
                                                        "#6883a3",
  };                                                    // 5 个独立 hex
  raw: {     // 对话后整体感受
    "Negative":  "#845aad",
    "Average":   "#706267",
    "Positive":  "#ede977",
  };                                                    // 3 档
  age: {
    "0-19":  "#d62d87",    "20-29": "#e06cd5",
    "30-39": "#e9a5e2",    "40-49": "#b0877b",
    "50-59": "#d0caa4",    "60+":   "#d6be76",
  };                                                    // 6 档
};
```

另外在 `colors-legend.json` 里还有第 6 个 lens,原作代码里 `affect` 可以是它:

```typescript
affect: {
  "Worse":  "#ff6bab",
  "Same":   "#706267",
  "Better": "#ede977",
};                                                      // 3 档("对话让我感觉更好/一样/更差")
```

#### 1431 真人的完整字段(people.json 每条记录)

每个可以在未来当新 lens:

```typescript
type PersonRecord = {
  sex:              "male" | "female";
  politics:         number;   // 1-6 连续
  race:             string;   // 8 个类别
  edu:              string;   // 8 个教育档位
  employ:           number;   // 就业状态
  age:              number;   // 连续年龄
  shared_reality:   number;   // 对话后"共同现实感"评分
  affect:           number;   // 当前情绪
  pre_affect:       number;   // 对话前情绪
  begin_affect:     number;   // 对话开始时情绪
  middle_affect:    number;   // 对话中段情绪
  end_affect:       number;   // 对话结束时情绪
  overall_affect:   number;   // 整体情绪
  worst_affect:     number;   // 最低点
  best_affect:      number;   // 最高点
};
```

**我们不用这些真人数据,但这些字段名本身是可借鉴的 lens 语义**——比如未来可以做 `employ` / `shared_reality` / `best_affect` 作为新维度,题目文案可以是"你现在的工作状态"/"你和对方多有共鸣"/"今天最开心的一刻"。

### 10.5 我们当前实际暴露的维度(v4 MVP)

对比全档案,我们目前只用到了最小子集:

| 档案中的维度 | MVP 是否用 | 我们命名为 | 备注 |
|---|---|---|---|
| A. sex | ✅ | sex | 原样 |
| A. color | ✅ | look | 重命名 |
| A. num | ✅ | pose | 重命名,独立于动画 |
| A. smile | ⚠️ | (由 mood 自动触发) | v4 决定不作为独立 UI 选项 |
| A. frame | ❌ | (内部动画,不暴露) | 由 mood 驱动动画速度 |
| B. backgroundColor + 派生三色 | ✅ | energy | 整套配色源 |
| B. mirror | ❌ | — | **免费扩展点**,未来加 |
| B. opacity / transform / scale | ❌ | — | 配对动画时可用 |
| B. selected / quoteText class | ❌ | — | 我们自己的 UI 不需要 |
| C. politics lens | ❌ | — | 课程场景不合适 |
| C. race lens | ❌ | — | 课程场景不合适 |
| C. edu lens | ❌ | — | 课程场景不合适 |
| C. raw / affect lens | ❌ | (mood 替换) | mood 灵感来自 affect |
| C. age lens | ❌ | (energy 替换) | energy 灵感来自 age |

### 10.6 未来扩展入口(3 条路)

基于上面档案,扩展 avatar 表达力的顺序从便宜到贵:

**便宜 — 数据里白拿(0 新素材)**:
1. 加 `mirror` 维度 → avatar 空间立刻 ×2
2. 加 `opacity` 动画 → "出场淡入"叙事感
3. 重新启用 `frame` 让用户选起始帧 → 3 档静态帧 + mood 动画叠加

**中等 — 自创 lens**(复用原作配色 palette):
4. 加一个新题 "今天想和什么人见面" → 5 档答案映射到 `politics` palette 的 5 个 hex(偷色不偷语义)
5. 加一个 "一天里你属于哪个时段"(morning/noon/afternoon/evening/night)→ 5 档映射到 `age` palette

**昂贵 — 自画部件**(avatar-studio 那条路):
6. 画 8 个新 hair 位图 → 给现有 sprite 做"发型替换"(技术上要手写"hair 区域 = row 2-5"的 mask 合成)
7. 画完整 32 部件组 → 走原 spec.md §3.1 的参数化方案

### 10.7 小结:为什么这份档案要单独存在

- **现在用不到,但会用到**:你(Xinyi)未来学期中段想给 avatar 加新选项时,翻这一节比再读源码快 10 倍
- **拦住"这个项目不提供 XX 维度"的错误直觉**:比如"能不能选头发?"—— 翻 §10.2 粒度说明,立刻知道原作数据不支持,需要走 §10.6 的"昂贵"路线
- **保留原作所有信息的出口**:MIT license 允许我们用他们的 palette 和 sprite,我们不该因为"MVP 没用到"而在脑子里忘掉这些数据存在
- **项目叙事价值**:写 final 报告时,说"我们调研了 hello-stranger 的 36 sprite + 5 lens + 15 字段的全部数据空间,选择其中 6 维做 MVP"——这是**严谨的设计决策证据**,比"我们用了 hello-stranger"有分量得多
