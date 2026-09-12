param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory
)

$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $BuildDirectory).Path
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::Combine(
    [IO.Path]::GetTempPath(), 'XiaoChuang-smoke-' + [guid]::NewGuid().ToString('N')))
if (-not $tempRoot.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Unsafe temporary directory.'
}
New-Item -ItemType Directory -Path $tempRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'XiaoChuang.exe') -Destination $tempRoot
Copy-Item -LiteralPath (Join-Path $source 'WebView2Loader.dll') -Destination $tempRoot
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'smoke-config.ini') -Destination (Join-Path $tempRoot 'config.ini')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'zoom-smoke.html') -Destination $tempRoot

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class XcNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    public struct MINMAXINFO {
        public POINT Reserved, MaxSize, MaxPosition, MinTrackSize, MaxTrackSize;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct MONITORINFO {
        public uint Size;
        public RECT Monitor;
        public RECT Work;
        public uint Flags;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct MOUSEINPUT {
        public int dx, dy;
        public uint mouseData, dwFlags, time;
        public UIntPtr dwExtraInfo;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct INPUT {
        public uint type;
        public MOUSEINPUT mouse;
    }
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern IntPtr CreateWindowEx(uint exStyle, string cls, string title,
        uint style, int x, int y, int width, int height, IntPtr parent, IntPtr menu,
        IntPtr instance, IntPtr parameter);
    [DllImport("user32.dll")]
    public static extern bool DestroyWindow(IntPtr h);
    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool PostMessage(IntPtr h, uint m, UIntPtr w, IntPtr l);
    [DllImport("user32.dll")]
    public static extern IntPtr SendMessage(IntPtr h, uint m, UIntPtr w, IntPtr l);
    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")]
    public static extern bool IsWindow(IntPtr h);
    [DllImport("user32.dll")]
    public static extern bool IsWindowEnabled(IntPtr h);
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")]
    public static extern IntPtr GetDC(IntPtr h);
    [DllImport("user32.dll")]
    public static extern int ReleaseDC(IntPtr h, IntPtr dc);
    [DllImport("gdi32.dll")]
    public static extern uint GetPixel(IntPtr dc, int x, int y);
    [DllImport("user32.dll")]
    public static extern IntPtr GetLastActivePopup(IntPtr h);
    [DllImport("user32.dll")]
    public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder text, int count);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern bool SetWindowText(IntPtr h, string text);
    [DllImport("user32.dll", EntryPoint="SendMessageW", CharSet=CharSet.Unicode)]
    public static extern IntPtr SendText(IntPtr h, uint message, UIntPtr wParam, string text);
    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y,
        int width, int height, uint flags);
    [DllImport("user32.dll")]
    public static extern IntPtr MonitorFromWindow(IntPtr h, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern bool GetMonitorInfo(IntPtr monitor, ref MONITORINFO info);
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")]
    public static extern IntPtr SetFocus(IntPtr h);
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr h, out uint processId);
    [DllImport("kernel32.dll")]
    public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")]
    public static extern bool AttachThreadInput(uint from, uint to, bool attach);
    [DllImport("user32.dll")]
    public static extern int GetWindowRgn(IntPtr h, IntPtr r);
    [DllImport("gdi32.dll")]
    public static extern IntPtr CreateRectRgn(int l, int t, int r, int b);
    [DllImport("gdi32.dll")]
    public static extern bool PtInRegion(IntPtr r, int x, int y);
    [DllImport("gdi32.dll")]
    public static extern bool DeleteObject(IntPtr o);
    [DllImport("user32.dll")]
    public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")]
    public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extraInfo);
    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool RegisterHotKey(IntPtr h, int id, uint modifiers, uint key);
    [DllImport("user32.dll")]
    public static extern bool UnregisterHotKey(IntPtr h, int id);
    [DllImport("user32.dll", SetLastError=true)]
    public static extern uint SendInput(uint count, INPUT[] inputs, int size);
    [DllImport("user32.dll")]
    public static extern void keybd_event(byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);
    [DllImport("user32.dll")]
    public static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("user32.dll")]
    public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("dwmapi.dll")]
    public static extern int DwmGetWindowAttribute(IntPtr h, int attribute, out uint value, int size);
}
'@

[void][XcNative]::SetThreadDpiAwarenessContext([IntPtr](-4))

function Wait-Condition([scriptblock]$Condition, [int]$Timeout = 6000) {
    $until = [Environment]::TickCount64 + $Timeout
    do {
        if (& $Condition) { return $true }
        Start-Sleep -Milliseconds 50
    } while ([Environment]::TickCount64 -lt $until)
    return $false
}

function Get-ControlFillColor([IntPtr]$Control) {
    $controlRect = New-Object XcNative+RECT
    if (-not [XcNative]::GetWindowRect($Control, [ref]$controlRect)) { return 0xffffffffu }
    $screenDc = [XcNative]::GetDC([IntPtr]::Zero)
    if ($screenDc -eq [IntPtr]::Zero) { return 0xffffffffu }
    try {
        return [XcNative]::GetPixel(
            $screenDc, $controlRect.Left + 8,
            [int](($controlRect.Top + $controlRect.Bottom) / 2))
    } finally {
        [void][XcNative]::ReleaseDC([IntPtr]::Zero, $screenDc)
    }
}

function Test-ColorRange([uint32]$Color, [int]$Minimum, [int]$Maximum) {
    $red = $Color -band 0xff
    $green = ($Color -shr 8) -band 0xff
    $blue = ($Color -shr 16) -band 0xff
    return $red -ge $Minimum -and $red -le $Maximum -and
        $green -ge $Minimum -and $green -le $Maximum -and
        $blue -ge $Minimum -and $blue -le $Maximum
}

function Send-XButton([uint32]$Button) {
    $downMouse = New-Object XcNative+MOUSEINPUT
    $downMouse.mouseData = $Button
    $downMouse.dwFlags = 0x0080
    $down = New-Object XcNative+INPUT
    $down.type = 0
    $down.mouse = $downMouse
    $upMouse = New-Object XcNative+MOUSEINPUT
    $upMouse.mouseData = $Button
    $upMouse.dwFlags = 0x0100
    $up = New-Object XcNative+INPUT
    $up.type = 0
    $up.mouse = $upMouse
    return [XcNative]::SendInput(
        2, [XcNative+INPUT[]]@($down, $up),
        [Runtime.InteropServices.Marshal]::SizeOf([type][XcNative+INPUT]))
}

function Test-HotkeyAvailable([uint32]$VirtualKey) {
    $registered = [XcNative]::RegisterHotKey([IntPtr]::Zero, 993, 0, $VirtualKey)
    if ($registered) { [void][XcNative]::UnregisterHotKey([IntPtr]::Zero, 993) }
    return $registered
}

function Send-KeyPress([byte]$VirtualKey) {
    [XcNative]::keybd_event($VirtualKey, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [XcNative]::keybd_event($VirtualKey, 0, 2, [UIntPtr]::Zero)
}

function Send-ZoomIn([IntPtr]$Target) {
    if (-not [XcNative]::IsWindowVisible($Target)) { throw 'Zoom input target is hidden.' }
    Start-Sleep -Milliseconds 150
    # Exercise browser zoom through actual Ctrl+plus input in web content.
    [XcNative]::keybd_event(17, 0x1d, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [XcNative]::keybd_event(0x6b, 0x4e, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [XcNative]::keybd_event(0x6b, 0x4e, 2, [UIntPtr]::Zero)
    [XcNative]::keybd_event(17, 0x1d, 2, [UIntPtr]::Zero)
}

function Set-TestForeground([IntPtr]$Target) {
    $currentThread = [XcNative]::GetCurrentThreadId()
    $foreground = [XcNative]::GetForegroundWindow()
    $foregroundProcess = 0u
    $foregroundThread = if ($foreground -ne [IntPtr]::Zero) {
        [XcNative]::GetWindowThreadProcessId($foreground, [ref]$foregroundProcess)
    } else { 0u }
    $attached = $foregroundThread -ne 0 -and $foregroundThread -ne $currentThread -and
        [XcNative]::AttachThreadInput($currentThread, $foregroundThread, $true)
    try {
        [void][XcNative]::SetForegroundWindow($Target)
        [void][XcNative]::SetFocus($Target)
    } finally {
        if ($attached) {
            [void][XcNative]::AttachThreadInput($currentThread, $foregroundThread, $false)
        }
    }
    return Wait-Condition { [XcNative]::GetForegroundWindow() -eq $Target }
}

$results = [ordered]@{}
$process = Start-Process -FilePath (Join-Path $tempRoot 'XiaoChuang.exe') -WorkingDirectory $tempRoot -PassThru -WindowStyle Hidden
$window = [IntPtr]::Zero
$externalWindow = [IntPtr]::Zero
try {
    $results.WindowCreated = Wait-Condition {
        $process.Refresh()
        $process.MainWindowHandle -ne [IntPtr]::Zero
    } 10000
    $process.Refresh()
    $window = $process.MainWindowHandle
    Start-Sleep -Milliseconds 1000

    $minMaxPointer = [Runtime.InteropServices.Marshal]::AllocHGlobal(
        [Runtime.InteropServices.Marshal]::SizeOf([type][XcNative+MINMAXINFO]))
    try {
        [Runtime.InteropServices.Marshal]::StructureToPtr(
            [XcNative+MINMAXINFO]::new(), $minMaxPointer, $false)
        [void][XcNative]::SendMessage($window, 0x0024, [UIntPtr]::Zero, $minMaxPointer)
        $minMax = [Runtime.InteropServices.Marshal]::PtrToStructure(
            $minMaxPointer, [type][XcNative+MINMAXINFO])
        $dpi = [XcNative]::GetDpiForWindow($window)
        $expectedMinWidth = [int]((160 * $dpi + 48) / 96)
        $expectedMinHeight = [int]((120 * $dpi + 48) / 96)
        $results.CompactMinimumWindowSize =
            $minMax.MinTrackSize.X -eq $expectedMinWidth -and
            $minMax.MinTrackSize.Y -eq $expectedMinHeight
        $results.MinimumSizeProbe = "dpi=$dpi min=$($minMax.MinTrackSize.X)x$($minMax.MinTrackSize.Y) expected=${expectedMinWidth}x${expectedMinHeight}"
    } finally {
        [Runtime.InteropServices.Marshal]::FreeHGlobal($minMaxPointer)
    }

    $registered = [XcNative]::RegisterHotKey([IntPtr]::Zero, 991, 0, 57)
    $results.StartupHideHotkeyOwned = -not $registered -and
        [Runtime.InteropServices.Marshal]::GetLastWin32Error() -eq 1409
    if ($registered) { [void][XcNative]::UnregisterHotKey([IntPtr]::Zero, 991) }

    $currentThread = [XcNative]::GetCurrentThreadId()
    $externalWindow = [XcNative]::CreateWindowEx(
        0, 'EDIT', 'XC External Input', 0x10cf0080,
        40, 40, 320, 120, [IntPtr]::Zero, [IntPtr]::Zero,
        [IntPtr]::Zero, [IntPtr]::Zero)
    $externalInputFocused = Set-TestForeground $externalWindow
    $allHotkeysProtected = Wait-Condition {
        @((48, 192, 53, 54, 55, 56, 57) | Where-Object { -not (Test-HotkeyAvailable $_) }).Count -eq 0
    } 5000
    $hideCountBefore = [XcNative]::SendMessage(
        $window, 0x8042, [UIntPtr]6, [IntPtr]::Zero).ToInt64()
    $visibleBeforeProtectedHide = [XcNative]::IsWindowVisible($window)
    Send-KeyPress 57
    Start-Sleep -Milliseconds 200
    $hideBlockedWhileTyping = $visibleBeforeProtectedHide -eq [XcNative]::IsWindowVisible($window)
    $hideCountAfter = [XcNative]::SendMessage(
        $window, 0x8042, [UIntPtr]6, [IntPtr]::Zero).ToInt64()
    $hideBlockedWhileTyping = $hideBlockedWhileTyping -and $hideCountAfter -eq $hideCountBefore
    if ($externalWindow -ne [IntPtr]::Zero) {
        [void][XcNative]::DestroyWindow($externalWindow)
        $externalWindow = [IntPtr]::Zero
    }
    $externalWindow = [XcNative]::CreateWindowEx(
        0, 'STATIC', 'XC External Focus', 0x10cf0000,
        40, 40, 260, 120, [IntPtr]::Zero, [IntPtr]::Zero,
        [IntPtr]::Zero, [IntPtr]::Zero)
    $externalFocused = Set-TestForeground $externalWindow
    $guardCleared = Wait-Condition {
        [XcNative]::SendMessage($window, 0x8045, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 0
    } 5000
    Start-Sleep -Milliseconds 250
    $allHotkeysRestored =
        @((48, 192, 53, 54, 55, 56, 57) | Where-Object { Test-HotkeyAvailable $_ }).Count -eq 0
    $tapProbes = @()
    foreach ($tapCase in @(
        @(192, 1, 'playPause'), @(53, 2, 'seekBackward'), @(54, 3, 'seekForward'),
        @(55, 4, 'previous'), @(56, 5, 'next'))) {
        $tapKey = [byte]$tapCase[0]
        $tapAction = [int]$tapCase[1]
        $tapBefore = [XcNative]::SendMessage(
            $window, 0x8042, [UIntPtr]$tapAction, [IntPtr]::Zero).ToInt64()
        Send-KeyPress $tapKey
        $tapTriggered = Wait-Condition {
            [XcNative]::SendMessage(
                $window, 0x8042, [UIntPtr]$tapAction, [IntPtr]::Zero).ToInt64() -gt $tapBefore
        } 1200
        $tapAfter = [XcNative]::SendMessage(
            $window, 0x8042, [UIntPtr]$tapAction, [IntPtr]::Zero).ToInt64()
        $tapProbes += [pscustomobject]@{
            Name = $tapCase[2]
            Passed = $tapTriggered -and $tapAfter -eq $tapBefore + 1
            Counts = "$tapBefore->$tapAfter"
        }
    }
    $externalMediaTapsWork = @($tapProbes | Where-Object { -not $_.Passed }).Count -eq 0
    $holdProbes = @()
    foreach ($holdCase in @(@(53, 2, 'backward'), @(54, 3, 'forward'))) {
        $holdKey = [byte]$holdCase[0]
        $holdAction = [int]$holdCase[1]
        $holdBefore = [XcNative]::SendMessage(
            $window, 0x8042, [UIntPtr]$holdAction, [IntPtr]::Zero).ToInt64()
        [XcNative]::keybd_event($holdKey, 0, 0, [UIntPtr]::Zero)
        $holdStarted = Wait-Condition {
            [XcNative]::SendMessage(
                $window, 0x8042, [UIntPtr]$holdAction, [IntPtr]::Zero).ToInt64() -gt $holdBefore
        } 1200
        $holdDuring = [XcNative]::SendMessage(
            $window, 0x8042, [UIntPtr]$holdAction, [IntPtr]::Zero).ToInt64()
        [XcNative]::keybd_event($holdKey, 0, 2, [UIntPtr]::Zero)
        $holdStopped = Wait-Condition {
            [XcNative]::SendMessage(
                $window, 0x8042, [UIntPtr]$holdAction, [IntPtr]::Zero).ToInt64() -gt $holdDuring
        } 1200
        $holdAfter = [XcNative]::SendMessage(
            $window, 0x8042, [UIntPtr]$holdAction, [IntPtr]::Zero).ToInt64()
        $holdProbes += [pscustomobject]@{
            Name = $holdCase[2]
            Passed = $holdStarted -and $holdStopped -and $holdAfter -eq $holdBefore + 2
            Counts = "$holdBefore->$holdDuring->$holdAfter"
        }
    }
    $externalHoldsWork = @($holdProbes | Where-Object { -not $_.Passed }).Count -eq 0
    $results.MediaHotkeysRestoreAfterExternalFocus = $externalInputFocused -and
        $allHotkeysProtected -and $hideBlockedWhileTyping -and
        $externalFocused -and $guardCleared -and $allHotkeysRestored -and $externalMediaTapsWork -and
        $externalHoldsWork
    $tapProbeText = ($tapProbes | ForEach-Object { "$($_.Name)=$($_.Passed):$($_.Counts)" }) -join ','
    $holdProbeText = ($holdProbes | ForEach-Object { "$($_.Name)=$($_.Passed):$($_.Counts)" }) -join ','
    $results.MediaHotkeyFocusProbe = "inputFocused=$externalInputFocused allProtected=$allHotkeysProtected hideBlocked=$hideBlockedWhileTyping hideCount=$hideCountBefore->$hideCountAfter external=$externalFocused guardCleared=$guardCleared allRestored=$allHotkeysRestored taps=[$tapProbeText] holds=[$holdProbeText]"
    if ($externalWindow -ne [IntPtr]::Zero) {
        [void][XcNative]::DestroyWindow($externalWindow)
        $externalWindow = [IntPtr]::Zero
    }
    [void][XcNative]::SetForegroundWindow($window)

    [void][XcNative]::SendMessage($window, 0x111, [UIntPtr]1010, [IntPtr]::Zero)
    $settingsShown = Wait-Condition {
        $popup = [XcNative]::GetLastActivePopup($window)
        $popup -ne [IntPtr]::Zero -and $popup -ne $window -and [XcNative]::IsWindowVisible($popup)
    }
    $settings = [XcNative]::GetLastActivePopup($window)
    $style = [XcNative]::GetDlgItem($settings, 2003)
    $zoomToggle = [XcNative]::GetDlgItem($settings, 2020)
    $zoomEdit = [XcNative]::GetDlgItem($settings, 2021)
    $results.ZoomDefaultsUnlocked = -not [XcNative]::IsWindowEnabled($zoomEdit)
    $zoomReady = Wait-Condition {
        [XcNative]::SendMessage($window, 0x8046, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64() -gt 0
    } 15000
    [void][XcNative]::SendMessage($zoomToggle, 0x00f1, [UIntPtr]1, [IntPtr]::Zero)
    [void][XcNative]::SendMessage($settings, 0x111, [UIntPtr]2020, $zoomToggle)
    Start-Sleep -Milliseconds 300
    [void][XcNative]::SendText($zoomEdit, 0x000c, [UIntPtr]::Zero, '135')
    [void][XcNative]::SendMessage($window, 0x804a, [UIntPtr]::Zero, [IntPtr]::Zero)
    $results.FixedZoomApplied = $zoomReady -and (Wait-Condition {
        [XcNative]::SendMessage($window, 0x8046, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 135
    }) -and [XcNative]::SendMessage($window, 0x8047, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 0
    $results.ZoomProbe = "ready=$zoomReady zoom=$([XcNative]::SendMessage($window, 0x8046, [UIntPtr]::Zero, [IntPtr]::Zero)) controls=$([XcNative]::SendMessage($window, 0x8047, [UIntPtr]::Zero, [IntPtr]::Zero)) config=$((Get-Content -LiteralPath (Join-Path $tempRoot 'config.ini') | Where-Object { $_ -match 'FixedWebZoom' }) -join ';')"
    [void](Set-TestForeground $window)
    [void][XcNative]::SendMessage($window, 0x804b, [UIntPtr]::Zero, [IntPtr]::Zero)
    Send-ZoomIn $window
    Start-Sleep -Milliseconds 300
    $results.FixedZoomRejectsChanges = Wait-Condition {
        [XcNative]::SendMessage($window, 0x8046, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 135
    }
    [void][XcNative]::SendMessage($window, 0x8048, [UIntPtr]1, [IntPtr]::Zero)
    $results.FixedZoomSurvivesNavigation = (Wait-Condition {
        [XcNative]::SendMessage($window, 0x8048, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 1
    }) -and [XcNative]::SendMessage($window, 0x8046, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 135
    [void][XcNative]::SendMessage($window, 0x8049, [UIntPtr]::Zero, [IntPtr]::Zero)
    $results.FixedZoomSurvivesRecreation = Wait-Condition {
        [XcNative]::SendMessage($window, 0x8046, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 135 -and
        [XcNative]::SendMessage($window, 0x8048, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 1
    } 15000
    [void][XcNative]::SendMessage($window, 0x111, [UIntPtr]1010, [IntPtr]::Zero)
    [void][XcNative]::SendMessage($zoomToggle, 0x00f1, [UIntPtr]0, [IntPtr]::Zero)
    [void][XcNative]::SendMessage($settings, 0x111, [UIntPtr]2020, $zoomToggle)
    # WebView2 applies IsZoomControlEnabled at the next top-level navigation.
    [void][XcNative]::SendMessage($window, 0x8048, [UIntPtr]1, [IntPtr]::Zero)
    $results.ZoomUnlockNavigation = Wait-Condition {
        [XcNative]::SendMessage($window, 0x8048, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 1
    }
    [void](Set-TestForeground $window)
    [void][XcNative]::SendMessage($window, 0x804b, [UIntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 300
    Send-ZoomIn $window
    $unlockedZoomChanged = Wait-Condition {
        [XcNative]::SendMessage($window, 0x8046, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64() -gt 135
    }
    $results.ZoomUnlockRestoresControls =
        [XcNative]::SendMessage($window, 0x8047, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 1 -and
        $unlockedZoomChanged
    $results.UnlockedZoomProbe = "zoom=$([XcNative]::SendMessage($window, 0x8046, [UIntPtr]::Zero, [IntPtr]::Zero)) controls=$([XcNative]::SendMessage($window, 0x8047, [UIntPtr]::Zero, [IntPtr]::Zero)) visible=$([XcNative]::IsWindowVisible($window)) enabled=$([XcNative]::IsWindowEnabled($window)) foreground=$([XcNative]::GetForegroundWindow()) main=$window"
    [void][XcNative]::SendMessage($window, 0x111, [UIntPtr]1010, [IntPtr]::Zero)
    $opacity = [XcNative]::GetDlgItem($settings, 2004)
    $autoFit = [XcNative]::GetDlgItem($settings, 2017)
    $lockAspect = [XcNative]::GetDlgItem($settings, 2019)
    $lockInitiallyDisabled = -not [XcNative]::IsWindowEnabled($lockAspect)
    [void][XcNative]::SendMessage($autoFit, 0x00f1, [UIntPtr]1, [IntPtr]::Zero)
    [void][XcNative]::SendMessage($settings, 0x111, [UIntPtr]2017, $autoFit)
    $lockEnabled = Wait-Condition { [XcNative]::IsWindowEnabled($lockAspect) }
    [void][XcNative]::SendMessage($lockAspect, 0x00f1, [UIntPtr]1, [IntPtr]::Zero)
    [void][XcNative]::SendMessage($settings, 0x111, [UIntPtr]2019, $lockAspect)
    $lockSaved = (Get-Content -LiteralPath (Join-Path $tempRoot 'config.ini') -Raw) -match
        '(?m)^LockVideoFullscreenAspect=1\r?$'
    [void][XcNative]::SendMessage($autoFit, 0x00f1, [UIntPtr]0, [IntPtr]::Zero)
    [void][XcNative]::SendMessage($settings, 0x111, [UIntPtr]2017, $autoFit)
    $lockDisabledAgain = Wait-Condition { -not [XcNative]::IsWindowEnabled($lockAspect) }
    $results.FullscreenAspectLockOption = $lockInitiallyDisabled -and $lockEnabled -and
        $lockSaved -and $lockDisabledAgain
    $nextEnabled = [XcNative]::GetDlgItem($settings, 2205)
    [void][XcNative]::SendMessage($nextEnabled, 0x00f1, [UIntPtr]0, [IntPtr]::Zero)
    [void][XcNative]::SendMessage($settings, 0x111, [UIntPtr]2205, $nextEnabled)
    $disabledReleased = Wait-Condition { Test-HotkeyAvailable 56 }
    $disabledSaved = (Get-Content -LiteralPath (Join-Path $tempRoot 'config.ini') -Raw) -match
        '(?m)^NextEp=0,56,0\r?$'
    [void][XcNative]::SendMessage($nextEnabled, 0x00f1, [UIntPtr]1, [IntPtr]::Zero)
    [void][XcNative]::SendMessage($settings, 0x111, [UIntPtr]2205, $nextEnabled)
    $enabledOwned = Wait-Condition { -not (Test-HotkeyAvailable 56) }
    $enabledSaved = (Get-Content -LiteralPath (Join-Path $tempRoot 'config.ini') -Raw) -match
        '(?m)^NextEp=0,56,1\r?$'
    $results.PerHotkeyEnableToggle = $nextEnabled -ne [IntPtr]::Zero -and
        $disabledReleased -and $disabledSaved -and $enabledOwned -and $enabledSaved
    $hiddenForHole = $opacity -ne [IntPtr]::Zero -and -not [XcNative]::IsWindowVisible($opacity)
    [void][XcNative]::SendMessage($style, 0x14e, [UIntPtr]1, [IntPtr]::Zero)
    $styleChanged = [UIntPtr]((1 -shl 16) -bor 2003)
    [void][XcNative]::SendMessage($settings, 0x111, $styleChanged, $style)
    $shownForAutoHide = Wait-Condition { [XcNative]::IsWindowVisible($opacity) }
    $results.OpacityOnlyForAutoHide = $settingsShown -and $hiddenForHole -and $shownForAutoHide
    [void][XcNative]::SendMessage($style, 0x14e, [UIntPtr]0, [IntPtr]::Zero)
    [void][XcNative]::SendMessage($settings, 0x111, $styleChanged, $style)
    [void][XcNative]::PostMessage($settings, 0x10, [UIntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 150

    [void][XcNative]::SendMessage($window, 0x111, [UIntPtr]1010, [IntPtr]::Zero)
    [void](Wait-Condition {
        $popup = [XcNative]::GetLastActivePopup($window)
        $popup -ne [IntPtr]::Zero -and $popup -ne $window -and [XcNative]::IsWindowVisible($popup)
    })
    $settings = [XcNative]::GetLastActivePopup($window)
    $themeCombo = [XcNative]::GetDlgItem($settings, 2013)
    $minimizeControl = [XcNative]::GetDlgItem($window, 1004)
    [void][XcNative]::SendMessage($themeCombo, 0x14e, [UIntPtr]2, [IntPtr]::Zero)
    $themeChangedDark = [UIntPtr]((1 -shl 16) -bor 2013)
    [void][XcNative]::SendMessage($settings, 0x111, $themeChangedDark, $themeCombo)
    $darkMode = 0u
    $darkApplied = Wait-Condition {
        $value = 0u
        [XcNative]::DwmGetWindowAttribute($window, 20, [ref]$value, 4) -eq 0 -and $value -eq 1
    }
    $darkColor = Get-ControlFillColor $minimizeControl
    [void][XcNative]::SendMessage($themeCombo, 0x14e, [UIntPtr]1, [IntPtr]::Zero)
    [void][XcNative]::SendMessage($settings, 0x111, $themeChangedDark, $themeCombo)
    $lightApplied = Wait-Condition {
        $value = 1u
        [XcNative]::DwmGetWindowAttribute($window, 20, [ref]$value, 4) -eq 0 -and $value -eq 0
    }
    $lightColor = Get-ControlFillColor $minimizeControl
    $results.ThemeSwitchUpdatesAllControls = $darkApplied -and $lightApplied -and
        (Test-ColorRange $darkColor 0 120) -and (Test-ColorRange $lightColor 180 255)
    $results.ThemeProbe = "darkApplied=$darkApplied darkColor=0x$($darkColor.ToString('X6')) lightApplied=$lightApplied lightColor=0x$($lightColor.ToString('X6'))"
    [void][XcNative]::PostMessage($settings, 0x10, [UIntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 150

    [void][XcNative]::SendMessage($window, 0x111, [UIntPtr]1005, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $rect = New-Object XcNative+RECT
    [void][XcNative]::GetWindowRect($window, [ref]$rect)
    $x = $rect.Left + 500
    $y = $rect.Top + 10
    $packed = [IntPtr](($x -band 0xffff) -bor (($y -band 0xffff) -shl 16))
    $hit = [XcNative]::SendMessage($window, 0x84, [UIntPtr]::Zero, $packed).ToInt64()
    $results.MaximizedBlankIsClient = $hit -eq 1
    $nonClientRendering = 1u
    $dwm = [XcNative]::DwmGetWindowAttribute($window, 1, [ref]$nonClientRendering, 4)
    $results.MaximizedBorderDisabled = $dwm -eq 0 -and $nonClientRendering -eq 0
    $results.MaximizedBorderProbe = "hr=$dwm nonClientRendering=$nonClientRendering"
    [void][XcNative]::SendMessage($window, 0x111, [UIntPtr]1005, [IntPtr]::Zero)

    [void][XcNative]::SendMessage($window, 0x111, [UIntPtr]1004, [IntPtr]::Zero)
    $hidden = Wait-Condition { -not [XcNative]::IsWindowVisible($window) }
    [void][XcNative]::PostMessage($window, 0x803c, [UIntPtr]::Zero, [IntPtr]0x00010202)
    $shown = Wait-Condition { [XcNative]::IsWindowVisible($window) }
    $results.TraySingleClickRestore = $hidden -and $shown

    [void][XcNative]::SendMessage($window, 0x111, [UIntPtr]1010, [IntPtr]::Zero)
    [void](Wait-Condition {
        $popup = [XcNative]::GetLastActivePopup($window)
        $popup -ne [IntPtr]::Zero -and $popup -ne $window -and [XcNative]::IsWindowVisible($popup)
    })
    $settings = [XcNative]::GetLastActivePopup($window)
    $hideBinding = [XcNative]::GetDlgItem($settings, 2106)
    [void][XcNative]::SendMessage($settings, 0x111, [UIntPtr]2106, $hideBinding)
    Start-Sleep -Milliseconds 100
    $captureSent = Send-XButton 1
    Start-Sleep -Milliseconds 350
    $bindingText = New-Object Text.StringBuilder 64
    [void][XcNative]::GetWindowText($hideBinding, $bindingText, $bindingText.Capacity)
    $savedConfig = Get-Content -LiteralPath (Join-Path $tempRoot 'config.ini') -Raw
    [void][XcNative]::PostMessage($settings, 0x10, [UIntPtr]::Zero, [IntPtr]::Zero)
    $externalWindow = [XcNative]::CreateWindowEx(
        0, 'STATIC', 'XC Mouse Hotkey Focus', 0x10cf0000,
        40, 40, 260, 120, [IntPtr]::Zero, [IntPtr]::Zero,
        [IntPtr]::Zero, [IntPtr]::Zero)
    [void](Set-TestForeground $externalWindow)
    [void](Wait-Condition {
        [XcNative]::SendMessage($window, 0x8045, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 0
    })
    $hideSent = Send-XButton 1
    $mouseHidden = Wait-Condition { -not [XcNative]::IsWindowVisible($window) }
    [void](Set-TestForeground $externalWindow)
    [void](Wait-Condition {
        [XcNative]::SendMessage($window, 0x8045, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 0
    })
    Start-Sleep -Milliseconds 300
    $inputBeforeMouseShow = [XcNative]::SendMessage(
        $window, 0x8045, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    $hideCountBeforeMouseShow = [XcNative]::SendMessage(
        $window, 0x8042, [UIntPtr]6, [IntPtr]::Zero).ToInt64()
    $showPosted = [XcNative]::PostMessage(
        $window, 0x8028, [UIntPtr]5, [IntPtr]1)
    $mouseShown = Wait-Condition { [XcNative]::IsWindowVisible($window) }
    $hideCountAfterMouseShow = [XcNative]::SendMessage(
        $window, 0x8042, [UIntPtr]6, [IntPtr]::Zero).ToInt64()
    $showSent = if ($showPosted) { 2 } else { 0 }
    $results.MouseSideButtonBinding = $captureSent -eq 2 -and $hideSent -eq 2 -and
        $showSent -eq 2 -and $bindingText.ToString() -eq '鼠标侧键1' -and
        $savedConfig -match '(?m)^HideWin=0,5,1\r?$' -and $mouseHidden -and $mouseShown
    $results.MouseSideButtonProbe = "capture=$captureSent text=$($bindingText.ToString()) saved=$($savedConfig -match '(?m)^HideWin=0,5,1\r?$') hidden=$mouseHidden shown=$mouseShown input=$inputBeforeMouseShow count=$hideCountBeforeMouseShow->$hideCountAfterMouseShow"

    [void][XcNative]::SendMessage($window, 0x111, [UIntPtr]1010, [IntPtr]::Zero)
    [void](Wait-Condition {
        $popup = [XcNative]::GetLastActivePopup($window)
        $popup -ne [IntPtr]::Zero -and $popup -ne $window -and [XcNative]::IsWindowVisible($popup)
    })
    $settings = [XcNative]::GetLastActivePopup($window)
    $hideBinding = [XcNative]::GetDlgItem($settings, 2106)
    [void][XcNative]::SendMessage($settings, 0x111, [UIntPtr]2106, $hideBinding)
    Start-Sleep -Milliseconds 100
    $captureSent2 = Send-XButton 2
    Start-Sleep -Milliseconds 350
    $bindingText2 = New-Object Text.StringBuilder 64
    [void][XcNative]::GetWindowText($hideBinding, $bindingText2, $bindingText2.Capacity)
    $savedConfig2 = Get-Content -LiteralPath (Join-Path $tempRoot 'config.ini') -Raw
    [void][XcNative]::PostMessage($settings, 0x10, [UIntPtr]::Zero, [IntPtr]::Zero)
    [void](Set-TestForeground $externalWindow)
    [void](Wait-Condition {
        [XcNative]::SendMessage($window, 0x8045, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 0
    })
    $hideSent2 = Send-XButton 2
    $mouseHidden2 = Wait-Condition { -not [XcNative]::IsWindowVisible($window) }
    [void](Set-TestForeground $externalWindow)
    [void](Wait-Condition {
        [XcNative]::SendMessage($window, 0x8045, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 0
    })
    Start-Sleep -Milliseconds 300
    $showPosted2 = [XcNative]::PostMessage(
        $window, 0x8028, [UIntPtr]6, [IntPtr]1)
    $mouseShown2 = Wait-Condition { [XcNative]::IsWindowVisible($window) }
    $showSent2 = if ($showPosted2) { 2 } else { 0 }
    $results.MouseSideButton2Binding = $captureSent2 -eq 2 -and $hideSent2 -eq 2 -and
        $showSent2 -eq 2 -and $bindingText2.ToString() -eq '鼠标侧键2' -and
        $savedConfig2 -match '(?m)^HideWin=0,6,1\r?$' -and $mouseHidden2 -and $mouseShown2
    $results.MouseSideButton2Probe = "capture=$captureSent2 text=$($bindingText2.ToString()) saved=$($savedConfig2 -match '(?m)^HideWin=0,6,1\r?$') hidden=$mouseHidden2 shown=$mouseShown2"
    if ($externalWindow -ne [IntPtr]::Zero) {
        [void][XcNative]::DestroyWindow($externalWindow)
        $externalWindow = [IntPtr]::Zero
    }

    $address = [XcNative]::GetDlgItem($window, 1001)
    $addressLength = [XcNative]::SendMessage(
        $address, 0x00c1, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    [void][XcNative]::SendMessage($address, 0x0203, [UIntPtr]1, [IntPtr]0x00010001)
    $selection = [XcNative]::SendMessage($address, 0x00b0, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    $selectionStart = $selection -band 0xffff
    $selectionEnd = ($selection -shr 16) -band 0xffff
    $results.AddressDoubleClickSelectsAll = $addressLength -gt 0 -and
        $selectionStart -eq 0 -and $selectionEnd -eq $addressLength
    $results.AddressSelectionProbe = "length=$addressLength start=$selectionStart end=$selectionEnd"
    [void][XcNative]::SetForegroundWindow($window)
    $processId = 0u
    $applicationThread = [XcNative]::GetWindowThreadProcessId($window, [ref]$processId)
    $currentThread = [XcNative]::GetCurrentThreadId()
    $attached = [XcNative]::AttachThreadInput($currentThread, $applicationThread, $true)
    $previousFocus = [XcNative]::SetFocus($address)
    if ($attached) { [void][XcNative]::AttachThreadInput($currentThread, $applicationThread, $false) }
    [void][XcNative]::PostMessage($window, 0x8029, [UIntPtr]::Zero, [IntPtr]::Zero)
    $results.AddressFocusPrepared = $address -ne [IntPtr]::Zero
    $nativeInputProtected = Wait-Condition {
        [XcNative]::SendMessage($window, 0x8045, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 3
    }
    [void][XcNative]::PostMessage($window, 0x312, [UIntPtr]107, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $results.InputProtectionBlocksHideHotkey = $nativeInputProtected -and
        [XcNative]::IsWindowVisible($window)
    $externalWindow = [XcNative]::CreateWindowEx(
        0, 'STATIC', 'XC Hide Focus', 0x10cf0000,
        40, 40, 260, 120, [IntPtr]::Zero, [IntPtr]::Zero,
        [IntPtr]::Zero, [IntPtr]::Zero)
    $hideExternalFocused = Set-TestForeground $externalWindow
    $hideGuardCleared = Wait-Condition {
        [XcNative]::SendMessage($window, 0x8045, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 0
    } 5000
    [void][XcNative]::PostMessage($window, 0x312, [UIntPtr]107, [IntPtr]::Zero)
    $hidden = Wait-Condition { -not [XcNative]::IsWindowVisible($window) }
    $hiddenInactiveHotkeysReleased = Wait-Condition {
        @((48, 192, 53, 54, 55, 56) | Where-Object { -not (Test-HotkeyAvailable $_) }).Count -eq 0
    } 5000
    $inputStateAfterExternalFocus = [XcNative]::SendMessage(
        $window, 0x8045, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    Start-Sleep -Milliseconds 300
    [void][XcNative]::PostMessage($window, 0x312, [UIntPtr]107, [IntPtr]::Zero)
    $shown = Wait-Condition { [XcNative]::IsWindowVisible($window) }
    $results.HideShowWithoutClick = $hideExternalFocused -and $hideGuardCleared -and $hidden -and
        $hiddenInactiveHotkeysReleased -and $shown
    $results.HideShowProbe = "externalFocused=$hideExternalFocused guardCleared=$hideGuardCleared inputState=$inputStateAfterExternalFocus hidden=$hidden inactiveReleased=$hiddenInactiveHotkeysReleased shown=$shown"
    if ($externalWindow -ne [IntPtr]::Zero) {
        [void][XcNative]::DestroyWindow($externalWindow)
        $externalWindow = [IntPtr]::Zero
    }

    [void][XcNative]::PostMessage($window, 0x10, [UIntPtr]::Zero, [IntPtr]::Zero)
    [void]$process.WaitForExit(8000)
    Start-Sleep -Milliseconds 500
    $process = Start-Process -FilePath (Join-Path $tempRoot 'XiaoChuang.exe') -WorkingDirectory $tempRoot -PassThru -WindowStyle Hidden
    $results.RestartAfterFocusedHide = Wait-Condition {
        $process.Refresh()
        $process.MainWindowHandle -ne [IntPtr]::Zero
    } 10000
    $process.Refresh()
    $window = $process.MainWindowHandle
    Start-Sleep -Milliseconds 500

    $externalWindow = [XcNative]::CreateWindowEx(
        0, 'STATIC', 'XC Immersion Focus', 0x10cf0000,
        40, 40, 260, 120, [IntPtr]::Zero, [IntPtr]::Zero,
        [IntPtr]::Zero, [IntPtr]::Zero)
    [void](Set-TestForeground $externalWindow)

    [void][XcNative]::SendMessage($window, 0x111, [UIntPtr]1010, [IntPtr]::Zero)
    [void](Wait-Condition {
        $popup = [XcNative]::GetLastActivePopup($window)
        $popup -ne [IntPtr]::Zero -and $popup -ne $window -and [XcNative]::IsWindowVisible($popup)
    })
    $settings = [XcNative]::GetLastActivePopup($window)
    $autoFit = [XcNative]::GetDlgItem($settings, 2017)
    [void][XcNative]::SendMessage($autoFit, 0x00f1, [UIntPtr]1, [IntPtr]::Zero)
    [void][XcNative]::SendMessage($settings, 0x111, [UIntPtr]2017, $autoFit)
    [void][XcNative]::PostMessage($settings, 0x10, [UIntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 150

    $monitorInfo = New-Object XcNative+MONITORINFO
    $monitorInfo.Size = [Runtime.InteropServices.Marshal]::SizeOf([type][XcNative+MONITORINFO])
    $monitor = [XcNative]::MonitorFromWindow($window, 2)
    [void][XcNative]::GetMonitorInfo($monitor, [ref]$monitorInfo)
    $topWidth = [Math]::Min(900, $monitorInfo.Monitor.Right - $monitorInfo.Monitor.Left)
    $topHeight = [Math]::Min(600, $monitorInfo.Monitor.Bottom - $monitorInfo.Monitor.Top)
    [void][XcNative]::SetWindowPos($window, [IntPtr]::Zero,
        $monitorInfo.Monitor.Left, $monitorInfo.Monitor.Top, $topWidth, $topHeight, 0x0014)
    [void][XcNative]::SendMessage($window, 0x0232, [UIntPtr]::Zero, [IntPtr]::Zero)
    [void][XcNative]::SendMessage($window, 0x0312, [UIntPtr]101, [IntPtr]::Zero)
    $topImmersiveRect = New-Object XcNative+RECT
    $topImmersionAttached = Wait-Condition {
        [void][XcNative]::GetWindowRect($window, [ref]$topImmersiveRect)
        $topImmersiveRect.Top -eq $monitorInfo.Monitor.Top
    }
    $topImmersionGuardCleared = Wait-Condition {
        [XcNative]::SendMessage($window, 0x8045, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 0
    }
    Start-Sleep -Milliseconds 400
    [void][XcNative]::SendMessage($window, 0x0312, [UIntPtr]101, [IntPtr]::Zero)
    $topRestoredRect = New-Object XcNative+RECT
    $topNormalRestored = Wait-Condition {
        [void][XcNative]::GetWindowRect($window, [ref]$topRestoredRect)
        $topRestoredRect.Top -eq $monitorInfo.Monitor.Top -and
        ($topRestoredRect.Bottom - $topRestoredRect.Top) -eq $topHeight
    }
    $results.TopSnapImmersionHasNoGap = $topImmersionAttached -and
        $topImmersionGuardCleared -and $topNormalRestored
    $results.TopSnapProbe = "immersive=$($topImmersiveRect.Left),$($topImmersiveRect.Top),$($topImmersiveRect.Right),$($topImmersiveRect.Bottom) guardCleared=$topImmersionGuardCleared restored=$($topRestoredRect.Left),$($topRestoredRect.Top),$($topRestoredRect.Right),$($topRestoredRect.Bottom)"

    $normalWidth = [Math]::Min(900, $monitorInfo.Work.Right - $monitorInfo.Work.Left)
    $normalHeight = [Math]::Min(600, $monitorInfo.Work.Bottom - $monitorInfo.Work.Top)
    $normalLeft = $monitorInfo.Work.Right - $normalWidth
    $normalTop = $monitorInfo.Work.Bottom - $normalHeight
    [void][XcNative]::SetWindowPos($window, [IntPtr]::Zero,
        $normalLeft, $normalTop, $normalWidth, $normalHeight, 0x0014)
    [void][XcNative]::SendMessage($window, 0x0232, [UIntPtr]::Zero, [IntPtr]::Zero)
    $beforeVideoFullscreen = New-Object XcNative+RECT
    [void][XcNative]::GetWindowRect($window, [ref]$beforeVideoFullscreen)

    [void][XcNative]::SendMessage($window, 0x111, [UIntPtr]1005, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    [void][XcNative]::PostMessage($window, 0x8041, [UIntPtr]1, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    [void][XcNative]::SendMessage($window, 0x111, [UIntPtr]1005, [IntPtr]::Zero)

    $fittedAfterRestore = New-Object XcNative+RECT
    $videoFitApplied = Wait-Condition {
        [void][XcNative]::GetWindowRect($window, [ref]$fittedAfterRestore)
        ($fittedAfterRestore.Right - $fittedAfterRestore.Left) -eq $normalWidth -and
        $fittedAfterRestore.Bottom -eq $monitorInfo.Monitor.Bottom -and
        ($fittedAfterRestore.Bottom - $fittedAfterRestore.Top) -ne $normalHeight
    } 5000
    [void][XcNative]::SendMessage($window, 0x0100, [UIntPtr]27, [IntPtr]::Zero)
    $restoredAfterEscape = Wait-Condition {
        $restored = New-Object XcNative+RECT
        [void][XcNative]::GetWindowRect($window, [ref]$restored)
        $restored.Left -eq $beforeVideoFullscreen.Left -and
        $restored.Top -eq $beforeVideoFullscreen.Top -and
        $restored.Right -eq $beforeVideoFullscreen.Right -and
        $restored.Bottom -eq $beforeVideoFullscreen.Bottom
    } 5000
    $results.VideoFullscreenFitAfterLeavingWindowMaximized =
        $videoFitApplied -and $restoredAfterEscape
    $results.VideoFullscreenTransitionProbe =
        "fit=$videoFitApplied fitted=$($fittedAfterRestore.Left),$($fittedAfterRestore.Top),$($fittedAfterRestore.Right),$($fittedAfterRestore.Bottom) monitorBottom=$($monitorInfo.Monitor.Bottom) restored=$restoredAfterEscape"

    $normal = New-Object XcNative+RECT
    [void][XcNative]::GetWindowRect($window, [ref]$normal)
    $cursorX = [int](($normal.Left + $normal.Right) / 2)
    $cursorY = [int](($normal.Top + $normal.Bottom) / 2)
    [void][XcNative]::SetCursorPos($cursorX, $cursorY)
    Start-Sleep -Milliseconds 300
    [void][XcNative]::PostMessage($window, 0x312, [UIntPtr]101, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $immersive = New-Object XcNative+RECT
    [void][XcNative]::GetWindowRect($window, [ref]$immersive)
    $region = [XcNative]::CreateRectRgn(0, 0, 1, 1)
    $regionType = [XcNative]::GetWindowRgn($window, $region)
    $centerVisible = [XcNative]::PtInRegion(
        $region, $cursorX - $immersive.Left, $cursorY - $immersive.Top)
    $cornerVisible = [XcNative]::PtInRegion($region, 2, 2)
    [void][XcNative]::DeleteObject($region)
    $results.FixedTransparentHole = $regionType -ne 0 -and -not $centerVisible -and $cornerVisible
    $results.HoleProbe = "type=$regionType centerVisible=$centerVisible cornerVisible=$cornerVisible"

    Start-Sleep -Milliseconds 300
    [void][XcNative]::PostMessage($window, 0x312, [UIntPtr]101, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 300
    [void][XcNative]::PostMessage($window, 0x312, [UIntPtr]101, [IntPtr]::Zero)
    [void][XcNative]::PostMessage($window, 0x312, [UIntPtr]107, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    $rapidHidden = -not [XcNative]::IsWindowVisible($window)
    $rapidStillRunning = [XcNative]::IsWindow($window) -and -not $process.HasExited
    Start-Sleep -Milliseconds 200
    [void][XcNative]::PostMessage($window, 0x312, [UIntPtr]107, [IntPtr]::Zero)
    $rapidShown = Wait-Condition { [XcNative]::IsWindowVisible($window) }
    $results.RapidModeKeysStable = $rapidStillRunning -and $rapidHidden -and $rapidShown
    [void][XcNative]::PostMessage($window, 0x312, [UIntPtr]101, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 300

    [void][XcNative]::PostMessage($window, 0x312, [UIntPtr]107, [IntPtr]::Zero)
    [void](Wait-Condition { -not [XcNative]::IsWindowVisible($window) })
    $second = Start-Process -FilePath (Join-Path $tempRoot 'XiaoChuang.exe') -WorkingDirectory $tempRoot -PassThru -WindowStyle Hidden
    [void]$second.WaitForExit(5000)
    $results.SecondLaunchRestoresExisting = (Wait-Condition {
        [XcNative]::IsWindowVisible($window)
    }) -and $second.HasExited

    [void][XcNative]::PostMessage($window, 0x10, [UIntPtr]::Zero, [IntPtr]::Zero)
    [void]$process.WaitForExit(8000)
    $results.CleanExit = $process.HasExited -and $process.ExitCode -eq 0
} finally {
    if ($externalWindow -ne [IntPtr]::Zero) {
        [void][XcNative]::DestroyWindow($externalWindow)
    }
    if ($window -ne [IntPtr]::Zero -and [XcNative]::IsWindow($window)) {
        [void][XcNative]::PostMessage($window, 0x10, [UIntPtr]::Zero, [IntPtr]::Zero)
        [void]$process.WaitForExit(4000)
    }
}

$results.GetEnumerator() | ForEach-Object { '{0}={1}' -f $_.Key, $_.Value }
$failed = $results.Values -contains $false
$resolvedTemp = [IO.Path]::GetFullPath($tempRoot)
$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$tempLeaf = [IO.Path]::GetFileName($resolvedTemp)
if (-not $resolvedTemp.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -or
    -not $tempLeaf.StartsWith('XiaoChuang-smoke-', [StringComparison]::Ordinal)) {
    throw 'Refusing to remove an unexpected temporary directory.'
}
for ($attempt = 0; $attempt -lt 80 -and [IO.Directory]::Exists($resolvedTemp); ++$attempt) {
    try {
        [IO.Directory]::Delete($resolvedTemp, $true)
    } catch [IO.IOException] {
        Start-Sleep -Milliseconds 250
    } catch [UnauthorizedAccessException] {
        Start-Sleep -Milliseconds 250
    }
}
'TemporaryDirectoryRemoved=' + (-not [IO.Directory]::Exists($resolvedTemp))
if ($failed) { exit 2 }
