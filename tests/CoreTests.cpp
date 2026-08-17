#include "../src/AppModel.h"
#include "../src/ConfigStore.h"
#include "../src/CoreLogic.h"

#include <windows.h>

#include <filesystem>
#include <iostream>
#include <string>

namespace {

int failures = 0;
int checks = 0;

void Check(bool condition, const wchar_t* name) {
    ++checks;
    if (condition) {
        std::cout << "[PASS " << checks << "]\n";
    } else {
        std::cout << "[FAIL " << checks << "]\n";
        OutputDebugStringW(name);
        ++failures;
    }
}

void TestConfigMigration() {
    wchar_t tempDirectory[MAX_PATH]{};
    GetTempPathW(static_cast<DWORD>(std::size(tempDirectory)), tempDirectory);
    const std::filesystem::path path = std::filesystem::path(tempDirectory) /
        (L"XiaoChuang-CoreTests-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    DeleteFileW(path.c_str());
    WritePrivateProfileStringW(L"Settings", L"HoleRadius", L"-20", path.c_str());
    WritePrivateProfileStringW(L"Settings", L"SnapThreshold", L"999", path.c_str());
    WritePrivateProfileStringW(L"Settings", L"HoldPlaybackRate", L"8", path.c_str());
    WritePrivateProfileStringW(L"Settings", L"HomeUrl", L"https://www.bilibili.com", path.c_str());
    WritePrivateProfileStringW(L"Session", L"TabCount", L"0", path.c_str());

    xiaochuang::AppState state;
    const xiaochuang::ConfigStore store(path);
    Check(store.Load(state), L"旧配置可加载");
    Check(state.settings.holeRadius == 0, L"挖孔半径范围校验");
    Check(state.settings.snapThreshold == 200, L"吸附距离范围校验");
    Check(state.settings.holdPlaybackRate == 5, L"长按倍速范围校验");
    Check(state.settings.holeOpacityPercent == 100 &&
          state.settings.autoHideOpacityPercent == 100, L"新增透明度键默认迁移");
    Check(!state.settings.backgroundMediaHotkeys, L"后台媒体热键默认关闭");
    Check(!state.settings.autoFitVideoFullscreen, L"网页全屏比例适配默认关闭");
    Check(!state.settings.maximizedTopDragEnabled, L"最大化顶部拖动恢复默认关闭");
    Check(state.tabs.size() == 1, L"空旧会话自动生成主页标签");

    RECT rect{20, 30, 920, 630};
    Check(store.Save(state, rect, false), L"新配置可保存");
    xiaochuang::AppState reloaded;
    Check(store.Load(reloaded), L"新配置可重新加载");
    Check(reloaded.settings.holeOpacityPercent == 100, L"新增配置往返一致");
    Check(!reloaded.settings.autoFitVideoFullscreen, L"网页全屏比例适配配置往返一致");
    reloaded.settings.maximizedTopDragEnabled = true;
    Check(store.Save(reloaded, rect, false), L"最大化顶部拖动配置可保存");
    xiaochuang::AppState dragSettingReloaded;
    Check(store.Load(dragSettingReloaded) && dragSettingReloaded.settings.maximizedTopDragEnabled,
          L"最大化顶部拖动配置往返一致");

    xiaochuang::Preset preset;
    preset.name = L"三标签";
    preset.tabs = {{L"哔哩哔哩", L"https://www.bilibili.com"},
                   {L"抖音", L"https://www.douyin.com"},
                   {L"YouTube", L"https://www.youtube.com"}};
    preset.activeTab = 1;
    preset.currentUrl = preset.tabs[1].url;
    reloaded.presets = {preset};
    Check(store.Save(reloaded, rect, false), L"多标签预设可保存");
    xiaochuang::AppState presetReloaded;
    Check(store.Load(presetReloaded), L"多标签预设可加载");
    Check(presetReloaded.presets.size() == 1 && presetReloaded.presets[0].tabs.size() == 3,
          L"预设完整保存全部标签页");
    Check(presetReloaded.presets[0].activeTab == 1 &&
          presetReloaded.presets[0].tabs[1].title == L"抖音",
          L"预设保存当前标签索引和标题");

    WritePrivateProfileSectionW(L"Presets", nullptr, path.c_str());
    WritePrivateProfileStringW(L"Presets", L"Count", L"1", path.c_str());
    WritePrivateProfileStringW(L"Presets", L"P0_Name", L"旧版预设", path.c_str());
    WritePrivateProfileStringW(L"Presets", L"P0_CurUrl", L"https://example.com/legacy", path.c_str());
    xiaochuang::AppState legacyPresetState;
    Check(store.Load(legacyPresetState), L"旧版单网址预设可迁移");
    Check(legacyPresetState.presets.size() == 1 &&
          legacyPresetState.presets[0].tabs.size() == 1 &&
          legacyPresetState.presets[0].tabs[0].url == L"https://example.com/legacy",
          L"旧版预设迁移为单标签数组");
    DeleteFileW(path.c_str());
}

void TestWindowTransitions() {
    using namespace xiaochuang;
    WindowStateSnapshot state;
    state = ReduceWindowState(state, WindowEvent::ToggleMaximize, ImmersionStyle::Hole);
    Check(state.visibleMode == WindowMode::Maximized, L"普通窗口进入无缝最大化");
    state = ReduceWindowState(state, WindowEvent::ToggleMaximize, ImmersionStyle::Hole);
    Check(state.visibleMode == WindowMode::Normal, L"无缝最大化恢复");
    state = ReduceWindowState(state, WindowEvent::ToggleImmersion, ImmersionStyle::AutoHide);
    Check(state.visibleMode == WindowMode::ImmersionAutoHide, L"进入整体透明沉浸模式");
    state = ReduceWindowState(state, WindowEvent::Hide, ImmersionStyle::AutoHide);
    Check(state.hidden, L"沉浸状态可原子隐藏");
    state = ReduceWindowState(state, WindowEvent::Show, ImmersionStyle::AutoHide);
    Check(!state.hidden && state.visibleMode == WindowMode::ImmersionAutoHide, L"显示后恢复可见模式");
}

void TestHoldGestures() {
    using namespace xiaochuang;
    HoldGestureState gesture;
    gesture.Press(1000);
    Check(gesture.Tick(1399) == HoldGestureResult::None, L"400ms 前不触发长按");
    Check(gesture.Release() == HoldGestureResult::Tap, L"短按在松开时触发跳转");
    gesture.Press(2000);
    Check(gesture.Tick(2400) == HoldGestureResult::HoldStart, L"400ms 触发长按开始");
    Check(gesture.Tick(2600) == HoldGestureResult::None, L"长按开始只触发一次");
    Check(gesture.Release() == HoldGestureResult::HoldStop, L"松开完整恢复长按状态");
}

void TestInputAndMediaSelection() {
    using namespace xiaochuang;
    Check(IsEditableClassName(L"RichEditD2DPT"), L"识别原生富文本输入控件");
    Check(IsEditableClassName(L"Scintilla"), L"识别代码/聊天输入控件");
    Check(!IsEditableClassName(L"Chrome_WidgetWin_1"), L"非输入窗口不误判");
    Check(IsLikelyAutomationTextInput(true, true, true), L"明确可写的 UI Automation 编辑控件触发输入保护");
    Check(!IsLikelyAutomationTextInput(true, true, false), L"仅有编辑语义的自绘画布不误判");
    Check(!IsLikelyAutomationTextInput(false, true, true), L"仅有可写 Value 的渲染表面不误判");
    Check(!IsLikelyAutomationTextInput(false, true, false), L"普通可聚焦网页文档不误判为输入框");
    Check(!ShouldInspectTextInputProcess(false, false), L"启动时忽略旧外部窗口的遗留输入焦点");
    Check(ShouldInspectTextInputProcess(false, true), L"新外部焦点事件后启用跨程序输入保护");
    Check(ShouldInspectTextInputProcess(true, false), L"应用自身输入控件无需等待外部焦点事件");
    for (const int transparency : {0, 1, 25, 50, 99, 100}) {
        int transparentColumns = 0;
        for (int coordinate = 0; coordinate < 100; ++coordinate) {
            if (IsHoleMaskColumnTransparent(coordinate, transparency)) ++transparentColumns;
        }
        Check(transparentColumns == transparency, L"挖孔像素掩码精确匹配透明度百分比");
    }
    Check(IsHoleMaskColumnTransparent(-100, 50) ==
          IsHoleMaskColumnTransparent(0, 50), L"挖孔掩码支持屏幕外负坐标");
    Check(SelectMediaSite(L"www.bilibili.com") == MediaSite::Bilibili, L"优先选择 Bilibili 适配器");
    Check(SelectMediaSite(L"www.douyin.com") == MediaSite::Douyin, L"选择抖音适配器");
    Check(SelectMediaSite(L"youtu.be") == MediaSite::YouTube, L"选择 YouTube 适配器");
    Check(SelectMediaSite(L"example.com") == MediaSite::GenericHtml5, L"未知网站回退 HTML5");

    const std::vector<VideoCandidate> videos = {
        {false, true, 800000, 10},
        {true, true, 300000, 20},
        {true, false, 2000000, 30},
    };
    const auto selected = SelectActiveVideo(videos);
    Check(selected && *selected == 1, L"活动视频兼顾播放、可见性与面积");
}

void TestRawMouseButtons() {
    using namespace xiaochuang;
    const auto sideOne = DecodeRawMouseButtons(RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP);
    Check(sideOne.size() == 2 && sideOne[0].virtualKey == VK_XBUTTON1 && sideOne[0].down &&
          sideOne[1].virtualKey == VK_XBUTTON1 && !sideOne[1].down,
          L"Raw Input 识别鼠标侧键1按下和松开");
    const auto sideTwo = DecodeRawMouseButtons(RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_BUTTON_5_UP);
    Check(sideTwo.size() == 2 && sideTwo[0].virtualKey == VK_XBUTTON2 && sideTwo[0].down &&
          sideTwo[1].virtualKey == VK_XBUTTON2 && !sideTwo[1].down,
          L"Raw Input 识别鼠标侧键2按下和松开");
    const auto middle = DecodeRawMouseButtons(RI_MOUSE_BUTTON_3_DOWN | RI_MOUSE_BUTTON_3_UP);
    Check(middle.size() == 2 && middle[0].virtualKey == VK_MBUTTON && middle[0].down &&
          middle[1].virtualKey == VK_MBUTTON && !middle[1].down,
          L"Raw Input 识别鼠标中键按下和松开");
    Check(DecodeRawMouseButtons(0).empty(), L"Raw Input 忽略无关鼠标移动");
}

void TestPresetNames() {
    using namespace xiaochuang;
    const std::vector<std::wstring> names = {L"原神", L"BILIBILI"};
    Check(TrimWhitespace(L"  场景名称 \r\n") == L"场景名称", L"预设名称去除首尾空白");
    Check(HasPresetNameConflict(names, L" 原神 "), L"预设名称拒绝空白变体重名");
    Check(HasPresetNameConflict(names, L"bilibili"), L"预设名称不区分拉丁字母大小写");
    Check(!HasPresetNameConflict(names, L"原神", 0), L"重命名时允许保留原名称");
    Check(HasPresetNameConflict(names, L"   "), L"预设名称拒绝纯空白");
}

void TestWindowGeometryHelpers() {
    using namespace xiaochuang;
    const RECT maximized{0, 0, 1920, 1040};
    const RECT normal{100, 100, 1100, 700};
    const RECT work{0, 0, 1920, 1040};
    const RECT restored = CalculateRestoredDragRect(maximized, normal, POINT{960, 20}, work, 68);
    Check(restored.left == 460 && restored.top == 0 && restored.right == 1460 && restored.bottom == 600,
          L"最大化拖动按光标比例恢复普通窗口");
    const SIZE wide = CalculateAspectFitWindowSize(1000, 68, 16.0 / 9.0, 1920, 1040);
    Check(wide.cx == 1000 && wide.cy == 631, L"16:9 视频自动计算小窗高度");
    const SIZE constrained = CalculateAspectFitWindowSize(1900, 68, 1.0, 1920, 1040);
    Check(constrained.cx == 972 && constrained.cy == 1040, L"视频比例适配不超出工作区");
}

} // namespace

int wmain() {
    TestConfigMigration();
    TestWindowTransitions();
    TestHoldGestures();
    TestInputAndMediaSelection();
    TestRawMouseButtons();
    TestPresetNames();
    TestWindowGeometryHelpers();
    if (failures == 0) {
        std::cout << "All XiaoChuang core tests passed.\n";
        return 0;
    }
    std::cout << failures << " test(s) failed.\n";
    return 1;
}
