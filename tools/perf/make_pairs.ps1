<#
  make_pairs.ps1 - picture assets for a comparison report (session 23).
    tools\perf\make_pairs.ps1 -Dir out\perf\s23 -Pairs pairs.txt -OutDir report
  pairs.txt: one line per comparison, "key|A.png|B.png" (paths relative to -Dir). For each line it writes
  <OutDir>\<key>-a.jpg, -b.jpg (previews, full size), -d.png (B's pixels dimmed to grey, red where A and B differ by
  more than 8 in a channel) and prints "key|changed|total|worst" to <OutDir>\pairs-stats.txt.
#>
param([string]$Dir, [string]$Pairs, [string]$OutDir = 'report', [int]$Threshold = 8)
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class PairDiff
{
    static byte[] Bytes(Bitmap b, out int stride)
    {
        var d = b.LockBits(new Rectangle(0, 0, b.Width, b.Height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        stride = d.Stride;
        var a = new byte[d.Stride * b.Height];
        Marshal.Copy(d.Scan0, a, 0, a.Length);
        b.UnlockBits(d);
        return a;
    }
    // Returns changed, worst; writes the difference picture.
    public static long[] Diff(string pa, string pb, string outPath, int thr)
    {
        using (var a = new Bitmap(pa))
        using (var b = new Bitmap(pb))
        {
            if (a.Width != b.Width || a.Height != b.Height) return new long[] { -1, -1 };
            int sa, sb;
            var x = Bytes(a, out sa);
            var y = Bytes(b, out sb);
            var o = new byte[x.Length];
            long changed = 0, worst = 0;
            for (int i = 0; i < x.Length; i += 4)
            {
                int d = Math.Max(Math.Abs(x[i] - y[i]), Math.Max(Math.Abs(x[i + 1] - y[i + 1]), Math.Abs(x[i + 2] - y[i + 2])));
                if (d > worst) worst = d;
                if (d > thr)
                {
                    changed++;
                    o[i] = 0; o[i + 1] = 0; o[i + 2] = 255;
                }
                else
                {
                    byte g = (byte)((y[i] + y[i + 1] + y[i + 2]) / 10);
                    o[i] = g; o[i + 1] = g; o[i + 2] = g;
                }
                o[i + 3] = 255;
            }
            using (var r = new Bitmap(a.Width, a.Height, PixelFormat.Format32bppArgb))
            {
                var d2 = r.LockBits(new Rectangle(0, 0, r.Width, r.Height), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
                Marshal.Copy(o, 0, d2.Scan0, o.Length);
                r.UnlockBits(d2);
                r.Save(outPath, ImageFormat.Png);
            }
            return new long[] { changed, worst };
        }
    }
    public static void Jpeg(string src, string dst, long quality)
    {
        using (var b = new Bitmap(src))
        {
            ImageCodecInfo jpg = null;
            foreach (var c in ImageCodecInfo.GetImageEncoders()) if (c.MimeType == "image/jpeg") jpg = c;
            var p = new EncoderParameters(1);
            p.Param[0] = new EncoderParameter(System.Drawing.Imaging.Encoder.Quality, quality);
            b.Save(dst, jpg, p);
        }
    }
}
"@
$out = Join-Path $Dir $OutDir
New-Item -ItemType Directory -Force $out | Out-Null
$stats = @()
foreach ($line in Get-Content (Join-Path $Dir $Pairs)) {
    if (-not $line.Trim() -or $line.StartsWith('#')) { continue }
    $key, $a, $b = $line.Split('|')
    $pa = Join-Path $Dir $a; $pb = Join-Path $Dir $b
    if (-not (Test-Path $pa) -or -not (Test-Path $pb)) { "missing: $line"; continue }
    foreach ($s in @(@($pa, "$key-a.jpg"), @($pb, "$key-b.jpg"))) {
        $dst = Join-Path $out $s[1]
        if (-not (Test-Path $dst)) { [PairDiff]::Jpeg($s[0], $dst, 90) }
    }
    $r = [PairDiff]::Diff($pa, $pb, (Join-Path $out "$key-d.png"), $Threshold)
    $img = [System.Drawing.Image]::FromFile($pa); $total = $img.Width * $img.Height; $img.Dispose()
    $stats += "$key|$($r[0])|$total|$($r[1])"
    "$key : $($r[0]) of $total changed, worst $($r[1])"
}
$stats | Set-Content -Encoding utf8 (Join-Path $out 'pairs-stats.txt')
