# 《Isolated》竖屏短视频

从 MV 成片（`../Isolated_MV.mp4`）里截最抓人的几段，排成 1080×1920 · 30 fps 的竖屏短视频，每条 8–11 秒。

```bash
node promo/promo.mjs              # 全部 → promo/out/Isolated_*.mp4
node promo/promo.mjs 03_dangao_w  # 只出一条
```

| 文件 | 段落 | 时长 |
| --- | --- | --- |
| `Isolated_01_onemore.mp4` | 鼓抽空 → 副歌一，停在 ONE MORE 字卡 | 11.3 s |
| `Isolated_02_rain.mp4` | 第一记底鼓 → 我心里下起 rain → ISOLATED 片名卡 → would u know that | 8.0 s |
| `Isolated_03_dangao_w.mp4` | dangao_w：鼓回来那一下（铁笼砸碎栏杆）→ 风铃 → 我只是不懂我真的不懂 | 11.2 s |
| `Isolated_04_rose.mp4` | dangao_w：纪念的花束被你丢掉 → 删掉曾经的唯一 → 定格在「下段 back to you, yuki」 | 8.3 s |

- 画面：MV 原样放在中间（不裁），背景是同一段画面糊开压暗；`promo.html` 画叠字层（片名、署名、跟着歌词走的大字幕和翻译、MV OUT NOW）。
- 第一帧就是封面：片名、红线、第一句歌词、歌手名第一帧都在。结尾硬切，平台循环播放时接得上。
- 要紧的字都避开平台界面：左右留 130 px，底部 340 px 不放东西。
- 翻译里的两处错字只在短视频里改（内心的阴霾、沉溺于你），MV 成片不动；`fonts/` 是补了这两个字的思源宋体子集。
- 声音用成片里那一段，限幅到真峰值约 -1 dBTP，响度约 -11 LUFS。
