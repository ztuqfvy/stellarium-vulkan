/*
 * DisplayProbe — T38-A **显示参数命令面探针**（STELQUICK_DISPLAY_PROBE=1）。
 *
 * ── 为什么第一相位是探针（T24/T33/T34/T35/T37 同款纪律）──────────────────────
 * A-1.0 范围表把「亮度/星等、视场、投影」列为**必须（基础级）**，T37 只做掉了同一
 * 单元格里的"主题/夜视"。这三项在合流形态**从未验证过**，开工前的代码审计给出：
 *   · **亮度/星等** → `StelSkyDrawer` 的 Q_PROPERTY 群：`relativeStarScale` /
 *     `absoluteStarScale` / `lightPollutionLuminance` / `customStarMagLimit` /
 *     `flagStarMagnitudeLimit`，**全部带 NOTIFY**（⇒ 可直接给 QML 建依赖）。
 *     ⚠️ 但 setter **一个都不夹取**（`setRelativeStarScale` 直接赋值），且每次都
 *     `StelApp::immediateSave(...)` ⇒ **范围闸必须由产品侧承担**（T33 同款教训）。
 *   · **星等限制的步进** → `actionShow_Stars_MagnitudeLimitIncrease/Reduce`
 *     （StarMgr:416/417），内部是 `customStarMagnitudeLimit ± 0.1`。
 *   · **视场** → `StelMovementMgr::currentFov`（Q_PROPERTY，get/set + NOTIFY）+
 *     `getMinFov/getMaxFov/getUserMaxFov`；⚠️ `setFov()` 里有 `qBound(min,max)` 夹取，
 *     而 AppFacade 现有 `setFieldOfView()` 走的是 **`zoomTo()` 动画**（T28 拍的）——
 *     滑块要的是"立即落定"，两条路语义不同，探针要量清楚。
 *   · **投影** → `StelCore::currentProjectionTypeKey`（Q_PROPERTY）+ `getAllProjectionTypeKeys()`
 *     （枚举名，12 个：Perspective/Stereographic/Fisheye/Orthographic/EqualArea/Hammer/
 *     Mollweide/Sinusoidal/Mercator/Miller/Cylinder/CylinderFill）+ `getCurrentProjectionNameI18n()`。
 *     ⚠️ 审计发现 `setCurrentProjectionTypeKey()` 对**非法 key 不报错**，而是
 *     `qWarning` 后**静默改成 Stereographic** ⇒ 产品侧必须自己闸门（否则用户一个错字
 *     就把投影换掉，还会立刻 `immediateSave("projection/type")` 落盘）。
 * ⇒ 先证"命令面可用 + 写读往返 + 夹取语义"，再写页面。探针不合格就不写 UI。
 *
 * ── 探针不是判据（刻意）────────────────────────────────────────────────────
 * 只报读数、不下 PASS/FAIL（断言在 T38-C 的 DisplayCheck）。
 *
 * ── ⚠️ T38-A 实测结论（2026-09-30，**产品落点与判据设计的依据**）─────────────
 *   ① **StelSkyDrawer 的四个数值属性一个都不夹取**（写 4/3/5/12.5 全部原样回读）
 *      ⇒ 范围闸必须由**产品侧**承担（T33 同款；引擎还会顺手 `immediateSave` 落盘）。
 *   ② **视场有夹取，且上限是投影的函数**：`setFov(-5)`→min、`setFov(1000)`→max；
 *      而 `maxFov` 实测 `Perspective=120 / Stereographic=235 / Fisheye=360 /
 *      EqualArea=360 / Mercator=270 / 其余 180~185` ⇒ **滑块上限必须跟着投影走**，
 *      否则用户拖到 300 会被静默夹回 235（"拖了没反应"的另一种形态）。
 *   ③ **12 个投影 key 全部 `set → get` 往返一致**，中文名齐全；但
 *      **非法 key 不报错**：`setCurrentProjectionTypeKey("Nonsense_Key_T38")` 实测
 *      静默落到 `ProjectionStereographic`（还会 `immediateSave("projection/type")`
 *      落盘）⇒ 产品侧必须**白名单闸门**。
 *   ④ 🔴 **`getLimitMagnitude()` 不是"用户设定的星等限制"** —— 它是引擎按
 *      大气/光污染/瞳孔适应**算出来的有效限制**（实测白天 = **-4.44**，与
 *      `customStarMagLimit=12.5` 无关）。用户设定的真值在
 *      `getCustomStarMagnitudeLimit()` + `getFlagStarMagnitudeLimit()`；
 *      真实生效点在 `ZoneArray.cpp:449`（**星表遍历的人工截断**）。
 *      ⇒ 判据若拿 `getLimitMagnitude()` 当"星等生效"的读数，就会得出**错误结论**
 *      （这正是 T38 要躲的那类假绿：**读错了量的名字**）。
 *   ⑤ **星等限制的视觉验证必须先让星星可见**：`flag/custom` 只控制"星表截断"，
 *      而白昼基帧里**本来就没有星星**（T37 血泪：大气在 + 白昼亮度模型压着）
 *      ⇒ T38-C 的星等判据必须在 **`actionShow_Atmosphere=false`** 的布场下测，
 *      否则整条判据零判别力（T37 假绿的同族）。
 *   ⑥ **静置期 NOTIFY 全部为 0**（1.5s 内 rel/fov/proj 增量 0/0/0）⇒ 这些量
 *      **不是每帧变的连续量**，滑块**可以安全绑定**（不触发 T15 的重算风暴）。
 *   ⑦ 星等步进动作 `actionShow_Stars_MagnitudeLimitIncrease/Reduce` 已注册
 *      （中文 "增加/降低恒星极限星等"），trigger 后 `customStarMagLimit` **±0.1**，
 *      走**模块 getter 独立回读**验证（陷阱 43）。
 *
 * ── 探针要回答的问题 ─────────────────────────────────────────────────────
 *   Q1 **亮度/星等属性的写读往返**：五项逐项（名 / 类型 / 初值 / 写入值 / 回读值 /
 *      是否与写入一致 / **setter 有没有夹取**）。写入值刻意取一个"远"值以暴露夹取。
 *   Q2 **星等限制步进动作**：两个 action 是否注册；trigger 后 `customStarMagLimit`
 *      是否真的 ±0.1（**模块 getter 独立回读**，陷阱 43）。
 *   Q3 **视场**：`currentFov / minFov / maxFov / userMaxFov / aimFov` 五个读数；
 *      `setFov(60)` 回读；`setFov(-5)` 与 `setFov(1000)` 的**夹取落点**。
 *   Q4 **投影**：12 个 key 清单 + 逐 key `setCurrentProjectionTypeKey` → 回读一致
 *      + 该 key 的中文名；**非法 key 的落点**（审计说会静默变 Stereographic，
 *      必须实测坐实）；`maxFov` 是否随投影变化（`updateMaximumFov()`）。
 *   Q5 **NOTIFY 活跃性**：这一轮里各属性 NOTIFY 信号各发射几次（决定 QML 能否建依赖）。
 *   收尾**全部还原**（值 + 投影 + 视场）。
 *
 * ── 退出码（由 main.cpp 决定）──────────────────────────────────────────────
 *   0=DONE（正常报完读数），6=UNAVAILABLE（非合流形态/引擎未引导）。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <functional>

class QCoreApplication;

namespace stelapp {

class AppFacade;

class DisplayProbe
{
public:
    struct Result
    {
        bool ran = false;          //!< 真正跑过（前置满足）
        bool unavailable = false;  //!< 环境里跑不起来（非合流形态/引擎未引导）
        QString summary;
        QStringList details;       //!< 逐行读数（前缀已在 main.cpp 侧统一加）
    };

    //! 全部在 GUI 线程。@p delayMs 后开跑（等引擎动作注册落定）。结果经 onDone 回传。
    static void run(QCoreApplication *app,
                    AppFacade *facade,
                    const std::function<void(const Result &)> &onDone,
                    int delayMs = 2500);
};

} // namespace stelapp
