param([string]$Label = "a")
. "G:\claude code projects\FM3-ReGPU recomp\tools\fm3ctl.ps1"
$log = "G:\claude code projects\FM3-ReGPU recomp\out\run\fm3.log"
for ($try = 1; $try -le 6; $try++) {
    $before = (Get-Content $log).Count
    Fm3 "capture" | Out-Null
    Fm3 "wait 3" | Out-Null
    Start-Sleep 2
    $lines = @(Get-Content $log | Select-Object -Skip $before)
    $idx = -1
    for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match "ps 0d1ce823ed690669") { $idx = $i; break } }
    if ($idx -lt 0) { "try $try : no composition draw"; continue }
    "[$Label] try $try"
    $d = $lines[$idx]
    "  " + [regex]::Match($d, "draw #\d+").Value + " scissor " + [regex]::Match($d, "scissor \S+").Value + " strips: " + (([regex]::Matches($d, "t[1-4]=\S+ \S+@[0-9A-F]+") | ForEach-Object { $_.Value }) -join " ")
    for ($j = $idx + 1; $j -lt [Math]::Min($idx + 40, $lines.Count); $j++) {
        if ($lines[$j] -match "draw #\d+ vs") { break }
        if ($lines[$j] -match "pc\d+ =") { "  " + ($lines[$j] -replace "^\[ *[0-9.]+\]\s+", "") }
    }
    break
}
