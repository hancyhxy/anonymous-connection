# 03 — Quote 文字气泡

> **角色**:讲 **"avatar 上方的自由文字气泡"** —— 我们项目相对 hello-stranger 的第二个**原创扩展**。
>
> **上一级**:[../design.md](../design.md) · **同级**:[index](./README.md)

---

## 为什么加这个

hello-stranger 的 avatar 是**被数据定义的**——一个人的 race/age/politics 不会因为改主意而变化。这在"可视化调研"语境下是对的。

但我们做的是**线下匿名社交装置**,avatar 必须是**可广播、可改写**的——"我此刻想说什么"。`quote` 就是这个**广播层**。profile 里其他字段偶尔改(换 mood 时),**quote 可以每几分钟改一次**,是产品唯一的高频主动输入。

## 规格

- **类型**:任意 ASCII 字符串
- **长度**:0 – 20 字符(0 即隐藏气泡)
- **编码**:纯 ASCII
  - ESP32 默认字体只支持 ASCII;允许 emoji/中文需要额外字体工程,Phase 1 放弃
  - 浏览器端理论能打任意 unicode,但为了两端一致,前端也限制 ASCII
- **预设灵感**(手机页提供的快捷按钮):
  `need coffee` / `say hi` / `want quiet` / `open to chat` /
  `new here` / `tired` / `daydreaming` / `same vibe?`

## 视觉位置

```
     ┌─────────────────┐
     │ "need coffee"   │    ← ASCII bubble 边框,位于 avatar 上方
     └────────┬────────┘
              ▼
         [avatar]             ← 原作一样的 ASCII 渲染
```

气泡用 ASCII 字符画边框:`┌─┐│└┘` + 指向 avatar 的 `▼` 尾巴。
边框色 = `mainColor`(和 avatar 字符同色)。
文字色 = `mainColor`。

## 传输与更新

手机 → 板子的 JSON 片段:

```json
{ "quote": "need coffee" }
```

**更新策略**:
- 网页端:用户在文本框打字 → `input` 事件即时 rerender + serial.send
- 板子端:收到新 profile → **只重绘 quote 气泡区**(不重绘整屏)
  - 原因:ESP32-C6 SRAM 紧张,全屏重画会看到闪烁
  - 实现:固定 quote 区域坐标,`fillRect(...background...) + drawString(...)` 两步

## 关系澄清:quote vs interests?

早期几轮讨论过是否加一层 "interests" emoji 标签(♪◆♥... 或 🎵🎨❤️...)。**MVP 不加**。原因:

1. hello-stranger 原作没有 interests 这一层——加了破坏原作的极简美学
2. interests 的视觉位置需要额外显示区,会让 avatar 旁边变"贴满标签"
3. quote 的自由文本已经可以承载兴趣表达(`"love coffee"` / `"film nerd"`)

interests **降级为 Phase 2 扩展**。如果 MVP demo 中用户反馈"写不出 quote",再把预设短语 + 图标加回来。

## 在我们的数据模型里的位置

`quote` 是 avatar profile 对象的一个字段,和 sex / color / num / smile / bg_level 并列。它**不经过 hello-stranger 的映射管道**,不影响配色、不影响 sprite 选择——纯粹叠加在渲染层上。

因此 quote 是**最容易单独修改**的字段:前端一个输入框,后端一个 JSON 键,固件一个 drawString 调用。如果以后换成中文 quote 或 emoji quote,只需要换字体(ESP32 端装更大的 unifont 子集字体),其他代码不用改。

## 与 mood 的关系

quote 和 mood **独立**。你可以 mood=tired 同时 quote=`"caffeine fix plz"`——这种"嘴硬身体诚实"的反差是我们项目期望的一种社交表达。

不要强制"quote 必须匹配 mood"。自由文本的价值就在自由。
