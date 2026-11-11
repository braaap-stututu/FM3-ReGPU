param([string]$Out, [string[]]$Files)
Add-Type -AssemblyName System.Drawing
$w = 640; $h = 360; $cols = 2
$rows = [math]::Ceiling($Files.Count / $cols)
$bmp = New-Object System.Drawing.Bitmap ($w * $cols), ($h * $rows)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = 'HighQualityBicubic'
$font = New-Object System.Drawing.Font 'Arial', 14
for ($i = 0; $i -lt $Files.Count; $i++) {
    $img = [System.Drawing.Image]::FromFile($Files[$i])
    $x = ($i % $cols) * $w; $y = [math]::Floor($i / $cols) * $h
    $g.DrawImage($img, $x, $y, $w, $h)
    $g.FillRectangle([System.Drawing.Brushes]::Black, $x, $y, 120, 22)
    $g.DrawString([IO.Path]::GetFileNameWithoutExtension($Files[$i]), $font, [System.Drawing.Brushes]::Yellow, $x + 2, $y + 1)
    $img.Dispose()
}
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
$Out
