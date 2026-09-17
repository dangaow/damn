/**
 * RC Bridge Display —— 带状态屏的蓝牙 HID 桥接盒固件
 *
 * 硬件：任意 ESP32 DevKit（WROOM-32，¥20-40）+ 外接 SSD1306 0.96" I2C 4针 OLED（¥5-10）
 *       4 根母对母杜邦线免焊直插（下方"接线"）
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
 *  语义说明：
 *  「被控端」= 朋友的 iPhone（通过蓝牙 HID 与盒子配对）
 *  「控制端」= 你的安卓手机（通过服务器连入同一房间，状态由服务器 room_state 广播）
 *
 * 接线（免焊）：
 *   屏幕 VCC → 板子 3V3
 *   屏幕 GND → 板子 GND
 *   屏幕 SCL → 板子 D22 (GPIO22)
 *   屏幕 SDA → 板子 D21 (GPIO21)
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
#include <BleCompositeHid.h>
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
  .card input[type=text],.card input[type=password]{
    width:100%;padding:12px 14px;font-size:15px;color:#2d2a3e;
    background:#f4f3fa;border:1px solid #e8e6f2;
    border-radius:12px;outline:none;transition:.2s;
  }
  .card input:focus{border-color:#667eea;box-shadow:0 0 0 3px rgba(102,126,234,.18)}
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

// ---- 指令协议（与安卓端约定）----
// {type:"hid", action:"move|drag|down|up|click|scroll", dx:int, dy:int}
// {type:"hid_text", value:"要打的字（英文/拼音，走真实键盘）"}

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

// ============================================================
// 初始化
// ============================================================

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  // ---- 状态屏启动 ----
  u8g2.begin();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);   // 中文字体（含常用汉字）
  runBootAnimation();                        // 开启动画：MAKE STUDIO 渐显→停留→渐隐
  drawStatus("启动中…");

  // ---- 蓝牙 HID 键鼠启动（iPhone 配对后终身自动回连）----
  compositeHID.addDevice(keyboard);
  compositeHID.addDevice(mouse);
  compositeHID.begin();
  Serial.println("[BLE] RC Bridge 已广播，等待 iPhone 配对…");

  // ---- 读取保存的配置 ----
  prefs.begin("rcbridge", false);
  serverUrl = prefs.getString("server", "");   // 无内置默认服务器，由用户填写
  room = prefs.getString("room", "");          // 无内置默认房间码，由用户填写

  // ---- WiFi 配网 ----
  drawStatus("配网/连WiFi…");
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
  ws.setReconnectInterval(5000);
  // 协议层心跳：15s 一次 ping、3s 超时、连失 2 次判死线
  // 防止 WiFi 静默断链后 wsConnected 还挂在 true（屏幕显示"控制端已连接"骗人）
  ws.enableHeartbeat(15000, 3000, 2);
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

void handleCommand(const char* json, size_t len) {
  DynamicJsonDocument doc(1024);   // 打字文本较长，静态 384 会解析失败，改用 1KB 动态缓冲
  if (deserializeJson(doc, json, len)) return;

  const char* type = doc["type"] | "";
  // 服务器广播：本房间控制端在线数（控制端加入/离开时推送）
  if (strcmp(type, "room_state") == 0) {
    int viewers = doc["viewers"] | 0;
    controllerOnline = viewers > 0;
    Serial.printf("[WS] 控制端在线数=%d\n", viewers);
    return;
  }
  if (strcmp(type, "hid") == 0) {
    if (!compositeHID.isConnected()) return;  // iPhone 未连蓝牙，丢弃
    String action = doc["action"] | "";
    int dx = doc["dx"] | 0;
    int dy = doc["dy"] | 0;

    if (action == "move") {
      mouse->mouseMove((signed char)dx, (signed char)dy);
    } else if (action == "drag") {
      mouse->mousePress();                          // 默认参数 = 左键
      mouse->mouseMove((signed char)dx, (signed char)dy);
    } else if (action == "down") {
      mouse->mousePress();
    } else if (action == "up") {
      mouse->mouseRelease();
    } else if (action == "click") {
      mouse->mouseClick();
    } else if (action == "scroll") {
      mouse->mouseMove(0, 0, 0, (signed char)dx);   // dx 字段复用承载滚轮值
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

/** 开启动画：插电 → 停 1s → MAKE STUDIO 渐显（居中）→ 停留 3s（期间初始化）→ 渐隐 */
void runBootAnimation() {
  const char* logo = "MAKE STUDIO";

  // 1) 插电后先停 1 秒（黑屏，等价"上电即启动"）
  delay(1000);

  // 2) 把 LOGO 画进缓冲（先淡，等待对比度爬升）
  u8g2.clearBuffer();
  u8g2.setDrawColor(1);
  int w = u8g2.getStrWidth(logo);
  int x = (128 - w) / 2;
  if (x < 0) x = 0;
  u8g2.drawStr(x, 40, logo);   // 主 LOGO，水平居中
  drawUTF8Center("远程助手", 54);   // 副标题小字
  u8g2.setContrast(0);         // 初始对比度为 0（全黑）
  u8g2.sendBuffer();

  // 3) 渐显：对比度 0 → 255（约 1.5s）
  for (int c = 8; c <= 255; c += 14) {
    u8g2.setContrast(c);
    delay(65);
  }

  // 4) 停留 3 秒：画面全亮，后台正好做蓝牙/配网初始化
  delay(3000);

  // 5) 渐隐：对比度 255 → 0（约 1.5s）
  for (int c = 255; c > 0; c -= 14) {
    u8g2.setContrast(c);
    delay(65);
  }

  // 6) 清屏，交给后续 drawStatus/drawMainStatus 接管
  u8g2.setContrast(255);       // 恢复默认对比度，避免后续画面偏暗
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
  u8g2.drawStr(4, 16, "RC Bridge");
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
  u8g2.drawUTF8(2, 12, "RC 远程助手");
  u8g2.setDrawColor(1);

  // WiFi 行（实时查询，掉线立刻变"未连接"，不缓存启动时的结果）
  bool wifiLive = (WiFi.status() == WL_CONNECTED);
  String wifiLine = wifiLive ? ("已连 " + WiFi.SSID()) : String("未连接");
  drawStateLine(1, "WiFi", wifiLive, wifiLine.c_str());

  // 被控端行：朋友的 iPhone 是否连上盒子的蓝牙（HID 库实时状态）
  bool bleOk = compositeHID.isConnected();
  drawStateLine(2, "被控端", bleOk, bleOk ? "已连接" : "未连接");

  // 控制端行：服务器 room_state 实时通知（viewer 加入/离开 1 秒内推送）
  // 三种状态：已连接 / 未连接 / 服务器断开（此时无从得知控制端状态，如实显示）
  bool ctrlOk = wifiLive && wsConnected && controllerOnline;
  const char* ctrlDetail = ctrlOk ? "已连接"
                       : (wifiLive && !wsConnected) ? "无服务器" : "未连接";
  drawStateLine(3, "控制端", ctrlOk, ctrlDetail);

  // 底部：房间号
  u8g2.drawHLine(0, 52, 128);
  String roomLine = "房间 " + room;
  u8g2.drawUTF8(2, 63, roomLine.c_str());

  u8g2.sendBuffer();
}

/** 画一行状态：标签 + ✓/✗ + 说明 */
void drawStateLine(int row, const char* label, bool ok, const char* detail) {
  int y = 18 + row * 11;               // 行高 11px
  u8g2.drawUTF8(2, y, label);
  u8g2.drawStr(46, y, ok ? "[OK]" : "[--]");   // 纯 ASCII，避免符号字体缺字
  u8g2.drawUTF8(78, y, detail);
}

// ============================================================
// 主循环
// ============================================================

void loop() {
  ws.loop();

  unsigned long now = millis();

  // 心跳保活
  if (wsConnected && now - lastPing > 20000) {
    lastPing = now;
    ws.sendTXT("{\"type\":\"ping\"}");
  }

  // 屏幕每 500ms 刷新一次（OLED 不宜高频重绘）
  if (now - lastDraw > 500) {
    lastDraw = now;
    drawMainStatus();
  }

  // LED 辅助指示：常亮 = 一切就绪；快闪 = iPhone 未连
  digitalWrite(LED_BUILTIN, compositeHID.isConnected() ? HIGH : ((now / 300) % 2));
}
