'use strict';
// ============================================================================
// WebGL2：动态背景 + 后期。
//   1. 背景着色器（烟雾 / 隧道 / 放射线 / 夜雨 / 漏光 / 雪花 / 网点）+ 叠上 2D 画布（文字、雨、粒子……）
//   2. 镜头：鱼眼猛推、旋转、径向色差、横向错位、方块撕裂、波浪扭曲、复印机颗粒、调色、反色
//   3. 辉光（bloom）
//   4. 闪光、压暗、胶片颗粒、扫描线、暗角、黑边
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
  float h21(vec2 p){ p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
  float vnoise(vec2 p){ vec2 i = floor(p), f = fract(p); f = f * f * (3. - 2. * f);
    return mix(mix(h21(i), h21(i + vec2(1, 0)), f.x), mix(h21(i + vec2(0, 1)), h21(i + vec2(1, 1)), f.x), f.y); }
  float fbm(vec2 p){ float s = 0., a = .5; for (int i = 0; i < 5; i++){ s += a * vnoise(p); p = p * 2.03 + 17.1; a *= .5; } return s; }
  float lum(vec3 c){ return dot(c, vec3(.299, .587, .114)); }
  mat2 rot(float a){ float c = cos(a), s = sin(a); return mat2(c, -s, s, c); }
  `;

  // ---- 1. 背景 + 合成 2D 层
  const FS_SCENE = LIB + `
  uniform sampler2D layer; uniform int mode; uniform vec3 cA, cB, cC; uniform float amt, speed, pulse, seed, mirror, bz; uniform vec2 boff;
  vec3 bg(vec2 p){
    float asp = res.x / res.y; vec2 q = (p - .5) * vec2(asp, 1.); float T = time * speed;
    if (mode == 1) {                                   // 烟雾
      vec2 w = vec2(fbm(q * 1.6 + T * .04), fbm(q * 1.6 - T * .05 + 5.2));
      float n = fbm(q * 2.2 + w * 1.8 + vec2(0., T * .06) + seed);
      return mix(cA, cB, smoothstep(.35, .95, n) * amt * (1. + pulse * .9)) + cC * pow(n, 6.) * amt * 2.;
    }
    if (mode == 2) {                                   // 隧道：方框一圈圈冲过来
      float d = max(abs(q.x) / asp * 1.15, abs(q.y)) * 2.;
      float z = 1. / (d + .03), f = fract(z * .42 - T * .9);
      float band = smoothstep(.93, 1., f) + smoothstep(.07, 0., f);
      float fade = smoothstep(.0, .5, d) * (.5 + .5 * fbm(q * 3. + T * .1));
      return cA + cB * band * amt * fade * (1. + pulse * 2.5) + cC * smoothstep(.2, 0., d) * .3;
    }
    if (mode == 3) {                                   // 放射线
      float a = atan(q.y, q.x), r = length(q);
      float s = smoothstep(.45, .55, fract(a / 6.2832 * 14. + T * .03 + sin(r * 3. - T * .2) * .05));
      return mix(cA, cB, s * amt * smoothstep(.0, .7, r) * (.6 + pulse)) + cC * exp(-r * 4.) * .4 * (1. + pulse);
    }
    if (mode == 4) {                                   // 夜雨：渐变 + 雾 + 远处竖条光
      vec3 col = mix(cB, cA, smoothstep(0., 1., p.y));
      col += cB * fbm(q * 2. + vec2(T * .03, -T * .02)) * .5 * amt;
      float cx = floor(p.x * 60.), s = h21(vec2(cx, 3.1));
      float on = smoothstep(.93, 1., s) * (.6 + .4 * sin(T * .7 + s * 50.));
      col += cC * on * pow(1. - p.y, 1.5) * .35 * amt * smoothstep(.5, .0, abs(fract(p.x * 60.) - .5));
      return col * (1. + pulse * .5);
    }
    if (mode == 5) {                                   // 暖色漏光 + 胶片烧灼
      float n = fbm(q * 2.5 + T * .05);
      float L1 = exp(-length((p - vec2(-.05, .95 + .05 * sin(T * .21))) * vec2(1., 1.4)) * 2.4);
      float L2 = exp(-length((p - vec2(1.08, .35 + .2 * sin(T * .13))) * vec2(1.3, .9)) * 2.8);
      float leak = (L1 + L2 * .8) * (.55 + .7 * n) * amt;
      float burn = smoothstep(.62, .9, fbm(q * 2.2 + seed + T * .015)) * amt * .12;   // 很淡的烧灼斑
      return cA + cB * leak + cC * leak * leak * 1.4 + cB * burn;
    }
    if (mode == 6) {                                   // 电视雪花
      float n = h21(floor(p * res / 2.) + floor(time * 30.) * vec2(13.1, 7.7));
      float roll = smoothstep(.0, .04, abs(fract(p.y * .7 + time * .4) - .5) - .44);
      return mix(cA, vec3(n) * (.75 + .25 * roll), amt);
    }
    if (mode == 7) {                                   // 复印网点
      vec2 g = q * 34.; vec2 cell = fract(g) - .5, id = floor(g);
      float v = fbm(id * .06 + vec2(T * .05, T * .03) + seed);
      float r = pow(v, 1.6) * .62 * amt * (1. + pulse * .6);
      return mix(cA, cB, smoothstep(r, r - .1, length(cell)));
    }
    return cA;
  }
  void main(){
    vec2 p = (uv - .5) / bz + .5 + boff; if (mirror > .5) p.x = .5 - abs(p.x - .5);   // bz / boff：背景跟着鼓点微微跳
    vec4 L = texture(layer, uv);
    o = vec4(bg(p) * (1. - L.a) + L.rgb, 1.);
  }`;

  // ---- 2. 镜头 + 调色
  const FS_LENS = LIB + `
  uniform sampler2D src; uniform float zoom, rotA, barrel, ca, split, glitch, gseed, wave, invert, xerox, gradeMix, tear;
  uniform vec2 shift, sq; uniform int grade;
  vec3 tap(vec2 u){ return (u.x < 0. || u.x > 1. || u.y < 0. || u.y > 1.) ? vec3(0.) : texture(src, u).rgb; }
  void main(){
    vec2 u = uv; float asp = res.x / res.y;
    if (glitch > 0.) {                                 // 方块撕裂
      float row = floor(u.y * 30.), hr = h21(vec2(row, gseed));
      if (hr < glitch * .55) u.x += (h21(vec2(row, gseed + 1.)) - .5) * .3 * glitch;
      vec2 b = floor(u * vec2(18., 10.)); float hb = h21(b + gseed * 3.1);
      if (hb < glitch * .12) u += (vec2(h21(b + 9.1 + gseed), h21(b + 4.7 + gseed)) - .5) * .12;
    }
    if (tear > 0.) { float band = step(.92, h21(vec2(floor(u.y * 120.), floor(time * 30.)))); u.x += band * tear * .03; }
    u.x += (sin(u.y * 38. + time * 11.) * .02 + sin(u.y * 6. - time * 4.) * .012) * wave;
    vec2 c = (u - .5 - shift) * vec2(asp, 1.);
    c = rot(rotA) * c; c /= zoom; c /= sq; c *= 1. + barrel * dot(c, c);
    vec2 su = c / vec2(asp, 1.) + .5, d = su - .5;
    vec3 col = vec3(tap(.5 + d * (1. + ca * .006) + vec2(split * .004, 0.)).r, tap(su).g, tap(.5 + d * (1. - ca * .006) - vec2(split * .004, 0.)).b);
    if (xerox > 0.) {                                  // 复印机：高对比 + 毛刺阈值
      float l = lum(col) + (h21(floor(su * res / 2.) + floor(time * 15.)) - .5) * .22;
      col = mix(col, vec3(smoothstep(.38, .52, l)) * mix(vec3(1.), col / max(lum(col), .05), .35), xerox);
    }
    float l = lum(col); vec3 g = col;
    if (grade == 1) g = mix(vec3(.02, 0., .005), vec3(.92, .05, .12), smoothstep(.0, .55, l)) + vec3(1., .85, .8) * smoothstep(.55, 1., l);
    else if (grade == 2) g = col * vec3(.72, .88, 1.22) + vec3(0., .01, .04);
    else if (grade == 3) g = col * vec3(1.22, .96, .72) + vec3(.03, .012, 0.);
    else if (grade == 4) g = mix(vec3(.02, .35, .2), vec3(.95, .1, .75), smoothstep(.1, .8, l)) * (.25 + l * 1.4);
    else if (grade == 5) g = vec3(l) * vec3(.94, .97, 1.04);
    col = mix(col, g, gradeMix);
    col = mix(col, 1. - col, invert);
    col *= 1. + (1. / max(sq.y, .01) - 1.) * .04;       // CRT 关机时压扁的亮线更亮
    o = vec4(col, 1.);
  }`;

  // ---- 3. 辉光
  const FS_BRIGHT = LIB + `uniform sampler2D src; uniform float thr;
  void main(){ vec3 c = texture(src, uv).rgb; o = vec4(max(c - thr, 0.) * 1.6, 1.); }`;
  const FS_BLUR = LIB + `uniform sampler2D src; uniform vec2 dir;
  void main(){ vec3 s = texture(src, uv).rgb * .227;
    s += (texture(src, uv + dir * 1.385).rgb + texture(src, uv - dir * 1.385).rgb) * .316;
    s += (texture(src, uv + dir * 3.231).rgb + texture(src, uv - dir * 3.231).rgb) * .07;
    o = vec4(s, 1.); }`;

  // ---- 4. 最终
  const FS_FINAL = LIB + `
  uniform sampler2D src, bloomTex; uniform float bloom, flash, dark, grain, scan, vig, lb, weight; uniform vec3 flashCol;
  void main(){
    vec3 col = texture(src, uv).rgb + texture(bloomTex, uv).rgb * bloom;
    col = mix(col, flashCol, clamp(flash, 0., 1.));
    col *= 1. - dark;
    float gn = h21(gl_FragCoord.xy + fract(time * 7.13) * 977.) - .5;
    col += gn * grain * (.25 + .5 * (1. - lum(col)));
    col *= 1. - scan * .4 * step(1.5, mod(gl_FragCoord.y, 3.));
    vec2 v = (uv - .5) * vec2(1., .9); col *= mix(1., smoothstep(.95, .25, length(v) * 1.25), vig);
    if (uv.y < lb || uv.y > 1. - lb) col = vec3(0.);
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

  const P = { scene: program(FS_SCENE), lens: program(FS_LENS), bright: program(FS_BRIGHT), blur: program(FS_BLUR), final: program(FS_FINAL), copy: program(FS_COPY) };
  const layerTex = tex(W, H, false);
  const F = { scene: fbo(W, H, true), lens: fbo(W, H, true), b1: fbo(W / 4, H / 4, true), b2: fbo(W / 4, H / 4, true), acc: fbo(W, H, true) };
  const hex = s => { const n = parseInt(s.slice(1), 16); return [(n >> 16 & 255) / 255, (n >> 8 & 255) / 255, (n & 255) / 255]; };
  const MODES = { solid: 0, smoke: 1, tunnel: 2, rays: 3, night: 4, leak: 5, static: 6, halftone: 7 };
  const GRADES = { none: 0, red: 1, cold: 2, warm: 3, sick: 4, silver: 5 };

  // 渲染一个子帧并累加（weight = 1/子帧数）。first 为 true 时先清空累加缓冲。
  function frame(layer, time, bg, fx, weight, first) {
    gl.bindTexture(gl.TEXTURE_2D, layerTex);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true); gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, true);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, layer);
    const common = { res: [W, H], time };
    pass(P.scene, F.scene, { ...common, mode: MODES[bg.mode] ?? 0, cA: hex(bg.a ?? '#000000'), cB: hex(bg.b ?? '#101830'), cC: hex(bg.c ?? '#000000'),
      amt: bg.amt ?? 1, speed: bg.speed ?? 1, pulse: bg.pulse ?? 0, seed: bg.seed ?? 0, mirror: bg.mirror ? 1 : 0,
      bz: fx.bz ?? 1, boff: fx.boff ?? [0, 0] }, { layer: layerTex });
    pass(P.lens, F.lens, { ...common, zoom: fx.zoom, rotA: fx.rot, barrel: fx.barrel, ca: fx.ca, split: fx.split, glitch: fx.glitch, gseed: fx.gseed,
      wave: fx.wave, invert: fx.invert, sq: fx.sq, xerox: fx.xerox, gradeMix: fx.gradeMix, tear: fx.tear, shift: fx.shift, grade: GRADES[fx.grade] ?? 0 }, { src: F.scene.t });
    pass(P.bright, F.b1, { ...common, thr: fx.bloomThr }, { src: F.lens.t });
    for (let k = 0; k < 2; k++) {
      pass(P.blur, F.b2, { ...common, dir: [1.6 / F.b1.w, 0] }, { src: F.b1.t });
      pass(P.blur, F.b1, { ...common, dir: [0, 1.6 / F.b1.h] }, { src: F.b2.t });
    }
    if (first) { gl.bindFramebuffer(gl.FRAMEBUFFER, F.acc.f); gl.clearColor(0, 0, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT); }
    gl.enable(gl.BLEND); gl.blendFunc(gl.ONE, gl.ONE);
    pass(P.final, F.acc, { ...common, bloom: fx.bloom, flash: fx.flash, flashCol: hex(fx.flashCol), dark: fx.dark, grain: fx.grain, scan: fx.scan,
      vig: fx.vig, lb: fx.lb * 140 / H, weight }, { src: F.lens.t, bloomTex: F.b1.t });
    gl.disable(gl.BLEND);
  }
  function present() { pass(P.copy, null, { res: [W, H], time: 0 }, { src: F.acc.t }); return canvas; }
  return { frame, present, canvas, float: FLOAT };
})();
