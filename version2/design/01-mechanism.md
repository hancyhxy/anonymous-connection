# 01 — Avatar 生成机制

> **角色**:本分册讲 **"一个 avatar 是怎么从几个数字变成屏幕上的像素的"**。
> 包含数据模型、spriteKey 生成、派生色算法、DOM 渲染、动画。
> 直接照搬 hello-stranger 的机制,是我们项目的技术底层。
>
> **上一级**:[../design.md](../design.md) · **同级**:[index](./README.md)

---

## 1. Avatar 数据模型

一个 avatar 被这些字段完全决定:

```jsonc
{
  "sex":       "male" | "female",   // 身型大类,2 选
  "color":     0 | 1 | 2 | 3 | 4,   // 身型档位,5 档(我们命名为 look,见 02-questions.md)
  "num":       0 | 1 | 2,           // 同身型子变体,3 选(我们命名为 pose)
  "smile":     true | false,        // 是否启用微笑变体(不是所有组合都有)
  "bg_level":  0..5,                // 背景色档位,6 档(我们命名为 energy)
  "quote":     string(0..20)        // 自由文字(本项目新增,见 03-quote.md)
}
```

字段命名和 hello-stranger 源码一致,便于追根。我们项目内的"友好重命名"见 [06-our-mapping.md](./06-our-mapping.md)。

## 2. spriteKey 生成规则

照抄 `HelloStranger.person.svelte` 第 153 + 162 行:

```js
spriteKey = `${sex}_${color}_${num}${smile ? "smile" : ""}`
```

例:
- `male_2_1`         → 男性身型 档位 2 的第 1 帧
- `female_0_0smile`  → 女性身型 档位 0 第 0 帧微笑版(不是每个组合都存在)

拿到 spriteKey 后查 `sprites.json[spriteKey]` 得到 **18×18 字符网格**(每帧一个字符串,帧数 2-5 不等)。sprites.json 里 36 个 top-level key 的完整清单见 [05-upstream-inventory.md](./05-upstream-inventory.md#102-layer-a-—-sprite-骨架维度来自-spritesjson)。

**smile fallback**:如果用户勾了 smile 但 `${sex}_${color}_${num}smile` 在 sprites.json 里不存在,**静默降级为非 smile 版本**。原作只有 4 个 key 有 smile 变体(见 upstream inventory)。

## 3. 派生色算法

照抄 `HelloStranger.person.svelte` 第 47-108 行。**关键原则:一个 hex 派生出整套视觉色**,不需要单独选"字符色"。

```js
mainColor   = darkenColor(backgroundColor, 0.3)    // 主字符颜色
bgColor     = lightenColor(backgroundColor, 1.3)   // 屏幕底色(外框)
darkerColor = darkestColor(backgroundColor, 0.27)  // 阴影/边框色
```

三个函数的本质:

- **darkenColor**:把 hex 转 RGB,找 R/G/B 里最大的那一路,给主色路多留 0.2 的亮度(`boost=0.2`)。这让整体暗化后仍保持色相——不是无脑 × 0.3。
- **lightenColor**:均匀乘 1.3,clamp 到 255。
- **darkestColor**:同 darkenColor,但默认 factor 从 0.3 压到 0.27,更暗。

这三个函数逐行拷贝到 `web/renderer.js` 和 `firmware/.../palette.cpp`,两端实现完全一致。

## 4. DOM 级渲染(手机端)

手机网页端照抄原作:

```html
<div class="asciiContainer" style="color: {mainColor}">
  <pre>{sprites[spriteKey][currentFrame]}</pre>
</div>
```

```css
.asciiContainer {
  font-family: "Lucida Console", Monaco, monospace;
  font-size: 18px;
  line-height: 0.55em;          /* KEY:行高 < 字号,压扁行距 */
  letter-spacing: -0.1em;        /* KEY:负字距,字符靠紧 */
  font-weight: bold;
  color: var(--main-color);
  white-space: pre;              /* 保留空格不折叠 */
}
```

**三条 key CSS 规则**(`line-height < 1em` + 负字距 + 固定等宽字体)共同构成 pudding.cool 那种像素画密度。少一条就不行。

设备端(ESP32-C6)走不同路线——见 [04-device-rendering.md](./04-device-rendering.md)。

## 5. 动画

照抄 `HelloStranger.person.svelte` 第 192-203 行:

```js
const frameRate = 6;          // 6 fps = 166 ms/帧
setInterval(() => {
  frameCount = (frameCount + 1) % sprites[spriteKey].length;
  currentAsciiArt = sprites[spriteKey][frameCount];
}, 1000 / frameRate);
```

每个 spriteKey 的 `frames` 数组长度不一定(2-5 帧不等,见 upstream inventory §10.2),循环切换产生"呼吸感"。

**我们的原创扩展**:把动画速度从原作固定 6 fps 改为**由 mood 动态驱动**——见 [06-our-mapping.md](./06-our-mapping.md) 的 mood 映射表。

## 6. 映射关系:原作字段 → 我们输入源

这是本项目相对原作的**核心改动**:我们不用 1431 人的真实 demographic 数据,用**用户当场答题**来驱动这些变量。

| 原作变量 | 原作数据源 | 我们的输入源 |
|---|---|---|
| `sex` | `people.json[id].sex` | 用户选择题 Q_S |
| `color` (0-4) | `personData.race` 查表 | 用户选择题 Q_AVATAR(v6 合并 look+pose)|
| `num` (0-2) | `personKey` 字符串哈希 | 用户选择题 Q_AVATAR(v6:同上,一次瓷砖点击同时写入两字段)|
| `smile` | 对话场景触发 | 用户选择题 Q_SMILE(v5 独立维度)|
| `backgroundColor` | `colors.json.age[personData.age]` | 用户选择题 Q_ENERGY |
| `mainColor` / `bgColor` / `darkerColor` | 派生 | **同原作,不改** |

**v6 说明**:原作里 `color` 和 `num` 都不是用户选项——`color` 从 race 派生,`num` 从 personKey 的 hash 派生(原作源码:[the-pudding/hello-stranger 的 HelloStranger.person.svelte:146-149](https://github.com/the-pudding/hello-stranger/blob/main/src/components/hellostranger/HelloStranger.person.svelte))。v5 把这两个字段机械地拆成 Q_LOOK + Q_POSE 两个 UI 问题,暗示了并不存在的组合语义(15 个 sprite 是完全独立手画的角色,不是 5×3 可组合矩阵)。v6 合并成单个 Q_AVATAR 视觉画廊——用户看缩略图直接挑,底层仍写入同样的 `(color, num)` 两字段。

问题的具体题目文案和候选值见 [02-questions.md](./02-questions.md)。
每个字段在我们项目里的重命名见 [06-our-mapping.md](./06-our-mapping.md)。
