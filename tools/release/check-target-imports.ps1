<#
    依赖闭包体检：判定一个发布包「能不能在目标 Windows 上**加载**」。

    用法：
        powershell -File tools\release\check-target-imports.ps1 -Dir dist\vtplay-0.3.0-win81-x64
        powershell -File tools\release\check-target-imports.ps1 -Dir dist\vtplay-0.3.0-win64 -Target win10

    为什么需要它（不是"防御性编程"，是踩过的坑）：
        0.3.0 的 Qt6 构建在 Windows 8.1 上表现为「双击没反应」，没有任何日志——
        因为进程在 main() 之前就被加载器拒绝了：Qt6Gui.dll 硬导入 d3d12.dll（Win10 才有），
        Qt6Core.dll 从 kernel32 硬导入 SetThreadDescription（Win10 1607 才有）。
        这类故障在用户侧只表现为"打不开"，而导入表**在没有目标机器的前提下就能检查**。

    检查两层：
        1. DLL 级：导入了目标系统不存在的 DLL → 失败（除非包内自带该 DLL）
        2. 函数级：从目标系统自带的 DLL 里导入了不存在的**函数** → 失败
           （只补 DLL 救不了 SetThreadDescription：文件在，函数不在）
        延迟导入（delay-load）只警告不失败：它不阻止进程启动。

    -Target 目前只支持 win81 与 win10；win81 是严格档，win10 仅做参考。
#>
param(
    [Parameter(Mandatory = $true)][string]$Dir,
    [ValidateSet('win81', 'win10')][string]$Target = 'win81',
    [string]$Objdump = ''
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Dir)) { throw "目录不存在: $Dir" }
$Dir = (Resolve-Path $Dir).Path

# objdump：优先用 MSYS2 binutils；找不到就明确报错（不猜路径）
if (-not $Objdump) {
    foreach ($c in @('objdump.exe', 'D:\dev\msys64\mingw64\bin\objdump.exe')) {
        $cmd = Get-Command $c -ErrorAction SilentlyContinue
        if ($cmd) { $Objdump = $cmd.Source; break }
        if (Test-Path $c) { $Objdump = $c; break }
    }
}
if (-not $Objdump -or -not (Test-Path $Objdump)) {
    throw "找不到 objdump.exe（MSYS2: mingw-w64-x86_64-binutils），可用 -Objdump 指定"
}

# ---- 目标系统不存在 / 需额外补丁的 DLL ----
# 说明：若包内自带同名 DLL，则视为满足（本地副本优先于系统目录）。
$denyDll = @{
    'd3d12.dll'         = 'Windows 10 引入'
    'd3d12core.dll'     = 'Windows 10 引入'
    'd3dcompiler_47.dll'= 'Windows 10 自带；8.1 需随包提供'
    'ucrtbase.dll'      = 'Windows 10 自带；8.1 需 KB2999226'
    'coremessaging.dll' = 'Windows 10 引入'
    'dwritecore.dll'    = 'Windows 10 引入'
}

# ---- 目标系统「DLL 在、函数不在」的导入 ----
$denyFunc = @{
    'SetThreadDescription'          = 'Windows 10 1607 引入'
    'GetThreadDescription'          = 'Windows 10 1607 引入'
    'SetProcessDpiAwarenessContext' = 'Windows 10 1703 引入'
    'GetDpiForWindow'               = 'Windows 10 1607 引入'
    'D3D12CreateDevice'             = 'Windows 10 引入'
    'ProcessPrng'                   = 'Windows 10 引入'
}

# ---- 目标系统自带的 DLL（用于把「未知依赖」单独标出来，而不是静默通过）----
# 只列我们实际见过、或 8.1 一定有的那些；不在表里也不算失败，仅提示。
$knownSystem = @(
    'kernel32.dll','user32.dll','gdi32.dll','advapi32.dll','shell32.dll','ole32.dll',
    'oleaut32.dll','ws2_32.dll','winmm.dll','version.dll','userenv.dll','netapi32.dll',
    'authz.dll','mpr.dll','ntdll.dll','bcrypt.dll','crypt32.dll','propsys.dll','avrt.dll',
    'dwmapi.dll','d3d11.dll','dxgi.dll','dwrite.dll','uxtheme.dll','combase.dll','shlwapi.dll',
    'secur32.dll','iphlpapi.dll','dnsapi.dll','psapi.dll','wtsapi32.dll','setupapi.dll',
    'mfplat.dll','mf.dll','mfreadwrite.dll','oleacc.dll','d2d1.dll','d3d9.dll','opengl32.dll',
    'msvcrt.dll','msimg32.dll','imm32.dll','winspool.drv','comdlg32.dll','wininet.dll',
    'dbghelp.dll','pdh.dll','powrprof.dll','rpcrt4.dll','usp10.dll','gdiplus.dll',
    'bcryptprimitives.dll','ncrypt.dll','winhttp.dll','shcore.dll','wsock32.dll',
    'winmm.dll','ole32.dll','windowscodecs.dll','msdmo.dll','strmiids.dll',
    # AVICAP32 是 avdevice 的摄像头输入用的，Win95 起就在系统里（8.1 当然有）
    'avicap32.dll'
)

# 一次性把黑名单函数名拼成一个正则，避免每行都做 6 次匹配（204 个二进制 × 上千行）
$denyFuncRe = '\b(' + ((@($denyFunc.Keys) | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')\b'

$binaries = @(Get-ChildItem $Dir -Recurse -Include '*.exe', '*.dll' -File)
if ($binaries.Count -eq 0) { throw "目录里没有 exe/dll: $Dir" }

$failures = @()
$unknown  = @()

foreach ($b in $binaries) {
    $curDll  = ''
    $inDelay = $false

    foreach ($line in (& $Objdump -p $b.FullName 2>$null)) {
        # 只认「延迟导入表」的表头。注意别匹配 'Delay Import Directory'——
        # 那一行出现在 PE 头摘要里（常规导入表之前），匹配它会把整张常规表误判成延迟导入。
        if ($line -match 'Delay Import Tables') { $inDelay = $true; continue }

        if ($line -match 'DLL Name:\s*(\S+)') {
            $curDll = $Matches[1].Trim()
            $lower  = $curDll.ToLower()

            if (Test-Path (Join-Path $Dir $curDll)) { continue }      # 包内自带，满足
            if ($denyDll.ContainsKey($lower)) {
                $why = $denyDll[$lower]
                if ($inDelay) {
                    $unknown += "[warn] $($b.Name) 延迟导入 $curDll（$why）——不阻止启动"
                } else {
                    $failures += "[dll ] $($b.Name) 硬导入 $curDll（$why，目标系统无此文件）"
                }
                continue
            }
            # api-ms-win-* 是 API Set：由系统按版本解析，磁盘上没有对应文件，
            # 离线无法判定可用性，因此不逐条登记（已知缺失的仍会命中上面的 denyDll）。
            $isApiSet = $lower -like 'api-ms-win-*'
            if (-not $isApiSet -and -not ($knownSystem -contains $lower)) {
                $unknown += "[info] $($b.Name) 导入了未登记的 $curDll —— 请确认目标系统是否存在"
            }
            continue
        }

        # 函数级：不解析 objdump 的列结构，直接在「当前 DLL 的导入区」里找黑名单函数名。
        # 原因：列数不固定——按名字导入时是
        #   "\t006cd7a8  <none>  0587  SetThreadDescription"（三列 + 名字），
        # 按序数导入时是 "\t 1f3\t 1e6  SomeFunc"（两列 + 名字）。
        # 先按「两列 hex」写死会漏掉前者（实测踩到：Qt6Core 的 SetThreadDescription 就是这种）。
        # 导出表在 $curDll 为空时出现，天然不会被误判。
        if ($curDll -and $line -match $denyFuncRe) {
            $fn = $Matches[1]
            if ($denyFunc.ContainsKey($fn)) {
                $why = $denyFunc[$fn]
                if ($inDelay) {
                    $unknown += "[warn] $($b.Name) 延迟调用 $fn（$why）——不阻止启动"
                } else {
                    $failures += "[func] $($b.Name) 从 $curDll 导入 $fn（$why，目标系统无此函数）"
                }
            }
        }
    }
}

Write-Output "目标系统: $Target  扫描目录: $Dir"
Write-Output "扫描二进制: $($binaries.Count) 个"

if ($unknown.Count -gt 0) {
    Write-Output ''
    Write-Output '--- 提示（不影响判定）---'
    $unknown | Sort-Object -Unique | ForEach-Object { Write-Output $_ }
}

if ($failures.Count -gt 0) {
    Write-Output ''
    Write-Output '--- 失败项 ---'
    $failures | Sort-Object -Unique | ForEach-Object { Write-Output $_ }
    Write-Output ''
    Write-Output "结果: 不通过（$($failures.Count) 项）—— 该包在目标系统上会在 main() 之前加载失败"
    exit 1
}

Write-Output ''
Write-Output "结果: 通过（0 项失败）—— $($binaries.Count) 个二进制的导入均在目标系统可用集内"
exit 0
