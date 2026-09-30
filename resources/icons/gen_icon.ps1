# ============================================================================
#  vtplay 应用图标生成器
#
#  用法（在仓库任意位置执行）：
#     powershell -NoProfile -ExecutionPolicy Bypass -File resources\icons\gen_icon.ps1
#
#  输入：source-logo.png（品牌素材「展示图」）
#  输出：vtplay_<16|24|32|48|64|128|256>.png + vtplay.ico（多尺寸）
#
#  为什么不直接使用素材：
#    1) 素材是 AI 展示图：荧光绿背景 + 投影 + "豆包AI生成" 水印
#    2) 素材含 "VTPlay" 文字，16x16 下必然糊成噪点；应用图标必须能在小尺寸辨识
#  因此：裁剪出深色圆角方块内的图形，用颜色键把深色底变透明，再以 8 倍超采样
#  重新合成到圆角深色方块上并降采样。
#
#  注意（踩过的坑，见 openspec/changes/add-brand-assets/tasks.md）：
#   - 背景亮绿与品牌色 #D4FF00 色相接近，颜色法无法区分，必须按几何裁剪
#   - GDI+ ColorMatrix 约定：Matrix[源通道*5 + 目标通道]，偏移量在最后一行。
#     「G -> A」是 Matrix13，alpha 偏移是 Matrix43；写成 Matrix31 会变成 B -> G 而发绿
#   - 颜色键必须把 Matrix33（A -> A）置 0，否则素材的不透明 alpha 会让一切保持可见
#   - 本脚本保持纯 ASCII：Windows PowerShell 会按 GBK 读取 .ps1，中文可能吞掉引号
# ============================================================================

param(
    [string]$Source,
    [string]$OutDir
)

Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)   # 仓库根
if (-not $Source) { $Source = Join-Path $PSScriptRoot 'source-logo.png' }
if (-not $OutDir) { $OutDir = $PSScriptRoot }

if (-not (Test-Path $Source)) { Write-Error "source not found: $Source"; exit 1 }
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

$bmp = [System.Drawing.Bitmap]::FromFile($Source)
Write-Output ("source: {0}  {1}x{2}" -f $Source, $bmp.Width, $bmp.Height)

# --- 素材 2048x2048 中深色圆角方块的边界（探得值；若更换素材需重新测量）---
$sqL = 351; $sqT = 355; $sqW = 1345; $sqH = 1356

# --- 图形（V + 播放三角）区域：水平居中、方块上部，刻意排除下方文字 ---
$gx = $sqL + [int]($sqW * 0.18)
$gy = $sqT + [int]($sqH * 0.15)
$gw = [int]($sqW * 0.64)
$gh = [int]($sqH * 0.50)
Write-Output ("glyph crop: $gx,$gy ${gw}x${gh}")

$glyph = New-Object System.Drawing.Bitmap($gw, $gh)
$gg = [System.Drawing.Graphics]::FromImage($glyph)
$gg.DrawImage($bmp, (New-Object System.Drawing.Rectangle(0, 0, $gw, $gh)),
              (New-Object System.Drawing.Rectangle($gx, $gy, $gw, $gh)),
              [System.Drawing.GraphicsUnit]::Pixel)
$gg.Dispose()

# --- 图标底色：取方块内部（避开圆角与图形）的平整色 ---
$bgPix = $bmp.GetPixel($sqL + 120, $sqT + 120)
Write-Output ("icon background: #{0:X2}{1:X2}{2:X2}" -f $bgPix.R, $bgPix.G, $bgPix.B)

# --- 颜色键：深色底 -> 透明（alpha = 2.55*G - 0.40，G 归一化 0..1）---
$ia = New-Object System.Drawing.Imaging.ImageAttributes
$cm = New-Object System.Drawing.Imaging.ColorMatrix
$cm.Matrix00 = 1.0     # R -> R
$cm.Matrix11 = 1.0     # G -> G
$cm.Matrix22 = 1.0     # B -> B
$cm.Matrix33 = 0.0     # A -> A:必须为 0
$cm.Matrix13 = 2.55    # G -> A
$cm.Matrix43 = -0.40   # alpha 偏移
$ia.SetColorMatrix($cm)

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$pngs  = @()
$SS    = 8   # 超采样倍数

foreach ($S in $sizes) {
    $big = $S * $SS
    $canvas = New-Object System.Drawing.Bitmap($big, $big)
    $g = [System.Drawing.Graphics]::FromImage($canvas)
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality

    # 圆角方块（圆角半径比例与素材一致）
    $radius = [int]($big * 0.2237)
    $d = $radius * 2
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc(0, 0, $d, $d, 180, 90)
    $path.AddArc($big - $d, 0, $d, $d, 270, 90)
    $path.AddArc($big - $d, $big - $d, $d, $d, 0, 90)
    $path.AddArc(0, $big - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    $brush = New-Object System.Drawing.SolidBrush($bgPix)
    $g.FillPath($brush, $path)

    # 图形居中，占画布约 62%
    $target = [int]($big * 0.62)
    $scale  = [Math]::Min($target / $gw, $target / $gh)
    $dw = [int]($gw * $scale); $dh = [int]($gh * $scale)
    $dx = [int](($big - $dw) / 2); $dy = [int](($big - $dh) / 2)
    $destRect = New-Object System.Drawing.Rectangle($dx, $dy, $dw, $dh)
    $g.DrawImage($glyph, $destRect, 0, 0, $gw, $gh,
                 [System.Drawing.GraphicsUnit]::Pixel, $ia)

    $g.Dispose(); $brush.Dispose(); $path.Dispose()

    # 降采样到目标尺寸
    $final = New-Object System.Drawing.Bitmap($S, $S)
    $fg = [System.Drawing.Graphics]::FromImage($final)
    $fg.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $fg.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $fg.DrawImage($canvas, 0, 0, $S, $S)
    $fg.Dispose(); $canvas.Dispose()

    $p = Join-Path $OutDir ("vtplay_{0}.png" -f $S)
    $final.Save($p, [System.Drawing.Imaging.ImageFormat]::Png)
    $final.Dispose()
    $pngs += $p
    Write-Output ("  rendered ${S}x${S}")
}

# --- 打包多尺寸 ICO（PNG 载荷；Vista 起支持）---
$icoPath = Join-Path $OutDir 'vtplay.ico'
$fs = [System.IO.File]::Create($icoPath)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$sizes.Count)

$offset = 6 + 16 * $sizes.Count
$payloads = @()
foreach ($i in 0..($sizes.Count - 1)) {
    $bytes = [System.IO.File]::ReadAllBytes($pngs[$i])
    $payloads += ,$bytes
    $s = $sizes[$i]
    $dim = if ($s -ge 256) { 0 } else { $s }   # 0 表示 256
    $bw.Write([Byte]$dim); $bw.Write([Byte]$dim)
    $bw.Write([Byte]0); $bw.Write([Byte]0)
    $bw.Write([UInt16]1); $bw.Write([UInt16]32)
    $bw.Write([UInt32]$bytes.Length); $bw.Write([UInt32]$offset)
    $offset += $bytes.Length
}
foreach ($p in $payloads) { $bw.Write($p) }
$bw.Flush(); $bw.Dispose(); $fs.Dispose()

$ico = Get-Item $icoPath
Write-Output ("ICO: {0}  {1} bytes  ({2} sizes)" -f $ico.FullName, $ico.Length, $sizes.Count)

$bmp.Dispose(); $glyph.Dispose()
