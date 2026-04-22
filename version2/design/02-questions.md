# 02 — 我们的题目设计

> **角色**:讲 **"用户在手机网页上看到什么题、每道题映射到 avatar 的哪个字段"**。
> 这是本项目相对 hello-stranger 的**语义替换**——原作用 race/age/politics/edu,我们换成更贴近课堂场景的词汇。
>
> **上一级**:[../design.md](../design.md) · **同级**:[index](./README.md)
>
> **v6 更新**:把 Q_LOOK + Q_POSE 合并成单一的 Q_AVATAR 视觉画廊。原因:这两个字段在 hello-stranger 原作里**从来不是用户选项**(`color` 从 race 派生,`num` 从 personKey 的 hash 派生,见 [HelloStranger.person.svelte:146-149](https://github.com/the-pudding/hello-stranger/blob/main/src/components/hellostranger/HelloStranger.person.svelte)),v5 误把底层 sprite 字段拆成两个 UI 问题,暗示了并不存在的组合语义。v6 把它们压成 1 个 "挑这张" 的视觉选择,回到"一次选定一个 sprite"的原作语义。

---

## 题目文案设计原则

所有选项都是 **"此刻状态"**,不是 **"固有属性"**。用户应该有合理的冲动明天重新选一个。这是 avatar 生成机制成为**持续互动**(而不是一次性捏脸)的语义基础。

## 题目一览

| # | 问题 | 驱动 avatar 字段 | 档位数 |
|---|---|---|---|
| Q_S | Pick a body — which one feels like you today? | `sex` | 2 |
| Q_AVATAR | Which avatar feels like you today? | `color` + `num` | 15(5 × 3 压平) |
| Q_ENERGY | What's your energy level? | `bg_level` | 6 |
| Q_MOOD | How do you feel right now? | (动画速度,不直接进 sprite) | 5 |
| Q_QUOTE | What are you thinking? | `quote` | 自由文本 |

---

## Q_S — Body

> "Pick a body — which one feels like you today?"

两个选项:`male` / `female`,直接决定 `sex`。

强调"today"——不是生理性别,是**今天想呈现**的身型。匿名社交装置的灵活性。

## Q_AVATAR — 视觉画廊(v6 合并 look + pose)

> "Which avatar feels like you today?"

按当前性别显示 **15 个 sprite 缩略图**(5 × 3 的网格),用户点一个瓷砖即选定。一次操作同时写入 `color`(0..4)和 `num`(0..2)两个字段。

### 为什么压平成一个问题

这 15 个 sprite 在 sprites.json 里是**完全独立手画**的角色:`male_0_0` 和 `male_0_1` 是两个毫不相关的人物,不是"同一个人的两个姿势"。不能拆分 hair/eye/face/body 单独选——原作 sprite 是整张手画,物理上部件不分层。

**原作根本没把这两个字段暴露给用户**:

- `color`:从 race 数据派生(white/asian → 0..1,black → 3..4,latino → 2..3)
- `num`:从 personKey 字符串 hash 派生(纯随机)

见 [HelloStranger.person.svelte:146-149](https://github.com/the-pudding/hello-stranger/blob/main/src/components/hellostranger/HelloStranger.person.svelte)。原作里用户只提供 "race" 这一人口学输入,系统自己在 15 个 sprite 里随机挑一个。这两个字段对用户是黑盒。

v5 版本在替换掉 race/age/politics/edu 四个人口学输入之后,机械地把剩下的 `color` + `num` 两个字段拆成 Q_LOOK + Q_POSE 两个 UI 问题,暗示了**并不存在的组合语义**——用户以为"换 pose 会保持同一个人",实际上是换到完全不同的角色。

v6 把它们压平成一个"看图挑"的画廊,回到原作的"一次选定一个 sprite"的语义,同时保留了用户自主选择的可控性(不再是 hash 随机)。

### 用户体验

- 所见即所得:15 个缩略图就是 15 种可能结果,无需预测组合
- 性别切换后,画廊重建成该性别的 15 张,但选中的 (color, num) 索引保持不变(视觉上"同一格子位置")
- 缩略图固定灰色,不跟随 energy 配色——避免用户在挑形状时被颜色变化干扰

## Q_ENERGY — 配色能量

> "What's your energy level?"

6 档滑块:`empty → low → medium → steady → high → electric`,驱动 `bg_level`(0..5)。

每档对应一个 hex,这个 hex 通过派生色算法(darkenColor / lightenColor / darkestColor)**单独决定整幅画的整套配色**——字符色、底色、边框色都从这一个 hex 出来。**不要独立选"字符色"**(那会破坏原作的色系和谐性)。

继承原作 hex(来自 colors.age palette):

| 档位 | 关键词 | hex | 源 |
|---|---|---|---|
| 0 | empty    | `#d62d87` | age.0-19 |
| 1 | low      | `#e06cd5` | age.20-29 |
| 2 | medium   | `#e9a5e2` | age.30-39 |
| 3 | steady   | `#b0877b` | age.40-49 |
| 4 | high     | `#d0caa4` | age.50-59 |
| 5 | electric | `#d6be76` | age.60+ |

## Q_MOOD — 情绪节奏

> "How do you feel right now?"

5 个选项(chill / curious / playful / tired / glowing)。

**重要**:mood **不决定颜色**。mood 在我们项目里是**原创维度**,驱动两件事:

1. **动画速度**(每帧切换的间隔 ms)
2. **是否尝试 smile 变体**(某些 mood 自动搜 `...smile` sprite,找不到 fallback)

| mood | 动画间隔 | prefer_smile | 感受 |
|---|---|---|---|
| chill    | 1200 ms | false | 慢呼吸 |
| curious  | 500  ms | false | 正常节奏,在看东西 |
| playful  | 180  ms | true  | 活泼,频繁动,尝试微笑 |
| tired    | 3000 ms | false | 几乎不动 |
| glowing  | 400  ms | true  | 稳定闪烁,尝试微笑 |

**这是我们对 hello-stranger 的第一个纯原创扩展**——原作只有固定 6 fps 一种节奏,不能被用户情绪驱动。完整改动说明见 [06-our-mapping.md](./06-our-mapping.md)。

## Q_QUOTE — 文字气泡

见 [03-quote.md](./03-quote.md)。

---

## 组合空间

```
sex (2) × avatar (15) × energy (6) = 180 种结构化 avatar 组合
  × mood (5) 驱动 5 种动画节奏
  × quote 自由文本 = 实际无限
```

avatar 虽然底层仍由 color (0..4) + num (0..2) 两个字段表示,但对用户而言是 **15 个不可拆分的视觉选项**,不是 5 × 3 的组合。

smile 作为独立维度在 Q_SMILE 提供——只有 4 个 sprite 组合有 smile 变体,缺失的 fallback 到 neutral。

## 为什么不用原作的 race/age/politics/edu

- **race** → 在匿名社交里当"配色"太重,也没必要把种族作为 avatar 属性
- **age** → 课堂场景年龄差几乎没有,没区分度
- **politics** → 美国两党语境,对澳洲 UTS 课堂无意义,且太敏感
- **edu** → 全班都是 UTS 同学,无区分度

我们保留原作的**配色机制和 palette**,只把**触发输入**从人口学数据换成"此刻状态"——这是最小的语义改动但有最大的场景贴合度。

## 题目 JSON 定义文件

实际实现放在 `web/questions.json`,是上面表格的机器可读版。题目文案和选项的修改直接改那个文件即可,不用改代码。
