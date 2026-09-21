// LegacyTestScene 实现。设计理由见头文件注释。

#include "render/legacy/LegacyTestScene.hpp"

#include <QDebug>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>

namespace stelapp {

namespace {

// 顶点着色器：全屏四边形（三角形条带）。只传位置，不含任何随帧变化的数据——
// 变化全部发生在片元着色器里，这样"帧内容变化"只能来自 uniform（仿真时间），
// 不会因为顶点缓冲被误改而伪装成"内容在变"。
//
// 注意：着色器源码**必须保持纯 ASCII**。Apple 的 GLSL 编译器对源文件里的非 ASCII
// 字节不保证接受（注释也一样）。中文说明一律写在这里，不写进 GLSL。
const char *kVertexShader = R"(#version 330 core
layout(location = 0) in vec2 a_pos;
out vec2 v_screen;
void main()
{
    v_screen = a_pos;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

// 片元着色器：三部分叠加
//   (a) 竖直渐变天空 + 昼夜色温 —— 随 u_simSeconds 变化
//   (b) 网格 hash 星点（亮度压到 0.6）—— 静止参照；压低是为了不污染太阳质心
//   (c) 太阳圆盘 —— 水平位置随 u_simSeconds 单调推进
//   另有两条固定方向标记带（顶部绿、左侧红），供外部独立验证行序翻转与水平方向。
const char *kFragmentShader = R"(#version 330 core
in vec2 v_screen;
uniform float u_simSeconds;
uniform vec2  u_resolution;
out vec4 fragColor;

// Deterministic hash: same cell -> same value, so re-rendering the same
// u_simSeconds is guaranteed to reproduce identical pixels.
float hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main()
{
    vec2 frag = gl_FragCoord.xy;                  // GL origin is bottom-left
    float t = fract(u_simSeconds / 24.0);         // 0..1; one day-night cycle per 24 sim seconds
    float sunHeight = sin(t * 6.28318530718);     // -1..1
    float daylight = clamp(sunHeight * 1.5 + 0.5, 0.0, 1.0);

    vec3 nightBottom = vec3(0.02, 0.03, 0.08);
    vec3 nightTop    = vec3(0.00, 0.00, 0.03);
    vec3 dayBottom   = vec3(0.55, 0.72, 0.92);
    vec3 dayTop      = vec3(0.10, 0.30, 0.65);
    float v = frag.y / u_resolution.y;            // 0 = bottom, 1 = top
    vec3 sky = mix(mix(nightBottom, dayBottom, daylight),
                   mix(nightTop,    dayTop,    daylight), v);

    // Stars: grid hash, dimmed to 0.6 so they never compete with the sun
    // for the "brightest 10%" centroid used by the external checker.
    vec2 grid = floor(frag / 16.0);
    float h = hash21(grid);
    float starOn = step(0.985, h) * (1.0 - daylight);
    vec2 center = (grid + vec2(0.25 + 0.5 * fract(h * 7.0),
                               0.25 + 0.5 * fract(h * 13.0))) * 16.0;
    float starMask = smoothstep(2.0, 0.0, length(frag - center)) * starOn;

    // Sun: horizontal position advances monotonically with sim time,
    // vertical follows a sine arc.
    float sunX = fract(t) * 1.2 - 0.1;
    float sunY = 0.5 - sunHeight * 0.45;
    vec2 sunPos = vec2(sunX, sunY) * u_resolution;
    float sunDisk = smoothstep(26.0, 16.0, length(frag - sunPos));

    vec3 color = sky + vec3(starMask * 0.6) + sunDisk * vec3(1.0, 0.93, 0.65);

    // Fixed orientation marker bands (used to verify the readback row flip
    // and horizontal orientation independently of the scene content):
    //   top 24 rows -> green, left 24 columns -> red
    float topBand  = step(u_resolution.y - 24.0, frag.y);
    float leftBand = 1.0 - step(24.0, frag.x);
    color = mix(color, vec3(0.0, 1.0, 0.0), topBand);
    color = mix(color, vec3(1.0, 0.0, 0.0), leftBand);

    fragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
)";

// 全屏四边形（三角形条带，4 顶点）
const GLfloat kQuad[] = {
    -1.f, -1.f,
     1.f, -1.f,
    -1.f,  1.f,
     1.f,  1.f,
};

} // namespace

namespace {
// 着色器源码纯 ASCII 自检。
// 这不是洁癖：Apple 的 GLSL 编译器对源串里的非 ASCII 字节不保证接受，
// 而本项目吃过同类亏（.cmd 里的中文注释被 cmd.exe 按 GBK 误解码后当命令执行）。
// 结论一样：**交给外部工具解析的文本，一律只放 ASCII**，中文说明放在 C++ 注释里。
bool isAsciiOnly(const char *s)
{
    if (!s)
        return false;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s); *p; ++p)
        if (*p >= 0x80)
            return false;
    return true;
}
} // namespace

struct LegacyTestScene::Private
{
    bool tried = false;
    bool ok = false;
    QString error;

    QOpenGLShaderProgram *program = nullptr;
    QOpenGLBuffer *vbo = nullptr;
    QOpenGLVertexArrayObject *vao = nullptr;
    int locationResolution = -1;
    int locationSimSeconds = -1;

    void fail(const QString &msg)
    {
        ok = false;
        error = msg;
    }
};

LegacyTestScene::LegacyTestScene() : d(new Private) {}

LegacyTestScene::~LegacyTestScene()
{
    // 注意：此处**不**释放 GL 资源——析构时上下文多半已不 current。
    // 调用方必须先 withContextCurrent() 调 releaseResources()。若忘了，
    // 这里只留下日志，不制造"在错误上下文里删除对象"的崩溃。
    if (d->program || d->vbo || d->vao)
        qWarning("LegacyTestScene 析构时仍有未释放的 GL 资源："
                 "调用方应先 releaseResources()（上下文 current 时）。");
    delete d->program;
    delete d->vbo;
    delete d->vao;
}

bool LegacyTestScene::ok() const { return d->ok; }

QString LegacyTestScene::lastError() const { return d->error; }

void LegacyTestScene::releaseResources()
{
    delete d->program; d->program = nullptr;
    delete d->vbo;     d->vbo = nullptr;
    delete d->vao;     d->vao = nullptr;
    d->tried = false;
    d->ok = false;
}

void LegacyTestScene::render(double simSeconds, const QSize &physicalSize)
{
    if (!d->tried)
    {
        d->tried = true;
        if (!isAsciiOnly(kVertexShader) || !isAsciiOnly(kFragmentShader))
        {
            d->fail(QStringLiteral("着色器源码含非 ASCII 字节——GLSL 编译器不保证接受，"
                                   "中文说明必须留在 C++ 注释里。"));
            return;
        }
        // 惰性装配：此处上下文必已 current（契约由 LegacySkyHost 保证）
        d->program = new QOpenGLShaderProgram();
        if (!d->program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader))
        {
            d->fail(QStringLiteral("顶点着色器编译失败：%1").arg(d->program->log()));
            return;
        }
        if (!d->program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader))
        {
            d->fail(QStringLiteral("片元着色器编译失败：%1").arg(d->program->log()));
            return;
        }
        if (!d->program->link())
        {
            d->fail(QStringLiteral("着色器链接失败：%1").arg(d->program->log()));
            return;
        }
        d->locationResolution = d->program->uniformLocation("u_resolution");
        d->locationSimSeconds = d->program->uniformLocation("u_simSeconds");

        d->vbo = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
        if (!d->vbo->create())
        {
            d->fail(QStringLiteral("顶点缓冲创建失败。"));
            return;
        }
        d->vbo->bind();
        d->vbo->setUsagePattern(QOpenGLBuffer::StaticDraw);
        d->vbo->allocate(kQuad, sizeof(kQuad));
        d->vbo->release();

        // Core Profile 下没有绑定 VAO 时所有 draw 调用都是空操作（**静默黑屏**的经典来源），
        // 因此这里必须真的建一个并在每帧绑定。
        d->vao = new QOpenGLVertexArrayObject();
        if (!d->vao->create())
        {
            d->fail(QStringLiteral("VAO 创建失败（Core Profile 下必须有 VAO）。"));
            return;
        }
        d->vao->bind();
        d->vbo->bind();
        d->program->bind();
        d->program->enableAttributeArray(0);
        d->program->setAttributeBuffer(0, GL_FLOAT, 0, 2, 2 * sizeof(GLfloat));
        d->program->release();
        d->vbo->release();
        d->vao->release();

        d->ok = true;
    }

    if (!d->ok)
        return;

    // 每帧显式绑定：借用模式（A3 与旧宿主同进程）下外部代码可能改过绑定状态。
    d->vao->bind();
    d->program->bind();
    d->program->setUniformValue(d->locationResolution,
                                float(physicalSize.width()), float(physicalSize.height()));
    d->program->setUniformValue(d->locationSimSeconds, float(simSeconds));
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    f->glDisable(GL_DEPTH_TEST);
    f->glDisable(GL_BLEND);
    f->glDisable(GL_CULL_FACE);
    f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    d->program->release();
    d->vao->release();
}

} // namespace stelapp
