# 编码规范（QML/Vulkan 重写层，定稿 2026-09-17）

适用范围：`src/app/`、`src/ui/`、`src/render/legacy/`、`tests/ui/`、`tests/integration/` 中所有**新写**代码。现有 Stellarium 源码维持上游风格，不做格式化清洗（避免污染 diff）。

## 1. 标识符语言：英文标识符 + 中文文档注释（定稿）

**不使用中文标识符。** 理由：

1. 新层包装的是 Stellarium 全英文 API（`StelCore`、`StelObjectMgr`…），中英混排比统一英文更难读。
2. 鸿蒙工具链、qmlcachegen/qmllint、CI 对非 ASCII 标识符支持参差；本项目 A1 就要探测鸿蒙，不为标识符风格引入未知变量。
3. 接口冻结与对上游 diff、补丁整理成本最低。

（千籁至音项目的中文标识符规范是独立项目约定，不迁移到本项目。）

## 2. 命名表

| 对象 | 规则 | 示例 |
|---|---|---|
| 类型/类 | PascalCase | `AppFacade`、`FrameMailbox` |
| 函数 | 小驼峰，动词开头 | `selectObject()`、`takeLatestFrame()` |
| 成员变量 | `m_` 前缀小驼峰 | `m_frameGeneration` |
| 常量/枚举值 | `k` 前缀小驼峰 | `kMaxQueuedFrames`、`FrameState::kComplete` |
| 信号 | 小驼峰，状态名/过去式 | `skyReady()`、`backendFailed(QString)` |
| QML 属性 | 小驼峰 | `viewport.viewportGeneration` |
| 文件名 | 与主类名一致，`.hpp/.cpp` | `FrameMailbox.hpp` |
| 目录 | 全小写 | `src/render/legacy/` |

## 3. 中文注释规则

- 每个公开类：`/** */` 写职责 + 禁止事项（对应开发计划一第 5 节表格）。
- 每个公开函数：一句话职责 + **线程归属**（GUI/仿真线程、场景图线程、GL 上下文线程）+ 参数/返回值语义。单位必须写明（JD 为 UT；时间速率单位是 Julian day/second）。
- 与旧代码的对应必须标注，如 `@see StelCore::setJD`。
- 禁止事项用"禁止："开头，评审逐条对表。

## 4. 线程标注约定

头文件中用注释标签声明线程契约（编译器不检查，靠评审 + 测试文档 L1/L3 用例兜底）：

```cpp
// THREAD: gui        — 只能在 GUI/仿真线程调用
// THREAD: scenegraph — 只能在 Qt Quick 渲染线程调用
// THREAD: gl-ctx     — 只能在旧 GL 上下文所属线程调用
// THREAD: any        — 线程无关（数据按不可变快照传递）
```

## 5. 硬性禁区（违反即打回）

- `src/app/` 头文件出现任何 GL/Vulkan 类型或句柄。
- `src/ui/qml/` 出现 GL/Vulkan 特定调用（CI grep 门禁）。
- QML 直接持有旧引擎裸指针或遍历模块单例。
- 渲染线程访问可变 Stellarium 模块。
- 生产者覆盖消费者正在读取的帧槽位。
- 通过符号链接目录修改原项目资源。
