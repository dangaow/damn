# 小克的一回合

一支 2 分 38 秒的小短片：你按下 Enter 之后、到小克（Claude 的像素小螃蟹）回你之前，这一回合里真正发生了什么。
片子里的命令、输出、文件名、分支名都是这一回合真实发生过的事。

画面由 HTML Canvas 逐帧绘制，配乐和音效由 Python 合成；导出时无头 Chrome 一帧一帧算，**用 PNG 无损**交给 FFmpeg。
1920 × 1080、30 fps，共 4740 帧。做法照 [abstract-algebra-promo](https://github.com/AnctyEnly453/abstract-algebra-promo) 和本仓库 `mv/` 的惯例。

## 文件

| 文件 | 用途 |
| --- | --- |
| `clawd.html` | 播放器 + 画面源码，`render(t)` 画出第 t 秒的画面 |
| `STORYBOARD.md` | 分镜：10 段，每段讲什么、画面怎么动 |
| `cues.json` | 段落表和音效点（由页面导出，`music.py` 读它编曲） |
| `music.py` | 合成配乐和音效 → `output/music.wav`、`music.mp3` |
| `music.mp3` | 播放器直接用的配乐 |
| `fonts.py`、`fonts/` | 从 Google Fonts 下载用到的字（子集），导出时不需要联网 |
| `export.mjs` | 逐帧导出 MP4 / 截图检查 / 导出音效点 |

## 使用

需要 Python 3（numpy、scipy）、Node.js 22+、Chrome/Edge/Chromium、PATH 中的 FFmpeg。

```bash
pip install -r requirements.txt
python fonts.py                          # 改了画面里的文字后重新跑（需要联网）
node export.mjs --cues cues.json         # 改了段落或音效点后重新跑
python music.py                          # → output/music.wav、music.mp3
# 浏览器打开 clawd.html 预览：空格 播放/暂停，← → 快退/快进 5 秒，F 全屏
node export.mjs --shots 10,55,96         # 截几张 PNG 到 shots/ 检查画面
node export.mjs --from 50 --to 74        # 导出一小段试看
node export.mjs                          # 整片导出 → output/小克的一回合.mp4
```

### 关于「无损」

- **传帧无损**（默认）：每一帧用 `canvas.toDataURL('image/png')` 取出，PNG 原样交给 FFmpeg，中间没有 JPEG 那种有损压缩。
  加 `--jpeg` 可以改用 JPEG 传帧，快一点，适合试看。
- **无损母版**：`node export.mjs --master` 会多输出一个 `output/小克的一回合_master.mkv`（FFV1，RGB 原样保留，配乐 FLAC）。
  文件很大（胶片颗粒让每帧都很难压缩，整片大约几个 GB），适合留作母版或再剪辑。
- **PNG 序列**：`node export.mjs --frames` 会把每一帧另存成 `output/frames/frame_00000.png`（4740 张，同样很占空间）。
- 发布用的 `.mp4` 是 H.264（CRF 16、yuv420p），这一步是有损的——各平台播放都要这样。

### 写画面时的约定（和 `mv/` 一样）

- 所有画面都写在 `render(t)` 里，**只依赖 t**；不用 `Math.random()` / `Date.now()`，随机数用 `hash(n)` / `mulberry(seed)`。这样预览和导出完全一致。
- 时间轴以小节为单位：120 BPM，一小节 2 秒；每段的 `bars`、`mus`（编曲段落）、`subs`（字幕）、`sfx`（音效点）都写在 `SC` 里。
- 运动模糊：每帧算 3 个子帧（快门 180°）取平均，`--mb 1` 关掉。

`output/`、`shots/` 是生成物，不进 Git。
