<#
  fm3ctl.ps1 - drive our own build (build\rel\fm3.exe) through ReGPU's debug server.
  Own code (FM3 project); the protocol is ReGPU's (src/debug.cpp): one text command per line on
  TCP 127.0.0.1:7341 (REGPU_DEBUG=1), one JSON reply per line.

      . .\tools\fm3ctl.ps1
      Start-Fm3Dev [-Fresh]              # REGPU_DEBUG=1, FM3_USER_DIR=out\user-route (-Fresh empties its profiles), log in out\run
      Fm3 "press A"                      # also: hold/release <A+B..>, stick l|r x y ms, trigger l|r v ms
      Fm3 "shot G:\...\x.png"            # next frame as PNG (absolute path; relative = game's working dir)
      Fm3 "status" ; Fm3 "frametimes" ; Fm3 "log 20" ; Fm3 "capture" (F12-style trace into out\run\out\capture)
      Stop-Fm3Dev

  Buttons: UP DOWN LEFT RIGHT START BACK LS RS LB RB A B X Y (join with +).
#>
$Fm3Root  = "G:\claude code projects\FM3-ReGPU recomp"
$Fm3DevExe  = Join-Path $Fm3Root "build\rel\fm3.exe"
$Fm3DevGame = Join-Path $Fm3Root "reference\rexglue recomp reference\hl360-forza3\game\disc1"
$Fm3DevRun  = Join-Path $Fm3Root "out\run"
$Fm3DevUser = Join-Path $Fm3Root "out\user-route"   # scripted runs only; manual runs use out\user
$Fm3Shots   = Join-Path $Fm3Root "out\shots"

function Start-Fm3Dev([switch]$Fresh, [hashtable]$Env = @{}) {
  New-Item -ItemType Directory -Force $Fm3DevRun, $Fm3DevUser, $Fm3Shots | Out-Null
  if ($Fresh -and (Test-Path (Join-Path $Fm3DevUser "profiles"))) { Remove-Item -Recurse -Force (Join-Path $Fm3DevUser "profiles") }
  $env:FM3_USER_DIR = $Fm3DevUser
  $env:REGPU_DEBUG = "1"
  foreach ($k in $Env.Keys) { Set-Item "env:$k" $Env[$k] }
  $script:Fm3DevProc = Start-Process -FilePath $Fm3DevExe -ArgumentList "`"$Fm3DevGame`"" -WorkingDirectory $Fm3DevRun -PassThru
  foreach ($k in $Env.Keys) { Remove-Item "env:$k" -ErrorAction SilentlyContinue }
  # Wait for the debug server.
  for ($i = 0; $i -lt 100; $i++) {
    try { $r = Fm3 "ping"; if ($r -match '"ok":true') { return "fm3 pid $($script:Fm3DevProc.Id): debug server up" } } catch {}
    Start-Sleep -Milliseconds 200
  }
  throw "debug server did not answer"
}

function Fm3([string]$Command, [int]$TimeoutMs = 70000) {
  if (-not $script:Fm3Sock -or -not $script:Fm3Sock.Connected) {
    $script:Fm3Sock = New-Object System.Net.Sockets.TcpClient
    $script:Fm3Sock.Connect("127.0.0.1", 7341)
    $script:Fm3Sock.ReceiveTimeout = $TimeoutMs
    $s = $script:Fm3Sock.GetStream()
    $script:Fm3Writer = New-Object System.IO.StreamWriter($s); $script:Fm3Writer.AutoFlush = $true
    $script:Fm3Reader = New-Object System.IO.StreamReader($s)
  }
  $script:Fm3Writer.Write("$Command`n")
  $script:Fm3Reader.ReadLine()
}

function Save-Fm3Shot([string]$Name) {
  $path = Join-Path $Fm3Shots $Name
  $r = Fm3 "shot $path"
  if ($r -match '"ok":true') { $path } else { "shot failed: $r" }
}

function Stop-Fm3Dev {
  if ($script:Fm3Sock) { try { $script:Fm3Sock.Close() } catch {}; $script:Fm3Sock = $null }
  if ($script:Fm3DevProc -and -not $script:Fm3DevProc.HasExited) { Stop-Process -Id $script:Fm3DevProc.Id -Force }
  Remove-Item env:REGPU_DEBUG -ErrorAction SilentlyContinue
}
