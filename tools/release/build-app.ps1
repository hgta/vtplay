<#
    配置 + 构建应用本体（默认 build-win81，即 8.1 那条 Qt5 线）。

    用法：
        powershell -File tools\release\build-app.ps1
        powershell -File tools\release\build-app.ps1 -BuildDir build -Clean

    为什么需要它（而不是直接照 README 手敲 cmake）：
        8.1 这条线链接的 FFmpeg **不是** MSYS2 那个，而是 tools\release\build-ffmpeg.ps1
        自建的共享库（third_party\win81\ffmpeg-shared）。原因见 build-ffmpeg.sh 第 6 节：
        MSYS2 的 avcodec-63.dll 硬导入 librsvg-2-2.dll 与 libSvtAv1Enc-4.dll，
        而这两个在 Win8.1 上会因为缺 ProcessPrng / SetThreadDescription 加载失败——
        应用在 main() 之前就挂。这一步把 PKG_CONFIG_PATH 指过去，让 CMake 链对。

    找不到自建库时不报错、只警告：主干（Win10+）本来就该用 MSYS2 那套。
#>
param(
    [string]$BuildDir = 'build-win81',
    [string]$Msys2Root = 'D:\dev\msys64',
    [string]$RepoRoot = '',
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'

if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

if (-not $RepoRoot) {
    $here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
    $RepoRoot = (Resolve-Path (Join-Path $here '..\..')).Path
}

$mingw = Join-Path $Msys2Root 'mingw64'
$bin   = Join-Path $mingw 'bin'
foreach ($tool in 'cmake.exe', 'ninja.exe', 'g++.exe', 'pkg-config.exe') {
    if (-not (Test-Path (Join-Path $bin $tool))) { throw "缺少 $bin\$tool" }
}
$env:PATH = "$bin;" + $env:PATH

# ---- FFmpeg 来源：有自建共享库就用它，没有就退回 MSYS2（并说明后果）----
$ffShared = Join-Path $RepoRoot 'third_party\win81\ffmpeg-shared'
$pcDir    = Join-Path $ffShared 'lib\pkgconfig'
if (Test-Path $pcDir) {
    # pkg-config（mingw 原生）接受 ';' 分隔的 Windows 路径
    $env:PKG_CONFIG_PATH = $pcDir
    Write-Output "[build-app] FFmpeg: 自建共享库 $ffShared"
} else {
    Remove-Item Env:\PKG_CONFIG_PATH -ErrorAction SilentlyContinue
    Write-Output "[build-app] FFmpeg: MSYS2 的 mingw-w64-x86_64-ffmpeg（未找到自建共享库）"
    Write-Output '[build-app]   注意：MSYS2 的 avcodec 硬导入 librsvg / libSvtAv1Enc，'
    Write-Output '[build-app]   Win8.1 上会在 main() 之前加载失败。目标系统是 10/11 才无所谓。'
}

$dest = Join-Path $RepoRoot $BuildDir
if ($Clean -and (Test-Path $dest)) { Remove-Item $dest -Recurse -Force }

# 换过 FFmpeg 前缀后必须重新探测：pkg-config 的结果不进缓存，但 -L 路径会留在 ninja 文件里，
# 所以改前缀时用 -Clean 重配最稳（否则可能链到旧的 libavcodec.dll.a 而不自知）。
& cmake -S $RepoRoot -B $dest -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_PREFIX_PATH="$($mingw -replace '\\','/')"
if ($LASTEXITCODE -ne 0) { throw 'cmake 配置失败' }

& cmake --build $dest
if ($LASTEXITCODE -ne 0) { throw 'cmake 构建失败' }

$exe = Join-Path $dest 'src\app\vtplay.exe'
if (-not (Test-Path $exe)) { throw "没有产出 $exe" }
Write-Output "[build-app] 完成: $exe"
