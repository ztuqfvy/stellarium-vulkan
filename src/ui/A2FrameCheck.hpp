/*
 * A2FrameCheck — 静态图管线的自动像素校验（用例 I-STC-01 / I-STC-02）。
 *
 * 为什么要有它：A2 的验收写的是"方向/颜色/DPR 正确"。这四类错误（垂直翻转、R/B 通道互换、
 * DPR 少乘一次、行步长错位）都能显示出一张"看起来没问题"的图，肉眼验收必然漏。
 * 因此用 grabWindow() 抓回设备像素，按 TestPattern 的探针逐点比对。
 *
 * 坐标系换算（这里最容易错，也最容易被忽略）：
 *   纹理坐标是**物理像素**；QML 项坐标是**逻辑像素**。
 *   物理点 P → 逻辑点 P/dpr → 场景点 scenePos + P/dpr → 抓帧图上的设备点 × dpr。
 *   因为纹理是按 logical×dpr 生成的，设备点与纹理点应当 1:1 对应。
 *
 * 容差：纯色探针允许 ±8（8 位取整 + 可能的色彩空间往返），
 * 仍足以抓出上述四类错误（错一个通道/翻转一格都是 255 量级差异）。
 */
#pragma once

#include <QSize>
#include <QString>
#include <QStringList>
#include <functional>

class QGuiApplication;
class QQuickItem;
class QQuickWindow;

namespace stelapp {

class FrameMailbox;

struct A2CheckResult
{
    bool checkRan = false;      // 校验手段是否可用（grabWindow 是否成功）
    bool pass = false;          // 全部探针是否通过
    int probeCount = 0;
    int failCount = 0;
    QString summary;
    QStringList details;
};

class A2FrameCheck
{
public:
    static constexpr int kColorTolerance = 8;
    static constexpr int kAlphaTolerance = 60;

    // 抓取窗口并与 TestPattern 的探针对比。physicalSize 必须与投递帧的物理尺寸一致。
    static A2CheckResult run(QQuickWindow *window, QQuickItem *viewport, const QSize &physicalSize);

    // 启动期完整流程：等布局稳定 → 投递测试图案 → 等场景图确认上传 → 抓帧比对 → 回调。
    // 全程在 GUI 线程用定时器推进，不阻塞事件循环。
    static void runStartupSequence(QGuiApplication *app,
                                   QQuickWindow *window,
                                   QQuickItem *viewport,
                                   FrameMailbox *mailbox,
                                   const std::function<void(const A2CheckResult &)> &done);
};

} // namespace stelapp
