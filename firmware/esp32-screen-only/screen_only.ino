/**
 * RC Bridge Screen-Only —— 只有屏幕 + 板子（无蜂鸣/震动/风扇）的测试固件
 *
 * 用途：配件（蜂鸣器/震动电机/风扇）还没到齐时，先用这块板子 + 屏幕
 *       验证「屏幕显示 / 开机动画 / 按键切页 / 配网 / 蓝牙 HID / 服务器连接」全流程。
 *       等配件到齐后，换回 display_bridge.ino（带外设的完整版）即可，功能不冲突。
 *
 * 硬件：任意 ESP32 DevKit（WROOM-32）+ 外接 SSD1306 0.96" I2C 4针 OLED
 *
 * 接线（免焊，4 根杜邦线）：
 *   屏幕 VCC → 板子 3V3
 *   屏幕 GND → 板子 GND
 *   屏幕 SCL → 板子 D22 (GPIO22)
 *   屏幕 SDA → 板子 D21 (GPIO21)
 *
 * 屏幕（128x64）实时显示：
 *   ┌────────────────┐
 *   │ RC 远程助手      │
 *   │ WiFi  [OK] 已连 │
 *   │ 被控端[OK] 已连接│  ← 朋友 iPhone 蓝牙连上了盒子
 *   │ 控制端[--] 未连接│  ← 你在家还没通过服务器接入
 *   │ 房间 XIAOMING    │
 *   └────────────────┘
 *
 * 物理按键（板载 BOOT 键，GPIO0，免接线）：
 *   - 单击：循环切页  主页面 → 帮助 → 重置 → 主页面
 *   - 双击：在【重置】页启动 5s 倒计时
 *   - 重置倒计时期间：任意按键 = 取消；5s 走满后进入三击确认
 *   - 重置确认页：双击退出，三击执行恢复出厂并重启
 *   - 屏幕右上角常驻小爱心 ♡；切页有渐隐渐现过渡
 *
 * 开机逻辑：若已填过配置并连上服务器 → 首次连接弹出"配置成功！"烟花特效后进入主屏
 *
 * 烧录（Arduino IDE）：
 *   开发板：ESP32 Dev Module
 *     Tools → Partition Scheme: 选 "Huge APP (3MB No OTA/1MB SPIFFS)"
 *     Tools → Flash Size: 4MB
 *   依赖库：
 *     1. "U8g2"（作者 Oliver Kraus）—— 屏幕驱动
 *     2. "NimBLE-Arduino"（作者 h2zero）
 *     3. "Callback"（作者 Tom Stewart）
 *     4. "WebSockets"（作者 Markus Sattler）
 *     5. "WiFiManager"（作者 tzapu）
 *     6. "ArduinoJson"（作者 Benoit Blanchon，v6）
 *     7. 主库 ZIP 安装：https://github.com/Mystfit/ESP32-BLE-CompositeHID
 *     ⚠ 卸载 ESP32-BLE-Keyboard / Mouse / Combo（若装过），否则重复定义报错
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

// ---- 配网页面美化：居中卡片 + 房间"留空=沿用上次"提示 ----
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
  var f=document.querySelector('form');
  if(f && f.parentNode){
    var c=document.createElement('div'); c.className='card';
    var logo=document.createElement('div'); logo.className='logo'; logo.textContent='RC 远程助手';
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

// ---- 屏幕（外接 SSD1306 OLED，HW I2C 默认 SDA=GPIO21 SCL=GPIO22）----
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

BleCompositeHID compositeHID("RC Bridge", "RCD", 100);
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

// ---- 物理按键（板载 BOOT 键，GPIO0，按下接地，INPUT_PULLUP）----
#define PIN_BUTTON    0
#define DBL_GAP_MS    280
#define DEBOUNCE_MS   20

// 页面（精简版：主页面 / 帮助 / 重置）
enum Page { P_MAIN, P_HELP, P_RESET };
Page page = P_MAIN;

// 按键状态机（单击 / 双击）
bool btnDown = false;
unsigned long btnDownAt = 0;
unsigned long btnUpAt = 0;
int clickCount = 0;
bool skipRelease = false;         // 重置倒计时取消后，吞掉本次松开的点击，避免误切页

// 重置流程
#define RST_HOLD_MS   5000
bool resetArmed = false;
unsigned long resetArmedAt = 0;
bool resetConfirming = false;
int  resetConfirmCount = 0;
unsigned long confirmDeadline = 0;

// 配置成功烟花
struct Part { float x, y, vx, vy; uint8_t life; };
Part parts[42];
bool configSplashShown = false;

// 被控端（iPhone 蓝牙）连接沿（精简版无震动，仅记录状态）
bool lastBleConnected = false;

// ============================================================
// 初始化
// ============================================================

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  // ---- 状态屏启动 ----
  u8g2.begin();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  runBootAnimation();
  drawStatus("启动中…");

  // ---- 蓝牙 HID 键鼠启动 ----
  compositeHID.addDevice(keyboard);
  compositeHID.addDevice(mouse);
  compositeHID.begin();
  Serial.println("[BLE] RC Bridge 已广播，等待 iPhone 配对…");

  // ---- 读取保存的配置 ----
  prefs.begin("rcbridge", false);
  serverUrl = prefs.getString("server", "");
  room = prefs.getString("room", "");

  // ---- WiFi 配网 ----
  drawStatus("配网/连WiFi…");
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  wm.setAPStaticIPConfig(
      IPAddress(10, 10, 10, 1),
      IPAddress(10, 10, 10, 1),
      IPAddress(255, 255, 255, 0));
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
  if (newServer.length()) { serverUrl = newServer; prefs.putString("server", serverUrl); }
  if (newRoom.length()) { room = newRoom; prefs.putString("room", room); }

  wifiOk = true;
  Serial.printf("[WiFi] 已连接 %s\n", WiFi.SSID().c_str());
  Serial.printf("[CFG] server=%s room=%s\n", serverUrl.c_str(), room.c_str());

  if (serverUrl.length() == 0 || room.length() == 0) {
    Serial.println("[CFG] 未配置服务器地址或房间码，等待重新配网…");
    drawStatus("未配置服务器/房间");
    return;
  }

  drawStatus("连接服务器…");
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
  uint16_t port = secure ? 443 : 9000;
  int colon = hostPort.indexOf(':');
  if (colon >= 0) {
    host = hostPort.substring(0, colon);
    port = hostPort.substring(colon + 1).toInt();
  }

  ws.begin(host, port, "/");
  if (secure) ws.beginSSL(host.c_str(), port, "/");
  ws.onEvent(onWebSocketEvent);
  ws.setReconnectInterval(5000);
  ws.enableHeartbeat(15000, 3000, 2);
}

// ============================================================
// 按键：单击切页 / 双击重置
// ============================================================

/** 切换到下一页（主页面 → 帮助 → 重置 → 主页面），带渐隐渐现 */
void nextPage() {
  for (int c = 255; c > 0; c -= 20) { u8g2.setContrast(c); delay(4); }
  switch (page) {
    case P_MAIN:  page = P_HELP;  break;
    case P_HELP:  page = P_RESET; break;
    default:      page = P_MAIN;  break;
  }
  refreshScreen();
  for (int c = 0; c <= 255; c += 20) { u8g2.setContrast(c); delay(4); }
  u8g2.setContrast(255);
}

void startResetArmed() {
  resetArmed = true;
  resetArmedAt = millis();
  resetConfirming = false;
  resetConfirmCount = 0;
  Serial.println("[RST] 进入 5s 重置倒计时");
}

void cancelResetArmed() {
  resetArmed = false;
  resetConfirming = false;
  resetConfirmCount = 0;
  Serial.println("[RST] 倒计时取消");
  refreshScreen();
}

void enterResetConfirm() {
  resetArmed = false;
  resetConfirming = true;
  resetConfirmCount = 0;
  confirmDeadline = millis() + 10000;
  Serial.println("[RST] 进入三击确认");
  refreshScreen();
}

void resetBox() {
  drawStatus("正在清除配置…");
  delay(400);
  prefs.end();
  nvs_flash_erase();
  nvs_flash_init();

  drawStatus("重启中…");
  delay(500);
  ESP.restart();
}

void handleButton() {
  unsigned long now = millis();
  bool pressed = (digitalRead(PIN_BUTTON) == LOW);

  // 重置倒计时期间：任意按键按下即取消，并吞掉本次按键（不触发切页/双击）
  if (resetArmed && pressed && !btnDown) {
    cancelResetArmed();
    btnDown = true;
    skipRelease = true;
  }

  if (pressed) {
    if (!btnDown) { btnDown = true; btnDownAt = now; }
  } else {
    if (btnDown) {
      btnDown = false;
      if (skipRelease) { skipRelease = false; return; }   // 吞掉取消用的松开
      if (now - btnDownAt < DEBOUNCE_MS) return;
      btnUpAt = now;
      clickCount++;
    }
  }

  if (clickCount > 0 && !btnDown && now - btnUpAt > DBL_GAP_MS) {
    int clicks = clickCount;
    clickCount = 0;

    if (resetConfirming) {
      if (clicks == 2) {
        resetConfirming = false;
        resetConfirmCount = 0;
        refreshScreen();
      } else if (clicks >= 3) {
        resetBox();
      } else {
        resetConfirmCount++;
        confirmDeadline = now + 10000;
        refreshScreen();
        if (resetConfirmCount >= 3) resetBox();
      }
      return;
    }

    if (clicks == 2) {
      if (page == P_RESET) startResetArmed();
    } else {
      nextPage();
    }
  }

  if (resetArmed && now - resetArmedAt >= RST_HOLD_MS) {
    enterResetConfirm();
  }

  if (resetConfirming && now > confirmDeadline) {
    resetConfirming = false;
    resetConfirmCount = 0;
    page = P_MAIN;
    refreshScreen();
  }
}

/** 右上角常驻小爱心 ♡ */
void drawHeart(int color = 1) {
  u8g2.setDrawColor(color);
  u8g2.drawCircle(115, 6, 3, 1);
  u8g2.drawCircle(121, 6, 3, 1);
  u8g2.drawTriangle(112, 5, 124, 5, 118, 13);
  u8g2.setDrawColor(1);
}

/** 底部操作提示条 */
void drawFooterHint(const char* hint) {
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  u8g2.drawHLine(0, 51, 128);
  int w = u8g2.getUTF8Width(hint);
  int x = (128 - w) / 2;
  if (x < 0) x = 0;
  u8g2.drawUTF8(x, 62, hint);
}

// ============================================================
// 页面绘制
// ============================================================

void drawHelpPage() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  drawHeart();

  u8g2.drawBox(0, 0, 128, 15);
  u8g2.setDrawColor(0);
  u8g2.drawUTF8(4, 12, "帮助");
  u8g2.setDrawColor(1);

  u8g2.drawUTF8(4, 28, "单击按钮：切换页面");
  u8g2.drawUTF8(4, 42, "重置页双击：进入倒计时");

  drawFooterHint("单击切页");
  u8g2.sendBuffer();
}

void drawResetPage() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  drawHeart();

  u8g2.drawBox(0, 0, 128, 15);
  u8g2.setDrawColor(0);
  u8g2.drawUTF8(4, 12, "重置配置");
  u8g2.setDrawColor(1);

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
    u8g2.drawFrame(14, 34, 100, 9);
    u8g2.setDrawColor(0);
    u8g2.drawBox(17, 37, 94 * pct / 100, 3);
    u8g2.setDrawColor(1);
    drawFooterHint("按任意键取消");
  } else {
    u8g2.drawUTF8(4, 28, "双击开始重置倒计时");
    u8g2.drawUTF8(4, 42, "将清空配置重开热点");
    drawFooterHint("单击切页 · 双击开始");
  }
  u8g2.sendBuffer();
}

void refreshScreen() {
  switch (page) {
    case P_MAIN:  drawMainStatus(); break;
    case P_HELP:  drawHelpPage(); break;
    case P_RESET: drawResetPage(); break;
  }
}

// ---- 配置成功烟花 ----
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
  last = now;
  for (int i = 0; i < 42; i++) {
    parts[i].vy += 0.35f;
    parts[i].x += parts[i].vx;
    parts[i].y += parts[i].vy;
    parts[i].vx *= 0.90f; parts[i].vy *= 0.90f;
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
  drawUTF8Center("配置成功！", 40);
  u8g2.sendBuffer();
}

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
      if (!configSplashShown) runConfigSuccess();
      break;
    }
    case WStype_DISCONNECTED:
      wsConnected = false;
      controllerOnline = false;
      break;
    case WStype_TEXT:
      handleCommand((const char*)payload, len);
      break;
    default:
      break;
  }
}

void handleCommand(const char* json, size_t len) {
  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, json, len)) return;

  const char* type = doc["type"] | "";
  if (strcmp(type, "room_state") == 0) {
    int viewers = doc["viewers"] | 0;
    controllerOnline = viewers > 0;
    Serial.printf("[WS] 控制端在线数=%d\n", viewers);
    return;
  }
  if (strcmp(type, "config") == 0) {
    // 精简版无外设，仅支持换房间码
    if (doc["room"].is<const char*>()) {
      String newRoom = (const char*)doc["room"];
      newRoom.trim(); newRoom.toUpperCase();
      if (newRoom.length()) {
        room = newRoom;
        prefs.putString("room", room);
        Serial.println("[CFG] 房间码 <- " + room);
        ws.disconnect();
      }
    }
    return;
  }
  if (strcmp(type, "hid") == 0) {
    if (!compositeHID.isConnected()) return;
    String action = doc["action"] | "";
    int dx = doc["dx"] | 0;
    int dy = doc["dy"] | 0;

    if (action == "move") {
      mouse->mouseMove((signed char)dx, (signed char)dy);
    } else if (action == "drag") {
      mouse->mousePress();
      mouse->mouseMove((signed char)dx, (signed char)dy);
    } else if (action == "down") {
      mouse->mousePress();
    } else if (action == "up") {
      mouse->mouseRelease();
    } else if (action == "click") {
      mouse->mouseClick();
    } else if (action == "scroll") {
      mouse->mouseMove(0, 0, 0, (signed char)dx);
    }
  }
  else if (strcmp(type, "hid_text") == 0) {
    if (!compositeHID.isConnected()) return;
    const char* value = doc["value"] | "";
    typeString(value);
  }
}

/** ASCII 字符 → HID 键码 + 是否需要 Shift */
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
    case '\n': return 0x28;
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

void drawUTF8Center(const char* s, int y) {
  int w = u8g2.getUTF8Width(s);
  int x = (128 - w) / 2;
  if (x < 0) x = 0;
  u8g2.drawUTF8(x, y, s);
}

void drawStatus(const char* msg) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  u8g2.drawStr(4, 16, "RC Bridge");
  u8g2.drawUTF8(4, 44, msg);
  u8g2.sendBuffer();
}

void drawMainStatus() {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  u8g2.drawBox(0, 0, 128, 15);
  u8g2.setDrawColor(0);
  u8g2.drawUTF8(2, 12, "RC 远程助手");
  u8g2.setDrawColor(1);
  drawHeart(0);

  bool wifiLive = (WiFi.status() == WL_CONNECTED);
  String wifiLine = wifiLive ? ("已连 " + WiFi.SSID()) : String("未连接");
  drawStateLine(1, "WiFi", wifiLive, wifiLine.c_str());

  bool bleOk = compositeHID.isConnected();
  drawStateLine(2, "被控端", bleOk, bleOk ? "已连接" : "未连接");

  bool ctrlOk = wifiLive && wsConnected && controllerOnline;
  const char* ctrlDetail = ctrlOk ? "已连接"
                       : (wifiLive && !wsConnected) ? "无服务器" : "未连接";
  drawStateLine(3, "控制端", ctrlOk, ctrlDetail);

  u8g2.drawHLine(0, 50, 128);
  String roomLine = "房间 " + room;
  u8g2.drawUTF8(2, 59, roomLine.c_str());

  u8g2.sendBuffer();
}

void drawStateLine(int row, const char* label, bool ok, const char* detail) {
  int y = 18 + row * 11;
  u8g2.drawUTF8(2, y, label);
  u8g2.drawStr(46, y, ok ? "[OK]" : "[--]");
  u8g2.drawUTF8(78, y, detail);
}

// ============================================================
// 主循环
// ============================================================

void loop() {
  ws.loop();

  unsigned long now = millis();

  handleButton();

  if (wsConnected && now - lastPing > 20000) {
    lastPing = now;
    ws.sendTXT("{\"type\":\"ping\"}");
  }

  bool bleNow = compositeHID.isConnected();
  lastBleConnected = bleNow;

  unsigned long refreshMs = (page == P_RESET && (resetArmed || resetConfirming)) ? 100 : 500;
  if (now - lastDraw > refreshMs) {
    lastDraw = now;
    refreshScreen();
  }

  digitalWrite(LED_BUILTIN, compositeHID.isConnected() ? HIGH : ((now / 300) % 2));
}
