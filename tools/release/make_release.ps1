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
    [string]$BuildDir = 'build'
)
$ErrorActionPreference = 'Stop'

# $PSScriptRoot 在某些宿主里是空的（例如脚本被外层脚本再包一层执行时），
# 所以按「脚本目录 -> $MyInvocation -> 当前目录」逐级兜底。
if (-not $RepoRoot) {
    $here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
    if (-not $here) { $here = (Get-Location).Path }
    $RepoRoot = (Resolve-Path (Join-Path $here '..\..')).Path
}
$env:PATH = "$QtBin;" + $env:PATH

function Info($m) { Write-Output ("[release] " + $m) }

# ---- 版本号取自 CMakeLists.txt，避免版本号出现两个来源 ----
$cmake = Join-Path $RepoRoot 'CMakeLists.txt'
$version = (Select-String -Path $cmake -Pattern 'project\(\s*\w+\s+VERSION\s+([0-9.]+)').Matches[0].Groups[1].Value
if (-not $version) { throw "cannot read version from $cmake" }
Info "version = $version"

$exe = Join-Path $RepoRoot "$BuildDir\src\app\vtplay.exe"
if (-not (Test-Path $exe)) { throw "not built: $exe (run the build first)" }

$dist = Join-Path $RepoRoot 'dist'
$dest = Join-Path $dist "vtplay-$version-win64"
if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
New-Item -ItemType Directory -Path $dest -Force | Out-Null
Copy-Item $exe $dest
Info "staged vtplay.exe"

$windeployqt = Join-Path $QtBin 'windeployqt.exe'
if (-not (Test-Path $windeployqt)) { throw "missing windeployqt in $QtBin" }
& $windeployqt --release --compiler-runtime --no-translations `
    --qmldir (Join-Path $RepoRoot 'src\app\qml') (Join-Path $dest 'vtplay.exe') | Out-Null
Info "windeployqt done"

foreach ($dll in 'libstdc++-6.dll', 'libgcc_s_seh-1.dll', 'libwinpthread-1.dll') {
    $dst = Join-Path $dest $dll
    if (-not (Test-Path $dst)) {
        Copy-Item (Join-Path $QtBin $dll) $dest
        Info "  fallback copied $dll"
    }
}

$ffmpeg = Join-Path $QtBin 'ffmpeg.exe'
if (Test-Path $ffmpeg) { Copy-Item $ffmpeg $dest; Info "bundled ffmpeg.exe" }

Set-Content -Path (Join-Path $dest 'qt.conf') `
    -Value "[Paths]`nPlugins = .`nQml2Imports = qml`n" -Encoding ASCII
Info "wrote qt.conf"

# ---- 依赖闭包：从包里每个二进制出发，反复解析导入直到不再新增 ----
$objdump = Join-Path $QtBin 'objdump.exe'
$sysDir  = Join-Path $env:WINDIR 'System32'
$copied = 0
for ($round = 1; $round -le 8; $round++) {
    $added = 0
    foreach ($b in (Get-ChildItem $dest -Recurse -Include '*.exe', '*.dll' -File)) {
        foreach ($line in (& $objdump -p $b.FullName 2>$null)) {
            if ($line -notmatch 'DLL Name:\s*(\S+)') { continue }
            $imp = $Matches[1].Trim()
            if (Test-Path (Join-Path $dest $imp)) { continue }
            if (Test-Path (Join-Path $sysDir $imp)) { continue }   # Windows 自带
            $src = Join-Path $QtBin $imp
            if (Test-Path $src) { Copy-Item $src $dest -Force; $added++; $copied++ }
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

$zip = Join-Path $dist "vtplay-$version-win64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $dest '*') -DestinationPath $zip -CompressionLevel Optimal
$sizeMb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Info "zip: $zip ($sizeMb MB)"
Info "done. 发布前请先跑 tools\release\verify_package.ps1"
