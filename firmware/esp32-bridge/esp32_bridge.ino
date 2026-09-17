/**
 * RC Bridge —— ESP32 蓝牙 HID 桥接盒固件
 *
 * 作用：常驻被控方（朋友家），WiFi 连中继服务器接收指令，
 *       对 iPhone 以 BLE HID 键盘+鼠标身份发出真实输入（全局有效、无需越狱）。
 *
 * 烧录（Arduino IDE）：
 *   开发板：ESP32 Dev Module 或 ESP32C3 Dev Module
 *     （C3 Super Mini 小如指甲盖，适合做成挂件/塞抽屉，固件同用；
 *       C3 板载 LED 多为 GPIO8，如指示灯不亮把 LED_BUILTIN 改成 8）
 *   依赖库（重要，按顺序装）：
 *     1. 库管理器安装 "NimBLE-Arduino"（作者 h2zero）——库的底层依赖
 *     2. 库管理器安装 "Callback"（作者 Tom Stewart）——库的底层依赖
 *     3. 库管理器安装 "WebSockets"（作者 Markus Sattler）
 *     4. 库管理器安装 "WiFiManager"（作者 tzapu）
 *     5. 库管理器安装 "ArduinoJson"（作者 Benoit Blanchon，v6）
 *     6. 主库不在库管理器里！浏览器打开 GitHub 仓库
 *        https://github.com/Mystfit/ESP32-BLE-CompositeHID
 *        → 绿色 Code 按钮 → Download ZIP →
 *        Arduino IDE 菜单 项目 → 导入库 → 添加 .ZIP 库 → 选刚下载的 ZIP
 *     ⚠ 如果以前装过 ESP32-BLE-Keyboard / ESP32-BLE-Mouse / ESP32-BLE-Combo，
 *       必须先卸载（会报 BleConnectionStatus 重复定义编译错误）
 *   ESP32 开发板支持包版本：3.2.0（经社区验证可编译本库）
 *
 * 使用流程：
 *   1. 首次上电 → 手机连热点 "mochadangao"（无密码），
 *      填朋友家 WiFi + 服务器地址（ws://你的服务器:9000）+ 房间号
 *   2. iPhone：设置 → 辅助功能 → 触控 → 辅助触控打开
 *      → 设备 → 蓝牙设备 → 配对 "RC Bridge"
 *   3. 之后通电即用，断电重启自动恢复（配置存 NVS）
 */

#include <WiFiManager.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <BleCompositeHID.h>
#include <KeyboardDevice.h>
#include <MouseDevice.h>

// ---- 服务器/房间码无内置默认，由配网页面填写并保存 ----

// ---- 指令协议（与安卓端约定）----
// {type:"hid", action:"move|drag|down|up|click|scroll", dx:int, dy:int}
// {type:"hid_text", value:"要打的字（英文/拼音，走真实键盘）"}

BleCompositeHID compositeHID("RC Bridge", "RCD", 100);
KeyboardDevice* keyboard = new KeyboardDevice();
MouseDevice* mouse = new MouseDevice();

WebSocketsClient ws;
Preferences prefs;
String serverUrl, room;

unsigned long lastPing = 0;
bool wsConnected = false;

// LED 状态指示（多数开发板 LED_BUILTIN = GPIO2）
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

// ============================================================
// 初始化
// ============================================================

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  // ---- 蓝牙 HID 键鼠启动（iPhone 配对后终身自动回连）----
  compositeHID.addDevice(keyboard);
  compositeHID.addDevice(mouse);
  compositeHID.begin();
  Serial.println("[BLE] RC Bridge 已广播，等待 iPhone 配对…");

  // ---- 读取保存的配置 ----
  prefs.begin("rcbridge", false);
  serverUrl = prefs.getString("server", "");
  room = prefs.getString("room", "");

  // ---- WiFi 配网（已配置则直接连接）----
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);  // 配网热点 3 分钟超时（超时后重启循环重试）

  WiFiManagerParameter pServer("server", "服务器地址 ws://…", serverUrl.c_str(), 80);
  WiFiManagerParameter pRoom("room", "房间号", room.c_str(), 20);
  wm.addParameter(&pServer);
  wm.addParameter(&pRoom);

  if (!wm.autoConnect("mochadangao")) {
    Serial.println("[WiFi] 配网超时，重启…");
    ESP.restart();
  }

  // 保存新配置
  String newServer = pServer.getValue();
  String newRoom = pRoom.getValue();
  newServer.trim(); newRoom.trim(); newRoom.toUpperCase();
  if (newServer.length() && newServer != serverUrl) { serverUrl = newServer; prefs.putString("server", serverUrl); }
  if (newRoom.length() && newRoom != room) { room = newRoom; prefs.putString("room", room); }

  Serial.printf("[WiFi] 已连接 %s\n", WiFi.SSID().c_str());
  Serial.printf("[CFG] server=%s room=%s\n", serverUrl.c_str(), room.c_str());

  // 未配置服务器地址 / 房间码 → 不连，等下次配网填写
  if (serverUrl.length() == 0 || room.length() == 0) {
    Serial.println("[CFG] 未配置服务器地址或房间码，跳过连接（重新配网填写）");
    return;
  }

  // ---- WebSocket 连中继服务器 ----
  connectWebSocket();
}

void connectWebSocket() {
  // 解析 ws://host:port
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
  ws.setReconnectInterval(5000);  // 断线自动重连
  // 协议层心跳：15s ping、3s 超时、连失 2 次判死线（防 WiFi 静默断链假在线）
  ws.enableHeartbeat(15000, 3000, 2);
}

// ============================================================
// WebSocket 事件
// ============================================================

void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t len) {
  switch (type) {
    case WStype_CONNECTED: {
      // 注册为 bridge 角色，加入房间
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
  if (strcmp(type, "hid") == 0) {
    if (!compositeHID.isConnected()) return;  // iPhone 未连蓝牙，丢弃
    String action = doc["action"] | "";
    int dx = doc["dx"] | 0;
    int dy = doc["dy"] | 0;

    if (action == "move") {
      mouse->mouseMove((signed char)dx, (signed char)dy);
    } else if (action == "drag") {
      // 按住左键移动
      mouse->mousePress();                          // 默认参数 = 左键
      mouse->mouseMove((signed char)dx, (signed char)dy);
    } else if (action == "down") {
      mouse->mousePress();
    } else if (action == "up") {
      mouse->mouseRelease();
    } else if (action == "click") {
      mouse->mouseClick();
    } else if (action == "scroll") {
      // 滚轮：mouseMove 的第 4 参数 scrollY = 垂直滚轮
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
    default:   return 0;                          // 其他字符跳过
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
// 主循环
// ============================================================

void loop() {
  ws.loop();

  // 心跳保活（服务器 30s 清死连接）
  unsigned long now = millis();
  if (wsConnected && now - lastPing > 20000) {
    lastPing = now;
    ws.sendTXT("{\"type\":\"ping\"}");
  }

  // LED：快闪 = 蓝牙未连 iPhone；常亮 = 一切就绪
  digitalWrite(LED_BUILTIN, compositeHID.isConnected() ? HIGH : ((now / 300) % 2));
}
