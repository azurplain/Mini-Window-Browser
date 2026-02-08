# Copilot Instructions

## General Guidelines
- First general instruction
- Second general instruction

## Code Style
- Use specific formatting rules
- Follow naming conventions

## Project-Specific Rules
- 保留核心机制：
  - 沉浸模式通过移动窗口坐标 (SetWindowPos)，禁止缩放网页或改变 WebView 视口。
  - 多标签页采用 TCS_OWNERDRAWFIXED 自绘，标签右侧 20px 为关闭区域，标题过长截断并在末尾拼接“ ×”。
  - 智能输入检测通过 GetCursorInfo 检查 IDC_IBEAM 或焦点在地址栏，输入时屏蔽全局热键。
  - 老板键（默认 '9'）隐藏窗口时根据 g_autoPauseOnHide 决定是否执行 JS 暂停视频。