#!/usr/bin/env bash
# RC 远程助手 - 中继服务器一键部署脚本（Debian / Ubuntu）
# 用法（在你的服务器上，root 或 sudo）：
#   bash deploy.sh
#
# 作用：
#   1. 安装 Node.js（若本机没有）
#   2. 把 server/ 复制到 /opt/rcrelay
#   3. npm install 安装依赖（ws）
#   4. 创建 systemd 服务（开机自启 + 崩溃自动重启）
#   5. 启动并验证
#
# 端口：默认 9000，可用环境变量 PORT 覆盖（见底部 systemd 配置）

set -euo pipefail

SVC_NAME="rc-relay"
APP_DIR="/opt/rcrelay"
PORT="${PORT:-9000}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "==> [1/5] 检查 Node.js"
if command -v node >/dev/null 2>&1; then
  echo "    Node.js 已存在: $(node -v)"
else
  echo "    未检测到 Node.js，开始安装 LTS 版..."
  curl -fsSL https://deb.nodesource.com/setup_20.x | bash -
  apt-get install -y nodejs
fi

echo "==> [2/5] 部署目录 $APP_DIR"
mkdir -p "$APP_DIR"
# 优先用脚本同目录的文件；没有（如 bash <(curl ...) 远程执行）则从 GitHub 拉取
if [ -f "$SCRIPT_DIR/server.js" ]; then
  cp -f "$SCRIPT_DIR"/server.js    "$APP_DIR"/
  cp -f "$SCRIPT_DIR"/package.json "$APP_DIR"/
  cp -f "$SCRIPT_DIR"/smoke-test.js "$APP_DIR"/ 2>/dev/null || true
else
  echo "    本地无源码，从 CDN/GitHub 拉取..."
  BASES=(
    "https://cdn.jsdelivr.net/gh/dangaow/damn@main/server"
    "https://raw.githubusercontent.com/dangaow/damn/main/server"
    "https://ghproxy.net/https://raw.githubusercontent.com/dangaow/damn/main/server"
  )
  for BASE in "${BASES[@]}"; do
    if curl -fsSL --max-time 20 -o "$APP_DIR/server.js" "$BASE/server.js"; then
      curl -fsSL --max-time 20 -o "$APP_DIR/package.json" "$BASE/package.json"
      echo "    使用源: $BASE"
      break
    fi
  done
  [ -s "$APP_DIR/server.js" ] || { echo "❌ 所有下载源都失败，请手动上传 server.js"; exit 1; }
fi

echo "==> [3/5] 安装依赖"
cd "$APP_DIR"
npm install --omit=dev

echo "==> [4/5] 创建 systemd 服务 $SVC_NAME"
cat > /etc/systemd/system/${SVC_NAME}.service <<EOF
[Unit]
Description=RC Relay WebSocket Server
After=network.target

[Service]
WorkingDirectory=$APP_DIR
Environment=NODE_ENV=production
Environment=PORT=$PORT
ExecStart=/usr/bin/node server.js
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload
systemctl enable ${SVC_NAME}

echo "==> [5/5] 启动并验证"
systemctl restart ${SVC_NAME}
sleep 2
systemctl --no-pager status ${SVC_NAME} | head -8 || true
echo ""
echo "✅ 部署完成！服务监听端口: $PORT"
echo "   服务器地址(填进安卓/盒子固件):  ws://<你的服务器IP>:${PORT}"
echo "   查看实时日志: journalctl -u ${SVC_NAME} -f"