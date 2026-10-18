<#
    自建 win81 用的单文件 ffmpeg.exe，并用依赖门禁验收它。

    用法：
        powershell -File tools\release\build-ffmpeg.ps1
        powershell -File tools\release\build-ffmpeg.ps1 -CacheDir D:\dev\cache -Jobs 16

    产物：
        third_party\win81\ffmpeg.exe        静态链接（x264/x265/dav1d/vidstab 全进 exe）
        third_party\win81\ffmpeg.BUILD.txt  来源 / 版本 / 编译器 / sha256 / configure 行

    导入表里只剩系统 DLL + libgcc_s_seh-1.dll（GCC 运行时，应用本来就随包带它）。

    为什么要有这个脚本，而不是直接拷 D:\dev\msys64\mingw64\bin\ffmpeg.exe：
    见 tools\release\build-ffmpeg.sh 顶部注释——MSYS2 那个构建的依赖闭包里有三项
    硬导入 Win10 才有的 API，导出功能在 Win8.1 上会「进程起不来且没有任何输出」。

    真正的构建逻辑在 .sh 里（configure 那串参数用 bash 写更不容易出错），
    这个脚本只负责：找到 bash、传路径、跑完再验一遍门禁。
#>
param(
    [string]$Msys2Root = 'D:\dev\msys64',
    [string]$CacheDir = 'D:\dev\cache',
    [int]$Jobs = 0,
    [string]$FfmpegVersion = '9.0.2'
)
$ErrorActionPreference = 'Stop'

if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    # MSYS2 工具会往 stderr 打进度信息（curl/tar 都是），pwsh 7 下会被当成错误
    $PSNativeCommandUseErrorActionPreference = $false
}

$bash = Join-Path $Msys2Root 'usr\bin\bash.exe'
if (-not (Test-Path $bash)) { throw "找不到 $bash（-Msys2Root 应指向 MSYS2 根目录）" }

$here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$sh = Join-Path $here 'build-ffmpeg.sh'
if (-not (Test-Path $sh)) { throw "找不到 $sh" }

# 路径要转成 MSYS2 形式（/d/dev/cache）。cygpath 就在刚校验过的这个 MSYS2 里。
function To-MsysPath([string]$p) {
    if (-not (Test-Path $p)) { New-Item -ItemType Directory -Path $p -Force | Out-Null }
    (& (Join-Path $Msys2Root 'usr\bin\cygpath.exe') -u (Resolve-Path $p).Path).Trim()
}

$cacheU = To-MsysPath $CacheDir
$outU   = To-MsysPath (Join-Path $here '..\..\third_party\win81')
$shU    = (& (Join-Path $Msys2Root 'usr\bin\cygpath.exe') -u $sh).Trim()
if ($Jobs -le 0) { $Jobs = [Environment]::ProcessorCount }

Write-Output "[build-ffmpeg] ffmpeg $FfmpegVersion -> $outU (cache: $cacheU, jobs: $Jobs)"

$env:FFMPEG_VERSION = $FfmpegVersion
$env:CACHE_DIR      = $cacheU
$env:OUT_DIR        = $outU
$env:JOBS           = "$Jobs"

& $bash -lc "bash '$shU'"
if ($LASTEXITCODE -ne 0) { throw "ffmpeg 构建失败（退出码 $LASTEXITCODE）—— 见上方日志路径" }

# ---- 门禁验收：把 ffmpeg.exe 单独放进一个空目录扫 ----
# 关键：必须是**空目录**。门禁的规则是「包内自带同名 DLL 即视为满足」，
# 若在 third_party\win81 里原地扫，漏了哪个 DLL 也会被这套豁免放过去。
$ffmpegWin = Join-Path (Join-Path $here '..\..\third_party\win81') 'ffmpeg.exe'
if (-not (Test-Path $ffmpegWin)) { throw "构建结束但没有产物: $ffmpegWin" }

$staged = Join-Path $env:TEMP ("vtplay-ffmpeg-gate-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $staged -Force | Out-Null
Copy-Item $ffmpegWin $staged -Force

$gate = Join-Path $here 'check-target-imports.ps1'
& powershell -NoProfile -ExecutionPolicy Bypass -File $gate -Dir $staged -Target win81
$gateCode = $LASTEXITCODE
Remove-Item $staged -Recurse -Force
if ($gateCode -ne 0) {
    throw '自建的 ffmpeg.exe 未通过 Win8.1 依赖门禁 —— 不要用它打包'
}

Write-Output '[build-ffmpeg] 完成：third_party\win81\ffmpeg.exe 已通过 Win8.1 依赖门禁'
