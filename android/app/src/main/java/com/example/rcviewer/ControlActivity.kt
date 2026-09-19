package com.example.rcviewer

import android.Manifest
import android.annotation.SuppressLint
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import org.json.JSONObject
import java.util.concurrent.TimeUnit

/**
 * 全屏操作页：
 * - 红框区域按 iPhone 逻辑屏 390:844 等比缩放，框住可操作范围
 * - 红点实时显示指针在对方屏幕上的位置（相对移动，滑出边界会停在边缘）
 * - 顶栏左上角"断开连接"，右侧可连本机蓝牙鼠标
 */
class ControlActivity : AppCompatActivity() {

    private lateinit var statusLabel: TextView
    private lateinit var statusDot: ImageView
    private lateinit var roomChip: TextView
    private lateinit var padContainer: FrameLayout
    private lateinit var padFrame: FrameLayout
    private lateinit var pointerDot: View
    private lateinit var offlineOverlay: View
    private lateinit var textInput: EditText
    private lateinit var btButton: Button

    private var webSocket: WebSocket? = null
    private var btMouse: BtHidMouse? = null
    private var useBridge = false
    private var room = ""
    private var bridgeOnline = false   // 房间里是否真的有桥接盒（服务器 room_state 通知）

    private val mainHandler = Handler(Looper.getMainLooper())
    private val client = OkHttpClient.Builder()
        .pingInterval(20, TimeUnit.SECONDS)
        .build()

    private var wantConnected = false
    private var reconnectScheduled = false   // 防止 onFailure/onClosed 重复排程导致重连抖动
    private var lastHidSentAt = 0L   // 桥接盒 hid 指令节流（30/s 上限）

    // 指针在对方屏幕（iPhone 逻辑坐标 390×844）上的位置
    private var px = PHONE_W / 2f
    private var py = PHONE_H / 2f

    // 手势状态
    private var touchDownAt = 0L
    private var lastX = 0f
    private var lastY = 0f
    private var movedFar = false
    private var dragArmed = false
    private var twoFingerScroll = false
    private val dragArmRunnable = Runnable {
        if (!movedFar) {
            dragArmed = true
            padFrame.performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
            btMouse?.leftDown()                          // 本机蓝牙直控
            if (useBridge) sendHid("down", 0, 0)         // 桥接盒：按下左键
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_control)

        val server = intent.getStringExtra("server") ?: ""
        room = intent.getStringExtra("room") ?: ""
        useBridge = intent.getBooleanExtra("bridge", false)

        statusLabel = findViewById(R.id.statusLabel)
        statusDot = findViewById(R.id.statusDot)
        roomChip = findViewById(R.id.roomChip)
        padContainer = findViewById(R.id.padContainer)
        padFrame = findViewById(R.id.padFrame)
        pointerDot = findViewById(R.id.pointerDot)
        offlineOverlay = findViewById(R.id.offlineOverlay)
        textInput = findViewById(R.id.textInput)
        btButton = findViewById(R.id.btButton)

        roomChip.text = "房间 $room"

        findViewById<Button>(R.id.disconnectButton).setOnClickListener {
            wantConnected = false
            webSocket?.close(1000, "bye")
            finish()
        }
        btButton.setOnClickListener { connectBluetoothMouse() }
        findViewById<Button>(R.id.settingsButton).setOnClickListener { showBoxSettings() }
        findViewById<Button>(R.id.sendTextButton).setOnClickListener { sendRemoteText() }
        textInput.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == android.view.inputmethod.EditorInfo.IME_ACTION_SEND) {
                sendRemoteText(); true
            } else false
        }

        // 红框按 390:844 等比缩放并居中（屏幕小就整体缩小，比例不变）
        padContainer.post {
            val availW = padContainer.width
            val availH = padContainer.height
            if (availW <= 0 || availH <= 0) return@post
            val ratio = PHONE_W / PHONE_H
            var w = availW.toFloat()
            var h = w / ratio
            if (h > availH) { h = availH.toFloat(); w = h * ratio }
            padFrame.layoutParams = FrameLayout.LayoutParams(
                w.toInt(), h.toInt(), android.view.Gravity.CENTER)
            updatePointerDot()
        }

        setupTouchListener()
        setupBtMouse()
        connect(server)
    }

    // MARK: - 服务器连接（WebSocket）

    private fun connect(server: String) {
        wantConnected = true
        webSocket?.close(1000, "reconnect")

        val request = Request.Builder().url(server).build()
        webSocket = client.newWebSocket(request, object : WebSocketListener() {
            override fun onOpen(ws: WebSocket, response: Response) {
                val register = JSONObject().apply {
                    put("type", "register")
                    put("role", "viewer")
                    put("room", room)
                }
                ws.send(register.toString())
                mainHandler.post {
                    bridgeOnline = false
                    refreshStatus()
                }
            }

            override fun onMessage(ws: WebSocket, text: String) {
                // 服务器广播房间状态：bridges>0 才说明盒子真的在线
                try {
                    val msg = JSONObject(text)
                    if (msg.opt("type") == "room_state") {
                        val bridges = msg.optInt("bridges", 0)
                        mainHandler.post {
                            bridgeOnline = bridges > 0
                            refreshStatus()
                        }
                    }
                } catch (_: Exception) {}
            }

            override fun onFailure(ws: WebSocket, t: Throwable, response: Response?) {
                if (ws !== webSocket) return
                mainHandler.post {
                    bridgeOnline = false
                    setStatus("连接断开（${t.message}），3 秒后自动重连…", "warn")
                    refreshOverlay()
                }
                scheduleReconnect(server)
            }

            override fun onClosed(ws: WebSocket, code: Int, reason: String) {
                if (ws !== webSocket) return
                mainHandler.post {
                    bridgeOnline = false
                    if (wantConnected) setStatus("连接断开，3 秒后自动重连…", "warn")
                    else setStatus("已断开", "idle")
                    refreshOverlay()
                }
                scheduleReconnect(server)
            }
        })
    }

    /** 按连接 + 盒子在线状态刷新文字/圆点/遮罩 */
    private fun refreshStatus() {
        when {
            !wantConnected -> setStatus("已断开", "idle")
            useBridge && bridgeOnline -> setStatus("已连接 · 桥接盒在线", "ok")
            useBridge -> setStatus("服务器已连 · 桥接盒未连接", "warn")
            else -> setStatus("已连接服务器", "ok")
        }
        refreshOverlay()
    }

    /** 桥接模式但房间里没盒子：红框上盖"盒子未连接"遮罩，禁止操作 */
    private fun refreshOverlay() {
        offlineOverlay.visibility =
            if (useBridge && wantConnected && !bridgeOnline) View.VISIBLE else View.GONE
    }

    /** 断线 3 秒后自动重连（去重：一次失败只排一个重连任务） */
    private fun scheduleReconnect(server: String) {
        if (!wantConnected || reconnectScheduled) return
        reconnectScheduled = true
        mainHandler.postDelayed({
            reconnectScheduled = false
            if (wantConnected) connect(server)
        }, 3000)
    }

    private fun setStatus(msg: String, state: String = "info") {
        statusLabel.text = msg
        when (state) {
            "ok" -> statusDot.setImageResource(R.drawable.dot_ok)
            "warn" -> statusDot.setImageResource(R.drawable.dot_warn)
            "idle" -> statusDot.setImageResource(R.drawable.dot_idle)
        }
    }

    // MARK: - 指针定位（红点）

    private fun updatePointerDot() {
        val fw = padFrame.width.toFloat()
        val fh = padFrame.height.toFloat()
        if (fw <= 0f || fh <= 0f) return
        pointerDot.translationX = fw * (px / PHONE_W) - pointerDot.width / 2f
        pointerDot.translationY = fh * (py / PHONE_H) - pointerDot.height / 2f
    }

    // MARK: - 蓝牙 HID 鼠标（本机直控）

    private fun setupBtMouse() {
        btMouse = BtHidMouse(this).apply {
            onStatus = { msg -> mainHandler.post { setStatus(msg) } }
            onConnected = { connected ->
                mainHandler.post {
                    btButton.text = if (connected) "蓝牙已连" else "蓝牙"
                }
            }
        }
    }

    private fun connectBluetoothMouse() {
        val mouse = btMouse ?: return

        if (mouse.isConnected) {
            mouse.disconnect()
            return
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
            ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT)
            != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(
                this, arrayOf(Manifest.permission.BLUETOOTH_CONNECT), 100)
            return
        }

        mouse.register()

        val devices = mouse.bondedDevices()
        if (devices.isEmpty()) {
            setStatus("无已配对设备：请先在 iPhone 辅助触控→设备→蓝牙设备 中配对本机")
            return
        }

        val names = devices.map { "${it.name ?: "未知设备"} (${it.address})" }
        AlertDialog.Builder(this)
            .setTitle("选择 iPhone")
            .setAdapter(ArrayAdapter(this, android.R.layout.simple_list_item_1, names)) { _, which ->
                mouse.connect(devices[which])
                setStatus("正在连接 ${devices[which].name}…")
            }
            .setNegativeButton("取消", null)
            .show()
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>,
                                            grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == 100 && grantResults.firstOrNull() == PackageManager.PERMISSION_GRANTED) {
            connectBluetoothMouse()
        }
    }

    // MARK: - 触摸手势（红框内）→ 鼠标事件

    @SuppressLint("ClickableViewAccessibility")
    private fun setupTouchListener() {
        padFrame.setOnTouchListener { view, event ->
            // 红框区域就是全部可操作范围，禁止父容器拦截手势
            view.parent.requestDisallowInterceptTouchEvent(true)
            handleMouseGesture(view, event, btMouse?.takeIf { it.isConnected })
            true
        }
    }

    private fun handleMouseGesture(view: View, event: MotionEvent, mouse: BtHidMouse?) {
        // 桥接模式但房间里没有盒子：一律不处理（遮罩层也会挡住触摸）
        if (useBridge && !bridgeOnline) return

        // 相对映射（触控板模式）：iOS 会对相对位移做指针加速，
        // 所以增益取偏低区间，抵消加速放大，让指针"走多少跟手多少"
        val gain = (PHONE_W / padFrame.width.coerceAtLeast(1)).coerceIn(0.4f, 1.0f)

        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                touchDownAt = System.currentTimeMillis()
                lastX = event.x
                lastY = event.y
                movedFar = false
                dragArmed = false
                twoFingerScroll = false
                view.postDelayed(dragArmRunnable, 550)  // 550ms 长按 → 按下左键
            }

            MotionEvent.ACTION_POINTER_DOWN -> {
                if (event.pointerCount >= 2) {
                    twoFingerScroll = true
                    view.removeCallbacks(dragArmRunnable)
                    if (dragArmed) {
                        dragArmed = false
                        mouse?.leftUp()
                        if (useBridge) sendHid("up", 0, 0)
                    }
                }
            }

            MotionEvent.ACTION_MOVE -> {
                val dx = event.x - lastX
                val dy = event.y - lastY
                lastX = event.x
                lastY = event.y

                if (twoFingerScroll) {
                    val wheel = (-dy / 6f).toInt().coerceIn(-127, 127)
                    mouse?.scroll(wheel)
                    if (useBridge) {
                        val now = System.currentTimeMillis()
                        if (now - lastHidSentAt >= 40) {   // 滚动同样节流，避免高频轰炸
                            lastHidSentAt = now
                            sendHid("scroll", wheel, 0)
                        }
                    }
                } else {
                    if (kotlin.math.hypot(dx, dy) > 15) movedFar = true
                    if (movedFar) view.removeCallbacks(dragArmRunnable)

                    // 相对映射（触控板）：手指移动多少，指针移动多少；单步限幅抑制 iOS 指针加速
                    val rdx = (dx * gain).toInt().coerceIn(-20, 20)
                    val rdy = (dy * gain).toInt().coerceIn(-20, 20)

                    if (rdx != 0 || rdy != 0) {
                        val now = System.currentTimeMillis()
                        val hidOk = useBridge && now - lastHidSentAt >= 40
                        var sent = false
                        if (useBridge) {
                            // 桥接盒模式：只在真实发出一条指令时才更新指针定位（避免节流丢弃导致的偏移）
                            if (hidOk) {
                                lastHidSentAt = now
                                sendHid(if (dragArmed) "drag" else "move", rdx, rdy)
                                sent = true
                            }
                        } else {
                            if (dragArmed) mouse?.drag(rdx, rdy) else mouse?.move(rdx, rdy)
                            sent = true   // 本机蓝牙直控，每条都发，必然更新
                        }
                        if (sent) {
                            px = (px + rdx).coerceIn(0f, PHONE_W)
                            py = (py + rdy).coerceIn(0f, PHONE_H)
                            updatePointerDot()
                        }
                    }
                }
            }

            MotionEvent.ACTION_POINTER_UP -> {
                if (event.pointerCount <= 2) twoFingerScroll = false
            }

            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                view.removeCallbacks(dragArmRunnable)
                val elapsed = System.currentTimeMillis() - touchDownAt

                if (dragArmed) {
                    mouse?.leftUp()
                    if (useBridge) sendHid("up", 0, 0)
                } else if (!movedFar && elapsed < 550) {
                    mouse?.click()
                    if (useBridge) sendHid("click", 0, 0)
                }
                dragArmed = false
                twoFingerScroll = false
            }
        }
    }

    // MARK: - 指令发送

    /** 桥接盒指令：ESP32 收到后以蓝牙 HID 键鼠身份操作 iPhone */
    private fun sendHid(action: String, dx: Int, dy: Int) {
        val msg = JSONObject().apply {
            put("type", "hid")
            put("action", action)
            // 坐标 clamp 到 HID 有符号单字节范围（±127）
            put("dx", dx.coerceIn(-127, 127))
            put("dy", dy.coerceIn(-127, 127))
        }
        webSocket?.send(msg.toString())
    }

    /** 远程打字：需桥接盒模式且盒子真的在线，由盒子以真实键盘键入 */
    private fun sendRemoteText() {
        if (!useBridge) {
            Toast.makeText(this, "打字需在首页开启\"桥接盒模式\"", Toast.LENGTH_SHORT).show()
            return
        }
        if (!bridgeOnline) {
            Toast.makeText(this, "桥接盒未连接，文字无法送达（盒子通电联网后自动恢复）", Toast.LENGTH_LONG).show()
            return
        }
        val text = textInput.text.toString()
        if (text.isEmpty()) return
        val msg = JSONObject().apply {
            put("type", "hid_text")
            put("value", text)
        }
        webSocket?.send(msg.toString())
        Toast.makeText(this, "已通过盒子键盘键入", Toast.LENGTH_SHORT).show()
    }

    /** 盒子设置对话框：提示音 / 震动 / 换房间码（经服务器 config 指令下发，桥接盒实时生效） */
    private fun showBoxSettings() {
        if (!useBridge) {
            Toast.makeText(this, "需开启桥接盒模式才能控制盒子", Toast.LENGTH_SHORT).show()
            return
        }
        if (!bridgeOnline) {
            Toast.makeText(this, "盒子不在线，配置无法送达", Toast.LENGTH_LONG).show()
            return
        }
        showBoxSettingsDialog()
    }

    /** 实际弹窗（含风扇转速 + 测试按钮，点击即发一次蜂鸣） */
    private fun showBoxSettingsDialog() {
        val view = layoutInflater.inflate(R.layout.dialog_box_settings, null)
        val vibSwitch = view.findViewById<androidx.appcompat.widget.SwitchCompat>(R.id.vibSwitch)
        val buzzSwitch = view.findViewById<androidx.appcompat.widget.SwitchCompat>(R.id.buzzSwitch)
        val fanSeekBar = view.findViewById<android.widget.SeekBar>(R.id.fanSeekBar)
        val fanValueLabel = view.findViewById<TextView>(R.id.fanValueLabel)
        val beepBtn = view.findViewById<Button>(R.id.beepBtn)

        val fanNames = listOf("停", "20%", "50%", "70%", "100%")
        fanSeekBar.setOnSeekBarChangeListener(object : android.widget.SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: android.widget.SeekBar?, progress: Int, fromUser: Boolean) {
                fanValueLabel.text = "当前：${fanNames[progress]}"
            }
            override fun onStartTrackingTouch(seekBar: android.widget.SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: android.widget.SeekBar?) {}
        })

        val dlg = AlertDialog.Builder(this)
            .setTitle("盒子设置")
            .setView(view)
            .setPositiveButton("应用", null)
            .setNegativeButton("关闭", null)
            .create()
        alertDlg = dlg
        dlg.setOnShowListener {
            dlg.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                sendConfig(
                    vib = vibSwitch.isChecked,
                    buzz = buzzSwitch.isChecked,
                    fan = fanSeekBar.progress
                )
                Toast.makeText(this, "已下发设置", Toast.LENGTH_SHORT).show()
            }
        }
        beepBtn.setOnClickListener { sendConfig(beep = true) }
        dlg.show()
    }

    // 持有对话框引用，避免多次弹开
    private var alertDlg: AlertDialog? = null

    /** 下发 config 指令到盒子（仅带参数项被发送，其余保持盒子当前值） */
    private fun sendConfig(vib: Boolean? = null, buzz: Boolean? = null,
                           fan: Int? = null,
                           beep: Boolean? = null, room: String? = null) {
        val msg = JSONObject().apply {
            put("type", "config")
            if (vib != null) put("vib", vib)
            if (buzz != null) put("buzz", buzz)
            if (fan != null) put("fan", fan)
            if (beep != null) put("beep", true)
            if (room != null) put("room", room)
        }
        webSocket?.send(msg.toString())
    }

    override fun onDestroy() {
        wantConnected = false
        mainHandler.removeCallbacksAndMessages(null)
        btMouse?.disconnect()
        webSocket?.close(1000, "bye")
        super.onDestroy()
    }

    companion object {
        /** iPhone 13 Pro 逻辑分辨率（pt），红框按此比例等比缩放 */
        const val PHONE_W = 390f
        const val PHONE_H = 844f
    }
}
