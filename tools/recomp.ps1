# Phase 2 pipeline: default.xex -> out/ppc (XenonRecomp output) plus the checks.
#   & "<project>\tools\recomp.ps1" [-Xex <path to default.xex>] [-Prepare]
# 1. copies default.xex into game\ (if -Xex) and checks its SHA-256 (only v0.0.0.8 is supported);
# 2. xexdump -> out\fm3.bin + out\fm3.sections;
# 3. -Prepare: fm3_config.py --write (helpers + exception words) and XenonAnalyse
#    -> config\fm3_switch_tables.toml (both reviewed in git diff afterwards);
# 4. XenonRecomp config\fm3.toml -> out\ppc (log: out\recomp.log);
# 5. fm3_check.py -> out\check.txt (errors, traps, run-offs, pointer-only targets).
# Needs the host tools (tools\setup.ps1).
param([string]$Xex, [switch]$Prepare)
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$sha = 'AA15372C13BC4C899B5EF90A1CFB5634D389B0A3620CC9715F6023D61A9B8742'
$tools = Join-Path $root 'build\tools'
$py = 'py'
# Windows PowerShell's > writes UTF-16; redirected native commands run through cmd (plain bytes).
function Invoke-To([string]$out, [string]$cmdline) { & cmd /c "$cmdline > `"$out`" 2>&1"; return $LASTEXITCODE }

New-Item -ItemType Directory -Force game, out | Out-Null
if ($Xex) { Copy-Item -LiteralPath $Xex game\default.xex -Force }
if (-not (Test-Path game\default.xex)) { throw 'game\default.xex is missing: pass -Xex <path>' }
if ((Get-FileHash game\default.xex -Algorithm SHA256).Hash -ne $sha) { throw 'game\default.xex: unsupported version (SHA-256 differs from v0.0.0.8)' }
foreach ($t in 'xexdump.exe', 'xenonrecomp\XenonRecomp\XenonRecomp.exe', 'xenonrecomp\XenonAnalyse\XenonAnalyse.exe') {
    if (-not (Test-Path (Join-Path $tools $t))) { throw "build\tools\$t is missing: run tools\setup.ps1" }
}

if ((Invoke-To out\fm3.sections "`"$tools\xexdump.exe`" game\default.xex out\fm3.bin") -ne 0) { throw 'xexdump failed' }

if ($Prepare) {
    if ((Invoke-To out\fm3.config.txt "$py -3 -I tools\fm3_config.py out\fm3.bin out\fm3.sections --write config\fm3.toml") -ne 0) {
        Get-Content out\fm3.config.txt; throw 'fm3_config.py failed'
    }
    [void](Invoke-To out\analyse.log "`"$tools\xenonrecomp\XenonAnalyse\XenonAnalyse.exe`" game\default.xex config\fm3_switch_tables.toml")
    Get-Content out\analyse.log | Select-Object -Last 5
}

if (Test-Path out\ppc) { Remove-Item -Recurse -Force out\ppc }
New-Item -ItemType Directory out\ppc | Out-Null
if ((Invoke-To out\recomp.log "`"$tools\xenonrecomp\XenonRecomp\XenonRecomp.exe`" config\fm3.toml thirdparty\XenonRecomp\XenonUtils\ppc_context.h") -ne 0) { Get-Content out\recomp.log -Tail 20; throw 'XenonRecomp failed (out\recomp.log)' }
Get-Content out\recomp.log | Where-Object { $_ -notmatch '^Recompiling' }

$refs = @()
$ref = Join-Path $root 'reference\rexglue recomp reference'
foreach ($r in 'hl360-forza3\config\fptr_functions.toml', 'hl360-forza3\config\codegen.toml', 'forza3-recomp\forza3_manifest.toml') {
    if (Test-Path (Join-Path $ref $r)) { $refs += (Join-Path $ref $r) }
}
$check = "$py -3 -I tools\fm3_check.py out\fm3.bin out\fm3.sections out\ppc"
if ($refs) { $check += ' --refs ' + (($refs | ForEach-Object { "`"$_`"" }) -join ' ') }
[void](Invoke-To out\check.txt $check)
Get-Content out\check.txt | Select-String -Pattern '^\d+ functions|^pointer targets'
