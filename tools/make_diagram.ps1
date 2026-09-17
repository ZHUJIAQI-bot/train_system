Add-Type -AssemblyName System.Drawing

# ---------- 画布 ----------
$W = 1020; $H = 592
$bmp = New-Object System.Drawing.Bitmap($W, $H)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$g.Clear([System.Drawing.Color]::White)

# ---------- 字体与画笔 ----------
$FONT = "Microsoft YaHei"
$fH1   = New-Object System.Drawing.Font($FONT, 20, [System.Drawing.FontStyle]::Bold,    [System.Drawing.GraphicsUnit]::Pixel)
$fH2   = New-Object System.Drawing.Font($FONT, 17, [System.Drawing.FontStyle]::Bold,    [System.Drawing.GraphicsUnit]::Pixel)
$fBody = New-Object System.Drawing.Font($FONT, 16, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$fMono = New-Object System.Drawing.Font("Consolas",   16, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)

$cText  = [System.Drawing.Color]::FromArgb(24, 39, 54)
$cMuted = [System.Drawing.Color]::FromArgb(91, 112, 132)
$cBlue  = [System.Drawing.Color]::FromArgb(54, 126, 190)
$cBox   = [System.Drawing.Color]::FromArgb(211, 225, 237)
$cSoft  = [System.Drawing.Color]::FromArgb(241, 247, 252)

$bText  = New-Object System.Drawing.SolidBrush($cText)
$bMuted = New-Object System.Drawing.SolidBrush($cMuted)
$bBlue  = New-Object System.Drawing.SolidBrush($cBlue)
$bSoft  = New-Object System.Drawing.SolidBrush($cSoft)
$pLine  = New-Object System.Drawing.Pen($cBlue, 2)
$pBox   = New-Object System.Drawing.Pen($cBox, 2)
$pBoxB  = New-Object System.Drawing.Pen($cBlue, 2)

function DrawCenter($text, $font, $brush, $cx, $y) {
    $sz = $g.MeasureString($text, $font)
    $g.DrawString($text, $font, $brush, $cx - $sz.Width / 2, $y)
}

# 参数显式声明成 float：否则 PowerShell 传进来的可能是 Object[]，
# 后面做减法会报「不包含 op_Subtraction 方法」，而且 FillPolygon 会因
# 数组元素类型不对而重载不明确。
# PointF 数组也必须强类型，不能靠 @() 推断。

# 向上箭头，箭头画在 yTo 那端
function DrawUpArrow([float]$x, [float]$yFrom, [float]$yTo) {
    $g.DrawLine($pLine, $x, $yFrom, $x, ($yTo + 11))
    $pts = [System.Drawing.PointF[]]@(
        (New-Object System.Drawing.PointF($x,       $yTo)),
        (New-Object System.Drawing.PointF(($x - 7), ($yTo + 12))),
        (New-Object System.Drawing.PointF(($x + 7), ($yTo + 12)))
    )
    $g.FillPolygon($bBlue, $pts)
}

# 向右箭头，箭头画在 xTo 那端
function DrawRightArrow([float]$xFrom, [float]$xTo, [float]$y) {
    $g.DrawLine($pLine, $xFrom, $y, ($xTo - 11), $y)
    $pts = [System.Drawing.PointF[]]@(
        (New-Object System.Drawing.PointF($xTo,        $y)),
        (New-Object System.Drawing.PointF(($xTo - 12), ($y - 7))),
        (New-Object System.Drawing.PointF(($xTo - 12), ($y + 7)))
    )
    $g.FillPolygon($bBlue, $pts)
}

# ================= 第 1 层：主存储（单链表） =================
$g.DrawString("主存储（单链表）", $fH1, $bText, 34, 22)

$chainY = 96
$boxH   = 46
$boxW   = 108
$midY   = $chainY + $boxH / 2

$g.DrawString("head", $fMono, $bText, 34, $midY - 11)
DrawRightArrow 96 158 $midY

$labels  = @("旅客1", "旅客2", "旅客3")
$centers = @()
$x = 162
foreach ($lb in $labels) {
    $g.FillRectangle($bSoft, $x, $chainY, $boxW, $boxH)
    $g.DrawRectangle($pBox, $x, $chainY, $boxW, $boxH)
    DrawCenter $lb $fBody $bText ($x + $boxW / 2) ($midY - 11)
    $centers += ($x + $boxW / 2)
    $x += $boxW
    DrawRightArrow $x ($x + 46) $midY
    $x += 52
}

DrawCenter "..." $fH1 $bMuted ($x + 18) ($midY - 14)
$x += 40
DrawRightArrow $x ($x + 46) $midY
$x += 52
$g.DrawString("NULL", $fMono, $bMuted, $x, $midY - 11)

# ================= 第 2 层：说明框 =================
$bracketTop = 300
$bracketH   = 88
$bracketL   = 74
$bracketR   = 812

foreach ($cx in $centers) { DrawUpArrow $cx ($bracketTop - 8) ($chainY + $boxH + 10) }

$g.FillRectangle($bSoft, $bracketL, $bracketTop, ($bracketR - $bracketL), $bracketH)
$g.DrawRectangle($pBoxB, $bracketL, $bracketTop, ($bracketR - $bracketL), $bracketH)
DrawCenter "两棵索引的结点都不另存旅客数据，" $fBody $bText (($bracketL + $bracketR) / 2) ($bracketTop + 15)
DrawCenter "而是用指针直接指向链表中的原结点" $fBody $bText (($bracketL + $bracketR) / 2) ($bracketTop + 45)

# ================= 第 3 层：两棵索引 =================
$idxTop = $bracketTop + $bracketH + 62
DrawUpArrow 240 ($idxTop - 14) ($bracketTop + $bracketH + 6)
DrawUpArrow 706 ($idxTop - 14) ($bracketTop + $bracketH + 6)

$g.DrawString("索引1：二叉搜索树", $fH2, $bBlue, 74, $idxTop)
$g.DrawString("SearchTreeNode *search_root", $fMono, $bText, 74, $idxTop + 30)
$g.DrawString("按身份证后 4 位排序", $fBody, $bMuted, 74, $idxTop + 58)
$g.DrawString("data  → 链表中的原结点", $fBody, $bText, 74, $idxTop + 84)

$g.DrawString("索引2：B 树", $fH2, $bBlue, 540, $idxTop)
$g.DrawString("BTreeNode *btree_root", $fMono, $bText, 540, $idxTop + 30)
$g.DrawString("按身份证后 4 位排序", $fBody, $bMuted, 540, $idxTop + 58)
$g.DrawString("values  → 链表中的原结点", $fBody, $bText, 540, $idxTop + 84)

# ================= 保存 =================
# 注意：用 [scriptblock]::Create 方式执行时 $PSScriptRoot 是空的，这里写绝对路径
$out = "D:\train_system\output\链表与索引关系图.png"
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Output "saved: $out"
