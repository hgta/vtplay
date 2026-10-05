<#
    验证便携包真的自包含。

    判据分两层，都是「不依赖人工看画面」的硬证据：

    1) 日志里出现启动行 —— 只判「进程还活着」是不够的：缺 DLL 或缺 QML 模块时
       程序可能弹个错误对话框干等，进程照样存活，却不会写任何日志。
       第一次打包就是这样被骗过去的（当时其实是 main() 之前就失败了）。
    2) stderr 里出现 [audio] t=... status=Playing —— 证明解码器与音频输出真的跑起来了。
       只验证「界面起来了」不足以说明这是个能用的播放器。

    另外把 PATH 剥到只剩 System32，使它无法回退到开发机的工具链；
    否则在开发机上永远测不出缺 DLL。

    用法：
        powershell -File tools\release\verify_package.ps1 -Media D:\clip.mp4
#>
param(
    [string]$Dir,
    [Parameter(Mandatory = $true)][string]$Media,
    [string]$RepoRoot
)
# 与 make_release.ps1 同样的兜底：$PSScriptRoot 在某些宿主里为空
if (-not $RepoRoot) {
    $here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
    if (-not $here) { $here = (Get-Location).Path }
    $RepoRoot = (Resolve-Path (Join-Path $here '..\..')).Path
}
if (-not $Dir) {
    $cmake = Join-Path $RepoRoot 'CMakeLists.txt'
    $version = (Select-String -Path $cmake -Pattern 'project\(\s*\w+\s+VERSION\s+([0-9.]+)').Matches[0].Groups[1].Value
    $Dir = Join-Path $RepoRoot "dist\vtplay-$version-win64"
}

$exe = Join-Path $Dir 'vtplay.exe'
if (-not (Test-Path $exe)) { Write-Output "missing: $exe"; exit 1 }
if (-not (Test-Path $Media)) { Write-Output "test media not found: $Media"; exit 1 }

$log = Join-Path $env:LOCALAPPDATA 'VTPlay\VTPlay\vtplay.log'
if (Test-Path $log) { Remove-Item $log -Force }      # 清空，只读本次运行

$savedPath = $env:PATH
$env:PATH = "$env:WINDIR\System32;$env:WINDIR"
Write-Output ("PATH during test = " + $env:PATH)

$errFile = Join-Path $env:TEMP 'vtplay_pkg_verify.err'
if (Test-Path $errFile) { Remove-Item $errFile -Force }

$p = Start-Process -FilePath $exe -ArgumentList $Media -PassThru `
     -WorkingDirectory $Dir -RedirectStandardError $errFile
Start-Sleep -Seconds 12
$p.Refresh()
$alive = -not $p.HasExited
if ($alive) { Stop-Process -Id $p.Id -Force }
$env:PATH = $savedPath
Start-Sleep -Milliseconds 600

Write-Output ("process alive after 12s : " + $alive)

$started = $false
if (Test-Path $log) {
    $lines = @(Get-Content $log -Encoding UTF8)
    Write-Output ("log lines              : " + $lines.Count)
    $started = (@($lines | Select-String -Pattern '\[settings\] transcoder path=').Count -gt 0)
    $lines | Select-String -Pattern 'WARN|ERROR|not installed' |
        Select-Object -First 6 | ForEach-Object { Write-Output ("  ! " + $_.Line) }
} else {
    Write-Output 'log lines              : (no log file -- Qt init failed)'
}

$playing = $false
$audio = @(Get-Content $errFile -ErrorAction SilentlyContinue |
           Select-String -Pattern '\[audio\] t=' | ForEach-Object { $_.Line })
if ($audio.Count -gt 0) {
    $playing = @($audio | Select-String 'status=Playing').Count -gt 0
    $audio | Select-Object -Last 2 | ForEach-Object { Write-Output ("  " + $_) }
} else {
    Write-Output '  (no audio diagnostics -- playback did not start)'
}

Write-Output ''
Write-Output ("startup PASS : " + $started)
Write-Output ("play    PASS : " + $playing)
if ($started -and $playing) { Write-Output 'VERDICT: OK - portable package works standalone'; exit 0 }
Write-Output 'VERDICT: FAIL'
exit 1
