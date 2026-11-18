# Phase 2 pipeline: default.xex -> out/ppc (XenonRecomp output) plus the checks.
#   & "<project>\tools\recomp.ps1" [-Xex <path to default.xex>] [-Prepare]
# 1. copies default.xex into game\ (if -Xex) and checks its SHA-256 (only v0.0.0.8 is supported);
# 2. xexdump -> out\fm3.bin + out\fm3.sections;
# 3. -Prepare: fm3_config.py --write (helpers + exception words) and XenonAnalyse
#    -> config\fm3_switch_tables.toml (both reviewed in git diff afterwards);
# 4. XenonRecomp config\fm3.toml -> out\ppc (log: out\recomp.log);
# 5. fm3_check.py -> out\check.txt (errors, traps, run-offs, pointer-only targets).
# Needs the host tools (tools\setup.ps1).
# -Recompiler rexenon (session 41): ReXenon (thirdparty/ReXenon, build\tools-rexenon) -> out\ppc-rexenon from the same
# config (config\fm3_rexenon.toml is written from config\fm3.toml with that output folder; log out\recomp-rexenon.log);
# build it with -DFM3_RECOMPILER=rexenon (build\rexenon). ReXenon stops with an error if anything is left unresolved.
# -Experimental (session 45, branch experimental): ReXenon with its register optimisations on -> out\ppc-rexenon-exp
# (config\fm3_rexenon_exp.toml, log out\recomp-rexenon-exp.log): r14-r31/f14-f31/v14-v31/v64-v127 in host locals
# (non_volatile_as_local, made safe by ReXenon's context-function analysis), save/restore helpers inlined, empty
# functions and return LR stores left out. Built in build\exp (-DFM3_EXPERIMENTAL=ON); the launcher's
# "Experimental build" option runs it. The normal out\ppc-rexenon is untouched.
param([string]$Xex, [switch]$Prepare, [ValidateSet('xenonrecomp', 'rexenon')][string]$Recompiler = 'rexenon', [switch]$Experimental)
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

if ($Recompiler -eq 'rexenon') {
    $rx = Join-Path $root 'build\tools-rexenon\XenonRecomp\XenonRecomp.exe'
    if (-not (Test-Path $rx)) { throw 'build\tools-rexenon is missing: run tools\setup.ps1' }
    if ($Experimental) {
        # sub_821F6AF8 hands the frame time back to sub_821F5DE8 in r30/f31 (src/hooks/frame_time.cpp reads and
        # writes them there): both stay on the context so that hand-back survives.
        $cfg = (Get-Content config\fm3.toml) -replace '^out_directory_path = "../out/ppc"$', 'out_directory_path = "../out/ppc-rexenon-exp"'
        $cfg = $cfg -replace '^non_volatile_as_local = false$', (@(
            'non_volatile_as_local = true', 'inline_register_thunks = true', 'elide_return_lr = true',
            'elide_empty_functions = true', 'context_functions = [0x821F6AF8, 0x821F5DE8]') -join "`r`n")
        $cfg | Set-Content -Encoding ascii config\fm3_rexenon_exp.toml
        if (Test-Path out\ppc-rexenon-exp) { Remove-Item -Recurse -Force out\ppc-rexenon-exp }
        if ((Invoke-To out\recomp-rexenon-exp.log "`"$rx`" config\fm3_rexenon_exp.toml thirdparty\ReXenon\XenonUtils\ppc_context.h") -ne 0) {
            Get-Content out\recomp-rexenon-exp.log -Tail 30; throw 'ReXenon failed (out\recomp-rexenon-exp.log)'
        }
        Get-Content out\recomp-rexenon-exp.log | Where-Object { $_ -notmatch '^Recompiling' }
        return
    }
    (Get-Content config\fm3.toml) -replace '^out_directory_path = "../out/ppc"$', 'out_directory_path = "../out/ppc-rexenon"' |
        Set-Content -Encoding ascii config\fm3_rexenon.toml
    if (Test-Path out\ppc-rexenon) { Remove-Item -Recurse -Force out\ppc-rexenon }
    if ((Invoke-To out\recomp-rexenon.log "`"$rx`" config\fm3_rexenon.toml thirdparty\ReXenon\XenonUtils\ppc_context.h") -ne 0) {
        Get-Content out\recomp-rexenon.log -Tail 30; throw 'ReXenon failed (out\recomp-rexenon.log)'
    }
    Get-Content out\recomp-rexenon.log | Where-Object { $_ -notmatch '^Recompiling' }
    return
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
