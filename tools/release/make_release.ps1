<#
    打 Windows 便携版发布包。

    用法（在仓库任意位置）：
        powershell -File tools\release\make_release.ps1 -QtBin D:\dev\msys64\mingw64\bin

    产物：
        <RepoRoot>\dist\vtplay-<版本>-win64\      便携目录
        <RepoRoot>\dist\vtplay-<版本>-win64.zip   发布用压缩包

    为什么需要这么多步骤（每一步都对应一个真实踩过的坑）：

    1. windeployqt —— exe 依赖 13 个 DLL（Qt6 六个、FFmpeg 五个、MinGW 运行时三个），
       直接发 exe 别人跑不起来。--qmldir 让 Qt 扫描 QML 导入，带上 QML 插件。
    2. --compiler-runtime —— 实测它并不总是生效，所以脚本还会兜底复制
       libstdc++-6 / libgcc_s_seh-1 / libwinpthread-1。缺了就是「双击没反应」。
    3. qt.conf —— 缺它时 QML 导入路径指向编译期的 msys2 前缀，换台机器报
       module "QtQuick.Controls.Basic" is not installed。（插件反而能加载，
       因为 Qt 默认会把应用目录加进 libraryPaths，QML 导入走的是另一套路径。）
    4. 依赖闭包 —— windeployqt 不追传递依赖。缺 zlib1/pcre2/icu/harfbuzz 以及
       FFmpeg 那一串编解码库时，加载器在 main() 之前就失败，连日志都不会产生。
    5. 随包 ffmpeg —— 放在 exe 同级目录，应用的探测顺序里有「应用目录」这一级，
       导出功能因此开箱可用。
#>
param(
    [Parameter(Mandatory = $true)][string]$QtBin,      # 例如 D:\dev\msys64\mingw64\bin
    [string]$RepoRoot,
    [string]$BuildDir = 'build',
    # 0 = 从构建目录的 CMakeCache 自动识别。legacy/win81 分支是 Qt5（build-win81），
    # 主干是 Qt6（build/），两者在同一台机器上并存，所以不能靠"装了哪个"判断。
    [int]$QtMajor = 0
)
$ErrorActionPreference = 'Stop'

# 关闭「原生命令的非零退出/ stderr 触发终止性错误」。
# PowerShell 7 的行为与 5.1 不同：windeployqt 会往 stderr 打 Warning
# （翻译缺失、dxcompiler 缺失——都是无害的），在 7.x 下可能被当成错误直接中断
# 整个脚本。CI 上跑的正是 pwsh 7，本地是 5.1——这个差异会让「本地过、CI 挂」。
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

# $PSScriptRoot 在某些宿主里是空的（例如脚本被外层脚本再包一层执行时），
# 所以按「脚本目录 -> $MyInvocation -> 当前目录」逐级兜底。
if (-not $RepoRoot) {
    $here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
    if (-not $here) { $here = (Get-Location).Path }
    $RepoRoot = (Resolve-Path (Join-Path $here '..\..')).Path
}

# -QtBin 允许给 MSYS2 根目录（C:\msys64）或直接给 bin 目录；两者都接受，
# 免得 CI 与本地写法不同就直接失败。
if (Test-Path (Join-Path $QtBin 'mingw64\bin')) { $QtBin = Join-Path $QtBin 'mingw64\bin' }
if (-not (Test-Path $QtBin)) { throw "QtBin not found: $QtBin" }
$QtBin = (Resolve-Path $QtBin).Path
$env:PATH = "$QtBin;" + $env:PATH

function Info($m) { Write-Output ("[release] " + $m) }

# ---- Qt 大版本：决定 windeployqt 用哪个、产物叫什么、以及要不要跑 8.1 门禁 ----
if ($QtMajor -eq 0) {
    $QtMajor = 6
    $cache = Join-Path $RepoRoot "$BuildDir\CMakeCache.txt"
    if (Test-Path $cache) {
        $cacheText = Get-Content $cache -Raw -Encoding UTF8
        if ($cacheText -match 'Qt5_DIR:') { $QtMajor = 5 }
        elseif ($cacheText -match 'Qt6_DIR:') { $QtMajor = 6 }
    }
}
Info "qt major = $QtMajor (build dir: $BuildDir)"

# 部署工具名各发行版不统一：MSYS2 把 Qt5 的命名为 windeployqt-qt5.exe，
# Qt6 的是 windeployqt6.exe / windeployqt.exe。按候选列表探测，别写死一个名字。
$windeployCandidates = @('windeployqt6.exe', 'windeployqt.exe')
if ($QtMajor -eq 5) { $windeployCandidates = @('windeployqt-qt5.exe', 'windeployqt5.exe', 'windeployqt.exe') }
$windeployName = $windeployCandidates[0]
foreach ($c in $windeployCandidates) {
    if (Test-Path (Join-Path $QtBin $c)) { $windeployName = $c; break }
}

# 产物名带平台标记：两个分支的包要能在同一个 dist 目录里共存，不能都叫 win64
$platformTag = 'win64'
if ($QtMajor -eq 5) { $platformTag = 'win81-x64' }

# 前置检查：缺哪个直接说清楚，而不是等到中途报一句难懂的错
# 注意 ffmpeg.exe 只在 Qt6 主干（Win10+）上从 QtBin 取；win81 线用自建的静态版（见下）
$needInQtBin = @($windeployName, 'objdump.exe')
if ($QtMajor -eq 6) { $needInQtBin += 'ffmpeg.exe' }
foreach ($tool in $needInQtBin) {
    if (-not (Test-Path (Join-Path $QtBin $tool))) {
        throw "missing $tool in $QtBin（Qt6 需 mingw-w64-x86_64-qt6-base；Qt5 需 qt5-base + qt5-tools；另有 -binutils / -ffmpeg）"
    }
}

# ---- 版本号取自 CMakeLists.txt，避免版本号出现两个来源 ----
$cmake = Join-Path $RepoRoot 'CMakeLists.txt'
$version = (Select-String -Path $cmake -Pattern 'project\(\s*\w+\s+VERSION\s+([0-9.]+)').Matches[0].Groups[1].Value
if (-not $version) { throw "cannot read version from $cmake" }
Info "version = $version"

$exe = Join-Path $RepoRoot "$BuildDir\src\app\vtplay.exe"
if (-not (Test-Path $exe)) { throw "not built: $exe (run the build first)" }

$dist = Join-Path $RepoRoot 'dist'
$dest = Join-Path $dist "vtplay-$version-$platformTag"
if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
New-Item -ItemType Directory -Path $dest -Force | Out-Null
Copy-Item $exe $dest
Info "staged vtplay.exe"

$windeployqt = Join-Path $QtBin $windeployName
if (-not (Test-Path $windeployqt)) { throw "missing $windeployName in $QtBin" }

# windeployqt 会先解析 exe 的导入再决定部署什么。win81 线的 avcodec/avformat 来自自建共享库，
# 不在 QtBin 上——解析不到时它的表现是「拷完 Qt 那几个 DLL 就结束」：plugins\ 与 qml\ 整块缺失。
# 后果很隐蔽：门禁照样通过（没有坏导入），但包双击起不来（缺 qwindows.dll）。
# 所以把自建库目录放进 PATH，让 windeployqt 能顺着找到。
if ($QtMajor -eq 5) {
    $sharedBin = Join-Path $RepoRoot 'third_party\win81\ffmpeg-shared\bin'
    if (Test-Path $sharedBin) {
        $env:PATH = "$sharedBin;" + $env:PATH
        Info "windeployqt sees: $sharedBin"
    } else {
        Info "警告: 没找到 $sharedBin（windeployqt 可能只部署 Qt DLL，plugins/qml 会缺）"
    }
}

$deployArgs = @('--release', '--compiler-runtime', '--no-translations')
if ($QtMajor -eq 5) {
    # --no-angle 不是可选优化，是必须的：MSYS2 没装 angleproject（本构建也是刻意不带 ANGLE，
    # 见 design D4），windeployqt 在部署 ANGLE 这一步发现 libGLESv2.dll 不存在就**直接退出**，
    # 结果是 Qt 那几个 DLL 拷完了、plugins\ 与 QML 模块一个都没拷——包能过门禁却起不来。
    $deployArgs += '--no-angle'
}
$deployArgs += @('--qmldir', (Join-Path $RepoRoot 'src\app\qml'))
& $windeployqt @deployArgs (Join-Path $dest 'vtplay.exe') | Out-Null
Info "windeployqt done"

# 部署完整性自检：这几个是「缺了必然起不来」的东西，宁可在这里失败也不发包
$mustHave = @('platforms\qwindows.dll')
if ($QtMajor -eq 5) {
    # Qt5 的 windeployqt 把 QML 模块放在**根目录**（QtQuick\、QtQml\ …），不是 qml\。
    # 注意 Qt5 里 QtQuick 2.15 的宿主目录叫 QtQuick.2\（QtQuick\ 下面是 Controls 等子模块），
    # 所以判据要落在 QtQuick.2\qmldir 与 QtQuick\Controls.2\qmldir 上。
    $mustHave += @('Qt5Core.dll', 'QtQuick.2\qmldir', 'QtQuick\Controls.2\qmldir')
} else {
    $mustHave += @('Qt6Core.dll', 'qml\QtQuick\qmldir')
}
foreach ($rel in $mustHave) {
    if (-not (Test-Path (Join-Path $dest $rel))) {
        throw "windeployqt 部署不完整：缺 $rel —— 这种包双击不会有任何反应"
    }
}

foreach ($dll in 'libstdc++-6.dll', 'libgcc_s_seh-1.dll', 'libwinpthread-1.dll') {
    $dst = Join-Path $dest $dll
    if (-not (Test-Path $dst)) {
        Copy-Item (Join-Path $QtBin $dll) $dest
        Info "  fallback copied $dll"
    }
}

# ---- 随包 ffmpeg：两条构建线来源不同，不能混用 ----
# win81：必须是自建的静态单文件版。MSYS2 的共享构建依赖闭包里有三项硬导入 Win10 API
#   的 DLL（libSvtAv1Enc-4 → SetThreadDescription；librsvg / libdovi → ProcessPrng），
#   在 8.1 上导出会「进程起不来、连一行输出都没有」。缺自建版就直接失败，
#   **不回退**到 QtBin 里那个——回退等于把已知会在目标机上挂的东西打进包。
# 主干 Qt6：目标系统是 Win10 1809+，那几个 API 本来就存在，沿用 MSYS2 的构建。
if ($QtMajor -eq 5) {
    $ffmpeg = Join-Path $RepoRoot 'third_party\win81\ffmpeg.exe'
    if (-not (Test-Path $ffmpeg)) {
        throw "缺少 win81 用的 ffmpeg.exe: $ffmpeg`n先跑：powershell -File tools\release\build-ffmpeg.ps1"
    }
    Copy-Item $ffmpeg $dest
    # 来源记录随包走：这份 exe 是自己编的，没有 pacman 包可以"查户口"
    $stamp = Join-Path $RepoRoot 'third_party\win81\ffmpeg.BUILD.txt'
    if (Test-Path $stamp) { Copy-Item $stamp (Join-Path $dest 'ffmpeg.BUILD.txt') }
    Info "bundled ffmpeg.exe (self-built, static, win81)$(if (Test-Path $stamp) { ' + ffmpeg.BUILD.txt' })"
} else {
    $ffmpeg = Join-Path $QtBin 'ffmpeg.exe'
    if (Test-Path $ffmpeg) { Copy-Item $ffmpeg $dest; Info "bundled ffmpeg.exe" }
}

# Qml2Imports 两条线不同：Qt6 的 windeployqt 把 QML 模块放进 qml\，
# Qt5 的放在**根目录**（QtQuick\、QtQml\ …）。写错这一行，换台机器就报
# module "QtQuick" is not installed —— 而在开发机上因为读的是 MSYS2 前缀，看不出来。
$qmlImports = if ($QtMajor -eq 5) { '.' } else { 'qml' }
Set-Content -Path (Join-Path $dest 'qt.conf') `
    -Value "[Paths]`nPlugins = .`nQml2Imports = $qmlImports`n" -Encoding ASCII
Info "wrote qt.conf (Qml2Imports = $qmlImports)"

# ---- 依赖闭包：从包里每个二进制出发，反复解析导入直到不再新增 ----
$objdump = Join-Path $QtBin 'objdump.exe'
$sysDir  = Join-Path $env:WINDIR 'System32'

# 搜索顺序很重要：win81 线的 avcodec/avformat 来自自建的共享库，
# 必须排在 QtBin 前——否则会把 MSYS2 那一套（硬导入 librsvg/svt-av1）拷进来，
# 包里的 exe 与 dll 版本还不一致。
$depSources = @()
if ($QtMajor -eq 5) { $depSources += (Join-Path $RepoRoot 'third_party\win81\ffmpeg-shared\bin') }
$depSources += $QtBin

$copied = 0
for ($round = 1; $round -le 8; $round++) {
    $added = 0
    foreach ($b in (Get-ChildItem $dest -Recurse -Include '*.exe', '*.dll' -File)) {
        foreach ($line in (& $objdump -p $b.FullName 2>$null)) {
            if ($line -notmatch 'DLL Name:\s*(\S+)') { continue }
            $imp = $Matches[1].Trim()
            if (Test-Path (Join-Path $dest $imp)) { continue }
            if (Test-Path (Join-Path $sysDir $imp)) { continue }   # Windows 自带
            foreach ($d in $depSources) {
                $src = Join-Path $d $imp
                if (Test-Path $src) { Copy-Item $src $dest -Force; $added++; $copied++; break }
            }
        }
    }
    # 注意 ${round} 的花括号：写成 "$round:" 时 PowerShell 会把冒号当成驱动器引用而报错
    Info "dep closure round ${round}: +$added"
    if ($added -eq 0) { break }
}
Info "dep closure copied $copied file(s)"

# ---- 便携说明 ----
$readme = Join-Path $PSScriptRoot 'PORTABLE-README.txt'
if (Test-Path $readme) {
    (Get-Content $readme -Raw -Encoding UTF8).Replace('{VERSION}', $version) |
        Set-Content -Path (Join-Path $dest 'README.txt') -Encoding UTF8
    Info "wrote README.txt"
}

$zip = Join-Path $dist "vtplay-$version-$platformTag.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
# 优先用 Windows 自带的 bsdtar：比 Compress-Archive 快得多，且 1500+ 文件时
# 更不容易出问题（Compress-Archive 在大文件集上有已知的性能与内存问题）。
$tar = Join-Path $env:WINDIR 'System32\tar.exe'
if (Test-Path $tar) {
    & $tar -a -c -f $zip -C $dest .
    if ($LASTEXITCODE -ne 0) { throw "tar 打包失败，退出码 $LASTEXITCODE" }
} else {
    Info 'tar.exe 不可用，回退 Compress-Archive'
    Compress-Archive -Path (Join-Path $dest '*') -DestinationPath $zip -CompressionLevel Optimal
}
if (-not (Test-Path $zip)) { throw "zip 未生成: $zip" }
$sizeMb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Info "zip: $zip ($sizeMb MB)"
# ---- 依赖闭包门禁：只在 8.1 构建上强制 ----
# 主干 Qt6 面向 Win10 1809+，d3d12 / SetThreadDescription 在目标系统本来就有，不存在这个门禁。
# legacy/win81 必须过：这类问题在用户侧只表现为「双击没反应」，连日志都不会产生，
# 与其等用户反馈，不如在打包这一步就拦住。
if ($QtMajor -eq 5) {
    $gate = Join-Path $PSScriptRoot 'check-target-imports.ps1'
    if (-not (Test-Path $gate)) { throw '缺少 tools\release\check-target-imports.ps1，无法校验 8.1 依赖' }
    Info 'running import gate (win81)'
    & powershell -NoProfile -ExecutionPolicy Bypass -File $gate -Dir $dest -Target win81 -Objdump $objdump
    if ($LASTEXITCODE -ne 0) {
        throw '依赖闭包门禁未通过（见上方失败项）：该包在 Windows 8.1 上会在 main() 之前加载失败'
    }
    Info 'import gate passed'
}

Info "done. 发布前请先跑 tools\release\verify_package.ps1"
