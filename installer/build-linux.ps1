# Builds the Linux package of the current build: out\installer\dist\FM3-ReGPU-linux-<version>.tar.gz (session 39).
#   powershell -ExecutionPolicy Bypass -File installer\build-linux.ps1 [-Version x.y.z] [-NoGameBuild]
# The game is the same fm3.exe as on Windows (it runs through Proton or Wine: launcher/FM3Launcher/Runner.cs); the
# launcher is published for linux-x64, self-contained (no .NET needed; WebKitGTK is). No game files inside: the
# launcher extracts Disc 1 from the player's own .iso, as on Windows.
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
$dotnet = if (Get-Command dotnet -ErrorAction SilentlyContinue) { "dotnet" } else { "C:\Program Files\dotnet\dotnet.exe" }

if (-not $NoGameBuild) {
    if (Get-Process fm3 -ErrorAction SilentlyContinue) { throw "Close the game first: the build replaces fm3.exe." }
    cmd /c "tools\vcenv.cmd ninja -C build/rel fm3"
    if ($LASTEXITCODE) { throw "Game build failed." }
}

$stage = Join-Path $root "out\installer\stage-linux"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force "$stage\launcher", "$stage\licenses", "$stage\out\run" | Out-Null

# The game (as in the Windows installer).
$rel = Join-Path $root "build\rel"
Copy-Item "$rel\fm3.exe", "$rel\fm3.pdb" $stage
Get-ChildItem $rel -Filter *.dll | Copy-Item -Destination $stage
$bootSrc = Join-Path $root "out\user\boot"
if (Test-Path "$bootSrc\background.png") {
    New-Item -ItemType Directory -Force "$stage\boot" | Out-Null
    Copy-Item "$bootSrc\background.png" "$stage\boot\"
    if (Test-Path "$bootSrc\background.txt") { Copy-Item "$bootSrc\background.txt" "$stage\boot\" }
}

# The launcher for Linux: self-contained, invariant globalization (no libicu needed).
& $dotnet publish "launcher\FM3Launcher" -c Release -r linux-x64 --self-contained true -o "$stage\launcher" --nologo -p:DebugType=none -p:InvariantGlobalization=true "-p:Version=$Version"
if ($LASTEXITCODE) { throw "Launcher publish failed." }

Copy-Item LICENSE, NOTICE.md, NOTICE-wheel.md "$stage\licenses"
if (Test-Path thirdparty\ffmpeg\LICENSE.txt) { Copy-Item thirdparty\ffmpeg\LICENSE.txt "$stage\licenses\FFmpeg-LICENSE.txt" }
if (Test-Path thirdparty\xess\LICENSE.txt) { Copy-Item thirdparty\xess\LICENSE.txt "$stage\licenses\XeSS-LICENSE.txt" }
if (Test-Path thirdparty\meshoptimizer\LICENSE.md) { Copy-Item thirdparty\meshoptimizer\LICENSE.md "$stage\licenses\meshoptimizer-LICENSE.md" }
Copy-Item thirdparty\aa\SOURCES.txt "$stage\licenses\AA-SOURCES.txt"

# The start script (LF line ends: written byte for byte).
$script = "#!/bin/sh`n" +
    "# Forza Motorsport 3 ReGPU: starts the launcher (the game itself runs through Proton or Wine; Settings > Linux).`n" +
    "# --play starts the game with the saved settings, without the window (e.g. a Steam shortcut).`n" +
    "here=`"`$(cd `"`$(dirname `"`$0`")`" && pwd)`"`n" +
    "chmod +x `"`$here/launcher/FM3Launcher`" 2>/dev/null`n" +
    "exec `"`$here/launcher/FM3Launcher`" `"`$@`"`n"
[System.IO.File]::WriteAllBytes("$stage\fm3-launcher.sh", [System.Text.Encoding]::UTF8.GetBytes($script))

$readme = @"
Forza Motorsport 3 ReGPU $Version for Linux

The game is the Windows build; it runs through Proton (tested) or Wine. The launcher is a native Linux program.

Needs:
  - WebKitGTK 4.1 for the launcher window (Ubuntu/Debian: libwebkit2gtk-4.1-0, Arch: webkit2gtk-4.1, Fedora: webkit2gtk4.1)
  - one of: umu-launcher (umu-run; Proton outside Steam), a Proton in Steam (Proton 9+, GE-Proton), or Wine
    (the game renders with Vulkan itself; the Legacy DX12 renderer is not in the Linux package)
  - a Vulkan driver (Mesa RADV for AMD, the NVIDIA driver)

Start:  ./fm3-launcher.sh   (or: sh fm3-launcher.sh)
Then on the Play tab: Game, "From .iso..." with your Forza Motorsport 3 Ultimate Collection (USA) Disc 1 image.
Settings > Linux picks how the game runs (Automatic: umu-launcher, then Steam's Proton, then Wine) and the prefix.

Saves and settings: ~/Documents/fm3. The game's log: out/run/fm3.log in this folder.
Steam (e.g. Steam Deck Game Mode): add fm3-launcher.sh as a non-Steam game with the launch option --play.
"@
[System.IO.File]::WriteAllBytes("$stage\README-LINUX.txt", [System.Text.Encoding]::UTF8.GetBytes(($readme -replace "`r`n", "`n")))

$out = Join-Path $root "out\installer\dist"
New-Item -ItemType Directory -Force $out | Out-Null
$tgz = Join-Path $out "FM3-ReGPU-linux-$Version.tar.gz"
python -I installer\pack_linux.py $stage $tgz
if ($LASTEXITCODE) { throw "Packing failed." }

# The portable package (0.43): the same files plus portable.txt, so saves, settings and the shader cache stay in user/
# beside fm3.exe (one folder, e.g. on a Steam Deck's SD card). The launcher passes that folder to the game as
# FM3_USER_DIR, so Proton's prefix doesn't matter.
[System.IO.File]::WriteAllBytes("$stage\portable.txt", [System.Text.Encoding]::UTF8.GetBytes("Saves and settings stay in the user folder beside fm3.exe. Delete this file to use ~/Documents/fm3 instead.`n"))
$portableReadme = ($readme -replace "Saves and settings: ~/Documents/fm3\.", "Saves and settings: user/ in this folder (portable.txt; delete it to use ~/Documents/fm3).")
[System.IO.File]::WriteAllBytes("$stage\README-LINUX.txt", [System.Text.Encoding]::UTF8.GetBytes(($portableReadme -replace "`r`n", "`n")))
$ptgz = Join-Path $out "FM3-ReGPU-linux-$Version-portable.tar.gz"
python -I installer\pack_linux.py $stage $ptgz
if ($LASTEXITCODE) { throw "Packing failed." }
Get-Item $tgz, $ptgz | Select-Object FullName, @{ n = "MB"; e = { [math]::Round($_.Length / 1MB, 1) } }
