// nightmode.frag — T37 夜视滤镜（Qt Quick 侧）。
//
// 为什么在 Qt Quick 侧、公式从哪来：见 MainWindow.qml keySink.layer.effect 的
// 完整注释（T37-A 探针定论 —— 引擎旧夜视后处理在合流形态物理不可达）。
// 公式逐字复刻引擎 NightModeGraphicsEffect（src/StelMainView.cpp:186-194）：
//   lum = max(max(r, g), b);  out = (lum, lum * 0.3, 0)
// 红光暗视：只保留亮度线索、把全部色彩压进红/暗红 —— 保护暗适应。
//
// ── 为什么必须是 .qsb 文件（血泪）──────────────────────────────────────────
// Qt 6.11 的 ShaderEffect **不再接受内联 GLSL 字符串**：fragmentShader 现在是
// URL、必须指向 qsb 预编译产物。实测把 GLSL 源码直接赋给它，运行期报：
//   "Failed to deserialize QShader ... In Qt 6 shaders must be preprocessed
//    using the Qt Shader Tools infrastructure."
// 且后果不是"效果不生效"而是**整个 layer 不渲染** —— keySink 全部内容消失、
// 露出窗口底色（全白屏）。构建侧由 qt6_add_shaders 自动调用 qsb（CMakeLists）。
//
// ── 接口约定（Qt 内置 vertex shader 的布局，改一个字段都会黑屏/不渲染）──────
//   uniform block (std140, binding 0)：qt_Matrix (mat4) + qt_Opacity (float)
//   sampler (binding 1)：source —— layer.effect 位置上由 Qt 自动绑定 layer 纹理
//   输入 (location 0)：qt_TexCoord0
#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};

layout(binding = 1) uniform sampler2D source;

void main()
{
    vec3 c = texture(source, qt_TexCoord0).rgb;
    float lum = max(max(c.r, c.g), c.b);
    fragColor = vec4(lum, lum * 0.3, 0.0, 1.0) * qt_Opacity;
}
