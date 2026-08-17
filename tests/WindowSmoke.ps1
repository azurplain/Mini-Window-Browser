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

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class XcNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
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
    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool PostMessage(IntPtr h, uint m, UIntPtr w, IntPtr l);
    [DllImport("user32.dll")]
    public static extern IntPtr SendMessage(IntPtr h, uint m, UIntPtr w, IntPtr l);
    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")]
    public static extern bool IsWindow(IntPtr h);
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
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr h);
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
    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool RegisterHotKey(IntPtr h, int id, uint modifiers, uint key);
    [DllImport("user32.dll")]
    public static extern bool UnregisterHotKey(IntPtr h, int id);
    [DllImport("user32.dll", SetLastError=true)]
    public static extern uint SendInput(uint count, INPUT[] inputs, int size);
    [DllImport("dwmapi.dll")]
    public static extern int DwmGetWindowAttribute(IntPtr h, int attribute, out uint value, int size);
}
'@

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

$results = [ordered]@{}
$process = Start-Process -FilePath (Join-Path $tempRoot 'XiaoChuang.exe') -WorkingDirectory $tempRoot -PassThru -WindowStyle Hidden
$window = [IntPtr]::Zero
try {
    $results.WindowCreated = Wait-Condition {
        $process.Refresh()
        $process.MainWindowHandle -ne [IntPtr]::Zero
    } 10000
    $process.Refresh()
    $window = $process.MainWindowHandle
    Start-Sleep -Milliseconds 1000

    $registered = [XcNative]::RegisterHotKey([IntPtr]::Zero, 991, 0, 57)
    $results.StartupHideHotkeyOwned = -not $registered -and
        [Runtime.InteropServices.Marshal]::GetLastWin32Error() -eq 1409
    if ($registered) { [void][XcNative]::UnregisterHotKey([IntPtr]::Zero, 991) }

    [void][XcNative]::SendMessage($window, 0x111, [UIntPtr]1010, [IntPtr]::Zero)
    $settingsShown = Wait-Condition {
        $popup = [XcNative]::GetLastActivePopup($window)
        $popup -ne [IntPtr]::Zero -and $popup -ne $window -and [XcNative]::IsWindowVisible($popup)
    }
    $settings = [XcNative]::GetLastActivePopup($window)
    $style = [XcNative]::GetDlgItem($settings, 2003)
    $opacity = [XcNative]::GetDlgItem($settings, 2004)
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
    Start-Sleep -Milliseconds 150
    $hideSent = Send-XButton 1
    $mouseHidden = Wait-Condition { -not [XcNative]::IsWindowVisible($window) }
    Start-Sleep -Milliseconds 300
    $showSent = Send-XButton 1
    $mouseShown = Wait-Condition { [XcNative]::IsWindowVisible($window) }
    $results.MouseSideButtonBinding = $captureSent -eq 2 -and $hideSent -eq 2 -and
        $showSent -eq 2 -and $bindingText.ToString() -eq '鼠标侧键1' -and
        $savedConfig -match '(?m)^HideWin=0,5\r?$' -and $mouseHidden -and $mouseShown
    $results.MouseSideButtonProbe = "capture=$captureSent text=$($bindingText.ToString()) saved=$($savedConfig -match '(?m)^HideWin=0,5\r?$') hidden=$mouseHidden shown=$mouseShown"

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
    Start-Sleep -Milliseconds 150
    $hideSent2 = Send-XButton 2
    $mouseHidden2 = Wait-Condition { -not [XcNative]::IsWindowVisible($window) }
    Start-Sleep -Milliseconds 300
    $showSent2 = Send-XButton 2
    $mouseShown2 = Wait-Condition { [XcNative]::IsWindowVisible($window) }
    $results.MouseSideButton2Binding = $captureSent2 -eq 2 -and $hideSent2 -eq 2 -and
        $showSent2 -eq 2 -and $bindingText2.ToString() -eq '鼠标侧键2' -and
        $savedConfig2 -match '(?m)^HideWin=0,6\r?$' -and $mouseHidden2 -and $mouseShown2
    $results.MouseSideButton2Probe = "capture=$captureSent2 text=$($bindingText2.ToString()) saved=$($savedConfig2 -match '(?m)^HideWin=0,6\r?$') hidden=$mouseHidden2 shown=$mouseShown2"

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
    Start-Sleep -Milliseconds 250
    $results.AddressFocusPrepared = $address -ne [IntPtr]::Zero
    [void][XcNative]::PostMessage($window, 0x312, [UIntPtr]107, [IntPtr]::Zero)
    $hidden = Wait-Condition { -not [XcNative]::IsWindowVisible($window) }
    Start-Sleep -Milliseconds 300
    [void][XcNative]::PostMessage($window, 0x312, [UIntPtr]107, [IntPtr]::Zero)
    $shown = Wait-Condition { [XcNative]::IsWindowVisible($window) }
    $results.HideShowWithoutClick = $hidden -and $shown
    $results.HideShowProbe = "hidden=$hidden shown=$shown"

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
    $results.RapidModeKeysStable = [XcNative]::IsWindow($window) -and
        -not $process.HasExited -and [XcNative]::IsWindowVisible($window)
    Start-Sleep -Milliseconds 200
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
