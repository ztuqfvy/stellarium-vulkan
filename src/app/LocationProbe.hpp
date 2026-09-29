/*
 * LocationProbe — T33-A 地点数据面**探针**（STELQUICK_LOC_PROBE=1）。
 *
 * ── 为什么第一相位是探针，不是 UI ─────────────────────────────────────────────
 * T24 的血泪：合流 bundle 布局下 **翻译从未加载**（`getLocaleDir` 三候选全不命中），
 * 潜伏了 14 个任务才被逼出来 —— 因为在它之前**没有任何判据依赖翻译名**。
 * 地点库是**同款风险**：它依赖 `data/base_locations.bin.gz`，而 `StelFileMgr`
 * 的 installDir 走 `STELLARIUM_DATA_ROOT`（默认 `"."`）⇒ **依赖启动时的 cwd**
 * （macOS 实测日志：`StelFileMgr found install location "."`）。
 * ⇒ 先证"数据面可用"，再写 UI。探针不合格就不写 UI。
 *
 * ── 探针不是判据（刻意）────────────────────────────────────────────────────
 * 探针**只报读数、不下 PASS/FAIL**。它是"查事实"，不是"立断言"。
 * 断言（成对 + 判别对照 + 非法值无副作用）在 T33-C 的 LocationCheck 里。
 * 把两者混在一起会让"数据面能不能用"这种**环境事实**污染判据的通过率。
 *
 * ── 探针要回答的四个问题 ───────────────────────────────────────────────────
 *   P1 环境：cwd / installDir / `data/base_locations.bin.gz` 能否被 findFile 命中
 *            （另查 CHECK_FILE `data/ssystem_major.ini`，它是 installDir 的判据文件）
 *   P2 规模：`getAll().size()`（地点条数）/ 区域数 / 时区名数 —— **非空才有 UI 可言**
 *   P3 查询：`locationForString("Beijing")` 之类能否解析出合格地点
 *   P4 写入与联动：`moveObserverTo(loc, **0.0**)`（瞬时分支）后回读
 *            —— 经纬度/名称/iana **真的变了**吗？
 *            —— ⚠️ 时区**联动**（`setObserver` 里 `setCurrentTimeZone(iana)`，
 *               `StelCore.cpp:1546`）真的落了吗？还是撞上 T22 那个静默失败
 *               （`setCurrentTimeZone` 只在 `getAllTimezoneNames()` 名单内才设，
 *                否则 **只 qWarning 不设置**，`StelCore.cpp:1715-1725`）？
 *
 * ⚠️ 为什么必须 `duration=0`：`moveObserverTo` 的 `duration>0` 走
 *    `SpaceShipObserver`（**飞行动画、跨帧异步**），`=0` 才走 `StelObserver` 瞬时
 *    （`StelCore.cpp:1550-1566`）。探针要的是"写下去就能读到"，不是"等它飞完"。
 *
 * ── 已知的引擎侧缺口（探针会**照实打印**，不掩盖）────────────────────────────
 * `StelLocation::isValid()`（`StelLocation.cpp:296`）**不校验纬度范围**：
 * 只查 `role=='!'` 与"经纬不同时为 0" ⇒ **lat=91 会被判合法**。
 * ⇒ "非法纬度必须被拒"这件事在应用层没有可依赖的引擎基础，得自己做。
 * 探针把这条的**实测读数**打出来，作为 T33-B 写入面设计的依据。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <functional>

class QCoreApplication;

namespace stelapp {

class AppFacade;

class LocationProbe
{
public:
    struct Result
    {
        bool ran = false;          //!< 真正跑过（前置满足）
        bool unavailable = false;  //!< 环境里跑不起来（合流形态缺失/引擎引导失败）
        QString summary;
        QStringList details;       //!< 逐行读数（前缀已在 main.cpp 侧统一加）
    };

    //! 全部在 GUI 线程。@p delayMs 后开跑（等引擎数据落定）。结果经 onDone 回传。
    static void run(QCoreApplication *app,
                    AppFacade *facade,
                    const std::function<void(const Result &)> &onDone,
                    int delayMs = 2500);
};

} // namespace stelapp
