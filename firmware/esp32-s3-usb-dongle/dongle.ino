/**
 * MochaTool Dongle —— ESP32-S3 插入式 USB HID 桥接固件
 *
 * 形态：直接插在朋友的 iPhone（USB-C，iPhone 15 及以后）上，
 *       手机供电、免充电线、免蓝牙配对——iPhone 把它当成一只
 *       普通的有线鼠标+键盘，即插即用，系统全局真实控制。
 *
 * 原理：iPhone USB-C 原生支持标准 USB HID 设备（无需 MFi 认证）。
 *       ESP32-S3 的原生 USB 外设直接枚举为复合键鼠，
 *       同时用 WiFi 连中继服务器接收你的远程指令。
 *
 * 烧录（Arduino IDE）：
 *   开发板：ESP32S3 Dev Module（务必启用 USB：
 *     Tools → USB CDC On Boot: Enabled / USB Mode: Hardware CDC and JTAG）
 *   库管理器安装：
 *     - "WebSockets"（Markus Sattler / arduinoWebSockets）
 *     - "WiFiManager"（tzapu）
 *     - "ArduinoJson"（v6）
 *   （USBHIDMouse / USBHIDKeyboard 由 arduino-esp32 核心自带，无需安装）
 *
 * 使用流程：
 *   1. 首次插到手机上 → 手机连热点 "mochadangao"（无密码），
 *      填朋友家 WiFi + 服务器地址（ws://你的服务器:9000）+ 房间号
 *   2. 拔插一次 → iPhone 立即识别为鼠标+键盘，屏幕出现指针
 *   3. 之后保持插着即可（建议给朋友配一条 OTG 分线器以便同时充电）
 *
 * 注意：Lightning 接口（iPhone 14 及以前）不支持通用 USB HID，
 *       只能用蓝牙版固件（esp32-bridge），或加苹果转接头（见项目说明）。
 */

#include <WiFiManager.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "USB.h"
#include "USBHIDMouse.h"
#include "USBHIDKeyboard.h"

// ---- 服务器/房间码无内置默认，由配网页面填写并保存 ----

// ---- 指令协议（与安卓端约定，同 esp32-bridge）----
// {type:"hid", action:"move|drag|down|up|click|scroll", dx:int, dy:int}
// {type:"hid_text", value:"要打的字"}

USBHIDMouse usbMouse;
USBHIDKeyboard usbKeyboard;

WebSocketsClient ws;
Preferences prefs;
String serverUrl, room;

unsigned long lastPing = 0;
bool wsConnected = false;

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

// ============================================================
// 初始化
// ============================================================

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  // ---- USB 复合键鼠（插入 iPhone 即识别）----
  usbKeyboard.begin();
  usbMouse.begin();
  USB.begin();
  Serial.println("[USB] HID 键鼠已就绪，插入 iPhone 即用");

  // ---- 读取保存的配置 ----
  prefs.begin("rcbridge", false);
  serverUrl = prefs.getString("server", "");
  room = prefs.getString("room", "");

  // ---- WiFi 配网（已配置则直接连接；供电来自手机 USB，随时可配）----
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);

  WiFiManagerParameter pServer("server", "服务器地址 ws://…", serverUrl.c_str(), 80);
  WiFiManagerParameter pRoom("room", "房间号", room.c_str(), 20);
  wm.addParameter(&pServer);
  wm.addParameter(&pRoom);

  if (!wm.autoConnect("mochadangao")) {
    Serial.println("[WiFi] 配网超时，重启…");
    ESP.restart();
  }

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
  // 协议层心跳：15s ping、3s 超时、连失 2 次判死线（防 WiFi 静默断链假在线）
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
      reg["role"] = "bridge";   // 与蓝牙盒同角色，服务器无需改动
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
    String action = doc["action"] | "";
    int dx = doc["dx"] | 0;
    int dy = doc["dy"] | 0;

    if (action == "move") {
      usbMouse.move((int8_t)dx, (int8_t)dy);
    } else if (action == "drag") {
      usbMouse.press(MOUSE_LEFT);
      usbMouse.move((int8_t)dx, (int8_t)dy);
    } else if (action == "down") {
      usbMouse.press(MOUSE_LEFT);
    } else if (action == "up") {
      usbMouse.release(MOUSE_LEFT);
    } else if (action == "click") {
      usbMouse.click(MOUSE_LEFT);
    } else if (action == "scroll") {
      usbMouse.move(0, 0, (int8_t)dx);  // dx 字段复用承载滚轮值
    }
  }
  else if (strcmp(type, "hid_text") == 0) {
    const char* value = doc["value"] | "";
    typeString(value);
  }
}

/** 远程打字：逐字符发送 HID 键码（ASCII；中文用拼音输入法+数字键选词） */
void typeString(const char* s) {
  for (size_t i = 0; s[i] != '\0'; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x20 || c > 0x7E) continue;
    usbKeyboard.write(c);
    delay(8);
  }
}

// ============================================================
// 主循环
// ============================================================

void loop() {
  ws.loop();

  unsigned long now = millis();
  if (wsConnected && now - lastPing > 20000) {
    lastPing = now;
    ws.sendTXT("{\"type\":\"ping\"}");
  }

  // LED：慢闪 = WiFi/服务器未连；常亮 = 一切就绪
  digitalWrite(LED_BUILTIN, wsConnected ? HIGH : ((now / 600) % 2));
}
