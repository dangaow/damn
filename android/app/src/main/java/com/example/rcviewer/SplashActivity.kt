package com.example.rcviewer

import android.animation.ValueAnimator
import android.content.Intent
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Shader
import android.os.Bundle
import android.view.View
import androidx.appcompat.app.AppCompatActivity

/**
 * 开屏动画（与固件开机动画同款风格）：
 *   黑屏 → 左右两道光柱在屏幕边缘亮起（闪烁）
 *   → 双柱向中间对扫（带渐变光晕尾迹）
 *   → 中心交汇白闪 → "MAKE STUDIO" 从正中向两侧展开
 *   → 副标题"莫莫专属助手"淡入 → 停留后进入连接配置页
 */
class SplashActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(SplashView(this))
    }

    private inner class SplashView(activity: SplashActivity) : View(activity) {

        // 时间轴（毫秒）
        private val tBlack = 300f      // 纯黑
        private val tFlick = 600f      // 边缘光柱闪烁
        private val tSweepEnd = 1100f  // 对扫
        private val tFlash = 140f      // 交汇白闪
        private val tReveal = 700f     // LOGO 展开
        private val tHold = 1500f      // 停留
        private val tFade = 350f       // 整体淡出
        private val total = tBlack + tFlick + tSweepEnd + tFlash + tReveal + tHold + tFade

        private val animator = ValueAnimator.ofFloat(0f, 1f).apply {
            duration = total.toLong()
            addUpdateListener { this@SplashView.postInvalidateOnAnimation() }
        }

        private var startTime = -1L
        private var launched = false

        // 画笔
        private val beamPaint = Paint(Paint.ANTI_ALIAS_FLAG)     // 光柱核心
        private val trailPaint = Paint(Paint.ANTI_ALIAS_FLAG)    // 光晕尾迹
        private val flashPaint = Paint(Paint.ANTI_ALIAS_FLAG)    // 白闪
        private val logoPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = Color.WHITE
            isFakeBoldText = true
            textAlign = Paint.Align.CENTER
            letterSpacing = 0.12f
        }
        private val subPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = Color.WHITE
            textAlign = Paint.Align.CENTER
        }
        private val veilPaint = Paint()                           // 收尾黑幕

        init {
            animator.start()
            startTime = android.os.SystemClock.elapsedRealtime()
        }

        override fun onDraw(canvas: Canvas) {
            if (startTime < 0) return
            val t = (android.os.SystemClock.elapsedRealtime() - startTime).toFloat()
            val w = width.toFloat()
            val h = height.toFloat()
            val cx = w / 2f
            val coreW = w * 0.030f        // 光柱核心宽
            val trailW = w * 0.22f         // 光晕尾迹宽
            val beamH = h * 0.86f
            val beamY0 = (h - beamH) / 2f

            // ---------- 阶段1：黑屏 ----------
            if (t < tBlack) {
                canvas.drawColor(Color.BLACK)
                scheduleNext()
                return
            }

            canvas.drawColor(Color.BLACK)
            var beamsAlpha = 1f

            // ---------- 阶段2：边缘闪烁（0→1 的方波感） ----------
            if (t < tBlack + tFlick) {
                val ft = (t - tBlack) / tFlick
                // 两下"啪啪"：0~0.3 亮暗交替，0.3 后常亮
                beamsAlpha = if (ft < 0.3f) {
                    if (((ft / 0.075f).toInt() % 2) == 0) 1f else 0.1f
                } else 1f
            }

            // ---------- 阶段3：对扫 + 尾迹光晕 ----------
            var sweepP = 0f
            if (t >= tBlack + tFlick && t < tBlack + tFlick + tSweepEnd) {
                sweepP = (t - tBlack - tFlick) / tSweepEnd
            } else if (t >= tBlack + tFlick + tSweepEnd) {
                sweepP = 1f
            }
            // 左柱位置：从最左扫到中心；右柱对称
            val lx = coreW + (cx - coreW * 3f) * sweepP
            val rx = (w - coreW) - (cx - coreW * 3f) * sweepP

            if (sweepP < 1f && t < tBlack + tFlick + tSweepEnd + tFlash) {
                // 左柱：核心亮带（白→紫渐变，竖直方向）
                beamPaint.alpha = (beamsAlpha * 255).toInt()
                beamPaint.shader = LinearGradient(
                    0f, beamY0, 0f, beamY0 + beamH,
                    intArrayOf(0x00FFFFFF, Color.WHITE, 0x00FFFFFF),
                    floatArrayOf(0f, 0.5f, 1f), Shader.TileMode.CLAMP
                )
                canvas.drawRect(lx - coreW, beamY0, lx + coreW, beamY0 + beamH, beamPaint)
                // 左柱尾迹（朝左渐隐的紫晕）
                trailPaint.alpha = (beamsAlpha * 200).toInt()
                trailPaint.shader = LinearGradient(
                    lx - coreW, 0f, lx - coreW - trailW, 0f,
                    intArrayOf(0x80667EEA.toInt(), 0x00000000),
                    null, Shader.TileMode.CLAMP
                )
                canvas.drawRect(lx - coreW - trailW, beamY0, lx - coreW, beamY0 + beamH, trailPaint)

                // 右柱
                canvas.drawRect(rx - coreW, beamY0, rx + coreW, beamY0 + beamH, beamPaint)
                trailPaint.shader = LinearGradient(
                    rx + coreW, 0f, rx + coreW + trailW, 0f,
                    intArrayOf(0x80764BA2.toInt(), 0x00000000),
                    null, Shader.TileMode.CLAMP
                )
                canvas.drawRect(rx + coreW, beamY0, rx + coreW + trailW, beamY0 + beamH, trailPaint)
            }

            // ---------- 阶段4：交汇白闪 ----------
            var flashAlpha = 0f
            val tAfterSweep = t - tBlack - tFlick - tSweepEnd
            if (tAfterSweep in 0f..tFlash) {
                flashAlpha = 1f - (tAfterSweep / tFlash)   // 255 → 0 快速衰减
                flashPaint.color = Color.WHITE
                flashPaint.alpha = (flashAlpha * 255).toInt()
                canvas.drawRect(0f, 0f, w, h, flashPaint)
            }

            // ---------- 阶段5：LOGO 从正中向两侧展开 ----------
            val tAfterFlash = tAfterSweep - tFlash
            if (tAfterFlash > 0f) {
                val revealP = (tAfterFlash / tReveal).coerceAtMost(1f)
                val halfW = w * 0.55f * easeOut(revealP)

                logoPaint.textSize = w * 0.085f
                subPaint.textSize = w * 0.042f
                subPaint.alpha = (255 * (revealP * revealP)).toInt()   // 副标题晚一点淡入

                val saveCount = canvas.save()
                canvas.clipRect(cx - halfW, 0f, cx + halfW, h)
                canvas.drawText("MAKE STUDIO", cx, h / 2f - w * 0.01f, logoPaint)
                canvas.drawText("莫莫专属助手", cx, h / 2f + w * 0.085f, subPaint)
                canvas.restoreToCount(saveCount)

                // LOGO 底部一条渐宽的光线，呼应光柱交汇
                if (revealP > 0.05f) {
                    trailPaint.shader = LinearGradient(
                        cx - halfW, 0f, cx + halfW, 0f,
                        intArrayOf(0x00667EEA, 0xCCFFFFFF.toInt(), 0x00764BA2),
                        null, Shader.TileMode.CLAMP
                    )
                    canvas.drawRect(cx - halfW, h / 2f + w * 0.02f, cx + halfW, h / 2f + w * 0.024f, trailPaint)
                }
            }

            // ---------- 阶段6：收尾淡出 ----------
            val tFadeStart = total - tFade
            if (t > tFadeStart) {
                veilPaint.color = Color.BLACK
                veilPaint.alpha = (255 * (t - tFadeStart) / tFade).toInt()
                canvas.drawRect(0f, 0f, w, h, veilPaint)
            }

            // 播完 → 进连接配置页
            if (t >= total && !launched) {
                launched = true
                startActivity(Intent(this@SplashActivity, MainActivity::class.java))
                overridePendingTransition(0, 0)
                finish()
                return
            }
            scheduleNext()
        }

        private fun easeOut(p: Float) = 1f - (1f - p) * (1f - p)

        private fun scheduleNext() {
            postInvalidateOnAnimation()
        }

        override fun onDetachedFromWindow() {
            animator.cancel()
            super.onDetachedFromWindow()
        }
    }
}
