# MAKE MIX

一個 VST3 插件，三個角色：人聲軌上選 **VOCAL**，伴奏軌上選 **BEAT**，總線（Master）上選 **BUS**。
同一個 FL Studio 工程裡的 MAKE MIX 互相看得見：按一下 **MIX**，它們一起聽一段歌，然後：

- **貼伴奏**：人聲電平按風格放到「人聲比伴奏」的目標上（用人聲頻段響度比，不被 808 帶偏）；
  伴奏在人聲唱的時候、只在被人聲需要的頻段（280 Hz–6 kHz 六段）自動讓位，人聲停了就回來。
- **人聲**：輸入電平歸一、低切、去渾濁、存在感、空氣感、壓縮（量出動態再定壓縮量）、去齒音、溫暖、同步回聲、空間。
- **總線**：音色往風格曲線靠、粘合壓縮、推到風格響度，真峰值限幅在 -1 dBTP。
- **風格化**：12 種風格（TRAP / DRILL / MELODIC RAP / BOOM BAP / R&B / 華語流行 / 華語抒情 / LO-FI / POP / ROCK / EDM / AFROBEATS），
  每種都有自己的人聲音色、動態、空間、人聲比伴奏、伴奏讓位、總線響度和音色曲線。

所有結果都寫進普通參數：旋鈕會動到新位置，可以接著手調，FL 的撤銷能用，**UNDO MIX** 一鍵回到 MIX 之前。導出和實時播放結果完全一樣。

## 安裝（Windows）

1. 解壓，把 `MAKE MIX.vst3` 整個資料夾複製到 `C:\Program Files\Common Files\VST3`。
2. FL Studio：Options ▸ Manage plugins ▸ Find more plugins（或 Start scan），找到 MAKE STUDIO ▸ MAKE MIX。

## 用法

1. 人聲軌最後一個插槽插 MAKE MIX，選 **VOCAL**（多軌人聲就每軌各插一個）。
2. 伴奏軌最後一個插槽插 MAKE MIX，選 **BEAT**。
3. Master 最後一個插槽插 MAKE MIX，選 **BUS**（後面不要再放別的限幅器）。
4. 頂上那行應該顯示 `LINK · VOCAL · BEAT · BUS`。如果寫著 `LINK OFF - BRIDGED`：插件包裝器 ▸ Processing ▸ 取消 *Make bridged*。
5. 任選一個實例選 **STYLE**，會同步到所有實例。
6. 人聲和伴奏的推子保持 0 dB（要調音量用 MAKE MIX 的 LEVEL）。
7. **循環播放副歌**，按 **MIX**：`LISTENING …%`（約 15 秒）→ `MIXING...` → `DONE`。每個實例的 *WHAT MIX DID* 會寫它改了什麼。

只插一兩個也能用：只有 VOCAL 時只改音色和動態、保持原響度；沒有 BUS 時就不做總線那一步。
沒按過 MIX 時可以按 **STYLE DEFAULTS** 載入這個風格的起點。

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
```

結構：`Source/Chains.*`（三條處理鏈，插件和解算器共用同一份代碼）、`Hub.*`（實例登記、人聲 key、錄音、MIX 流程）、
`AutoMix.*`（解算：每一步都用真實的處理鏈在錄音上跑一遍、量出來再定）、`Analysis.*`（響度 / 頻譜 / 動態 / 齒音 / 遮蔽）、
`Styles.h`（風格數值）、`docs/DESIGN.md`（完整設計，v1 實現的是其中的精簡版）。
Windows 版由 `.github/workflows/make-mix.yml` 在 GitHub Actions 上編譯、跑 pluginval、發佈到 Releases。
