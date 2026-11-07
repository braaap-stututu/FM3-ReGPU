# One-time (and idempotent) setup of the build tree:
#   1. submodules (XenonRecomp + its thirdparty, ReGPU) at the pinned commits, LF line endings;
#   2. our XenonRecomp patches, applied in order (skipped when already applied);
#   3. the host tools (XenonRecomp, XenonAnalyse, xexdump) built into build\tools.
# Usage (PowerShell, from anywhere):  & "<project>\tools\setup.ps1" [-NoBuild]
param([switch]$NoBuild)
# Native tools report on stderr; failures are checked through $LASTEXITCODE.
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# Order matters: each patch is made against the tree the previous ones leave.
$patches = @(
    'xenonrecomp-fm2',              # divide-by-zero semantics, MSR[EE] -> PPC_MSR_CHANGED interrupt lock
    'xenonrecomp-modules',          # function-name prefix per image, visible no-op thunks for unnamed imports
    'xenonrecomp-fm4',              # jump-table back-trace through registers, fallthrough count, XEX image-size fix
    'xenonrecomp-fm4-instructions', # extra/fixed VMX128 and CR-bit instructions
    'xenonrecomp-switch-u32'        # switch on the u32 index; vcmpbfp implemented
)

function Invoke-Git { & git @args; if ($LASTEXITCODE -ne 0) { throw "git $args failed ($LASTEXITCODE)" } }

Invoke-Git submodule sync --recursive
Invoke-Git submodule update --init --recursive
# Patches and sources must be LF for `git apply`; the global core.autocrlf=true would make them CRLF.
foreach ($sm in @('thirdparty/XenonRecomp', 'thirdparty/ReGPU')) {
    Invoke-Git -C $sm config core.autocrlf false
}
Invoke-Git -C thirdparty/XenonRecomp submodule foreach --quiet 'git config core.autocrlf false'

$xr = Join-Path $root 'thirdparty/XenonRecomp'
# Idempotent check of the whole stack: build the tree "upstream commit + all patches" in a scratch
# index and compare it with the working tree (the patches overlap, so reverse-checking one at a
# time does not work once later ones are applied).
function Get-Tree([scriptblock]$fill) {
    $idx = Join-Path ([IO.Path]::GetTempPath()) ("fm3-xr-" + [guid]::NewGuid() + ".idx")
    $env:GIT_INDEX_FILE = $idx
    try { & $fill | Out-Null; $t = (& git -C $xr write-tree); if ($LASTEXITCODE -ne 0) { throw 'write-tree failed' }; return $t }
    finally { Remove-Item Env:GIT_INDEX_FILE; Remove-Item $idx -ErrorAction SilentlyContinue }
}
$expected = Get-Tree {
    & git -C $xr read-tree HEAD
    foreach ($p in $patches) {
        & git -C $xr apply --cached (Join-Path $root "patches/$p.patch")
        if ($LASTEXITCODE -ne 0) { throw "patch $p does not apply to the pinned XenonRecomp commit" }
    }
}
$current = Get-Tree { & git -C $xr read-tree HEAD; & git -C $xr add -A }
$clean = Get-Tree { & git -C $xr read-tree HEAD }
if ($current -eq $expected) {
    Write-Host "XenonRecomp: all $($patches.Count) patches already applied"
} elseif ($current -eq $clean) {
    foreach ($p in $patches) {
        & git -C $xr apply (Join-Path $root "patches/$p.patch")
        if ($LASTEXITCODE -ne 0) { throw "patch $p failed (CRLF checkout? see NOTES.md gotchas)" }
        Write-Host "patch $p : applied"
    }
} else {
    throw "thirdparty/XenonRecomp has changes that are neither upstream nor exactly our patch stack: inspect with git -C thirdparty/XenonRecomp diff"
}

if ($NoBuild) { return }
$vcenv = Join-Path $PSScriptRoot 'vcenv.cmd'
& cmd /c "`"$vcenv`" cmake -S tools/hosttools -B build/tools -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl"
if ($LASTEXITCODE -ne 0) { throw 'configuring build\tools failed' }
& cmd /c "`"$vcenv`" ninja -C build/tools"
if ($LASTEXITCODE -ne 0) { throw 'building build\tools failed' }
Write-Host 'host tools: build\tools\xenonrecomp\XenonRecomp\XenonRecomp.exe, ...\XenonAnalyse\XenonAnalyse.exe, build\tools\xexdump.exe'
