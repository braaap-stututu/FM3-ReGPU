# Crops the same region from several screenshots into one labelled strip (session 23).
#   tools\perf\crops.ps1 OUT.png x y w h scale A.png B.png ...
param([string]$Out, [int]$X, [int]$Y, [int]$W, [int]$H, [int]$Scale, [Parameter(ValueFromRemainingArguments)] [string[]]$Files)
Add-Type -AssemblyName System.Drawing
$cw = $W * $Scale; $chh = $H * $Scale
$bmp = New-Object System.Drawing.Bitmap $cw, ($chh * $Files.Count)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = 'NearestNeighbor'
$font = New-Object System.Drawing.Font 'Arial', 13
for ($i = 0; $i -lt $Files.Count; $i++) {
    $img = [System.Drawing.Image]::FromFile($Files[$i])
    $g.DrawImage($img, (New-Object System.Drawing.Rectangle 0, ($i * $chh), $cw, $chh), (New-Object System.Drawing.Rectangle $X, $Y, $W, $H), 'Pixel')
    $g.FillRectangle([System.Drawing.Brushes]::Black, 0, $i * $chh, 330, 20)
    $g.DrawString([IO.Path]::GetFileNameWithoutExtension($Files[$i]), $font, [System.Drawing.Brushes]::Yellow, 2, $i * $chh + 1)
    $img.Dispose()
}
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose(); $Out
