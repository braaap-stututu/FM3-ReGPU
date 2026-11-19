# Builds the installer for the current build: out\installer\dist\FM3-ReGPU-Setup-<version>.exe, and beside it the
# launcher updater's FM3-ReGPU-<version>-patch.zip (attach both to the GitHub release).
#   powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1 [-Version x.y.z] [-NoGameBuild]
# Needs: build\rel\fm3.exe built (the game is rebuilt first unless -NoGameBuild; close it), the .NET 9 SDK,
# Inno Setup 6 (winget install JRSoftware.InnoSetup). No game files are packed: see installer\fm3.iss.
param([string]$Version = "", [switch]$NoGameBuild)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

if (-not $Version) {
    # The release in VERSION (also what the launcher shows); a build that isn't the tagged release adds the commit.
    $Version = (Get-Content (Join-Path $root "VERSION") -TotalCount 1).Trim()
    $dirty = if (git status --porcelain --untracked-files=no) { "-dirty" } else { "" }
    $tagged = git tag --points-at HEAD | Where-Object { $_ -eq "v$Version" }
    if (-not $tagged -or $dirty) { $Version += "-" + (git rev-parse --short HEAD).Trim() + $dirty }
}

$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup 6 isn't installed (winget install JRSoftware.InnoSetup)." }
$dotnet = if (Get-Command dotnet -ErrorAction SilentlyContinue) { "dotnet" } else { "C:\Program Files\dotnet\dotnet.exe" }

if (-not $NoGameBuild) {
    if (Get-Process fm3 -ErrorAction SilentlyContinue) { throw "Close the game first: the build replaces fm3.exe." }
    cmd /c "tools\vcenv.cmd ninja -C build/rel fm3"
    if ($LASTEXITCODE) { throw "Game build failed." }
}

$stage = Join-Path $root "out\installer\stage"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force "$stage\launcher", "$stage\licenses" | Out-Null

# The game: fm3.exe, its DLLs (FFmpeg, FidelityFX, shader compilers, OpenXR loader) and its symbols (crash logs name
# functions with them).
$rel = Join-Path $root "build\rel"
Copy-Item "$rel\fm3.exe", "$rel\fm3.pdb" $stage
Get-ChildItem $rel -Filter *.dll | Copy-Item -Destination $stage

# The boot screen's picture and its layout (src/boot_screen.cpp: boot\ beside fm3.exe, used when the player's user
# folder has none), from this PC's user folder: kept out of the repository, shipped in the installer (user's decision).
$bootSrc = Join-Path $root "out\user\boot"
if (Test-Path "$bootSrc\background.png") {
    New-Item -ItemType Directory -Force "$stage\boot" | Out-Null
    Copy-Item "$bootSrc\background.png" "$stage\boot\"
    if (Test-Path "$bootSrc\background.txt") { Copy-Item "$bootSrc\background.txt" "$stage\boot\" }
} else { Write-Warning "No boot screen picture in out\user\boot: the installed game shows the rev counter on black." }

# The launcher, self-contained (the other PC needs no .NET; WebView2 comes with Windows 10/11).
& $dotnet publish "launcher\FM3Launcher" -c Release -r win-x64 --self-contained true -o "$stage\launcher" --nologo -p:DebugType=none "-p:Version=$Version"
if ($LASTEXITCODE) { throw "Launcher publish failed." }

# Licences: ours, the ported runtime's and the wheel code's notices, FFmpeg (LGPL), the AA shaders' sources.
Copy-Item LICENSE, NOTICE.md, NOTICE-wheel.md "$stage\licenses"
if (Test-Path thirdparty\ffmpeg\LICENSE.txt) { Copy-Item thirdparty\ffmpeg\LICENSE.txt "$stage\licenses\FFmpeg-LICENSE.txt" }
if (Test-Path thirdparty\xess\LICENSE.txt) { Copy-Item thirdparty\xess\LICENSE.txt "$stage\licenses\XeSS-LICENSE.txt" }
if (Test-Path thirdparty\meshoptimizer\LICENSE.md) { Copy-Item thirdparty\meshoptimizer\LICENSE.md "$stage\licenses\meshoptimizer-LICENSE.md" }
Copy-Item thirdparty\aa\SOURCES.txt "$stage\licenses\AA-SOURCES.txt"
Get-ChildItem thirdparty\aa -Recurse -Include LICENSE*, COPYING* | ForEach-Object {
    Copy-Item $_.FullName ("$stage\licenses\AA-" + $_.Directory.Name + "-" + $_.Name)
}

@"
Forza Motorsport 3 ReGPU $Version

This installs the PC build and its launcher. It contains no game files.

After installing, open the launcher:
  1. Play tab, Game tile: "From .iso..." and choose your Forza Motorsport 3 Ultimate Collection (USA) Disc 1 image
     (Play Disc). It is checked, then extracted next to the game (about 6.8 GB).
  2. Optional: Disc 2 content tile, "Install from .iso..." with the Content Install Disc image (about 3.4 GB).
  3. Graphics tab: resolution, frame cap, graphics card. Then Play.

The first start compiles shaders for the graphics card, which takes a while.
Saves and settings go to Documents\fm3. The game's log is out\run\fm3.log in the install folder.
"@ | Set-Content -Encoding utf8 "$stage\before-install.txt"

# The install's file list (launcher\FM3Launcher\Updater.cs Manifest): installed with the setup exe and in every patch, so an
# update removes files the previous release had and this one doesn't.
$manifest = Join-Path $stage "install-manifest.txt"
if (Test-Path $manifest) { Remove-Item $manifest }
$names = Get-ChildItem $stage -Recurse -File | Where-Object { $_.Name -ne "before-install.txt" } |
    ForEach-Object { $_.FullName.Substring($stage.Length + 1).Replace('\', '/') }
@($names) + "install-manifest.txt" | Set-Content -Encoding ascii $manifest
$out = Join-Path $root "out\installer\dist"
& $iscc "/DAppVersion=$Version" "/DStageDir=$stage" "/DOutDir=$out" "installer\fm3.iss"
if ($LASTEXITCODE) { throw "Inno Setup failed." }
# The launcher's updater (launcher\FM3Launcher\Updater.cs) takes this from the GitHub release: the installer's files,
# laid out as the install folder, unpacked over an existing install.
$patch = Join-Path $out "FM3-ReGPU-$Version-patch.zip"
if (Test-Path $patch) { Remove-Item $patch }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::Open($patch, "Create")
try {
    Get-ChildItem $stage -Recurse -File | Where-Object { $_.Name -ne "before-install.txt" } | ForEach-Object {
        $name = $_.FullName.Substring($stage.Length + 1).Replace('\', '/')
        [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, $name, "Optimal")
    }
} finally { $zip.Dispose() }

Get-Item (Join-Path $out "FM3-ReGPU-Setup-$Version.exe"), $patch | Select-Object FullName, @{ n = "MB"; e = { [math]::Round($_.Length / 1MB, 1) } }
