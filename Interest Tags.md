# Interest Tags & Matching Logic

> Source of truth: `version2/web/questions.json` (`Q_INTERESTS` block).
> Last synced: 2026-05-12.

---

## 1. 层级总表(Primary × Subtag)

每个 primary 下挂 8 个 subtag,共 **12 × 8 = 96 个 subtag + 12 个 primary = 108 个标签**。
Primary 和 subtag 在数据层是**两个独立的字段**(`interests` 数组 + `interest_details` 字典),不构成树结构 —— 用户**可以只选 primary 不选 subtag**(subtag 整块是 optional)。

| Primary (icon)    | Subtag 1     | Subtag 2     | Subtag 3      | Subtag 4       | Subtag 5      | Subtag 6      | Subtag 7      | Subtag 8       |
|-------------------|--------------|--------------|---------------|----------------|---------------|---------------|---------------|----------------|
| music (♪)         | jazz         | classical    | pop           | electronic     | rock          | hip-hop       | lo-fi         | indie          |
| film (▶)          | sci-fi       | drama        | comedy        | documentary    | horror        | animation     | art-house     | action         |
| sport (◎)         | running      | yoga         | cycling       | swimming       | climbing      | team-sport    | martial-arts  | skating        |
| art (◈)           | painting     | sculpture    | digital       | illustration   | street-art    | ceramics      | design        | collage        |
| tech (※)          | coding       | ai           | hardware      | gaming-tech    | web           | robotics      | data          | open-source    |
| food (◆)          | cooking      | baking       | asian         | italian        | vegan         | street-food   | coffee        | desserts       |
| gaming (♠)        | rpg          | fps          | indie-games   | puzzle         | strategy      | co-op         | retro         | mobile         |
| travel (✈︎)        | solo         | city         | nature        | road-trip      | backpacking   | food-tour     | festival      | remote         |
| books (❏)         | fiction      | nonfiction   | poetry        | sci-fi         | mystery       | philosophy    | manga         | essays         |
| photo (◉)         | portrait     | street       | landscape     | film-photo     | macro         | night         | phone         | documentary    |
| dance (♬)         | ballet       | hip-hop      | contemporary  | salsa          | swing         | k-pop         | club          | freestyle      |
| pets (♥)          | dogs         | cats         | birds         | fish           | reptiles      | rabbits       | exotic        | farm-animals   |

注意几个**字符串重名但不跨 primary 折叠**的现象:
- `sci-fi` 同时出现在 film + books
- `hip-hop` 同时出现在 music + dance
- `documentary` 同时出现在 film + photo
- 这些在匹配时**按 primary 隔离**统计(详见第 3 节),不会被当成同一个标签。

---

## 2. 全量 Markdown 标签列表

### 一层(Primary, 12 个)
- music
- film
- sport
- art
- tech
- food
- gaming
- travel
- books
- photo
- dance
- pets

### 二层(Subtag, 按 primary 分组)

**music** — jazz, classical, pop, electronic, rock, hip-hop, lo-fi, indie
**film** — sci-fi, drama, comedy, documentary, horror, animation, art-house, action
**sport** — running, yoga, cycling, swimming, climbing, team-sport, martial-arts, skating
**art** — painting, sculpture, digital, illustration, street-art, ceramics, design, collage
**tech** — coding, ai, hardware, gaming-tech, web, robotics, data, open-source
**food** — cooking, baking, asian, italian, vegan, street-food, coffee, desserts
**gaming** — rpg, fps, indie-games, puzzle, strategy, co-op, retro, mobile
**travel** — solo, city, nature, road-trip, backpacking, food-tour, festival, remote
**books** — fiction, nonfiction, poetry, sci-fi, mystery, philosophy, manga, essays
**photo** — portrait, street, landscape, film-photo, macro, night, phone, documentary
**dance** — ballet, hip-hop, contemporary, salsa, swing, k-pop, club, freestyle
**pets** — dogs, cats, birds, fish, reptiles, rabbits, exotic, farm-animals

---

## 3. 匹配逻辑(Matching Logic)

匹配在**两处**计算,语义完全一致但展开形式不同:

| 场景                  | 在哪算                                    | 用什么                          |
|-----------------------|-------------------------------------------|---------------------------------|
| Collective wall 布局  | `web/collective.js` → `similarity(a, b)`  | 弹簧 rest-length 反比缩放       |
| 两块板 NFC tap match  | `web/server.py` → `score_pair()` + `/api/device/tap` | 0–100 score + common_tags 列表 |

### 3.1 评分公式

```
score = primaryShared × 1.0  +  subShared × 3.0
```

- `primaryShared`:两人 `state.interests` 数组的**集合交集大小**(忽略顺序)。
- `subShared`:**按 primary 分桶**统计 `interest_details` 字典的交集
  ——也就是说,A 的 `music: [jazz, indie]` 和 B 的 `music: [jazz, rock]` 在 music 这桶里贡献 1(jazz),
  但 A 的 `books: [sci-fi]` 和 B 的 `film: [sci-fi]` **不会**算重合(分桶隔离,字符串重名不跨桶)。
- 权重 **1 : 3** 的设计意图:同选 `music` 很常见,同选 `jazz` 才稀有,稀有的更值钱。

### 3.2 输出形态

- **Collective wall** 用 score 直接换算 spring rest-length:
  ```
  rest = restMin + (restBase - restMin) × exp(-score × k)
  ```
  分数越高 → rest 越短 → 弹簧把两块 tile 拉得越近。`k = 0.6` 是衰减系数,经验值(0.5–0.8 是甜点区,<0.3 看起来随机,>1.0 全挤一坨)。

- **两板 NFC tap** 把 score 截到 0–100 之后,通过 SSE/HTTP poll 发到两块板的 LCD,同时附带:
  - `common`:重合的 primary tag 列表
  - `common_tags`:**扁平化后**的合并列表(server 端 prefer subtag over primary —— 如果 subtag 有交集,优先回 subtag,因为它更具体)
  - `hint`:server 根据 score 区间挑一条 icebreaker(如 `"ask them about their favorite jazz album"`)

### 3.3 Subtag 是 optional —— 边界情况

- 用户**只选 primary 不选 subtag** → `interest_details` 为空 dict → subShared = 0 → 整个 score 只看一层交集。
- 一人选了 subtag、另一人没选 → 那一桶 subShared = 0,但 primary 重合仍然算分。
- 双方 interests 完全无交集 → score = 0 → 弹簧拉到最大 rest,板上播 "you are so different, but..." 兜底文案。

---

## 4. 播放逻辑(Match Animation State Machine)

定义在 `firmware/serial_avatar/serial_avatar.ino` → `tickMatchAnimation()`。

### 4.1 入口

两块板任意一块完成 NFC tap → server 收到 `/api/device/tap` → 同时把 match payload 推给**两块**对应板的 HTTP poll 队列 → 板上 `handleMatch()` 把当前 avatar 的 bg/main 色冻结成 `matchBg565/matchFg565`(让 match screen 视觉上和刚才的 sprite 连续),然后进入状态机。

### 4.2 状态机相位

```
                   tap
                    ↓
          MATCH_NUMBER_ROLLING      （score>0 时入口；score=0 跳过,直接 STATIC）
                    │
              (老虎机 easing,
               每步 10ms + 进度 × 100ms,
               逼近 target 时减速)
                    ↓
          MATCH_NUMBER_STATIC       ← 按板上按钮推进
                    │
          MATCH_COMMON_FADING_IN    （600ms,fg 从 bg 色 fade 出"you both like" + tags）
                    ↓
          MATCH_COMMON_STATIC       ← 按板上按钮推进
                    │
          MATCH_COMMON_FADING_OUT   （600ms,反向 fade）
                    ↓
          MATCH_HINT_FADING_IN      （600ms,fade 出 wrapped hint 文本）
                    ↓
          MATCH_HINT_STATIC         ← 按板上按钮推进
                    │
                    ↓
                MATCH_IDLE          → 重新渲染 avatar(`outerDirty = true`)
```

> 注:`MATCH_NUMBER_FADING` 这个 phase 在代码里还在,但 `COMMON_FADING_OUT` 之后直接跳到 `HINT_FADING_IN`,**不再经过 NUMBER_FADING**。理由:tags 之后再 flash 一下数字会有"跳回去"的感觉,arc 不连贯。

### 4.3 三个 decision point

整套动画有三处"等用户按按钮才推进"的卡点,**不再是定时器自动播过去**:

1. **NUMBER_STATIC** —— "let them read the percentage"。
2. **COMMON_STATIC** —— "let them digest the shared interests"。
3. **HINT_STATIC** —— "let them read the icebreaker"。

设计动机:演示场景下两块板各被一个人看,自动定时会强行同步两人的阅读节奏;改成手动 advance 让两边各自 acknowledge 之后再翻篇。

### 4.4 颜色派生

整套 match screen 不重新选色,而是**复用刚才那只 avatar 的调色板**:
```cpp
uint32_t bg     = PALETTE[mood][energy];
matchBg565      = hexToRgb565( lightenHex(bg) );   // 屏幕底色
matchFg565      = hexToRgb565( darkenHex(bg) );    // 数字/文字色
```
所以哪怕屏幕从 sprite 切到了纯数字屏,视觉上仍然在同一个色相里,过渡顺。

### 4.5 文字排版常量

(都在 `drawMatchHint` / `drawCommonScreen` 里,v13.6 最终版)
- 字号:全部用 `drawString2x`,16 × 16 px 的 cell(从最早的 8px 翻倍而来)
- 单行最大字符数:`MAX_CHARS = 15`
- 行高:`LINE_H = 18`(16 px glyph + 2 px 间隙,密一点,读起来像"一块文字"不像"列表")
- 共享 hint 的 caption→body 间距:`CAPTION_GAP = 8`
- 屏幕宽固定 240 px,水平居中靠 `(240 - line_w) / 2`

### 4.6 长 hint / 长 tag 列表换行

`wrapHint()` 做的事:
- 按 cell(也就是 16 px 一格)算宽度,而不是按 byte
- 优先在**最后一个空格**断行,fallback 才硬切
- 最大 6 行(hint) / 4 行(common body),溢出截掉
- 每画一行都 `drainSerialIntoBuf()` 一次 —— 防止长文本绘制阻塞 SPI 时,主机端两个 HTTP poll 响应在 USB-CDC RX FIFO 里被合并成一个 1025 B 大行(项目里踩过的坑)。

---

### 4.7 Match Animation — English Reference & Examples

> English version of the state-machine description above, plus three worked
> examples (0% / 33% / 90%) showing what each of the three screens looks
> like for representative interest combinations.

#### 4.7.1 State Machine (English)

```
                    tap
                     ↓
        MATCH_NUMBER_ROLLING       (entered when score > 0;
                                    score = 0 skips straight to STATIC)
                     │
              (slot-machine easing,
               decelerates as it
               approaches target)
                     ↓
        MATCH_NUMBER_STATIC        ← waits for board button press
                     │              (Screen 1 decision point —
                     │               read the percentage)
                     ↓
        MATCH_COMMON_FADING_IN     (caption + tags fade in)
                     ↓
        MATCH_COMMON_STATIC        ← waits for board button press
                     │              (Screen 2 decision point —
                     │               read the shared topics)
                     ↓
        MATCH_COMMON_FADING_OUT    (caption + tags fade out)
                     ↓
        MATCH_HINT_FADING_IN       (wrapped hint text fades in)
                     ↓
        MATCH_HINT_STATIC          ← waits for board button press
                     │              (Screen 3 decision point —
                     │               read the icebreaker)
                     ↓
                 MATCH_IDLE         → repaint avatar
                                      (`outerDirty = true`)
```

#### 4.7.2 Three Screens, Three Button Presses

The full match animation is **three screens** in fixed order:

1. **Screen 1 — Percentage.** A large 0–100 score, centered.
2. **Screen 2 — Common topics.** Caption (`you both like` / `you are so different,`) plus the shared interest tags (or the `but...` fallback).
3. **Screen 3 — Icebreaker hint.** A short prompt suggesting what to ask the other person.

Between every pair of screens the firmware **waits for a press of the board's physical button** — there is no auto-advance timer. The design reason: in a demo each of the two boards is in front of a different observer, and a timer would force both observers to read at the same pace. Manual advance lets each side acknowledge the screen before turning the page.

When `score = 0`, the rolling animation is skipped — Screen 1 paints a static `0%` and Screen 2 uses the `you are so different, but...` fallback instead of `you both like ...`.

#### 4.7.3 Example A — 0% match

**Mocked profiles:**

- Sticker A: `interests = ["sport", "tech"]`, `interest_details = { sport: ["climbing"], tech: ["robotics"] }`
- Sticker B: `interests = ["music", "books"]`, `interest_details = { music: ["jazz"], books: ["poetry"] }`
- Primary overlap: 0. Subtag overlap: 0. → **score = 0**.

**Three screens:**

```
┌─ Screen 1 ─────────────────┐
│                            │
│           0%               │
│                            │
│  (static, no roll —        │
│   score=0 path skips       │
│   straight to STATIC)      │
│                            │
└────────────────────────────┘
          ↓ press button

┌─ Screen 2 ─────────────────┐
│                            │
│   you are so different,    │
│                            │
│           but...           │
│                            │
└────────────────────────────┘
          ↓ press button

┌─ Screen 3 ─────────────────┐
│                            │
│    ask them what they      │
│    were curious about      │
│         this week          │
│                            │
└────────────────────────────┘
          ↓ press button → MATCH_IDLE → avatar repaints
```

#### 4.7.4 Example B — 33% match

**Mocked profiles:**

- Sticker A: `interests = ["music", "film", "travel"]`, `interest_details = { music: ["indie"], film: ["sci-fi"], travel: ["city"] }`
- Sticker B: `interests = ["music", "books", "photo"]`, `interest_details = { music: ["jazz"], books: ["sci-fi"], photo: ["street"] }`
- Primary overlap: 1 (`music`). Subtag overlap: 0 — note that `sci-fi` appears in both A's `film` bucket and B's `books` bucket, but the matcher does **not** merge subtags across primaries.
- Raw score = 1×1.0 + 0×3.0 = 1. After server-side normalisation into the 0–100 display range, the percentage shown on screen is `33%`.
- `common_tags = ["music"]` (no subtag overlap → falls back to the primary tag).

**Three screens:**

```
┌─ Screen 1 ─────────────────┐
│                            │
│          33%               │
│                            │
│  (rolls up from 1 with     │
│   slot-machine easing,     │
│   decelerates near 33)     │
│                            │
└────────────────────────────┘
          ↓ press button

┌─ Screen 2 ─────────────────┐
│                            │
│      you both like         │
│                            │
│          music             │
│                            │
└────────────────────────────┘
          ↓ press button

┌─ Screen 3 ─────────────────┐
│                            │
│    ask them which          │
│    album they had on       │
│    repeat last month       │
│                            │
└────────────────────────────┘
          ↓ press button → MATCH_IDLE → avatar repaints
```

#### 4.7.5 Example C — 90% match

**Mocked profiles:**

- Sticker A: `interests = ["music", "film", "art"]`, `interest_details = { music: ["jazz", "indie"], film: ["sci-fi", "drama"], art: ["painting"] }`
- Sticker B: `interests = ["music", "film", "art"]`, `interest_details = { music: ["jazz", "indie"], film: ["sci-fi"], art: ["painting", "design"] }`
- Primary overlap: 3 (`music`, `film`, `art`). Subtag overlap: 4 (`jazz`, `indie` in music; `sci-fi` in film; `painting` in art).
- Raw score = 3×1.0 + 4×3.0 = 15. After server-side normalisation, the percentage shown on screen is `90%`.
- `common_tags = ["jazz", "indie", "sci-fi", "painting"]` — server prefers subtags over primaries when subtags overlap.

**Three screens:**

```
┌─ Screen 1 ─────────────────┐
│                            │
│          90%               │
│                            │
│  (rolls from 1 with        │
│   slot-machine easing,     │
│   long deceleration tail,  │
│   stops at 90)             │
│                            │
└────────────────────────────┘
          ↓ press button

┌─ Screen 2 ─────────────────┐
│                            │
│      you both like         │
│                            │
│        jazz, indie,        │
│     sci-fi, painting       │
│                            │
│  (wraps at MAX_CHARS=15;   │
│   commas + spaces preserved)│
│                            │
└────────────────────────────┘
          ↓ press button

┌─ Screen 3 ─────────────────┐
│                            │
│    open with the one       │
│    you'd both rewatch      │
│         tonight            │
│                            │
└────────────────────────────┘
          ↓ press button → MATCH_IDLE → avatar repaints
```

#### 4.7.6 Notes on the Examples

- The **hint text** on Screen 3 is selected server-side by score bucket (see `/api/device/tap` in `server.py`). The hints shown above are representative samples, not fixed strings.
- On Screen 2, `common_tags` **prefers subtags over primaries** when subtags overlap — that's why Example C reads `jazz, indie, sci-fi, painting` rather than `music, film, art`.
- Example A (`score = 0`) is the only branch that routes to the `you are so different, but...` fallback. Any raw score ≥ 1 takes the `you both like ...` branch.
- The displayed percentage is **server-normalised**, not the raw weighted-Jaccard number. The raw score grows roughly linearly with the count of shared interests; the server compresses that into 0–100 so the LCD always has a clean 2- or 3-digit display.

---

## 5. 设计不变量(load-bearing)

这些是项目里被反复确认的"不能改"的边界条件,改动前先确认:

1. **Interest 永不上屏**。Phone preview / collective wall / 硬件 LCD 三处 surface 都**不渲染 interest 标签**;interest 在 v10 之后是"纯 matching 层"语义。原来那一行(slot)现在被 nickname 占据(blank = 不渲染)。
2. **subtag 是 optional**。表单里 `qblock-subtags` 这一段默认 hidden,只在用户选了至少一个 primary 之后才显出来,而且整个 fieldset 不参与 step1 通过性校验。
3. **同名 subtag 不跨 primary 折叠**(例:music/hip-hop 和 dance/hip-hop 是两个独立标签)。`similarity()` 用 `interest_details[primary]` 分桶计算,天然就分开了。
4. **server 端 prefer subtag over primary** —— common_tags 的扁平化优先回 subtag。这让板上显示的"you both like __"更具体、更值得 ice-break。
5. **Match animation 三处手动 advance**,不是定时器播放完就走;让两块板各自的观察者掌握节奏。
