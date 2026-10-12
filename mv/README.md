# DEMO · MV

一首歌的 MV 工程：画面由 HTML Canvas 逐帧绘制，跟着歌曲的节拍和歌词时间轴走；最后用无头 Chrome 逐帧渲染、FFmpeg 合成 MP4。

## 文件

| 文件 | 用途 |
| --- | --- |
| `song.mp3` | 歌曲 |
| `stems/` | 伴奏、鼓、贝斯分轨（`analyze.py` 用来读节奏；片尾音乐取自伴奏） |
| `credits_audio.py` | 片尾音乐（伴奏桥段降 3 个半音，变调也变速）接在原曲后面 → `output/谎话_完整音轨.wav`，导出和预览默认用它 |
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
python credits_audio.py                 # 生成完整音轨（原曲 + 片尾），导出前先跑一次
# 浏览器打开 mv.html 预览：空格 播放/暂停，← → 快退/快进 5 秒，D 显示调试信息（拍点、段落、歌词时间线）
node export.mjs --shots 5,22.5,45       # 截几帧到 shots/ 检查画面
node export.mjs --from 20 --to 35       # 导出一小段试看
node export.mjs                         # 整首导出（片头警告 + 正片 + 片尾）→ mv.mp4（30 帧/秒）
node export.mjs --fps 60 --crf 16 --preset slow --out output/谎话_1080p60.mp4   # 正式版：1080p 60 帧/秒
```

> **运动模糊**：默认 `--mb auto`。每帧先渲快门开、关两头，量一下这一帧里画面动了多少像素（2D 层缩略图上的光流 + 镜头参数的变化），动得越快补的子帧越多（2–14 个），相邻子帧最多差 4 像素，所以拖影是连续的一条；不动的帧只渲两个子帧。快门 180°（`--shutter .5`），从这一帧往后开，按 1/30 秒跳的东西（闪烁、雪花、卡帧、抽帧的雨）在一帧里不会糊成两半。导出结束会打印子帧数的分布。`--mb 3` 之类是固定子帧数，`--mb 1` 关掉。
>
> **调色**：最后整体调一次色（`js/main.js` 的 `LOOK_DEFAULT`，参数说明见 `js/gl.js` 的 `present()`）。想试别的调色不用改代码：`--look '{"sat":.9,"curve":.3}'`。

> 时间轴按歌曲算：0 是歌曲开头；片头的光敏警告和 3 秒倒数在 **-3.6 到 0 秒**（没有声音，导出时音轨自动往后推 3.6 秒，截图写 `--shots -2`）。片尾最后那滴雨落到画面正中就是视频第一帧，所以导出的视频可以无缝循环；`mv.html` 预览放到结尾也会接回片头继续放。

> 用 `file://` 直接打开 `mv.html` 即可，不需要起服务器。

## 写画面时的约定

- 所有画面都写在 `render(t)` 里，**只依赖 t**；不要用 `Math.random()` / `Date.now()`，随机数用 `rand(seed)`。这样预览和导出完全一致。
- 音乐信息通过 `M` 查询：`M.energy(t)`、`M.bass(t)`、`M.pulse(t)`（拍点冲击）、`M.downPulse(t)`（重拍）、`M.beat(t)`、`M.section(t)`、`M.lyric(t)`。
