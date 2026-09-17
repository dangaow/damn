package com.example.rcviewer

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothHidDevice
import android.bluetooth.BluetoothHidDeviceAppSdpSettings
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.content.Context
import android.os.Handler
import android.os.Looper

/**
 * 把安卓手机模拟成经典蓝牙 HID 鼠标（Android 9+ 的 HID Device 角色）。
 *
 * iPhone 一次性配置（后全局真实控制，无需越狱）：
 *   设置 → 辅助功能 → 触控 → 辅助触控 → 打开
 *   → 设备（指针设备）→ 蓝牙设备 → 选中本安卓手机完成配对
 * 之后在本 App 里点"连接"即可，安卓发的移动/点击/拖拽会被 iOS
 * 当作真实鼠标输入处理，可在任意界面操作。
 *
 * 注意：部分 ROM（尤其某些国行机型）裁剪了 HID Device profile，
 * 表现为 registerApp 后 iPhone 搜不到设备，属系统限制。
 */
@SuppressLint("MissingPermission")
class BtHidMouse(private val context: Context) {

    companion object {
        private const val TAG = "BtHidMouse"
        private const val REPORT_ID = 1
    }

    /** 标准鼠标报告描述符：3 键 + 相对 XY + 滚轮，Report ID = 1 */
    private val descriptor = intArrayOf(
        0x05, 0x01,       // Usage Page (Generic Desktop)
        0x09, 0x02,       // Usage (Mouse)
        0xA1, 0x01,       // Collection (Application)
        0x85, REPORT_ID,  //   Report ID (1)
        0x09, 0x01,       //   Usage (Pointer)
        0xA1, 0x00,       //   Collection (Physical)
        0x05, 0x09,       //     Usage Page (Buttons)
        0x19, 0x01,       //     Usage Minimum (1)
        0x29, 0x03,       //     Usage Maximum (3)
        0x15, 0x00,       //     Logical Minimum (0)
        0x25, 0x01,       //     Logical Maximum (1)
        0x95, 0x03,       //     Report Count (3)
        0x75, 0x01,       //     Report Size (1)
        0x81, 0x02,       //     Input (Data, Variable, Absolute)
        0x95, 0x01,       //     Report Count (1)
        0x75, 0x05,       //     Report Size (5) —— 按钮位补齐
        0x81, 0x01,       //     Input (Constant)
        0x05, 0x01,       //     Usage Page (Generic Desktop)
        0x09, 0x30,       //     Usage (X)
        0x09, 0x31,       //     Usage (Y)
        0x15, 0x81,       //     Logical Minimum (-127，toByte 后即 0x81)
        0x25, 0x7F,       //     Logical Maximum (127)
        0x75, 0x08,       //     Report Size (8)
        0x95, 0x02,       //     Report Count (2)
        0x81, 0x06,       //     Input (Data, Variable, Relative)
        0x09, 0x38,       //     Usage (Wheel)
        0x15, 0x81,
        0x25, 0x7F,
        0x75, 0x08,
        0x95, 0x01,
        0x81, 0x06,       //     Input (Data, Variable, Relative)
        0xC0,             //   End Collection
        0xC0              // End Collection
    ).map { it.toByte() }.toByteArray()

    // 状态回调（主线程）
    var onStatus: ((String) -> Unit)? = null
    var onConnected: ((Boolean) -> Unit)? = null

    private val mainHandler = Handler(Looper.getMainLooper())
    private var hid: BluetoothHidDevice? = null
    private var serviceReady = false
    private var host: BluetoothDevice? = null

    var isConnected = false
        private set

    // MARK: - 注册 HID 应用

    /** 初始化并注册为 HID 鼠标（必须在 iPhone 配对前调用一次，之后保持注册） */
    fun register() {
        val adapter = adapter() ?: run {
            onStatus?.invoke("此设备不支持蓝牙")
            return
        }
        if (!adapter.isEnabled) {
            onStatus?.invoke("请先打开系统蓝牙")
            return
        }
        if (serviceReady) return

        adapter.getProfileProxy(context, object : BluetoothProfile.ServiceListener {
            override fun onServiceConnected(profile: Int, proxy: BluetoothProfile) {
                if (profile != BluetoothProfile.HID_DEVICE) return
                hid = proxy as? BluetoothHidDevice
                serviceReady = hid != null

                val sdp = BluetoothHidDeviceAppSdpSettings(
                    "Android Mouse",           // 设备名（iPhone 配对列表里显示）
                    "Remote Control Mouse",
                    "RCViewer",
                    BluetoothHidDevice.SUBCLASS1_MOUSE,
                    descriptor
                )
                val ok = hid?.registerApp(sdp, null, null, Runnable::run, callback)
                if (ok != true) {
                    mainHandler.post { onStatus?.invoke("HID 注册失败（ROM 可能不支持外设角色）") }
                }
            }

            override fun onServiceDisconnected(profile: Int) {
                if (profile == BluetoothProfile.HID_DEVICE) {
                    hid = null
                    serviceReady = false
                    updateConnected(false, null)
                }
            }
        }, BluetoothProfile.HID_DEVICE)
    }

    private val callback = object : BluetoothHidDevice.Callback() {
        override fun onAppStatusChanged(pluggedDevice: BluetoothDevice?, plugged: Boolean) {
            // plugged = true 表示 SDP 记录已对外广播，iPhone 可以搜到并配对
            if (plugged) mainHandler.post { onStatus?.invoke("HID 鼠标已就绪，等待 iPhone 配对/连接") }
        }

        override fun onConnectionStateChanged(device: BluetoothDevice, state: Int) {
            updateConnected(state == BluetoothProfile.STATE_CONNECTED, device)
        }
    }

    private fun updateConnected(connected: Boolean, device: BluetoothDevice?) {
        mainHandler.post {
            isConnected = connected
            host = if (connected) device else host?.takeIf { connected }
            if (!connected) host = null
            onConnected?.invoke(connected)
            onStatus?.invoke(if (connected) "蓝牙鼠标已连接 iPhone ✓" else "蓝牙鼠标已断开")
        }
    }

    // MARK: - 连接管理

    /** 已配对设备列表（iPhone 在辅助触控里配对过后会出现在这里） */
    fun bondedDevices(): List<BluetoothDevice> =
        adapter()?.bondedDevices?.toList() ?: emptyList()

    /** 连接指定 iPhone（须先在 iPhone 端完成过一次配对） */
    fun connect(device: BluetoothDevice) {
        if (!serviceReady) {
            register()
            onStatus?.invoke("HID 初始化中，稍候 2 秒再点连接")
            return
        }
        host = device
        hid?.connect(device)
    }

    fun disconnect() {
        host?.let { hid?.disconnect(it) }
    }

    // MARK: - 鼠标报告

    private fun report(buttons: Int, dx: Int, dy: Int, wheel: Int = 0): Boolean {
        val h = hid ?: return false
        val d = host ?: return false
        return h.sendReport(d, REPORT_ID, byteArrayOf(
            buttons.toByte(),
            dx.coerceIn(-127, 127).toByte(),
            dy.coerceIn(-127, 127).toByte(),
            wheel.coerceIn(-127, 127).toByte()
        ))
    }

    /** 移动指针（相对位移） */
    fun move(dx: Int, dy: Int) {
        if (isConnected) report(0, dx, dy)
    }

    /** 按住左键移动（拖拽） */
    fun drag(dx: Int, dy: Int) {
        if (isConnected) report(1, dx, dy)
    }

    /** 左键按下（长按开始拖拽时调用） */
    fun leftDown() {
        if (isConnected) report(1, 0, 0)
    }

    /** 左键抬起 */
    fun leftUp() {
        if (isConnected) report(0, 0, 0)
    }

    /** 单击：按下 30ms 后抬起，确保 iOS 识别为完整点击 */
    fun click() {
        if (!isConnected) return
        report(1, 0, 0)
        mainHandler.postDelayed({ report(0, 0, 0) }, 30)
    }

    /** 滚轮（正=向上滚动） */
    fun scroll(wheel: Int) {
        if (isConnected) report(0, 0, 0, wheel)
    }

    private fun adapter(): BluetoothAdapter? =
        (context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter
}
