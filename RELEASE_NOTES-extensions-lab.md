# 小窗浏览器 v1.4.6-extensions-lab · 长期实验版

**预发布（Pre-release），不会替换正式版 v1.4.5，也不会加入最新正式版下载入口。**

本轮增加 WebView2 的 Manifest V3 解压扩展管理基础功能：

- 单独实验版 EXE `XiaoChuang-ExtensionsLab.exe`，普通用户权限运行；与正式版的配置、浏览器缓存、Cookie 和单实例标识隔离。
- 地址栏右侧 **🧩 拼图** 一级入口打开原生扩展管理窗口；显示扩展名称与状态，每行复选开关和“删除”，并支持安装本地扩展、刷新列表。
- 从包含 `manifest.json` 的本地解压目录导入 MV3 扩展，经过用户确认后保存到实验版自有目录，并通过 WebView2 `AddBrowserExtension` 安装。
- 通过官方 Profile API 启用、禁用、卸载扩展；扩展状态不会被场景预设修改。
- 外部 `扩展管理.cmd` 可开启单独的 Windows 原生扩展管理窗口，**无须先操作主浏览器窗口**。它仍需要 WebView2 Runtime 与实验 Profile 正常工作；不能保证处理所有运行时损坏情况。
- 实验版版本资源、文件名与说明均和正式版区分；保留三个空白提醒 TXT 及必要许可证。

## 使用提示

1. **先退出正式版以外的旧实验进程**，解压实验版 ZIP 到单独文件夹，运行 `XiaoChuang-ExtensionsLab.exe`。
2. 在地址栏右侧点击拼图图标 → 安装本地扩展 → 选择包含 `manifest.json` 的文件夹。
3. 通过列表里勾选框启停，或者使用“删除”移除。遇到主窗口无法操作，双击 `扩展管理.cmd` 打开独立管理窗口；完成后再打开小窗验证。
4. **不要在实验版里安装来源不明的扩展**，它们可能读取已授权的网站信息或损害兼容性。登录/财务/工作敏感网页暂不建议用来测试陌生扩展。

## 已知限制与测试边界

- GitHub Windows x64 编译及核心测试主要验证代码/打包，**不等于 Windows 桌面扩展实际 GUI 测试**。用户将帮助确认 WebView2 Runtime、扩展安装、勾选状态及重启持久化。
- 对扩展支持采用 Chromium MV3 本地解压目录方式；没有商店一键安装、CRX/ZIP 导入、自动更新和完整扩展权限详情界面。
- 部分依赖 Chrome 扩展 toolbar/popup/action 的扩展无法完整使用，这是 WebView2 宿主限制；不能承诺任何 Chrome 扩展都兼容。
- 实验版与正式版分离；如果扩展使 Runtime 本身崩溃，外部原生管理窗口可能仍无法完成 Profile 操作。请记录日志和具体扩展名称反馈。
- 卸载通过 WebView2 Profile API 持久生效；程序会尽量清理自己保存的扩展目录，未关联或被占用的源目录可能需要后续维护。

详细说明见安装包 `README-扩展实验版使用说明.md`。
问题反馈：https://github.com/azurplain/Mini-Window-Browser/issues
