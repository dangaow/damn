/**
 * 房间式中继服务器
 * - 客户端通过 JSON {type:"register", role:"...", room:"CODE"} 加入房间
 * - 二进制帧 / JSON 指令：转发给房间内其他成员
 * 角色: "bridge" (ESP32 蓝牙键鼠盒), "viewer" (安卓控制端)
 *
 * 同一端口同时提供：
 *   WebSocket /       —— 设备与控制端的中继通道
 *   HTTP    /         —— 网页运行状态面板（浏览器直接打开）
 *          /api/status —— JSON 状态接口
 */
const http = require('http');
const { WebSocketServer } = require('ws');

const PORT = process.env.PORT || 9000;
const START_TIME = Date.now();

/** room -> Set<ws> */
const rooms = new Map();

// ---- 运行统计（状态面板展示） ----
const stats = {
  totalConnections: 0,   // 历史累计连接数
  totalMessages: 0,      // 历史累计收到的消息数
  totalForwarded: 0,     // 历史累计转发成功数
  lastEvent: '',         // 最近一条事件日志（面板显示）
};

function log(room, msg) {
  const line = `[${new Date().toISOString()}] [room=${room}] ${msg}`;
  console.log(line);
  stats.lastEvent = line;
}

/**
 * 向房间内所有成员广播实时状态：
 * - bridge 收到 viewers（控制端在线数）→ 屏幕显示"控制端 已连接/未连接"
 * - viewer 收到 bridges（桥接盒在线数）→ App 据此验证盒子是否真的在房间
 */
function broadcastRoomState(room) {
  const members = rooms.get(room);
  if (!members || members.size === 0) return;
  let viewers = 0, bridges = 0;
  for (const m of members) {
    if (m.role === 'viewer') viewers++;
    else if (m.role === 'bridge') bridges++;
  }
  const msg = JSON.stringify({ type: 'room_state', viewers, bridges });
  for (const m of members) {
    if (m.readyState === 1) m.send(msg);
  }
}

// ---- WebSocket 中继 ---- 
const server = http.createServer((req, res) => handleHttp(req, res));
const wss = new WebSocketServer({ server });

wss.on('connection', (ws, req) => {
  ws.isAlive = true;
  ws.room = null;
  ws.role = null;
  stats.totalConnections++;
  log('-', 'new connection');

  ws.on('pong', () => { ws.isAlive = true; });

  ws.on('message', (data, isBinary) => {
    stats.totalMessages++;

    if (ws.room === null) {
      // 第一条消息必须是 register
      try {
        const msg = JSON.parse(data.toString());
        if (msg.type !== 'register') return;
        ws.room = String(msg.room || '').trim().toUpperCase() || 'DEMO';
        ws.role = msg.role || 'viewer';
        if (!rooms.has(ws.room)) rooms.set(ws.room, new Set());
        rooms.get(ws.room).add(ws);
        log(ws.room, `registered role=${ws.role}`);
        ws.send(JSON.stringify({ type: 'registered', room: ws.room, role: ws.role }));
        broadcastRoomState(ws.room);
      } catch (e) {
        ws.close(4001, 'invalid register message');
      }
      return;
    }

    // 转发给同房间的其他成员
    const members = rooms.get(ws.room);
    if (!members) return;
    for (const member of members) {
      if (member !== ws && member.readyState === 1) {
        member.send(data, { binary: isBinary });
        stats.totalForwarded++;
      }
    }
  });

  ws.on('close', () => {
    if (ws.room && rooms.has(ws.room)) {
      rooms.get(ws.room).delete(ws);
      log(ws.room, `left role=${ws.role}, remaining=${rooms.get(ws.room).size}`);
      broadcastRoomState(ws.room);
      if (rooms.get(ws.room).size === 0) rooms.delete(ws.room);
    }
  });

  ws.on('error', (err) => log(ws.room || '-', `error: ${err.message}`));
});

// 心跳清理死连接
setInterval(() => {
  for (const ws of wss.clients) {
    if (!ws.isAlive) { ws.terminate(); continue; }
    ws.isAlive = false;
    ws.ping();
  }
}, 30_000);

// ---- HTTP 状态面板 ----
function fmtUptime(ms) {
  const s = Math.floor(ms / 1000);
  const d = Math.floor(s / 86400);
  const h = Math.floor((s % 86400) / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = s % 60;
  return (d ? `${d} 天 ` : '') + (h ? `${h} 时 ` : '') + (m ? `${m} 分 ` : '') + `${sec} 秒`;
}

function collectRooms() {
  const list = [];
  for (const [code, members] of rooms) {
    let viewers = 0, bridges = 0;
    for (const m of members) {
      if (m.role === 'viewer') viewers++;
      else if (m.role === 'bridge') bridges++;
    }
    list.push({ room: code, viewers, bridges });
  }
  return list;
}

function apiStatus() {
  const roomList = collectRooms();
  const active = roomList.reduce((a, r) => a + r.viewers + r.bridges, 0);
  return {
    status: 'ok',
    port: PORT,
    uptimeMs: Date.now() - START_TIME,
    uptime: fmtUptime(Date.now() - START_TIME),
    activeConnections: active,
    activeRooms: roomList.length,
    totalConnections: stats.totalConnections,
    totalMessages: stats.totalMessages,
    totalForwarded: stats.totalForwarded,
    rooms: roomList,
    lastEvent: stats.lastEvent,
    now: new Date().toISOString(),
  };
}

function handleHttp(req, res) {
  // 供同源轮询/前端刷新的 JSON 接口
  if (req.url === '/api/status' && req.method === 'GET') {
    res.writeHead(200, { 'Content-Type': 'application/json; charset=utf-8' });
    res.end(JSON.stringify(apiStatus(), null, 2));
    return;
  }

  if (req.url !== '/' && req.url !== '/index.html') {
    res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' });
    res.end('Not Found');
    return;
  }

  const s = apiStatus();
  const roomRows = s.rooms.length
    ? s.rooms.map(r =>
        `<tr>
          <td><span class="chip">${r.room}</span></td>
          <td class=${r.bridges ? 'on' : 'off'}>${r.bridges ? '● 在线' : '○ 离线'}</td>
          <td class=${r.viewers ? 'on' : 'off'}>${r.viewers ? '● 在线' : '○ 离线'}</td>
          <td>${r.bridges + r.viewers}</td>
        </tr>`).join('')
    : `<tr><td colspan="4" class="empty">当前无房间在线</td></tr>`;

  const html = `<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MochaTool 中继服务器 · 运行状态</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{
    font-family:-apple-system,"PingFang SC","Microsoft YaHei",system-ui,sans-serif;
    background:linear-gradient(135deg,#667eea,#764ba2);min-height:100vh;
    padding:24px;color:#2d2a3e;
  }
  .wrap{max-width:820px;margin:0 auto}
  .card{
    background:#fff;border-radius:18px;box-shadow:0 14px 44px rgba(0,0,0,.25);
    padding:26px 30px;margin-bottom:18px;
  }
  h1{font-size:22px;font-weight:700;color:#5a3fd4;margin-bottom:4px;display:flex;align-items:center;gap:10px}
  .pulse{width:10px;height:10px;border-radius:50%;background:#22c55e;box-shadow:0 0 0 0 rgba(34,197,94,.5);animation:pulse 2s infinite}
  @keyframes pulse{0%{box-shadow:0 0 0 0 rgba(34,197,94,.5)}70%{box-shadow:0 0 0 12px rgba(34,197,94,0)}100%{box-shadow:0 0 0 0 rgba(34,197,94,0)}}
  .sub{color:#8a8a9e;font-size:13px;margin-bottom:18px}
  .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px}
  .stat{background:#f4f3fa;border-radius:12px;padding:16px;text-align:center}
  .stat .num{font-size:28px;font-weight:700;color:#5a3fd4}
  .stat .lbl{font-size:12px;color:#8a8a9e;margin-top:4px}
  table{width:100%;border-collapse:collapse;font-size:14px}
  th,td{text-align:left;padding:10px 12px;border-bottom:1px solid #eee}
  th{color:#8a8a9e;font-weight:600;font-size:12px}
  td.on{color:#16a34a;font-weight:600}
  td.off{color:#9ca3af}
  .chip{background:#ede9fe;color:#5a3fd4;font-weight:700;padding:3px 10px;border-radius:8px;font-size:13px}
  .empty{color:#9ca3af;text-align:center;padding:18px}
  .log{font-family:ui-monospace,Menlo,monospace;font-size:12px;color:#555;background:#f4f3fa;
    border-radius:10px;padding:12px;word-break:break-all;margin-top:6px}
  .foot{text-align:center;color:rgba(255,255,255,.7);font-size:12px;margin-top:8px}
</style>
</head>
<body>
<div class="wrap">
  <div class="card">
    <h1><span class="pulse"></span>MochaTool 中继服务器</h1>
    <div class="sub">WebSocket 房间式中继 · 端口 ${s.port} · 已运行 ${s.uptime}</div>
    <div class="grid">
      <div class="stat"><div class="num">${s.activeConnections}</div><div class="lbl">当前在线设备</div></div>
      <div class="stat"><div class="num">${s.activeRooms}</div><div class="lbl">活跃房间数</div></div>
      <div class="stat"><div class="num">${s.totalConnections}</div><div class="lbl">历史连接数</div></div>
      <div class="stat"><div class="num">${s.totalForwarded}</div><div class="lbl">转发消息数</div></div>
    </div>
  </div>

  <div class="card">
    <h1 style="font-size:17px">当前房间</h1>
    <table>
      <thead><tr><th>房间码</th><th>桥接盒</th><th>控制端</th><th>设备数</th></tr></thead>
      <tbody>${roomRows}</tbody>
    </table>
  </div>

  <div class="card">
    <h1 style="font-size:17px">最近事件</h1>
    <div class="log">${s.lastEvent || '（暂无）'}</div>
  </div>

  <div class="foot">MochaTool · 运行状态面板 · 请用浏览器直接打开本网址</div>
</div>
</body>
</html>`;

  res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
  res.end(html);
}

server.listen(PORT);
console.log(`MochaTool relay server listening on ws://0.0.0.0:${PORT}  (status page: http://0.0.0.0:${PORT}/)`);