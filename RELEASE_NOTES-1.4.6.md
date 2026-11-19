# 小窗浏览器 v1.4.6（正式版）

本次是基于 v1.4.5 的小型体验修复版，感谢社区贡献者 [Floatingcloud01](https://github.com/Floatingcloud01) 提交的 [PR #27](https://github.com/azurplain/Mini-Window-Browser/pull/27)。

## 本次修复

- **地址栏支持 Ctrl+A 全选**：修复 Windows 原生单行地址栏无法使用 `Ctrl+A` 选中全部网址的问题。
- **避免提示音**：对 Ctrl+A 的键盘消息及转译的控制字符进行处理，全选后不再向原生编辑框传递该控制字符，从而避免系统蜂鸣提示音。
- **保持兼容**：未修改 WebView2 引擎、页面导航、鼠标穿透、快捷键绑定、窗口置顶、场景预设、日志或旧配置存储格式。v1.4.5 已有功能全部保留。

## 下载与升级

1. 下载 `Mini-Window-Browser-v1.4.6-x64.zip`，完整解压到可写位置。
2. 更新前关闭正在运行的小窗。若已有个人配置、书签和登录状态，**请先备份原来的 `config.ini` 及 WebView2 用户数据目录**，升级时不要用空目录覆盖这些数据。
3. 解压后运行 `XiaoChuang.exe`，Windows UAC 权限请求与此前正式版相同。缺少 WebView2 Runtime 时请从微软官方渠道安装。

安装包继续只提供程序、`WebView2Loader.dll`、用户使用说明、三个空白快捷提示 TXT 和必要许可证，不包含历史开发测试资料或用户隐私数据。

## 验证与限制

- Windows x64 Release 构建、项目核心自动化测试及安装包内容检查由 GitHub Actions 执行。
- 自动化核心测试通过并不等于已在各版本 Windows 或每种输入法中进行了人工地址栏快捷键实测。
- 本版本仍沿用共享单个 WebView2 网页控件的设计；偶发 GPU 合成白屏无法保证被探测器识别，可通过标签栏右键重建网页视图。请在检查隐私后提交相关日志和复现步骤。

保留旧正式版 v1.4.5，不覆盖历史 Release 或其安装包。

项目主页：https://github.com/azurplain/Mini-Window-Browser
