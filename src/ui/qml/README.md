QML 页面组件目录（A1 起填充）。

硬性禁区（docs/CODING_STANDARD.zh_CN.md 第 5 节）：
- 本目录内禁止出现 GL/Vulkan 特定调用（CI grep 门禁）。
- 禁止直接持有旧引擎裸指针或遍历模块单例；一律经 AppFacade/ActionRouter/模型访问。
