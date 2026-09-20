/*
 * TestPattern — A2 静态图阶段的测试图案（含独立探针）。
 *
 * 为什么需要它：I-STC-01 要求"逐像素比对方向、颜色通道顺序、透明度、DPR 缩放"。
 * 靠肉眼看"图上来了"不能发现垂直翻转、R/B 通道互换、DPR 少乘一次、行步长错位这四类错误——
 * 它们都能正常显示出一张"看起来是图"的图。因此这里把图案做成**可判定的**：
 *   - 四角色标：TL 红 / TR 绿 / BL 蓝 / BR 黄 —— 一次抓出翻转 + 旋转 + 通道互换
 *   - 中部色条：R / G / B / 白 —— 抓出通道顺序（如 BGRA 误当 RGBA）
 *   - 左下棋盘：#202020 与 #e0e0e0 相邻两格 —— 抓出缩放/DPR 错位（棋盘错位立刻可见）
 *   - 右侧半透明块：α=128 白 —— 抓出 alpha 被忽略（误判为不透明）
 *   - 中心十字：定位用，同时验证中心不偏移
 *
 * 坐标约定：所有几何常量与探针坐标都是**物理像素**（纹理像素），原点左上，
 * 与 LegacyFrame 的格式契约一致（RGBA8、行优先、非预乘 alpha）。
 * 传入的 physicalSize 必须是"逻辑尺寸 × DPR"，这样纹理与设备像素 1:1，
 * 比对无需插值容差。
 */
#pragma once

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QSize>
#include <QVector>

namespace stelapp {

class TestPattern
{
public:
    // 几何常量（物理像素）
    static constexpr int kMarkerSize = 28;    // 角标边长
    static constexpr int kMarkerInset = 10;   // 角标距图边
    static constexpr int kBarHeight = 26;     // 色条高度
    static constexpr int kCellSize = 40;      // 棋盘格边长
    static constexpr int kAlphaBlock = 72;    // 半透明块边长

    // 角序：0=左上 1=右上 2=左下 3=右下
    static QColor markerColor(int corner);
    static QString markerName(int corner);

    // 色条序：0=R 1=G 2=B 3=白
    static QColor barColor(int index);

    static QColor backgroundColor(); // #303030，不透明

    // 生成图案。RGBA8、非预乘 alpha、原点左上。
    static QImage render(const QSize &physicalSize);

    // 单点探针：pos 为物理像素坐标，expect 为该点期望颜色（与 render 同源声明）。
    struct Probe
    {
        QPoint pos;
        QColor expect;
        QString name;
        bool exact = true;   // false 时允许容差（用于 alpha 合成点）
    };
    static QVector<Probe> probes(const QSize &physicalSize);

    // 与 render 实现解耦的第二道守卫：四角色标必须仍是这四个颜色。
    // 若有人改了 render 却忘了改探针，这条断言会先炸。
    static bool selfCheck();
};

} // namespace stelapp
