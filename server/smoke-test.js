// 冒烟测试：模拟 ESP32 bridge 端 + Android viewer 端，验证房间转发
// 用法：PORT=9377 node server.js &  node smoke-test.js  (默认连 ws://localhost:9000)
const WebSocket = require('ws');

const URL_ = process.env.WS || 'ws://localhost:9000';
let failures = 0;

function check(name, cond) {
  console.log(cond ? `PASS: ${name}` : `FAIL: ${name}`);
  if (!cond) failures++;
}

async function main() {
  const mk = () => new WebSocket(URL_);
  const [bridge, viewer] = await Promise.all(
    [mk(), mk()].map(ws => new Promise(r => ws.on('open', () => r(ws))))
  );

  bridge.send(JSON.stringify({ type: 'register', role: 'bridge', room: 'TEST' }));
  viewer.send(JSON.stringify({ type: 'register', role: 'viewer', room: 'TEST' }));

  await new Promise(r => setTimeout(r, 300));

  const state = { viewerGotFrame: false, bridgeGotInput: false, bridgeGotOwnFrame: false };

  viewer.on('message', (data, isBinary) => {
    if (isBinary) state.viewerGotFrame = true;
  });
  bridge.on('message', (data, isBinary) => {
    if (isBinary) state.bridgeGotOwnFrame = true;
    else if (JSON.parse(data.toString()).type === 'hid') state.bridgeGotInput = true;
  });

  // bridge 发二进制帧（占位） -> viewer 应收到（bridge 自己不应收到）
  bridge.send(Buffer.from([0xFF, 0xD8]), { binary: true });
  // viewer 发 HID 指令 -> bridge 应收到（各取所需）
  viewer.send(JSON.stringify({ type: 'hid', action: 'move', dx: 10, dy: 10 }));

  await new Promise(r => setTimeout(r, 500));

  check('viewer 收到画面帧', state.viewerGotFrame);
  check('bridge 收到 HID 控制指令', state.bridgeGotInput);
  check('发送者不会收到自己的帧（无回环）', !state.bridgeGotOwnFrame);

  [bridge, viewer].forEach(ws => ws.close());
  setTimeout(() => {
    console.log(failures === 0 ? '\nALL TESTS PASSED' : `\n${failures} TEST(S) FAILED`);
    process.exit(failures === 0 ? 0 : 1);
  }, 300);
}

main().catch(e => { console.error(e); process.exit(1); });