# MAKE MIX

一個 VST3 插件，三個角色：人聲軌上選 **人聲**，伴奏軌上選 **伴奏**，總線（Master）上選 **總線**。
同一個 FL Studio 工程裡的 MAKE MIX 互相看得見：按一下 **一鍵混音**，它們一起聽一段歌，然後：

- **貼伴奏**：人聲電平按風格放到「人聲比伴奏」的目標上（用人聲頻段響度比，不被 808 帶偏）；
  伴奏在人聲唱的時候、只在被人聲需要的頻段（280 Hz–6 kHz 六段）自動讓位，人聲停了就回來。
- **人聲染色**：輸入電平歸一、低切、去渾濁、存在感、空氣感、壓縮（量出動態再定壓縮量）、去齒音、溫暖（磁帶式飽和）。不加混響、不加回聲。
- **總線**：音色往風格曲線靠、粘合壓縮、**提升**（低頻壓緊加泛音並收成單聲道、中頻磁帶密度、高頻動態空氣感、中高頻加寬、軟削波），推到風格響度，真峰值限幅在 -1 dBTP。
- **人聲錄音室質感**：電平器 + 峰值壓縮兩級串聯，**圓潤**（2.5–5 kHz 刺的瞬間才壓）。**伴奏粘合**：伴奏自己一層輕粘合，配合讓位讓人聲貼進去。
- **風格化**：15 種風格（TRAP / DRILL / MELODIC RAP / BOOM BAP / R&B / 華語流行 / 華語抒情 / LO-FI / POP / ROCK / EDM / AFROBEATS / MAKE STUDIO 自己的聲音 / GEEBAR 夜色 / UDG 地下 GAMEBOI），界面全部是繁體中文，
  每種都有自己的人聲音色、動態、人聲比伴奏、伴奏讓位、總線響度和音色曲線。
  MAKE STUDIO、GEEBAR、UDG 三種是用真實發行歌曲整首訓練出來的（見下面「訓練」）。
- **人聲跟隨伴奏**：一鍵混音記住主唱段的「人聲比伴奏」，之後副歌伴奏變響、或者歌手唱輕了，人聲在 3 秒內慢慢推回去（最多 +6 / -3 dB）；
  ad-lib、稀疏段不跟。像混音師按段落推人聲。
- **風格化效果**（三個角色都有，不加混響、不加回聲）：電話、收音機、對講機、擴音器、失真、Lo-fi、地下 UDG、錄音帶、黑膠、遊戲機 8-bit，旋鈕調效果量。
- **預設**：人聲 24 個、伴奏 17 個、總線 17 個（含 GEEBAR / UDG 訓練出來的），只改音色、動態和效果，電平和推子留給一鍵混音。

**一鍵分析**（，單個實例就能用，不用別的實例）：播放時按一下，聽 10 秒自己這條軌，然後顯示：
- 伴奏：響度、真峰值、調性（例如 C# minor / E major）、低頻佔比（808 / 底鼓多重）、人聲頻段有多擠、立體聲寬度、最接近的風格（一鍵套用）。
- 人聲：響度、動態、齒音，和專業人聲的頻譜比哪裡偏渾 / 偏暗 / 偏亮，以及該怎麼調。
- 總線：整體響度和真峰值對比風格目標、低 / 中 / 高頻對比風格曲線、立體聲相關（單聲道兼容）。

所有結果都寫進普通參數：旋鈕會動到新位置，可以接著手調，FL 的撤銷能用，**撤銷混音** 一鍵回到混音之前。導出和實時播放的差異低於 -110 dBFS（浮點誤差，聽不出來）。

## 安裝（Windows）

1. 執行 `MAKE-MIX-x.y.z-Setup.exe`（或解壓 zip，把 `MAKE MIX.vst3` 整個資料夾複製到 `C:\Program Files\Common Files\VST3`）。
2. FL Studio：Options ▸ Manage plugins ▸ Find more plugins（或 Start scan），找到 MAKE STUDIO ▸ MAKE MIX。

## 用法

1. 人聲軌最後一個插槽插 MAKE MIX，選 **人聲**（多軌人聲就每軌各插一個）。
2. 伴奏軌最後一個插槽插 MAKE MIX，選 **伴奏**。
3. Master 最後一個插槽插 MAKE MIX，選 **總線**（後面不要再放別的限幅器）。
4. 頂上那行應該顯示 `已連接 · 人聲 · 伴奏 · 總線`。如果寫著「未連接（橋接模式）」：插件包裝器 ▸ Processing ▸ 取消 *Make bridged*。
5. 任選一個實例選風格，會同步到所有實例。
6. 人聲和伴奏的推子保持 0 dB（要調音量用 MAKE MIX 的電平旋鈕）。
7. **循環播放副歌**，按 **一鍵混音**：聽約 20 秒 → 解算 → 完成。每個實例下面會寫它改了什麼。再按一次可以提前結束聆聽。

只插一兩個也能用：只有人聲時只改音色和動態、保持原響度；沒有總線時就不做總線那一步。
沒按過一鍵混音時可以按 **風格預設** 載入這個風格的起點。

## 開發

```
cmake -S make-mix -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # 可加 -DJUCE_DIR=/path/to/JUCE，否則自動下載 JUCE 8.0.10
ninja -C build MakeMix_VST3 mm_mixrender
```

`mm_mixrender` 模擬 FL：人聲、伴奏、總線三個實例共用一個 hub，按 MIX、學習、解算、完整渲染，
再用隨機塊長和隨機處理順序渲染一遍檢查「導出 = 實時」，最後印出各項指標：

```
mm_mixrender --vocal vocal.wav --beat beat.wav --style trap --bpm 94 --out out_dir
mm_mixrender --snapshot ui_dir          # 三個角色的界面截圖
mm_mixrender --bpm 94 --analyze beat beat.wav [report.png]   # 一鍵分析（vocal / beat / bus）
```

結構：`Source/Chains.*`（三條處理鏈，插件和解算器共用同一份代碼）、`Hub.*`（實例登記、人聲 key、錄音、MIX 流程）、
`AutoMix.*`（解算：每一步都用真實的處理鏈在錄音上跑一遍、量出來再定）、`Analysis.*`（響度 / 頻譜 / 動態 / 齒音 / 遮蔽）、
`Styles.h`（風格數值）、`Fx.h`（風格化效果）、`Presets.h`（預設）、`docs/DESIGN.md`（完整設計，v1 實現的是其中的精簡版）。

## 訓練

`tools/train/`：用發行歌曲整首訓練風格目標。
1. 分離人聲 / 伴奏：`sep.py`（MSST 的 BS-Roformer，品質最好、CPU 上慢）或 htdemucs（快 8 倍）+ `link_demucs.sh`。
2. `mm_mixrender --measure-song vocal.wav inst.wav mix.wav`：整首 + 自動分段（前奏 / 主歌 / 副歌 / 間奏 / 尾奏）+ 每 5 秒一個 20 秒窗，全部用插件自己的分析算法。
3. `train.py CORPUS`：按風格統計（中位數和四分位距）、副歌比主歌、20 秒窗和整首的偏差、伴奏分類（留一首驗證）。
4. 驗證：`mm_mixrender ... --reference 原曲` 比混音結果和發行版的音色距離。

目前的訓練資料：MAKE STUDIO 4 首、Geebar 16 首、GAMEBOI 25 個 30 秒片段。伴奏分類（只靠伴奏猜風格）在這三類上只有 64–71%，所以一鍵分析的「最接近的風格」只是建議。
Windows 版由 `.github/workflows/make-mix.yml` 在 GitHub Actions 上編譯、跑 pluginval、發佈到 Releases。
