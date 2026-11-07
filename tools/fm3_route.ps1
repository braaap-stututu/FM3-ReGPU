<#
  fm3_route.ps1 - drive the hl360 baseline (fm3.exe) along a fixed route and
  capture comparable frames (screenshots + patch-0007 texture dumps).

  Dot-source it, then call the functions:
      . .\tools\fm3_route.ps1
      Start-Fm3 -Fresh            # deletes ONLY the route profile's save, then boots
      Send-Key Return             # keys reach the window via PostMessage (no focus needed)
      Save-Shot grid.png          # client-area screenshot into $Fm3Run
      Request-Dump grid           # one guest frame -> dumps\hl360\<label>\ (index + textures)
      Get-FrameStats              # last frame_stats lines from the current log
      Stop-Fm3
  Or run the whole route:  Invoke-Fm3Route  (see $Fm3Route below)

  Determinism: every run starts from an empty save for a dedicated profile
  (XUID $Fm3RouteXuid, passed on the command line, which outranks fm3.user.toml).
  Timing is wall-clock, so captures land on the same *screen* each run, not
  necessarily the same frame; use static moments (e.g. the start grid).

  Default SDK keyboard binds (mnk_mode=true): A=Space  B=Backspace
  Start=Return  Back=Tab  D-pad=Shift+Arrows  LStick=WASD  RT(throttle)=E  LT=Q
#>

$Fm3Root      = "G:\claude code projects\FM3-ReGPU recomp"
$Fm3Exe       = Join-Path $Fm3Root "reference\rexglue recomp reference\hl360-forza3\out\build\win-amd64-release\fm3.exe"
$Fm3UserRoot  = Join-Path $Fm3Root "reference\rexglue recomp reference\hl360-forza3\user"
$Fm3DumpRoot  = Join-Path $Fm3Root "dumps\hl360"
$Fm3Run       = Join-Path $Fm3Root "dumps\route"
$Fm3RouteXuid = "E0000BA5E11E0002"   # route-only profile; Start-Fm3 -Fresh deletes its save
$Fm3RouteName = "Route"

if (-not ("Fm3Win" -as [type])) {
  Add-Type -AssemblyName System.Drawing
  Add-Type @"
using System; using System.Runtime.InteropServices;
public static class Fm3Win {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
}
"@
}

$script:Fm3Proc = $null
$script:Fm3Hwnd = [IntPtr]::Zero
$script:Fm3Log  = $null

$Fm3Keys = @{
  Return = 0x0D; Space = 0x20; Backspace = 0x08; Tab = 0x09; Escape = 0x1B; Shift = 0x10
  Left = 0x25; Up = 0x26; Right = 0x27; Down = 0x28
}

function Get-Fm3VK([string]$key) {
  if ($Fm3Keys.ContainsKey($key)) { return $Fm3Keys[$key] }
  if ($key.Length -eq 1) { return [int][char]$key.ToUpper() }
  throw "unknown key '$key'"
}

function Send-KeyEvent([string]$key, [bool]$down) {
  $vk = Get-Fm3VK $key
  $sc = [Fm3Win]::MapVirtualKey([uint32]$vk, 0)
  $ext = if ($vk -ge 0x21 -and $vk -le 0x28) { 1 -shl 24 } else { 0 }
  if ($down) {
    $lp = [int64](1 -bor ($sc -shl 16) -bor $ext)
    [Fm3Win]::PostMessage($script:Fm3Hwnd, 0x100, [IntPtr]$vk, [IntPtr]$lp) | Out-Null
  } else {
    $lp = [int64](0xC0000001 -bor ($sc -shl 16) -bor $ext)
    [Fm3Win]::PostMessage($script:Fm3Hwnd, 0x101, [IntPtr]$vk, [IntPtr]$lp) | Out-Null
  }
}

# Tap a key (default 150 ms) or hold it with -Ms. "Shift+Up" style combos are supported.
function Send-Key([string]$key, [int]$Ms = 150) {
  $parts = $key -split '\+'
  foreach ($p in $parts) { Send-KeyEvent $p $true; Start-Sleep -Milliseconds 20 }
  Start-Sleep -Milliseconds $Ms
  [array]::Reverse($parts)
  foreach ($p in $parts) { Send-KeyEvent $p $false; Start-Sleep -Milliseconds 20 }
}

function Start-Fm3([switch]$Fresh, [int]$WaitWindowSec = 30) {
  if (Get-Process fm3 -ErrorAction SilentlyContinue) { throw "fm3.exe is already running" }
  if ($Fresh) {
    $save = Join-Path $Fm3UserRoot $Fm3RouteXuid
    if ((Split-Path $save -Leaf) -ne $Fm3RouteXuid) { throw "refusing to delete $save" }
    if (Test-Path $save) { Remove-Item -Recurse -Force $save }
  }
  New-Item -ItemType Directory -Force $Fm3Run, $Fm3DumpRoot | Out-Null
  $dir = Split-Path $Fm3Exe
  $before = @(Get-ChildItem (Join-Path $dir "logs\*.log") -ErrorAction SilentlyContinue | ForEach-Object Name)
  $script:Fm3Proc = Start-Process -FilePath $Fm3Exe -WorkingDirectory $dir -PassThru -ArgumentList @(
    "--profile_xuid=$Fm3RouteXuid", "--profile_name=$Fm3RouteName")
  $deadline = (Get-Date).AddSeconds($WaitWindowSec)
  while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 500
    $script:Fm3Proc.Refresh()
    if ($script:Fm3Proc.HasExited) { throw "fm3.exe exited early (0x{0:X8})" -f $script:Fm3Proc.ExitCode }
    if ($script:Fm3Proc.MainWindowHandle -ne [IntPtr]::Zero) { break }
  }
  $script:Fm3Hwnd = $script:Fm3Proc.MainWindowHandle
  if ($script:Fm3Hwnd -eq [IntPtr]::Zero) { throw "no fm3 window after $WaitWindowSec s" }
  $script:Fm3Log = Get-ChildItem (Join-Path $dir "logs\*.log") | Where-Object { $before -notcontains $_.Name } |
    Sort-Object LastWriteTime | Select-Object -Last 1
  "fm3 pid $($script:Fm3Proc.Id), log $($script:Fm3Log.Name), started $(Get-Date -Format HH:mm:ss)"
}

# Re-attach to an already running fm3.exe (e.g. from a new PowerShell session).
function Connect-Fm3 {
  $p = Get-Process fm3 -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $p) { throw "fm3.exe is not running" }
  $script:Fm3Proc = $p
  $script:Fm3Hwnd = $p.MainWindowHandle
  $script:Fm3Log = Get-ChildItem (Join-Path (Split-Path $Fm3Exe) "logs\*.log") | Sort-Object LastWriteTime | Select-Object -Last 1
  "attached to fm3 pid $($p.Id), log $($script:Fm3Log.Name)"
}

function Test-Fm3 {
  if (-not $script:Fm3Proc) { return "not started" }
  $script:Fm3Proc.Refresh()
  if ($script:Fm3Proc.HasExited) { "EXITED 0x{0:X8}" -f $script:Fm3Proc.ExitCode } else { "running" }
}

# Client-area capture via PrintWindow (works without focus; DPI-aware so it isn't cropped).
function Get-Fm3Bitmap {
  [Fm3Win]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
  $r = New-Object Fm3Win+RECT
  if (-not [Fm3Win]::GetClientRect($script:Fm3Hwnd, [ref]$r) -or $r.R -le 0 -or $r.B -le 0) { return $null }
  $bmp = New-Object System.Drawing.Bitmap $r.R, $r.B
  $g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
  [Fm3Win]::PrintWindow($script:Fm3Hwnd, $dc, 3) | Out-Null
  $g.ReleaseHdc($dc); $g.Dispose()
  $bmp
}

function Save-Shot([string]$file) {
  $bmp = Get-Fm3Bitmap
  if (-not $bmp) { return "no window ($(Test-Fm3))" }
  $path = Join-Path $Fm3Run $file
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
  $path
}

# In-race pause menu: its highlighted RESUME bar is solid red at x 86-312,
# y 202-230 (1280x720 client). Coordinates scale with the window size.
# Shape test (red inside the bar, not red just outside it), confirmed on two
# captures 300 ms apart: red car paint in the intro video moves, the menu doesn't.
function Test-PausedOnce {
  $bmp = Get-Fm3Bitmap
  if (-not $bmp) { return $false }
  $sx = $bmp.Width / 1280.0; $sy = $bmp.Height / 720.0
  function IsRed($x, $y) { $c = $bmp.GetPixel([int]($x * $sx), [int]($y * $sy)); $c.R -gt 170 -and $c.G -lt 100 -and $c.B -lt 90 }
  $hit = 0; $total = 0
  for ($y = 206; $y -le 226; $y += 5) {
    for ($x = 100; $x -le 300; $x += 20) { $total++; if (IsRed $x $y) { $hit++ } }
  }
  $outside = 0
  foreach ($p in @(@(60, 216), @(345, 216), @(200, 188), @(200, 244), @(120, 188), @(280, 244))) {
    if (IsRed $p[0] $p[1]) { $outside++ }
  }
  $bmp.Dispose()
  return ($hit -ge [int]($total * 0.8) -and $outside -eq 0)
}

function Test-Paused {
  if (-not (Test-PausedOnce)) { return $false }
  Start-Sleep -Milliseconds 300
  return (Test-PausedOnce)
}

# Press Return once per $IntervalMs until the pause menu shows (skips any
# cutscene on the way; the loading screen ignores it). Returns seconds taken.
function Wait-Paused([int]$TimeoutSec = 90, [int]$IntervalMs = 1000) {
  $t0 = Get-Date
  while (((Get-Date) - $t0).TotalSeconds -lt $TimeoutSec) {
    if (Test-Paused) { return [math]::Round(((Get-Date) - $t0).TotalSeconds, 1) }
    Send-Key Return
    Start-Sleep -Milliseconds $IntervalMs
  }
  throw "not paused after $TimeoutSec s ($(Test-Fm3))"
}

function Resume-Race([int]$Tries = 3) {
  for ($i = 0; $i -lt $Tries; $i++) {
    if (-not (Test-Paused)) { return $true }
    Send-Key Return; Start-Sleep -Milliseconds 800
  }
  return -not (Test-Paused)
}

# Capture one guest frame with the patch-0007 hook and rename it to <label>.
function Request-Dump([string]$Label, [int]$TimeoutSec = 15) {
  $existing = @(Get-ChildItem $Fm3DumpRoot -Directory -Filter "frame_*" -ErrorAction SilentlyContinue | ForEach-Object Name)
  New-Item -ItemType File -Force (Join-Path $Fm3DumpRoot "TRIGGER") | Out-Null
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 250
    $new = Get-ChildItem $Fm3DumpRoot -Directory -Filter "frame_*" | Where-Object { $existing -notcontains $_.Name } | Select-Object -First 1
    if ($new) {
      $idx = Join-Path $new.FullName "index.txt"
      if ((Test-Path $idx) -and (Select-String -Path $idx -Pattern "^# end frame" -Quiet)) {
        $target = Join-Path $Fm3DumpRoot $Label
        if (Test-Path $target) { Remove-Item -Recurse -Force $target }
        Rename-Item $new.FullName $Label
        $n = (Get-ChildItem $target -Filter "tex_*.bin").Count
        return "dump '$Label': $n textures, $((Select-String -Path (Join-Path $target 'index.txt') -Pattern '^# resolve').Count) resolves"
      }
    }
  }
  Remove-Item -Force (Join-Path $Fm3DumpRoot "TRIGGER") -ErrorAction SilentlyContinue
  "dump '$Label' timed out ($(Test-Fm3))"
}

function Get-FrameStats([int]$Last = 3) {
  if (-not $script:Fm3Log) { return }
  Get-Content $script:Fm3Log.FullName | Select-String "frame stats" | Select-Object -Last $Last | ForEach-Object {
    if ($_.Line -match '(\d\d:\d\d:\d\d).*= ([\d.]+) fps.*p99 ([\d.]+) ms.*>1/60s: (\d+)') {
      "{0} {1,5} fps p99 {2,6} ms late {3}" -f $matches[1], $matches[2], $matches[3], $matches[4] } }
}

function Stop-Fm3 {
  if ($script:Fm3Proc -and -not $script:Fm3Proc.HasExited) {
    $null = $script:Fm3Proc.CloseMainWindow()
    if (-not $script:Fm3Proc.WaitForExit(10000)) { Stop-Process -Id $script:Fm3Proc.Id -Force }
  }
  "stopped"
}

# The route: list of steps. Wait = seconds to sleep first; Key = key to tap;
# Shot / Dump = label to capture after the wait/key. Tuned by observation;
# see NOTES.md for what each step lands on.
#
# Fresh-profile boot flow (observed 2026-11-07): ESRB -> Turn 10 -> Microsoft ->
# Dolby -> R8 intro video -> controls/loading screen (~72 s) -> R8 car intro
# cutscene (Return skips) -> the intro race (R8 at Montserrat, lap 0/2, start
# grid). With warm shader caches it reaches the grid by ~80 s without input.
# In the race Return toggles the pause menu, which freezes the 3D scene: the
# most repeatable capture.
#
# Timed Return presses are NOT reliable past the title: Return both skips and
# toggles pause, so the route pauses state-aware (Wait-Paused / Test-Paused).
function Invoke-Fm3Route([string]$RunLabel = "run", [switch]$NoStart, [switch]$KeepRunning) {
  if (-not $NoStart) { Start-Fm3 -Fresh }
  $runDir = Join-Path $Fm3DumpRoot $RunLabel
  if (Test-Path $runDir) { Remove-Item -Recurse -Force $runDir }
  New-Item -ItemType Directory $runDir | Out-Null
  function Capture([string]$name, [switch]$Dump) {
    Save-Shot "$name.png" | Out-Null
    Move-Item -Force (Join-Path $Fm3Run "$name.png") (Join-Path $runDir "$name.png")
    if ($Dump) { $r = Request-Dump $name -TimeoutSec 90; Move-Item (Join-Path $Fm3DumpRoot $name) $runDir -ErrorAction SilentlyContinue; $r }
  }
  Start-Sleep -Seconds 60                                    # boot logos + intro video
  # Return/s until paused; a pause menu can also appear before the race (then the
  # game moves on into a load), so re-confirm after 2 s and start over if gone.
  $stable = $false
  for ($attempt = 1; $attempt -le 5 -and -not $stable; $attempt++) {
    $t = Wait-Paused -TimeoutSec 120
    Start-Sleep -Seconds 2
    $stable = Test-Paused
    "pause attempt ${attempt}: detected after $t s, still paused 2 s later: $stable"
  }
  if (-not $stable) { "no stable pause; aborting"; Stop-Fm3; return }
  $r = Capture "r01_grid_paused" -Dump                       # frozen start-grid scene (most repeatable)
  $r
  if ($r -match ": (\d+) textures" -and [int]$matches[1] -lt 100) { "WARNING: r01 capture has only $($matches[1]) textures - not the race scene" }
  if (-not (Resume-Race)) { "could not resume" }
  Start-Sleep -Seconds 10
  Capture "r02_race_t10" -Dump                               # ~10 s of live racing (approximate)
  Get-FrameStats
  if (-not $KeepRunning) { Stop-Fm3 }
  "captures in $runDir"
}
