'use strict';
// ============================================================================
// WebGL2 后期：把 2D 层"印"成一页漫画。
//   1. 镜头：推拉、旋转、平移、波浪扭曲、反色（纸 ↔ 墨对调）
//   2. 网点：大片平的灰色自动变成 45° 网点纸；线条、字的边缘（灰度变化剧烈的地方）保持原样，不会被打成锯齿；
//      有颜色的地方（勿忘我的蓝）不打网点，像水彩
//   3. 纸：纸纹、墨色的轻微不均
//   4. 闪白 / 压黑、颗粒、很淡的暗角
//   5. 多次采样累加 = 运动模糊（导出时用）
// ============================================================================
const GLX = (() => {
  const canvas = mk();
  const gl = canvas.getContext('webgl2', { premultipliedAlpha: false, preserveDrawingBuffer: true, antialias: false });
  if (!gl) throw new Error('需要 WebGL2');
  const FLOAT = !!gl.getExtension('EXT_color_buffer_float');
  gl.getExtension('OES_texture_float_linear');

  const VS = `#version 300 es
  in vec2 p; out vec2 uv; void main(){ uv = p * .5 + .5; gl_Position = vec4(p, 0., 1.); }`;
  const LIB = `#version 300 es
  precision highp float; in vec2 uv; out vec4 o;
  uniform vec2 res; uniform float time;
  const vec3 PAPER = vec3(.957, .945, .918), INK = vec3(.067, .063, .078);
  float h21(vec2 p){ p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
  float vnoise(vec2 p){ vec2 i = floor(p), f = fract(p); f = f * f * (3. - 2. * f);
    return mix(mix(h21(i), h21(i + vec2(1, 0)), f.x), mix(h21(i + vec2(0, 1)), h21(i + vec2(1, 1)), f.x), f.y); }
  float fbm(vec2 p){ float s = 0., a = .5; for (int i = 0; i < 5; i++){ s += a * vnoise(p); p = p * 2.03 + 17.1; a *= .5; } return s; }
  float lum(vec3 c){ return dot(c, vec3(.299, .587, .114)); }
  mat2 rot(float a){ float c = cos(a), s = sin(a); return mat2(c, -s, s, c); }
  `;

  // ---- 1. 镜头
  const FS_LENS = LIB + `
  uniform sampler2D src; uniform float zoom, rotA, wave, invert; uniform vec2 shift;
  vec3 tap(vec2 u){ return (u.x < 0. || u.x > 1. || u.y < 0. || u.y > 1.) ? PAPER : texture(src, u).rgb; }
  void main(){
    vec2 u = uv; float asp = res.x / res.y;
    u.x += (sin(u.y * 22. + time * 3.) * .012 + sin(u.y * 5. - time * 1.7) * .008) * wave;
    vec2 c = (u - .5 - shift) * vec2(asp, 1.);
    c = rot(rotA) * c; c /= zoom;
    vec3 col = tap(c / vec2(asp, 1.) + .5);
    col = mix(col, PAPER + INK - col, invert);
    o = vec4(col, 1.);
  }`;

  // ---- 2. 网点 + 纸
  const FS_TONE = LIB + `
  uniform sampler2D src; uniform float tone, cell, paperAmt;
  void main(){
    vec2 px = 1. / res;
    vec3 col = texture(src, uv).rgb; float l = lum(col);
    // 边缘检测：周围灰度变化大 = 线条 / 字 / 原本就有网点的插画 → 不打网点
    float e = 0.;
    for (int k = 0; k < 8; k++) {
      float a = float(k) * .785398; vec2 d = vec2(cos(a), sin(a)) * 1.6 * px;
      e = max(e, abs(lum(texture(src, uv + d).rgb) - l));
    }
    float flat_ = 1. - smoothstep(.035, .12, e);
    float cover = clamp((lum(PAPER) - l) / (lum(PAPER) - lum(INK)), 0., 1.);
    vec2 g = rot(.785398) * gl_FragCoord.xy / cell;
    float spot = .5 - .25 * (cos(6.2832 * g.x) + cos(6.2832 * g.y));
    float c2 = smoothstep(.05, .95, cover);
    float dots = smoothstep(spot - .16, spot + .16, c2);
    float ink = mix(cover, dots, flat_ * tone);
    // 纸纹：大的纤维云 + 细的纤维 + 少量斑点
    vec2 q = gl_FragCoord.xy;
    float fib = fbm(q * .004) * .6 + fbm(q * vec2(.05, .012)) * .4;
    float speck = step(.9993, h21(floor(q / 2.)));
    vec3 paper = PAPER * (1. - paperAmt * (.045 * (fib - .5) * 2. + .03)) - speck * .25 * paperAmt;
    vec3 inkC = INK * (1. + paperAmt * .5 * (fbm(q * .02) - .5));
    vec3 outc = mix(paper, inkC, ink);
    // 有颜色的地方：保留颜色，乘上纸纹
    float chroma = max(col.r, max(col.g, col.b)) - min(col.r, min(col.g, col.b));
    float colored = smoothstep(.06, .16, chroma);
    outc = mix(outc, col * paper / PAPER, colored);
    o = vec4(outc, 1.);
  }`;

  // ---- 3. 最终
  const FS_FINAL = LIB + `
  uniform sampler2D src; uniform float flash, dark, grain, vig, weight; uniform vec3 flashCol;
  void main(){
    vec3 col = texture(src, uv).rgb;
    col = mix(col, flashCol, clamp(flash, 0., 1.));
    col *= 1. - dark;
    float gn = h21(gl_FragCoord.xy + fract(time * 7.13) * 977.) - .5;
    col += gn * grain;
    vec2 v = (uv - .5) * vec2(1., .9); col *= mix(1., smoothstep(1.05, .3, length(v) * 1.2), vig);
    o = vec4(max(col, 0.) * weight, 1.);
  }`;
  const FS_COPY = LIB + `uniform sampler2D src; void main(){ o = vec4(texture(src, uv).rgb, 1.); }`;

  // ---------------------------------------------------------------- GL 工具
  function compile(type, src) { const s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s); if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s)); return s; }
  function program(fs) {
    const p = gl.createProgram(); gl.attachShader(p, compile(gl.VERTEX_SHADER, VS)); gl.attachShader(p, compile(gl.FRAGMENT_SHADER, fs));
    gl.bindAttribLocation(p, 0, 'p'); gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
    const u = {}; const n = gl.getProgramParameter(p, gl.ACTIVE_UNIFORMS);
    for (let i = 0; i < n; i++) { const info = gl.getActiveUniform(p, i); u[info.name] = { loc: gl.getUniformLocation(p, info.name), type: info.type }; }
    return { p, u };
  }
  function set(pr, vals) {
    for (const k in vals) {
      const u = pr.u[k]; if (!u) continue; const v = vals[k];
      if (u.type === gl.FLOAT) gl.uniform1f(u.loc, v);
      else if (u.type === gl.FLOAT_VEC2) gl.uniform2fv(u.loc, v);
      else if (u.type === gl.FLOAT_VEC3) gl.uniform3fv(u.loc, v);
      else if (u.type === gl.INT || u.type === gl.SAMPLER_2D) gl.uniform1i(u.loc, v);
    }
  }
  function tex(w, h, float) {
    const t = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, t);
    gl.texImage2D(gl.TEXTURE_2D, 0, float && FLOAT ? gl.RGBA16F : gl.RGBA8, w, h, 0, gl.RGBA, float && FLOAT ? gl.HALF_FLOAT : gl.UNSIGNED_BYTE, null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    return t;
  }
  function fbo(w, h, float) { const t = tex(w, h, float), f = gl.createFramebuffer(); gl.bindFramebuffer(gl.FRAMEBUFFER, f); gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, t, 0); return { t, f, w, h }; }
  const buf = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, buf);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
  gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
  function pass(pr, target, vals, texs = {}) {
    gl.useProgram(pr.p);
    let unit = 0;
    for (const k in texs) { gl.activeTexture(gl.TEXTURE0 + unit); gl.bindTexture(gl.TEXTURE_2D, texs[k]); set(pr, { [k]: unit }); unit++; }
    set(pr, vals);
    gl.bindFramebuffer(gl.FRAMEBUFFER, target ? target.f : null);
    gl.viewport(0, 0, target ? target.w : W, target ? target.h : H);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  }

  const P = { lens: program(FS_LENS), tone: program(FS_TONE), final: program(FS_FINAL), copy: program(FS_COPY) };
  const layerTex = tex(W, H, false);
  const F = { lens: fbo(W, H, false), tone: fbo(W, H, false), acc: fbo(W, H, true) };
  const hex = s => { const n = parseInt(s.slice(1), 16); return [(n >> 16 & 255) / 255, (n >> 8 & 255) / 255, (n & 255) / 255]; };

  // 渲染一个子帧并累加（weight = 1/子帧数）。first 为 true 时先清空累加缓冲。
  function frame(layer, time, fx, weight, first) {
    gl.bindTexture(gl.TEXTURE_2D, layerTex);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, layer);
    const common = { res: [W, H], time };
    pass(P.lens, F.lens, { ...common, zoom: fx.zoom, rotA: fx.rot, shift: fx.shift, wave: fx.wave, invert: fx.invert }, { src: layerTex });
    pass(P.tone, F.tone, { ...common, tone: fx.tone, cell: fx.cell, paperAmt: fx.paper }, { src: F.lens.t });
    if (first) { gl.bindFramebuffer(gl.FRAMEBUFFER, F.acc.f); gl.clearColor(0, 0, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT); }
    gl.enable(gl.BLEND); gl.blendFunc(gl.ONE, gl.ONE);
    pass(P.final, F.acc, { ...common, flash: fx.flash, flashCol: hex(fx.flashCol), dark: fx.dark, grain: fx.grain, vig: fx.vig, weight }, { src: F.tone.t });
    gl.disable(gl.BLEND);
  }
  function present() { pass(P.copy, null, { res: [W, H], time: 0 }, { src: F.acc.t }); return canvas; }
  return { frame, present, canvas };
})();
