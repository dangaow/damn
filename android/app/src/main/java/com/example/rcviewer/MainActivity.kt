package com.example.rcviewer

import android.content.Intent
import android.os.Bundle
import android.widget.Button
import android.widget.EditText
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity

/**
 * 连接配置页：填服务器/房间码 → 点"连接"进入全屏操作页（ControlActivity）
 */
class MainActivity : AppCompatActivity() {

    private lateinit var serverField: EditText
    private lateinit var roomField: EditText
    private lateinit var bridgeModeBox: androidx.appcompat.widget.SwitchCompat

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        serverField = findViewById(R.id.serverField)
        roomField = findViewById(R.id.roomField)
        bridgeModeBox = findViewById(R.id.bridgeModeBox)
        findViewById<Button>(R.id.connectButton).setOnClickListener { openControl() }

        // 恢复上次输入的服务器/房间号（默认留空，由用户填写）
        val prefs = getSharedPreferences("rc", MODE_PRIVATE)
        serverField.setText(prefs.getString("server", ""))
        roomField.setText(prefs.getString("room", ""))
    }

    private fun openControl() {
        val server = serverField.text.toString().trim()
        val room = roomField.text.toString().trim()
        if (server.isEmpty()) {
            Toast.makeText(this, "请输入服务器地址", Toast.LENGTH_SHORT).show()
            return
        }
        if (room.isEmpty()) {
            Toast.makeText(this, "请输入房间码（两端必须一致）", Toast.LENGTH_SHORT).show()
            return
        }

        // 记住输入，下次打开自动填
        getSharedPreferences("rc", MODE_PRIVATE).edit()
            .putString("server", server).putString("room", room).apply()

        startActivity(Intent(this, ControlActivity::class.java).apply {
            putExtra("server", server)
            putExtra("room", room)
            putExtra("bridge", bridgeModeBox.isChecked)
        })
    }
}
