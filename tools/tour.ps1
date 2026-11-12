<#
  tour.ps1 - shader/pipeline discovery tour (session 28): drives FM3 through Free Play quick races on every
  environment, so the pipelines each track, car, photo mode and post-race replay need are drawn once and land in
  <user>\pipelines.manifest (merge it into the seed afterwards: python -I tools/pipeline_seed.py out/user/pipelines.manifest).

      . .\tools\tour.ps1 [-Run name]
      Start-Tour                    # boot (minimized) to Free Play > Quick Race > Acura RSX, then every environment
      Start-Tour -From 10 -To 12    # only environments 10..12 (0 = Amalfi Coast ... 24 = Twin Ring Motegi)

  Every step is checked on a shot (tools/analysis/tour_state.py: mainmenu, assists, envselect, trackselect, menu,
  other); a step that doesn't land where expected is retried once, then the tour stops (shot "stuck-*") rather than
  pressing on in the wrong screen. Menu facts it relies on (FM3 retail, Baseline profile, Disc 2 installed):
  - Environment select lists 25 environments alphabetically and stops at both ends; the cursor stays where it was.
  - Track select: A = Go race (first layout). Pre-race menu over the track fly-over: Start race is the first item.
  - The pre-race and post-race menus hide after ~5 s idle; the next press only brings them back. LB wakes them and
    does nothing else there.
  - Pause menu: Resume, Restart, Photo mode, Audio, HUD, Rewind, Quit race (+ a confirm). Post-race menu: Continue
    (first) returns to Track select.
  The journal (out\perf\tour\<run>\journal.txt) gets the "seed coverage" log line at each stop.
#>
param([string]$Run = (Get-Date -Format 'yyyyMMdd-HHmm'))

$TourRoot = 'G:\claude code projects\FM3-ReGPU recomp'
. (Join-Path $TourRoot 'tools\fm3ctl.ps1')
$TourOut = Join-Path $TourRoot "out\perf\tour\$Run"
$TourLog = Join-Path $TourRoot 'out\run\fm3.log'
New-Item -ItemType Directory -Force $TourOut | Out-Null

$TourEnvs = 'Amalfi Coast', 'Benchmark', 'Camino Viejo', 'Catalunya', 'Fujimi Kaido', 'Iberian International',
    'Ladera Test Track', 'Le Mans', 'Maple Valley', 'Laguna Seca', 'Mugello', 'New York', 'Nurburgring GP',
    'Nordschleife', 'Rally di Positano', 'Road America', 'Road Atlanta', 'Sebring', 'Sedona', 'Sidewinder',
    'Silverstone', 'Sunset Peninsula', 'Suzuka', 'Tsukuba', 'Twin Ring Motegi'

function Tour-Note([string]$Text) {
    $line = '[{0}] {1}' -f (Get-Date -Format 'HH:mm:ss'), $Text
    $line
    Add-Content -Encoding utf8 (Join-Path $TourOut 'journal.txt') $line
}

function Tour-Conn { if ($script:Fm3Sock) { try { $script:Fm3Sock.Close() } catch {} }; $script:Fm3Sock = $null }

function Tour-Press([string]$Button, [double]$After = 0.7) {
    Fm3 "press $Button 150" | Out-Null
    Start-Sleep -Milliseconds ([int]($After * 1000))
}

function Tour-Shot([string]$Name) {
    $p = Join-Path $TourOut "$Name.png"
    Remove-Item $p -ErrorAction SilentlyContinue
    Fm3 "shot $p" | Out-Null
    for ($i = 0; $i -lt 50 -and -not (Test-Path $p); $i++) { Start-Sleep -Milliseconds 100 }
    Start-Sleep -Milliseconds 200
    $p
}

function Tour-State([string]$Name = 'state') {
    $p = Tour-Shot $Name
    (& python -I (Join-Path $TourRoot 'tools\analysis\tour_state.py') $p).Trim()
}

# Do $Action until the screen is $Want (checked after each try); $false (with a "stuck" shot) after $Tries.
function Tour-Expect([string]$Want, [scriptblock]$Action, [int]$Tries = 2, [string]$Label = $Want) {
    for ($t = 0; $t -lt $Tries; $t++) {
        & $Action
        $s = Tour-State
        if ($s -eq $Want) { return $true }
    }
    Copy-Item (Join-Path $TourOut 'state.png') (Join-Path $TourOut "stuck-$Label.png") -Force
    Tour-Note "stuck: wanted $Want ($Label), screen is $s" | Out-Host   # (Out-Host: a function's output is its return value)
    $false
}

function Tour-Coverage {
    $l = Select-String -Path $TourLog -Pattern 'seed coverage: \d' -ErrorAction SilentlyContinue | Select-Object -Last 1
    if ($l) { ($l.Line -replace '^.*seed coverage: ', '') } else { '(no coverage line yet)' }
}

# Boot (minimized, Baseline profile via play.cmd) and drive to Environment select with the Acura RSX.
function Start-TourBoot {
    if (Get-Process fm3 -ErrorAction SilentlyContinue) { throw 'fm3 is running: close it first' }
    $env:FM3_MINIMIZED = '1'
    Start-Process cmd.exe "/c `"$TourRoot\play.cmd`"" -WindowStyle Hidden
    Remove-Item env:FM3_MINIMIZED
    Start-Sleep 20
    Tour-Conn
    # Title screen / attract video: the pad is unreliable there (CLAUDE.md); A held until the main menu shows.
    if (-not (Tour-Expect mainmenu { Fm3 'press A 400' | Out-Null; Start-Sleep 5 } 8 'title')) { return $false }
    Tour-Press DOWN 1; Tour-Press A 4          # Free Play (Quick Race highlighted)
    if (-not (Tour-Expect envselect { Tour-Press A 5 } 1 'manufacturer')) { return $false }
    if (-not (Tour-Expect envselect { Tour-Press A 5 } 1 'car')) { return $false }
    if (-not (Tour-Expect assists { Tour-Press A 5 } 3 'assists')) { return $false }   # colour, then assists
    if (-not (Tour-Expect envselect { Tour-Press A 4 } 1 'envselect')) { return $false }
    $script:TourCursor = 0
    Tour-Note "boot done: environment select. Coverage: $(Tour-Coverage)" | Out-Host
    $true
}

# One stop: from Environment select at $script:TourCursor, move to $Index, race, back to Environment select.
function Invoke-TourStop([int]$Index) {
    $name = $TourEnvs[$Index]
    $moves = $Index - $script:TourCursor
    $btn = if ($moves -ge 0) { 'RIGHT' } else { 'LEFT' }
    for ($i = 0; $i -lt [math]::Abs($moves); $i++) { Tour-Press $btn 0.9 }
    $script:TourCursor = $Index
    Start-Sleep 1
    Tour-Shot ('{0:D2}-0-env' -f $Index) | Out-Null
    if (-not (Tour-Expect trackselect { Tour-Press A 4 } 2 "$Index-trackselect")) { return $false }
    Tour-Press A 2                              # Go race: loading, then the pre-race menu over the fly-over
    Start-Sleep 50
    Tour-Note "$Index $name : fly-over. Coverage: $(Tour-Coverage)" | Out-Host
    if (-not (Tour-Expect menu { Tour-Press LB 1 } 3 "$Index-prerace")) { return $false }
    Tour-Press A 16                             # Start race; countdown
    Tour-Shot ('{0:D2}-2-race' -f $Index) | Out-Null
    for ($i = 0; $i -lt 5; $i++) { Fm3 'press RB 300' | Out-Null; Start-Sleep 4 }   # camera views (mirror too)
    Tour-Note "$Index $name : race + cameras. Coverage: $(Tour-Coverage)" | Out-Host
    if (-not (Tour-Expect menu { Tour-Press START 2 } 3 "$Index-pause")) { return $false }
    Tour-Press DOWN 0.6; Tour-Press DOWN 0.6; Tour-Press A 4   # Photo mode
    Fm3 'stick r 0.7 0 1500' | Out-Null; Start-Sleep 2
    Tour-Shot ('{0:D2}-3-photo' -f $Index) | Out-Null
    Tour-Press B 3                              # back to the pause menu (cursor on Photo mode)
    for ($i = 0; $i -lt 4; $i++) { Tour-Press DOWN 0.6 }
    Tour-Press A 2; Tour-Press A 10             # Quit race, confirm; post-race replay behind the menu
    if (-not (Tour-Expect menu { Tour-Press LB 1 } 3 "$Index-postrace")) { return $false }
    if (-not (Tour-Expect trackselect { Tour-Press A 8 } 1 "$Index-continue")) { return $false }
    if (-not (Tour-Expect envselect { Tour-Press B 3 } 2 "$Index-back")) { return $false }
    Tour-Note "$Index $name : done. Coverage: $(Tour-Coverage)" | Out-Host
    $true
}

function Start-Tour([int]$From = 0, [int]$To = 24, [switch]$NoBoot) {
    if (-not $NoBoot -and -not (Start-TourBoot)) { Tour-Note 'boot route failed'; return }
    for ($k = $From; $k -le $To; $k++) {
        if (-not (Get-Process fm3 -ErrorAction SilentlyContinue)) { Tour-Note "fm3 exited before stop $k"; return }
        if (-not (Invoke-TourStop $k)) { Tour-Note "tour stopped at $k"; return }
    }
    Tour-Note "tour finished ($From..$To). Coverage: $(Tour-Coverage)"
}
