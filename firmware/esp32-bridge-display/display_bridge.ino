/**
 * MochaTool Display —— 带状态屏的蓝牙 HID 桥接盒固件
 *
 * 硬件：任意 ESP32 DevKit（WROOM-32，¥20-40）+ 外接 SSD1306 0.96" I2C 4针 OLED（¥5-10）
 *       4 根母对母杜邦线免焊直插（下方"接线"）
 *
 * 屏幕（128x64）实时显示：
 *   ┌────────────────┐
 *   │ MochaTool        │
 *   │ WiFi  [OK] 已连 │
 *   │ 被控端[OK] 已连接│  ← 朋友 iPhone 蓝牙连上了盒子
 *   │ 控制端[--] 未连接│  ← 你在家还没通过服务器接入
 *   │ 房间 XIAOMING    │
 *   └────────────────┘
 *
 *  语义说明：
 *  「被控端」= 朋友的 iPhone（通过蓝牙 HID 与盒子配对）
 *  「控制端」= 你的安卓手机（通过服务器连入同一房间，状态由服务器 room_state 广播）
 *
 * 接线（免焊）：
 *   屏幕 VCC → 板子 3V3
 *   屏幕 GND → 板子 GND
 *   屏幕 SCL → 板子 D22 (GPIO22)
 *   屏幕 SDA → 板子 D21 (GPIO21)
 *   蜂鸣器(+/-) → GPIO33 / GND（有源注意正负极、反接不响不坏；无源两极可反接；模块版 VCC→3V3、GND→GND、信号→GPIO33）
 *   震动电机 → GPIO25（小电流电机可直接接；稍大用三极管/MOS 驱动）
 *   散热风扇 PWM → GPIO26（可用 MOS 模块调速，或接支持 PWM 的风扇）
 *
 * 外设（蜂鸣 / 震动 / 风扇）接口：
 *   - 开机有低音量开机音效；连服务器/双方在线会"嘀"一声并震动
 *   - 风扇 5 档调速：0%/20%/50%/70%/100%，操作页用格子状态显示
 *   - 运行中可被手机 App 实时下发 config 指令开关蜂鸣震动/风扇/换房间码，免重烧录
 *
 * 物理按键（板载 BOOT 键，GPIO0，免接线）：
 *   - 单击：循环切页  主页面 → 帮助 → 震动 → 蜂鸣 → 风扇 → 重置 → 主页面
 *   - 双击：在【震动/蜂鸣】页切换该外设 开/关；在【风扇】页循环切换 5 档转速；
 *           在【重置】页启动 5s 倒计时
 *   - 重置倒计时期间：任意按键 = 取消；5s 走满后进入三击确认
 *   - 重置确认页：双击退出，三击执行恢复出厂并重启
 *   - 屏幕右上角常驻小爱心 ♡；切页有渐隐渐现过渡；操作页底部操作提示
 *
 * 开机逻辑：若已填过配置并连上服务器 → 首次连接弹出"配置成功！"烟花特效后进入主屏
 *
 * 烧录（Arduino IDE）：
 *   开发板：ESP32 Dev Module
 *     Tools → Partition Scheme: 选 "Huge APP (3MB No OTA/1MB SPIFFS)"
 *           （中文相关，避免编译报 region overflow）
 *     Tools → Flash Size: 4MB
 *   依赖库：
 *     1. "U8g2"（作者 Oliver Kraus）—— 屏幕驱动
 *     2. "NimBLE-Arduino"（作者 h2zero）
 *     3. "Callback"（作者 Tom Stewart）
 *     4. "WebSockets"（作者 Markus Sattler）
 *     5. "WiFiManager"（作者 tzapu）
 *     6. "ArduinoJson"（作者 Benoit Blanchon，v6）
 *     7. 主库 ZIP 安装：https://github.com/Mystfit/ESP32-BLE-CompositeHID
 *        （Code → Download ZIP → 项目 → 导入库 → 添加 .ZIP 库）
 *     ⚠ 卸载 ESP32-BLE-Keyboard / Mouse / Combo（若装过），否则重复定义报错
 *     ⚠ 屏幕显示乱码/漏字：确认用的是 U8g2 界面里的 "u8g2_font_wqy12_t_gb2312"
 *       完整 GB2312 字库（勿用 gb2312a，它缺全角标点）
 */

#include <U8g2lib.h>
#include <WiFiManager.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <nvs_flash.h>
#include <BleCompositeHID.h>
#include <KeyboardDevice.h>
#include <MouseDevice.h>

// ---- 配网页面字段由用户填写（无内置默认服务器/房间码）----
// 修改前两行注意：DEFAULT_SERVER / DEFAULT_ROOM 已移除，全部由配网页面输入并按次保存

// ---- 配网页面美化：居中卡片 + 房间"留空=沿用上次"提示 ----
// 通过 WiFiManager 的 setCustomHeadElement 注入 <style>/<script>，
// 不动它默认的保存逻辑（输入过的字段下次自动带值，留空则不覆盖上次保存）。
static const char settingsHead[] PROGMEM = R"html(<style>
  *{box-sizing:border-box}
  body{
    font-family:-apple-system,"PingFang SC","Microsoft YaHei",system-ui,sans-serif;
    background:linear-gradient(135deg,#667eea,#764ba2);
    margin:0;min-height:100vh;
    display:flex;align-items:center;justify-content:center;padding:20px;
  }
  .card{
    background:#fff;border-radius:18px;box-shadow:0 14px 44px rgba(0,0,0,.25);
    padding:28px 30px;width:100%;max-width:420px;text-align:center;
  }
  .logo{font-size:22px;font-weight:700;color:#5a3fd4;margin-bottom:2px}
  .sub{color:#8a8a9e;font-size:12px;margin:2px 0 18px}
  .card form{display:flex;flex-direction:column;gap:12px}
  .card label{font-size:13px;color:#2d2a3e;text-align:left;font-weight:700}
  .card input[type=text],.card input[type=password],.card select{
    width:100%;padding:12px 14px;font-size:15px;color:#2d2a3e;
    background:#f4f3fa;border:1px solid #e8e6f2;
    border-radius:12px;outline:none;transition:.2s;
  }
  .card input:focus,.card select:focus{border-color:#667eea;box-shadow:0 0 0 3px rgba(102,126,234,.18)}
  .hint{font-size:12px;color:#8a8a9e;text-align:left;margin-top:-4px}
  .card input[type=submit],.card input[type=button],.card button{
    width:100%;padding:13px;font-size:16px;font-weight:600;color:#fff;
    background:linear-gradient(135deg,#667eea,#764ba2);border:none;
    border-radius:14px;cursor:pointer;margin-top:6px;
  }
  .card input[type=submit]:active,.card button:active{
    background:linear-gradient(135deg,#5568d5,#6a4396);
  }
</style>
<script>
window.addEventListener('DOMContentLoaded',function(){
  function box(n){return document.querySelector('[name="'+n+'"]')}
  // 把 WiFiManager 默认表单包进居中卡片
  var f=document.querySelector('form');
  if(f && f.parentNode){
    var c=document.createElement('div'); c.className='card';
    var logo=document.createElement('div'); logo.className='logo'; logo.textContent='MochaTool';
    var sub=document.createElement('div'); sub.className='sub'; sub.textContent='请配置 WiFi 网络';
    f.parentNode.insertBefore(c, f); c.appendChild(logo); c.appendChild(sub); c.appendChild(f);
  }
  var room=box('room'), server=box('server');
  function addHint(input,text){
    var h=document.createElement('div'); h.className='hint';
    h.textContent=text; input.parentNode.insertBefore(h, input.nextSibling);
  }
  if(room){
    room.setAttribute('placeholder', '房间码  留空沿用上次');
    addHint(room, '留空沿用上次保存的房间码');
  }
  if(server && !(server.value||'').trim()){
    server.setAttribute('placeholder','ws://服务器IP:9000  留空沿用上次');
  }
});
</script>)html";

// ---- 指令协议（与安卓端约定）----
// {type:"hid", action:"move|drag|down|up|click|scroll", dx:int, dy:int}
// {type:"hid_text", value:"要打的字（英文/拼音，走真实键盘）"}

// ---- 屏幕（外接 SSD1306 OLED，HW I2C 默认 SDA=GPIO21 SCL=GPIO22）----
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

BleCompositeHID compositeHID("MochaTool", "MochaTool", 100);
KeyboardDevice* keyboard = new KeyboardDevice();
MouseDevice* mouse = new MouseDevice();

WebSocketsClient ws;
Preferences prefs;
String serverUrl, room;

unsigned long lastPing = 0;
unsigned long lastDraw = 0;
bool wsConnected = false;
bool wifiOk = false;
bool controllerOnline = false;   // 控制端（安卓 viewer）是否在本房间在线，由服务器 room_state 通知

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

// ---- 外设引脚（接线见文件头注释；部分 ROM 引脚可能不同，按需改这里） ----
#define PIN_BUZZER  33   // 蜂鸣器：GPIO33 → 蜂鸣器正极（负极接 GND）
#define PIN_VIB     25   // 震动电机：GPIO25 → 电机驱动管控制脚（小电流电机也可直连）
#define PIN_FAN     26   // 散热风扇 PWM：GPIO26 → MOS/风扇 PWM 控制脚

// ---- 运行时配置（存 NVS，可在手机端/本地面板实时调整，免重烧录） ----
// ---- LEDC 通道：core 3.x 用 pin 寻址（ledcWrite 传 pin），core 2.x 用 channel ----
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  #define VIB_PWM_CH   PIN_VIB
  #define BUZZ_PWM_CH  PIN_BUZZER
  #define FAN_PWM_CH   PIN_FAN
#else
  #define VIB_PWM_CH   0   // LEDC 通道 0 → 震动
  #define BUZZ_PWM_CH  1   // LEDC 通道 1 → 蜂鸣
  #define FAN_PWM_CH   2   // LEDC 通道 2 → 风扇
#endif
#define PWM_BITS     8   // 8 位分辨率，占空比 0..255

// 动态改蜂鸣频率（无源蜂鸣器变调用）：core 3.x 用 ledcChangeFrequency，core 2.x 用 ledcSetup
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  #define BUZZ_SET_FREQ(f)  ledcChangeFrequency(PIN_BUZZER, (f), PWM_BITS)
#else
  #define BUZZ_SET_FREQ(f)  ledcSetup(BUZZ_PWM_CH, (f), PWM_BITS)
#endif
#define FAN_PWM_INVERTED 1 // 1=低电平触发模块（如 JY-25-003），输出占空比取反

// 外设统一 5 档百分比：0/20/50/70/100（震动/蜂鸣/风扇共用同一档位结构）
const uint8_t PERI_LEVELS = 5;
const char* const PERI_PCT[PERI_LEVELS] = {"0%", "20%", "50%", "70%", "100%"};
const uint8_t fanSpeeds[PERI_LEVELS]  = {0, 51, 128, 178, 255}; // 风扇 duty
const uint8_t vibSpeeds[PERI_LEVELS]  = {0, 51, 128, 178, 255}; // 震动强度 duty（与风扇同映射）
const uint8_t buzzSpeeds[PERI_LEVELS] = {0, 40, 70, 100, 130};  // 蜂鸣音量 duty（上限低，避免太吵）
uint8_t fanLevel = 0;          // 风扇档位（默认 0 档 = 停）
bool fanOn = false;            // 风扇开关（0 档时视为关）
uint8_t vibLevel = 3;          // 震动强度档位（默认 70%）
uint8_t buzzLevel = 3;         // 蜂鸣音量档位（默认 70%）

// ---- 物理按键（板载 BOOT 键，GPIO0 = 电源键旁的 BOOT，按下接地，INPUT_PULLUP）----
#define PIN_BUTTON    0                // 免额外接线，直接用板子自带 BOOT 键
#define DBL_GAP_MS    280              // 两次松开在多少毫秒内算双击
#define DEBOUNCE_MS   20               // 按下抖动过滤

// 页面
enum Page { P_MAIN, P_HELP, P_VIB, P_BUZZ, P_FAN, P_RESET };
Page page = P_MAIN;

// 按键状态机（单击 / 双击）
bool btnDown = false;
unsigned long btnDownAt = 0;
unsigned long btnUpAt = 0;             // 松开时刻，用于识别双击
int clickCount = 0;                    // 当前窗口内点击次数
bool skipRelease = false;              // 重置倒计时取消后，吞掉本次松开的点击

// 重置流程
#define RST_HOLD_MS   5000             // 重置倒计时 5s
bool resetArmed = false;               // 正在 5s 走条（任意按键取消）
unsigned long resetArmedAt = 0;
bool resetConfirming = false;          // 走条完成，等待三击确认（双击退出）
int  resetConfirmCount = 0;            // 确认阶段累计点击数
unsigned long confirmDeadline = 0;     // 确认窗口超时

// 配置成功烟花
struct Part { float x, y, vx, vy; uint8_t life; };
Part parts[42];
bool configSplashShown = false;        // 是否已展示过烟花（仅首次连服务器展示）

// 被控端（iPhone 蓝牙）连接沿触发震动
bool lastBleConnected = false;

// ---- HID 鼠标位移累积 + 节流（避免 BLE 报告队列积压导致卡顿/断连）----
int  pendingDx = 0, pendingDy = 0;   // 累积的鼠标位移（待合并发送）
int8_t pendingWheel = 0;             // 累积滚轮
bool dragHeld = false;               // 左键是否处于按下拖动状态
unsigned long lastHidFlush = 0;
#define HID_FLUSH_MS  15             // 每 15ms 合并刷新一次（≈66 报告/s，稳妥不积压）

// WiFi 断线自愈
unsigned long lastWifiRetry = 0;
int wifiFailCount = 0;

// ============================================================
// 初始化
// ============================================================

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);          // 板载 BOOT 键（按下 = LOW）

  // ---- 状态屏启动 ----
  u8g2.begin();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);   // 中文字体（含常用汉字）
  runBootAnimation();                        // 开启动画：MAKE STUDIO 渐显→停留→渐隐
  drawStatus("启动中…");

  // ---- 蓝牙 HID 键鼠启动（iPhone 配对后终身自动回连）----
  compositeHID.addDevice(keyboard);
  compositeHID.addDevice(mouse);
  compositeHID.begin();
  Serial.println("[BLE] MochaTool 已广播，等待 iPhone 配对…");

  // ---- 读取保存的配置 ----
  prefs.begin("rcbridge", false);
  serverUrl = prefs.getString("server", "");   // 无内置默认服务器，由用户填写
  room = prefs.getString("room", "");          // 无内置默认房间码，由用户填写

  // 外设配置：蜂鸣器/震动/风扇（均 5 档百分比，存 NVS）
  vibLevel  = constrain(prefs.getInt("vib", 3), 0, PERI_LEVELS - 1);
  buzzLevel = constrain(prefs.getInt("buzz", 3), 0, PERI_LEVELS - 1);
  fanLevel = constrain(prefs.getInt("fan", 0), 0, PERI_LEVELS - 1);
  fanOn = (fanLevel > 0) ? prefs.getBool("fanOn", true) : false;
  setupPeripherals();                          // 初始化 PWM 并应用到保存的配置
  bootTune();                                  // 四音符开机音效 + 同步震动

  // ---- WiFi 配网 ----
  drawStatus("配网/连WiFi…");
  softBeep();
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);

  // 配网热点用固定 IP（避开常见 192.168.x，防止被局域网路由器占用冲突）
  // 连上 "mochadangao" 后访问 http://10.10.10.1 打开配网页
  wm.setAPStaticIPConfig(
      IPAddress(10, 10, 10, 1),   // 热点自身 IP（也是网关）
      IPAddress(10, 10, 10, 1),
      IPAddress(255, 255, 255, 0));

  // 注入美化页面 + "留空沿用上次"提示（见文件顶部 settingsHead）
  wm.setCustomHeadElement(settingsHead);

  WiFiManagerParameter pServer("server", "服务器地址 ws://…", serverUrl.c_str(), 80);
  WiFiManagerParameter pRoom("room", "房间号", room.c_str(), 20);
  wm.addParameter(&pServer);
  wm.addParameter(&pRoom);

  if (!wm.autoConnect("mochadangao")) {
    Serial.println("[WiFi] 配网超时，重启…");
    drawStatus("配网超时,重启…");
    delay(2000);
    ESP.restart();
  }

  String newServer = pServer.getValue();
  String newRoom = pRoom.getValue();
  newServer.trim(); newRoom.trim(); newRoom.toUpperCase();
  if (newServer.length()) { serverUrl = newServer; prefs.putString("server", serverUrl); }   // 留空则沿用上次
  if (newRoom.length()) { room = newRoom; prefs.putString("room", room); }                    // 留空则沿用上次

  wifiOk = true;
  WiFi.setSleep(false);                  // 关闭 WiFi 省电（modem sleep），避免 WebSocket 心跳延迟导致断连
  softBeep();                            // 配置保存完成，小声提示
  Serial.printf("[WiFi] 已连接 %s\n", WiFi.SSID().c_str());
  Serial.printf("[CFG] server=%s room=%s\n", serverUrl.c_str(), room.c_str());

  // ---- 配置校验：未填服务器地址则不连服务器，提示重新配网 ----
  if (serverUrl.length() == 0 || room.length() == 0) {
    Serial.println("[CFG] 未配置服务器地址或房间码，等待重新配网…");
    drawStatus("未配置服务器/房间");
    return;
  }

  // ---- WebSocket 连中继服务器 ----
  drawStatus("连接服务器…");
  softBeep();
  connectWebSocket();
}

void connectWebSocket() {
  String url = serverUrl;
  url.replace("ws://", "");
  url.replace("wss://", "");
  bool secure = serverUrl.startsWith("wss://");
  int slash = url.indexOf('/');
  String hostPort = slash >= 0 ? url.substring(0, slash) : url;

  String host = hostPort;
  uint16_t port = secure ? 443 : 9000;   // 默认 9000（与服务器端口一致），避免漏填端口连不上
  int colon = hostPort.indexOf(':');
  if (colon >= 0) {
    host = hostPort.substring(0, colon);
    port = hostPort.substring(colon + 1).toInt();
  }

  ws.begin(host, port, "/");
  if (secure) ws.beginSSL(host.c_str(), port, "/");
  ws.onEvent(onWebSocketEvent);
  ws.setReconnectInterval(3000);
  // 协议层心跳：15s 一次 ping、5s 超时、连失 2 次判死线
  // 防止 WiFi 静默断链后 wsConnected 还挂在 true（屏幕显示"控制端已连接"骗人）
  ws.enableHeartbeat(15000, 5000, 2);
}

// ============================================================
// 外设：蜂鸣器 / 震动
// ============================================================

/** 蜂鸣器短鸣一次（事件提示音，蜂鸣 0 档静音；duty=相对音量 0..255，按档位缩放） */
void buzzerBeep(int ms = 120, int duty = 110) {
  if (buzzLevel == 0) return;
  ledcWrite(BUZZ_PWM_CH, (int)(duty * buzzSpeeds[buzzLevel] / 130.0f));
  delay(ms);
  ledcWrite(BUZZ_PWM_CH, 0);
}

/** 状态切换小声提示音（音量统一、小声） */
void softBeep() {
  if (buzzLevel == 0) return;
  ledcWrite(BUZZ_PWM_CH, (int)(60 * buzzSpeeds[buzzLevel] / 130.0f));
  delay(40);
  ledcWrite(BUZZ_PWM_CH, 0);
}

/** 播放指定频率音符（无源蜂鸣器变调，单位 Hz/ms），结束后恢复默认提示音频率 2500Hz */
void playTone(int freq, int ms, int duty = 110) {
  if (buzzLevel == 0) return;
  BUZZ_SET_FREQ(freq);
  ledcWrite(BUZZ_PWM_CH, (int)(duty * buzzSpeeds[buzzLevel] / 130.0f));
  delay(ms);
  ledcWrite(BUZZ_PWM_CH, 0);
  BUZZ_SET_FREQ(2500);   // 恢复事件提示音频率
}

/** 震动：强度从低到高线性爬升（苹果式"嗡——"），到顶立刻停止，无下降沿 */
void vibrateRamp(int ms, int maxStrength = 220) {
  if (vibLevel == 0) return;
  float g = vibSpeeds[vibLevel] / 255.0f;
  int steps = ms / 10;
  if (steps < 1) steps = 1;
  for (int i = 0; i <= steps; i++) {
    int duty = (int)(map(i, 0, steps, 40, maxStrength) * g);
    ledcWrite(VIB_PWM_CH, duty);
    delay(ms / steps);
  }
  ledcWrite(VIB_PWM_CH, 0);
}

/** 震动一次（事件提示，强度固定，按档位缩放） */
void vibrateOnce(int ms = 200, int strength = 200) {
  if (vibLevel == 0) return;
  ledcWrite(VIB_PWM_CH, (int)(strength * vibSpeeds[vibLevel] / 255.0f));
  delay(ms);
  ledcWrite(VIB_PWM_CH, 0);
}

/** 开机音效：do mi so do'（无源蜂鸣器变调），每个音符同步一段上升震动 */
void bootTune() {
  // 音符频率：C4, E4, G4, C5
  const int tones[4] = {262, 330, 392, 523};
  const int toneMs = 170;
  const int gapMs  = 50;
  for (int i = 0; i < 4; i++) {
    // 蜂鸣 + 震动同时开始
    if (buzzLevel > 0) {
      BUZZ_SET_FREQ(tones[i]);
      ledcWrite(BUZZ_PWM_CH, (int)(100 * buzzSpeeds[buzzLevel] / 130.0f));
    }
    if (vibLevel > 0) {
      // 震动在音符期间从弱快速爬到强，然后断掉
      float g = vibSpeeds[vibLevel] / 255.0f;
      int rampSteps = toneMs / 12;
      for (int s = 0; s <= rampSteps; s++) {
        int duty = (int)(map(s, 0, rampSteps, 35, 230) * g);
        ledcWrite(VIB_PWM_CH, duty);
        delay(toneMs / rampSteps);
        if (s == rampSteps) ledcWrite(VIB_PWM_CH, 0);
      }
    } else {
      delay(toneMs);
    }
    // 音符结束，蜂鸣停
    ledcWrite(BUZZ_PWM_CH, 0);
    if (i < 3) delay(gapMs);
  }
  // 恢复默认提示音频率
  BUZZ_SET_FREQ(2500);
}

/** 切页同步音效：蜂鸣 "bi" 一声 + 震动完全同步 */
void pageBeepVib() {
  const int beepMs = 80;
  if (buzzLevel > 0) {
    BUZZ_SET_FREQ(2000);
    ledcWrite(BUZZ_PWM_CH, (int)(90 * buzzSpeeds[buzzLevel] / 130.0f));
  }
  if (vibLevel > 0) {
    float g = vibSpeeds[vibLevel] / 255.0f;
    int steps = beepMs / 8;
    for (int s = 0; s <= steps; s++) {
      int duty = (int)(map(s, 0, steps, 50, 200) * g);
      ledcWrite(VIB_PWM_CH, duty);
      delay(beepMs / steps);
      if (s == steps) ledcWrite(VIB_PWM_CH, 0);
    }
  }
  ledcWrite(BUZZ_PWM_CH, 0);
  BUZZ_SET_FREQ(2500);
}

/** 初始化所有外设 PWM 通道并应用到保存的配置 */
void setupPeripherals() {
  // 震动 5kHz / 蜂鸣 2.5kHz / 风扇 25kHz
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcAttach(PIN_VIB, 5000, PWM_BITS);
  ledcAttach(PIN_BUZZER, 2500, PWM_BITS);
  ledcAttach(PIN_FAN, 25000, PWM_BITS);
#else
  ledcSetup(VIB_PWM_CH, 5000, PWM_BITS);
  ledcAttachPin(PIN_VIB, VIB_PWM_CH);
  ledcSetup(BUZZ_PWM_CH, 2500, PWM_BITS);
  ledcAttachPin(PIN_BUZZER, BUZZ_PWM_CH);
  ledcSetup(FAN_PWM_CH, 25000, PWM_BITS);
  ledcAttachPin(PIN_FAN, FAN_PWM_CH);
#endif
  applyFan();
}

/** 应用风扇 PWM 输出 */
void applyFan() {
  uint8_t duty = (fanOn && fanLevel > 0) ? fanSpeeds[fanLevel] : 0;
#if FAN_PWM_INVERTED
  duty = 255 - duty; // 低电平触发模块：高电平=关，低电平=开
#endif
  ledcWrite(FAN_PWM_CH, duty);
  Serial.printf("[FAN] %s 档位=%d duty=%d\n", fanOn ? "开" : "关", fanLevel, duty);
}

/** 循环切换风扇档位：0→20→50→70→100→0 */
void cycleFanLevel() {
  fanLevel++;
  if (fanLevel >= PERI_LEVELS) fanLevel = 0;
  fanOn = (fanLevel > 0);
  prefs.putInt("fan", fanLevel);
  prefs.putBool("fanOn", fanOn);
  applyFan();
  buzzerBeep(60, 70);
  Serial.printf("[BTN] 风扇 -> %s\n", PERI_PCT[fanLevel]);
}

// ============================================================
// 外设开关 + 触摸切换（长按 切开关 / 短按 切页）
// ============================================================

/** 循环切换震动强度档位：0→20→50→70→100→0 */
void cycleVibLevel() {
  vibLevel++;
  if (vibLevel >= PERI_LEVELS) vibLevel = 0;
  prefs.putInt("vib", vibLevel);
  if (vibLevel == 0) ledcWrite(VIB_PWM_CH, 0);
  else vibrateOnce(120, 160);
  buzzerBeep(60, 70);
  Serial.printf("[BTN] 震动 -> %s\n", PERI_PCT[vibLevel]);
}

/** 循环切换蜂鸣音量档位：0→20→50→70→100→0 */
void cycleBuzzLevel() {
  buzzLevel++;
  if (buzzLevel >= PERI_LEVELS) buzzLevel = 0;
  prefs.putInt("buzz", buzzLevel);
  if (buzzLevel == 0) ledcWrite(BUZZ_PWM_CH, 0);
  else buzzerBeep(120, 90);   // 打开时回一个提示音确认
  Serial.printf("[BTN] 蜂鸣 -> %s\n", PERI_PCT[buzzLevel]);
}

/** 切换到下一页（主页面 → 帮助 → 震动 → 蜂鸣 → 风扇 → 重置 → 主页面） */
void nextPage() {
  pageBeepVib();                         // 切页瞬间同步 "bi" + 震动
  for (int c = 255; c > 0; c -= 20) { u8g2.setContrast(c); delay(4); }   // 渐隐
  switch (page) {
    case P_MAIN:  page = P_HELP; break;
    case P_HELP:  page = P_VIB;  break;
    case P_VIB:   page = P_BUZZ; break;
    case P_BUZZ:  page = P_FAN;  break;
    case P_FAN:   page = P_RESET;break;
    default:      page = P_MAIN; break;
  }
  refreshScreen();                     // 立刻画新页
  for (int c = 0; c <= 255; c += 20) { u8g2.setContrast(c); delay(4); } // 渐现
  u8g2.setContrast(255);
}

/** 启动重置倒计时（重置页双击进入） */
void startResetArmed() {
  resetArmed = true;
  resetArmedAt = millis();
  resetConfirming = false;
  resetConfirmCount = 0;
  buzzerBeep(80, 100);
  Serial.println("[RST] 进入 5s 重置倒计时");
}

/** 取消重置倒计时 */
void cancelResetArmed() {
  resetArmed = false;
  resetConfirming = false;
  resetConfirmCount = 0;
  buzzerBeep(60, 60);
  Serial.println("[RST] 倒计时取消");
  refreshScreen();
}

/** 重置倒计时走满 → 进入三击确认 */
void enterResetConfirm() {
  resetArmed = false;
  resetConfirming = true;
  resetConfirmCount = 0;
  confirmDeadline = millis() + 10000;
  buzzerBeep(200, 120);
  Serial.println("[RST] 进入三击确认");
  refreshScreen();
}

/** 重置盒子：清空全部 NVS（含 WiFi 凭据）→ 重启自动重开配网热点 */
void resetBox() {
  // 重置必须震动 + 提示音（不受开关限制）
  ledcWrite(VIB_PWM_CH, 255); delay(220); ledcWrite(VIB_PWM_CH, 0);
  ledcWrite(BUZZ_PWM_CH, 130); delay(150); ledcWrite(BUZZ_PWM_CH, 0);

  drawStatus("正在清除配置…");
  delay(400);
  prefs.end();
  nvs_flash_erase();     // 擦除 NVS（含服务器/房间/已存 WiFi）
  nvs_flash_init();

  drawStatus("重启中…");
  delay(500);
  ESP.restart();
}

/**
 * 按键扫描：
 *   单击 = 切下一页；双击 = 切换当前页状态（震动/蜂鸣页切换开关；重置页进入 5s 倒计时）
 *   重置倒计时期间任意按键 = 取消；倒计时满 5s 后进入确认页
 *   确认页：双击 = 退出确认；三击 = 执行重置
 */
void handleButton() {
  unsigned long now = millis();
  bool pressed = (digitalRead(PIN_BUTTON) == LOW);   // GPIO0 按下接地

  // 重置倒计时期间：任意按键按下即取消，并吞掉本次按键（不触发切页/双击）
  if (resetArmed && pressed && !btnDown) {
    cancelResetArmed();
    btnDown = true;
    skipRelease = true;
  }

  if (pressed) {
    if (!btnDown) {                       // 按下沿
      btnDown = true; btnDownAt = now;
    }
  } else {
    if (btnDown) {                        // 松开沿
      btnDown = false;
      if (skipRelease) { skipRelease = false; return; }   // 吞掉取消用的松开
      if (now - btnDownAt < DEBOUNCE_MS) return;   // 抖动忽略
      btnUpAt = now;
      clickCount++;
    }
  }

  // 双击窗口结束后再判定动作
  if (clickCount > 0 && !btnDown && now - btnUpAt > DBL_GAP_MS) {
    int clicks = clickCount;
    clickCount = 0;

    if (resetConfirming) {
      // 确认页：双击退出，三击执行重置
      if (clicks == 2) {
        resetConfirming = false;
        resetConfirmCount = 0;
        buzzerBeep(60, 60);
        refreshScreen();
      } else if (clicks >= 3) {
        resetBox();
      } else {
        // 单击：记录为确认计数
        resetConfirmCount++;
        buzzerBeep(60, 80);
        confirmDeadline = now + 10000;
        refreshScreen();
        if (resetConfirmCount >= 3) resetBox();
      }
      return;
    }

    if (clicks == 2) {
      // 双击：切换当前页档位 / 重置页进入倒计时
      if (page == P_VIB) { cycleVibLevel(); refreshScreen(); }
      else if (page == P_BUZZ) { cycleBuzzLevel(); refreshScreen(); }
      else if (page == P_FAN) { cycleFanLevel(); refreshScreen(); }
      else if (page == P_RESET) { startResetArmed(); }
    } else {
      // 单击：切页
      nextPage();
    }
  }

  // 重置倒计时走满
  if (resetArmed && now - resetArmedAt >= RST_HOLD_MS) {
    enterResetConfirm();
  }

  // 确认窗口超时 → 放弃
  if (resetConfirming && now > confirmDeadline) {
    resetConfirming = false;
    resetConfirmCount = 0;
    page = P_MAIN;
    buzzerBeep(60, 60);
    refreshScreen();
  }
}

/** 右上角常驻小爱心 ♡（图形绘制，避免字体缺字；color 决定深浅以适配背景） */
void drawHeart(int color = 1) {
  u8g2.setDrawColor(color);
  u8g2.drawDisc(112, 5, 4, U8G2_DRAW_ALL);   // 左鼓包
  u8g2.drawDisc(119, 5, 4, U8G2_DRAW_ALL);   // 右鼓包（与左重叠，形成连续上缘）
  u8g2.drawTriangle(108, 5, 123, 5, 116, 14); // 下尖（收窄，衔接两个鼓包）
  u8g2.setDrawColor(1);
}

// ============================================================
// 图标绘制（12x12 像素级极简图标）
// ============================================================

/** 画一个 12x12 图标在 (x,y)，y 为顶部；color=1 白，0 黑 */
void drawIconHelp(int x, int y, int color) {
  u8g2.setDrawColor(color);
  u8g2.drawCircle(x + 6, y + 6, 5, 1);
  u8g2.setDrawColor(1);
}
void drawIconVib(int x, int y, int color) {
  u8g2.setDrawColor(color);
  u8g2.drawFrame(x + 2, y + 1, 8, 10);
  u8g2.drawVLine(x + 4, y + 3, 6);
  u8g2.drawVLine(x + 6, y + 3, 6);
  u8g2.setDrawColor(1);
}
void drawIconBuzz(int x, int y, int color) {
  u8g2.setDrawColor(color);
  u8g2.drawTriangle(x + 2, y + 9, x + 5, y + 1, x + 5, y + 7);
  u8g2.drawLine(x + 7, y + 2, x + 9, y);
  u8g2.drawLine(x + 7, y + 5, x + 10, y + 4);
  u8g2.drawLine(x + 7, y + 8, x + 9, y + 10);
  u8g2.setDrawColor(1);
}
void drawIconFan(int x, int y, int color) {
  u8g2.setDrawColor(color);
  u8g2.drawCircle(x + 6, y + 6, 5, 1);
  for (int i = 0; i < 4; i++)
    u8g2.drawLine(x + 6, y + 6, x + 6 + (int)(5 * cos(i * 1.57f)), y + 6 - (int)(5 * sin(i * 1.57f)));
  u8g2.setDrawColor(1);
}
void drawIconReset(int x, int y, int color) {
  u8g2.setDrawColor(color);
  u8g2.drawCircle(x + 6, y + 7, 4, 1);
  u8g2.drawLine(x + 9, y + 2, x + 10, y);
  u8g2.drawLine(x + 10, y, x + 6, y);
  u8g2.drawLine(x + 6, y, x + 6, y + 4);
  u8g2.setDrawColor(1);
}

// ============================================================
// 操作页：统一设计语言（图标 + 大状态条 + 白底划过动画）
// ============================================================

/** 底部操作提示条：在所有操作页面底部常驻 */
void drawFooterHint(const char* hint) {
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  u8g2.drawHLine(0, 51, 128);
  int w = u8g2.getUTF8Width(hint);
  int x = (128 - w) / 2;
  if (x < 0) x = 0;
  u8g2.drawUTF8(x, 62, hint);
}

/** 画横向选项条静态帧：selected 项白底黑字，其余黑底白字 */
void drawSegmentBarFrame(int selected, const char* const labels[], int count, const char* title, int iconType) {
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);

  // 标题栏
  u8g2.setDrawColor(1);
  u8g2.drawBox(0, 0, 128, 15);
  u8g2.setDrawColor(0);
  switch (iconType) {
    case 0: drawIconVib(2, 2, 0); break;
    case 1: drawIconBuzz(2, 2, 0); break;
    case 2: drawIconFan(2, 2, 0); break;
    case 3: drawIconReset(2, 2, 0); break;
    case 4: drawIconHelp(2, 2, 0); break;
  }
  u8g2.drawUTF8(16, 12, title);
  u8g2.setDrawColor(1);

  int barX = 4, barY = 24, barW = 120, barH = 22;
  int segW = barW / count;
  int fillX = barX + selected * segW;

  // 外框与分隔线
  u8g2.drawFrame(barX, barY, barW, barH);
  for (int i = 1; i < count; i++)
    u8g2.drawVLine(barX + i * segW, barY + 1, barH - 2);

  // 白色高亮块
  u8g2.setDrawColor(1);
  u8g2.drawBox(fillX + 1, barY + 1, segW - 2, barH - 2);

  // 文字：当前项黑字，其余白字
  for (int i = 0; i < count; i++) {
    int tx = barX + i * segW + (segW - u8g2.getUTF8Width(labels[i])) / 2;
    int ty = barY + 15;
    u8g2.setDrawColor(i == selected ? 0 : 1);
    u8g2.drawUTF8(tx, ty, labels[i]);
  }
  u8g2.setDrawColor(1);
  drawHeart(0);   // 右上角黑色爱心（标题栏白底上）
}

/** 带白底划过动画的选项条；from=-1 表示无动画 */
void drawSegmentBar(int selected, int from, const char* const labels[], int count, const char* title, int iconType) {
  int barX = 4, barY = 24, barW = 120, barH = 22;
  int segW = barW / count;
  int targetX = barX + selected * segW;
  int startX = (from >= 0) ? (barX + from * segW) : targetX;

  if (from != selected && from >= 0) {
    int steps = 6;
    for (int s = 0; s <= steps; s++) {
      int fillX = startX + (targetX - startX) * s / steps;
      u8g2.clearBuffer();
      // 画静态背景（标题 + 框线 + 分隔线）
      drawSegmentBarFrame(selected, labels, count, title, iconType);
      // 擦除高亮区并重画滑块到中间位置
      u8g2.setDrawColor(0);
      u8g2.drawBox(barX + 1, barY + 1, barW - 2, barH - 2);
      u8g2.setDrawColor(1);
      u8g2.drawBox(fillX + 1, barY + 1, segW - 2, barH - 2);
      // 文字：滑块覆盖的项黑字，其余白字
      for (int i = 0; i < count; i++) {
        int cellCenter = barX + i * segW + segW / 2;
        bool under = (cellCenter >= fillX && cellCenter < fillX + segW);
        int tx = barX + i * segW + (segW - u8g2.getUTF8Width(labels[i])) / 2;
        int ty = barY + 15;
        u8g2.setDrawColor(under ? 0 : 1);
        u8g2.drawUTF8(tx, ty, labels[i]);
      }
      u8g2.setDrawColor(1);
      u8g2.sendBuffer();
      delay(10);
    }
  }

  // 最终帧
  u8g2.clearBuffer();
  drawSegmentBarFrame(selected, labels, count, title, iconType);
}

/** 档位页（震动/蜂鸣/风扇共用）：5 档百分比，双击循环切档 */
void drawLevelPage(const char* title, int iconType, int level, int& lastSel) {
  drawSegmentBar(level, lastSel, PERI_PCT, PERI_LEVELS, title, iconType);
  lastSel = level;
  drawFooterHint("单击切页 · 双击切档位");
  u8g2.sendBuffer();
}

/** 帮助页 */
void drawHelpPage() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);

  // 标题栏白底黑字
  u8g2.drawBox(0, 0, 128, 15);
  u8g2.setDrawColor(0);
  drawIconHelp(2, 2, 0);
  u8g2.drawUTF8(16, 12, "帮助");
  u8g2.setDrawColor(1);
  drawHeart(0);   // 右上角黑色爱心（标题栏白底上）

  u8g2.drawUTF8(4, 26, "单击按钮：切换页面");
  u8g2.drawUTF8(4, 38, "双击按钮：改变档位");
  u8g2.drawUTF8(4, 50, "重置页：双击启动倒计时");

  drawFooterHint("单击切页");
  u8g2.sendBuffer();
}

/** 重置页：三态（待机 / 5s 倒计时 / 三击确认） */
void drawResetPage() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);

  // 标题栏白底黑字
  u8g2.drawBox(0, 0, 128, 15);
  u8g2.setDrawColor(0);
  drawIconReset(2, 2, 0);
  u8g2.drawUTF8(16, 12, "重置配置");
  u8g2.setDrawColor(1);
  drawHeart(0);   // 右上角黑色爱心（标题栏白底上）

  if (resetConfirming) {
    u8g2.drawUTF8(4, 28, "确认要恢复出厂？");
    char buf[24];
    snprintf(buf, sizeof(buf), "已按 %d/3", resetConfirmCount);
    u8g2.drawUTF8(4, 42, buf);
    drawFooterHint("双击退出 · 三击重置");
  } else if (resetArmed) {
    int pct = (int)((long)(millis() - resetArmedAt) * 100 / RST_HOLD_MS);
    if (pct > 100) pct = 100;
    u8g2.drawUTF8(4, 26, "警告：此操作不可逆！");
    u8g2.drawFrame(14, 34, 100, 9);                       // 进度条外框
    u8g2.setDrawColor(0);
    u8g2.drawBox(17, 37, 94 * pct / 100, 3);              // 填充
    u8g2.setDrawColor(1);
    drawFooterHint("按任意键取消");
  } else {
    u8g2.drawUTF8(4, 28, "双击开始重置倒计时");
    u8g2.drawUTF8(4, 42, "将清空配置重开热点");
    drawFooterHint("单击切页 · 双击开始");
  }
  u8g2.sendBuffer();
}

/** 按当前页重绘屏幕（主循环周期调用；每页自带 latest 帧） */
void refreshScreen() {
  static int lastVibSel = -1, lastBuzzSel = -1, lastFanSel = -1;
  switch (page) {
    case P_MAIN:  drawMainStatus(); break;
    case P_HELP:  drawHelpPage(); break;
    case P_VIB:   drawLevelPage("震动强度", 0, vibLevel, lastVibSel); break;
    case P_BUZZ:  drawLevelPage("蜂鸣音量", 1, buzzLevel, lastBuzzSel); break;
    case P_FAN:   drawLevelPage("风扇转速", 2, fanLevel, lastFanSel); break;
    case P_RESET: drawResetPage(); break;
  }
}

// ---- 配置成功烟花：粒子从屏幕上部爆开 + 背景文字 ----
void initFireworks() {
  randomSeed(esp_random());
  for (int i = 0; i < 42; i++) {
    float a = (random(360) * 3.14159f) / 180.0f;
    float sp = random(28, 90) / 10.0f;
    parts[i].x = 64; parts[i].y = 18;
    parts[i].vx = cos(a) * sp; parts[i].vy = -sin(a) * sp;
    parts[i].life = 255;
  }
}

void updateFireworks() {
  static unsigned long last = 0;
  unsigned long now = millis();
  if (now - last < 35) return;
  int dt = (int)(now - last); last = now;
  (void)dt;
  for (int i = 0; i < 42; i++) {
    parts[i].vy += 0.35f;                 // 重力
    parts[i].x += parts[i].vx;
    parts[i].y += parts[i].vy;
    parts[i].vx *= 0.90f; parts[i].vy *= 0.90f;   // 阻尼
    if (parts[i].life > 8) parts[i].life -= 8;
  }
}

void drawFireworksFrame() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  u8g2.setDrawColor(1);
  for (int i = 0; i < 42; i++) {
    int x = (int)parts[i].x, y = (int)parts[i].y;
    if (x >= 0 && x < 128 && y >= 0 && y < 64 && parts[i].life > 120) {
      u8g2.drawPixel(x, y);
    }
  }
  drawUTF8Center("配置成功！", 40);      // 文字叠在烟花之上
  u8g2.sendBuffer();
}

/** 首次连上服务器：烟花庆祝 3s + 提示音/震动，然后进主屏 */
void runConfigSuccess() {
  if (configSplashShown) return;
  configSplashShown = true;
  initFireworks();
  unsigned long end = millis() + 3000;
  while (millis() < end) {
    updateFireworks();
    drawFireworksFrame();
    delay(35);
  }
  buzzerBeep(180, 100);
  vibrateOnce(200);
  u8g2.setContrast(255);
  refreshScreen();
}

// ============================================================
// WebSocket 事件
// ============================================================

void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t len) {
  switch (type) {
    case WStype_CONNECTED: {
      StaticJsonDocument<128> reg;
      reg["type"] = "register";
      reg["role"] = "bridge";
      reg["room"] = room;
      String out; serializeJson(reg, out);
      ws.sendTXT(out);
      wsConnected = true;
      Serial.println("[WS] 已连入房间 " + room);
      // 首次连上服务器 → 烟花"配置成功！"；之后的重连只做普通提示
      if (!configSplashShown) runConfigSuccess();
      else { buzzerBeep(120); vibrateOnce(150); }
      break;
    }
    case WStype_DISCONNECTED:
      wsConnected = false;
      controllerOnline = false;   // 服务器断开：无法得知控制端在线与否，视为未在线
      break;
    case WStype_TEXT:
      handleCommand((const char*)payload, len);
      break;
    default:
      break;
  }
}

/** 把累积的鼠标位移/滚轮合并成一次 HID 报告发送（loop 里定时调用，避免逐条发导致 BLE 积压） */
void flushHid() {
  if (pendingWheel != 0) {
    mouse->mouseMove(0, 0, 0, pendingWheel);   // dx/dy=0，wheel=pendingWheel
    pendingWheel = 0;
  }
  if (pendingDx != 0 || pendingDy != 0) {
    int sx = constrain(pendingDx, -127, 127);
    int sy = constrain(pendingDy, -127, 127);
    mouse->mouseMove((signed char)sx, (signed char)sy);
    pendingDx -= sx;   // 超出 ±127 的余量留给下一帧，不丢位移
    pendingDy -= sy;
  }
}

void handleCommand(const char* json, size_t len) {
  DynamicJsonDocument doc(1024);   // 打字文本较长，静态 384 会解析失败，改用 1KB 动态缓冲
  if (deserializeJson(doc, json, len)) return;

  const char* type = doc["type"] | "";
  // 服务器广播：本房间控制端在线数（控制端加入/离开时推送）
  if (strcmp(type, "room_state") == 0) {
    int viewers = doc["viewers"] | 0;
    bool wasOnline = controllerOnline;
    controllerOnline = viewers > 0;
    // 控制端新上线（本房间有人接入）→ 震动 + 提示音（"双方在线"庆祝一次）
    if (controllerOnline && !wasOnline) {
      vibrateOnce(160);
      buzzerBeep(120, 90);
    }
    Serial.printf("[WS] 控制端在线数=%d\n", viewers);
    return;
  }
  // 运行时配置指令：{type:"config", vib:int, buzz:int, fan:int, beep:bool, room:"..."}
  if (strcmp(type, "config") == 0) {
    if (doc["vib"].is<int>()) {
      vibLevel = constrain((int)doc["vib"], 0, PERI_LEVELS - 1);
      prefs.putInt("vib", vibLevel);
      if (vibLevel == 0) ledcWrite(VIB_PWM_CH, 0);
    }
    if (doc["buzz"].is<int>()) {
      buzzLevel = constrain((int)doc["buzz"], 0, PERI_LEVELS - 1);
      prefs.putInt("buzz", buzzLevel);
      if (buzzLevel == 0) ledcWrite(BUZZ_PWM_CH, 0);
    }
    if (doc["fan"].is<int>()) {
      int fanIdx = doc["fan"];
      fanLevel = constrain(fanIdx, 0, PERI_LEVELS - 1);
      fanOn = (fanLevel > 0);
      prefs.putInt("fan", fanLevel);
      prefs.putBool("fanOn", fanOn);
      applyFan();
    }
    // 主动请求一次提示音（App 里"测试蜂鸣"）
    if (doc["beep"].is<bool>() && doc["beep"]) {
      buzzerBeep(150);
    }
    // 换房间码：写 NVS 后重连服务器
    if (doc["room"].is<const char*>()) {
      String newRoom = (const char*)doc["room"];
      newRoom.trim(); newRoom.toUpperCase();
      if (newRoom.length()) {
        room = newRoom;
        prefs.putString("room", room);
        Serial.println("[CFG] 房间码 <- " + room);
        ws.disconnect();          // 触发重连 + 重新 register 进新房间
      }
    }
    return;
  }
  if (strcmp(type, "hid") == 0) {
    if (!compositeHID.isConnected()) return;  // iPhone 未连蓝牙，丢弃
    String action = doc["action"] | "";
    int dx = doc["dx"] | 0;
    int dy = doc["dy"] | 0;

    if (action == "move") {
      pendingDx += dx; pendingDy += dy;        // 累积，等 loop 定时合并发送
    } else if (action == "drag") {
      if (!dragHeld) { mouse->mousePress(); dragHeld = true; }  // 按下只在首次触发，不重复 press
      pendingDx += dx; pendingDy += dy;
    } else if (action == "down") {
      flushHid();                              // 先把积压的位移发掉，再按下
      mouse->mousePress();
      dragHeld = true;
    } else if (action == "up") {
      flushHid();                              // 松手前把剩余位移发完
      mouse->mouseRelease();
      dragHeld = false;
    } else if (action == "click") {
      flushHid();
      // 库的 mouseClick() 是空实现（No-op），这里手动按下+松开模拟一次点击
      mouse->mousePress();
      delay(60);                               // 按下保持 60ms，确保 iOS 能识别为一次点击
      mouse->mouseRelease();
    } else if (action == "scroll") {
      pendingWheel = (int8_t)constrain((int)pendingWheel + dx, -127, 127);  // 累积滚轮
    }
  }
  else if (strcmp(type, "hid_text") == 0) {
    if (!compositeHID.isConnected()) return;
    const char* value = doc["value"] | "";
    typeString(value);
  }
}

/** ASCII 字符 → HID 键码 + 是否需要 Shift（返回 0 = 不支持该字符） */
uint8_t asciiToHid(unsigned char c, bool &needShift) {
  needShift = false;
  if (c >= 'a' && c <= 'z') return 0x04 + (c - 'a');
  if (c >= 'A' && c <= 'Z') { needShift = true; return 0x04 + (c - 'A'); }
  if (c >= '1' && c <= '9') return 0x1E + (c - '1');
  switch (c) {
    case '0': return 0x27;  case ')': { needShift = true; return 0x27; }
    case '!': { needShift = true; return 0x1E; }
    case '@': { needShift = true; return 0x1F; }
    case '#': { needShift = true; return 0x20; }
    case '$': { needShift = true; return 0x21; }
    case '%': { needShift = true; return 0x22; }
    case '^': { needShift = true; return 0x23; }
    case '&': { needShift = true; return 0x24; }
    case '*': { needShift = true; return 0x25; }
    case '(': { needShift = true; return 0x26; }
    case '\n': return 0x28;                       // 回车
    case ' ': return 0x2C;
    case '-': return 0x2D;  case '_': { needShift = true; return 0x2D; }
    case '=': return 0x2E;  case '+': { needShift = true; return 0x2E; }
    case '[': return 0x2F;  case '{': { needShift = true; return 0x2F; }
    case ']': return 0x30;  case '}': { needShift = true; return 0x30; }
    case '\\': return 0x31; case '|': { needShift = true; return 0x31; }
    case ';': return 0x33;  case ':': { needShift = true; return 0x33; }
    case '\'': return 0x34; case '"': { needShift = true; return 0x34; }
    case '`': return 0x35;  case '~': { needShift = true; return 0x35; }
    case ',': return 0x36;  case '<': { needShift = true; return 0x36; }
    case '.': return 0x37;  case '>': { needShift = true; return 0x37; }
    case '/': return 0x38;  case '?': { needShift = true; return 0x38; }
    case '\t': return 0x2B;
    default:   return 0;
  }
}

/** 远程打字：逐字符转 HID 键码发送（中文需对方用拼音输入法逐字选词） */
void typeString(const char* s) {
  for (size_t i = 0; s[i] != '\0'; i++) {
    unsigned char c = (unsigned char)s[i];
    bool shift = false;
    uint8_t key = asciiToHid(c, shift);
    if (key == 0) continue;
    if (shift) keyboard->modifierKeyPress(KEY_MOD_LSHIFT);
    keyboard->keyPress(key);
    delay(8);
    keyboard->keyRelease(key);
    if (shift) keyboard->modifierKeyRelease(KEY_MOD_LSHIFT);
    delay(8);
  }
}

// ============================================================
// 状态屏
// ============================================================

/**
 * 开机动画：插电 → 停 1s → MAKE STUDIO 渐显（居中）→ 停留 3s（期间初始化）→ 渐隐
 */
void runBootAnimation() {
  const char* logo = "MAKE STUDIO";
  const char* sub = "莫莫专属助手";

  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  u8g2.setMaxClipWindow();
  u8g2.clearBuffer();
  u8g2.sendBuffer();
  delay(800);

  // 计算 LOGO 与副标题的左右边界，用于逐列扫描
  int lw = u8g2.getUTF8Width(logo);
  int lx = (128 - lw) / 2; if (lx < 0) lx = 0;
  int sw = u8g2.getUTF8Width(sub);
  int sx = (128 - sw) / 2; if (sx < 0) sx = 0;
  int left  = (lx < sx) ? lx : sx;
  int right = (lx + lw > sx + sw) ? (lx + lw) : (sx + sw);
  if (right > 128) right = 128;

  // 渐显：逐列从左到右揭示（真正"从无到有"，不依赖对比度）
  for (int x = left; x <= right; x += 3) {
    u8g2.clearBuffer();
    u8g2.setDrawColor(1);
    u8g2.setClipWindow(0, 0, x, 63);
    drawUTF8Center(logo, 40);
    drawUTF8Center(sub, 54);
    u8g2.sendBuffer();
    u8g2.setMaxClipWindow();
    delay(10);
  }

  delay(3000);   // 停留 3 秒，后台初始化

  // 渐隐：从右向左逐列擦黑
  for (int x = 128; x >= 0; x -= 4) {
    u8g2.clearBuffer();
    u8g2.setClipWindow(x, 0, 128, 63);
    u8g2.sendBuffer();
    u8g2.setMaxClipWindow();
    delay(6);
  }

  u8g2.clear();
  u8g2.sendBuffer();
}

/** 在水平方向居中的位置画一行 UTF-8（y 为基线） */
void drawUTF8Center(const char* s, int y) {
  int w = u8g2.getUTF8Width(s);
  int x = (128 - w) / 2;
  if (x < 0) x = 0;
  u8g2.drawUTF8(x, y, s);
}

/** 启动阶段的单行提示（统一 12px，节省 flash 不加载 16px 字库） */
void drawStatus(const char* msg) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  u8g2.drawStr(4, 16, "MochaTool");
  u8g2.drawUTF8(4, 44, msg);
  u8g2.sendBuffer();
}

/** 主状态屏：WiFi / 被控端 / 控制端 / 房间 实时状态 */
void drawMainStatus() {
  u8g2.clearBuffer();

  // 标题栏
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  u8g2.drawBox(0, 0, 128, 15);
  u8g2.setDrawColor(0);
  u8g2.drawUTF8(2, 12, "MochaTool");
  u8g2.setDrawColor(1);
  drawHeart(0);           // 白标题栏上用黑色小爱心（常驻右上角）

  // WiFi 行（实时查询，掉线立刻变空心，不缓存启动时的结果）
  bool wifiLive = (WiFi.status() == WL_CONNECTED);
  drawStateLine(1, "WiFi", wifiLive);

  // 被控端行：朋友的 iPhone 是否连上盒子的蓝牙（HID 库实时状态）
  bool bleOk = compositeHID.isConnected();
  drawStateLine(2, "被控端", bleOk);

  // 控制端行：服务器 room_state 实时通知（viewer 加入/离开 1 秒内推送）
  bool ctrlOk = wifiLive && wsConnected && controllerOnline;
  drawStateLine(3, "控制端", ctrlOk);

  // 底部：只显示房间名称（居中）
  u8g2.drawHLine(0, 50, 128);
  String roomLine = "房间 " + room;
  int rw = u8g2.getUTF8Width(roomLine.c_str());
  u8g2.drawUTF8((128 - rw) / 2, 59, roomLine.c_str());

  u8g2.sendBuffer();
}

/** 画一行状态：标签 + 右侧圆点（实心=已连接，空心=未连接） */
void drawStateLine(int row, const char* label, bool ok) {
  int y = 18 + row * 11;               // 行基线
  u8g2.drawUTF8(2, y, label);
  int cx = 120, cy = y - 6;            // 状态圆点中心（上移避免碰底部分割线）
  if (ok) u8g2.drawDisc(cx, cy, 4, U8G2_DRAW_ALL);     // 实心=已连接
  else    u8g2.drawCircle(cx, cy, 4, U8G2_DRAW_ALL);   // 空心=未连接
}

// ============================================================
// 主循环
// ============================================================

void loop() {
  ws.loop();

  unsigned long now = millis();

  // WiFi 断线检测与自愈：掉线后异步重连，多次失败则重启（长年待命设备最可靠）
  if (wifiOk && WiFi.status() != WL_CONNECTED) {
    if (now - lastWifiRetry > 10000) {
      lastWifiRetry = now;
      wifiFailCount++;
      Serial.printf("[WiFi] 掉线，第 %d 次重连…\n", wifiFailCount);
      WiFi.disconnect();
      WiFi.reconnect();            // 异步触发重连，不阻塞 loop
      if (wifiFailCount >= 5) {    // 约 50s 仍未恢复 → 重启
        Serial.println("[WiFi] 重连多次失败，重启…");
        delay(200);
        ESP.restart();
      }
    }
  } else if (WiFi.status() == WL_CONNECTED) {
    wifiFailCount = 0;
  }

  // 物理按键：短按切页 / 长按切开关 / 重置流程
  handleButton();

  // HID 鼠标位移合并发送（每 15ms 一次，避免逐条发导致 BLE 队列积压）
  if (now - lastHidFlush >= HID_FLUSH_MS) {
    lastHidFlush = now;
    flushHid();
  }

  // 心跳保活
  if (wsConnected && now - lastPing > 20000) {
    lastPing = now;
    ws.sendTXT("{\"type\":\"ping\"}");
  }

  // 被控端（iPhone 蓝牙）连上沿 → 震动 + 提示音（代表"双方接通"）
  bool bleNow = compositeHID.isConnected();
  if (bleNow && !lastBleConnected) { vibrateOnce(180); buzzerBeep(120, 90); }
  lastBleConnected = bleNow;

  // 屏幕刷新：正常 500ms；重置倒计时/确认阶段需更跟手，100ms
  unsigned long refreshMs = (page == P_RESET && (resetArmed || resetConfirming)) ? 100 : 500;
  if (now - lastDraw > refreshMs) {
    lastDraw = now;
    refreshScreen();
  }

  // LED 辅助指示：常亮 = 一切就绪；快闪 = iPhone 未连
  digitalWrite(LED_BUILTIN, compositeHID.isConnected() ? HIGH : ((now / 300) % 2));
}
