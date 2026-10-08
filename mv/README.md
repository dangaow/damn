# DEMO · MV

一首歌的 MV 工程：画面由 HTML Canvas 逐帧绘制，跟着歌曲的节拍和歌词时间轴走；最后用无头 Chrome 逐帧渲染、FFmpeg 合成 MP4。

## 文件

| 文件 | 用途 |
| --- | --- |
| `song.mp3` | 歌曲 |
| `lyrics.lrc` | 歌词：每句的文字和开始时间 |
| `lyrics_words.lrc` | 逐字时间轴（每个字什么时候唱到），标错的字会被自动插值修正 |
| `analyze.py` | 分析歌曲和歌词 → `data.js`（BPM、拍点、重拍、能量曲线、段落、逐字歌词时间） |
| `data.js` | 分析结果，`mv.html` 直接加载 |
| `mv.html` | 播放器 + 画面源码，`render(t)` 画出第 t 秒的画面 |
| `STORYBOARD.md` | 分镜说明 |
| `fonts.py`、`fonts/` | 从 Google Fonts 下载用到的字（子集），导出时不需要联网 |
| `export.mjs` | 逐帧导出 MP4 / 截图检查 |

## 使用

需要 Python 3、Node.js 22+、Chrome/Edge/Chromium、PATH 中的 FFmpeg。

```bash
pip install -r requirements.txt
python analyze.py                       # 改了歌曲或歌词后重新跑
python fonts.py                         # 改了歌词或画面里的文字后重新跑（需要联网）
# 浏览器打开 mv.html 预览：空格 播放/暂停，← → 快退/快进 5 秒，D 显示调试信息（拍点、段落、歌词时间线）
node export.mjs --shots 5,22.5,45       # 截几帧到 shots/ 检查画面
node export.mjs --from 20 --to 35       # 导出一小段试看
node export.mjs                         # 整首导出 → mv.mp4
```

> 用 `file://` 直接打开 `mv.html` 即可，不需要起服务器。

## 写画面时的约定

- 所有画面都写在 `render(t)` 里，**只依赖 t**；不要用 `Math.random()` / `Date.now()`，随机数用 `rand(seed)`。这样预览和导出完全一致。
- 音乐信息通过 `M` 查询：`M.energy(t)`、`M.bass(t)`、`M.pulse(t)`（拍点冲击）、`M.downPulse(t)`（重拍）、`M.beat(t)`、`M.section(t)`、`M.lyric(t)`。
