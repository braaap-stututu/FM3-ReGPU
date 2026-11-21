# Portable build toolchain (GitHub issue #2): everything FM3 needs to build, downloaded into one folder instead of
# installed. Nothing goes into Program Files or the registry, and no admin rights are needed.
#   powershell -ExecutionPolicy Bypass -File tools\get-toolchain.ps1 [-AcceptMicrosoftLicense] [-Parts msvc,llvm,...] [-Launcher]
# Parts (all by default):
#   msvc        the MSVC 14.44 x64 compiler libraries/headers and the Windows 11 SDK 10.0.26100, taken from Microsoft's
#               own Visual Studio 2022 package feed (the packages the Build Tools installer would install), unpacked
#               into toolchain\msvc and toolchain\sdk. Needs the Microsoft licence accepted (asked, or -AcceptMicrosoftLicense).
#   llvm        LLVM 23.1.5 (clang-cl, lld-link) from the LLVM project's GitHub release (901 MB) -> toolchain\llvm
#   cmake       CMake 3.31.10 -> toolchain\cmake        ninja   Ninja 1.13.2 -> toolchain\ninja
#   fidelityfx  AMD FidelityFX SDK 2.4.0 (Kits\FidelityFX only, a sparse git checkout of the tag) -> thirdparty\fidelityfx
#               (2.4.0 for the Vulkan renderer: its FSR 3/4 DLLs; 2.1.0 had Direct3D 12 ones only)
#   ffmpeg      FFmpeg 8.1 LGPL shared (BtbN's Windows build) -> thirdparty\ffmpeg
#   dxc         Microsoft's DirectX Shader Compiler v1.9.2609 (its GitHub release; dxc.exe, dxcompiler.dll, dxil.dll with
#               SPIR-V output) -> thirdparty\dxc: ReGPU's Vulkan renderer compiles its shaders with it, and dxcompiler.dll
#               ships beside fm3.exe. With it the Vulkan SDK isn't needed to build.
#   python      Python 3.14 (python.org's embeddable package) -> toolchain\python, with a py.cmd for the build's
#               "py -3" calls (ReGPU embeds its anti-aliasing shaders with tools\embed_hlsl.py)
# -Launcher also gets the .NET 9 SDK (toolchain\dotnet) for the launcher.
# Every download is checked: GitHub's SHA-256 of the asset, the SHA-256 in Microsoft's package feed, or (FidelityFX)
# the SHA-256 of the signed DLLs we ship. Downloads are kept in toolchain\downloads, so a rerun only fetches what is
# missing; delete that folder afterwards to save space. toolchain\ and the two thirdparty folders are gitignored.
# Afterwards tools\vcenv.cmd (and so tools\setup.ps1 and every build command in the README) uses toolchain\env.cmd
# instead of an installed Visual Studio and LLVM.
param(
    [string]$Dir = '',
    [switch]$AcceptMicrosoftLicense,
    [string[]]$Parts = @('msvc', 'llvm', 'cmake', 'ninja', 'python', 'fidelityfx', 'ffmpeg', 'dxc'),
    [switch]$Launcher,
    [string]$ThirdParty = ''   # where fidelityfx\ and ffmpeg\ go (default: the repository's thirdparty\)
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Dir) { $Dir = Join-Path $root 'toolchain' }
if (-not $ThirdParty) { $ThirdParty = Join-Path $root 'thirdparty' }
# (relative to PowerShell's current folder, not .NET's working directory, which can differ)
$Dir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Dir)
$ThirdParty = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ThirdParty)
$downloads = Join-Path $Dir 'downloads'
New-Item -ItemType Directory -Force $Dir, $downloads | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.Web.Extensions
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

# ---- pinned versions
$MsvcVersion = '14.44'           # the toolset the project is built and tested with (VS 2022 17.14)
$SdkVersion = '10.0.26100'
$Llvm = @{ Url = 'https://github.com/llvm/llvm-project/releases/download/llvmorg-23.1.5/clang+llvm-23.1.5-x86_64-pc-windows-msvc.tar.xz'
    Sha256 = '62fdada7f6fa2b73856486dfbbdba3d2c1882991621615bcb55c60207611c6d4'; Size = '901 MB' }
# (Not the 276 MB .tar.zst: it is compressed with a long-distance window Windows' tar.exe refuses to decode.)
$CMake = @{ Url = 'https://github.com/Kitware/CMake/releases/download/v3.31.10/cmake-3.31.10-windows-x86_64.zip'
    Sha256 = '13d1a463d7130df5339baedd63d8ae990aaf385062b2f42f372796143ae94086'; Size = '47 MB' }
$Ninja = @{ Url = 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip'
    Sha256 = '07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65'; Size = '0.3 MB' }
$Python = @{ Url = 'https://www.python.org/ftp/python/3.14.10/python-3.14.10-embed-amd64.zip'
    Sha256 = '90905e64a840557d8c1bc4f5b74db3d7ee85e3ffb217611cac1c7813acea4159'; Size = '12.6 MB' }
# BtbN deletes daily builds after a while: when this one is gone, the rolling n8.1 build is taken (its SHA-256 from GitHub).
$FFmpeg = @{ Url = 'https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-11-18-12-39/ffmpeg-n8.1.3-36-ge22a4510c9-win64-lgpl-shared-8.1.zip'
    Sha256 = '13a795e5620236ca4c304b580f0155130bdcecd87bf5e9f66f84b59d98783668'; Size = '82 MB' }
$Dxc = @{ Url = 'https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip'
    Sha256 = 'ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1'; Size = '31 MB' }
$FidelityFx = @{ Repo = 'https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git'; Tag = 'v2.4.0'
    Dlls = @{ 'amd_fidelityfx_loader_vk.dll' = '38A56A01FB3565C0757D9F5C1E4E69DE4B65E5F2E5769B323B200DC1F142BAB9'
              'amd_fidelityfx_upscaler_vk.dll' = '4A7E895AF6E0B615D40A84D59EB8C2B1054AD91CDD0C739567940270D51BDAB7'
              'amd_fidelityfx_loader_dx12.dll' = 'E2191D3C0828BE45EB8AFFD4E3EC0378F2293ADC345B772A7170EDCF3B021ECB'
              'amd_fidelityfx_upscaler_dx12.dll' = 'AAD4FCCCCB29084A655F9A4F9F613BD22C8C03FB4A0F8840148CC6543B4CCACE' } }

function Say([string]$t) { Write-Host "toolchain: $t" }

# A file into downloads (kept if already there with the right hash). $Algorithm SHA256 or SHA512.
function Get-Download([string]$Url, [string]$Name, [string]$Hash, [string]$Algorithm = 'SHA256') {
    $path = Join-Path $downloads $Name
    if ((Test-Path $path) -and $Hash -and (Get-FileHash $path -Algorithm $Algorithm).Hash -eq $Hash.ToUpperInvariant()) { return $path }
    New-Item -ItemType Directory -Force (Split-Path -Parent $path) | Out-Null
    Say "downloading $Name"
    & curl.exe -L --fail --retry 3 --silent --show-error -o "$path.part" $Url
    if ($LASTEXITCODE -ne 0) { Remove-Item "$path.part" -ErrorAction SilentlyContinue; throw "download failed: $Url" }
    if ($Hash) {
        $got = (Get-FileHash "$path.part" -Algorithm $Algorithm).Hash
        if ($got -ne $Hash.ToUpperInvariant()) { Remove-Item "$path.part"; throw "$Name`: $Algorithm mismatch (expected $Hash, got $got)" }
    }
    Move-Item -Force "$path.part" $path
    return $path
}

function Read-Json([string]$Url) {
    $text = & curl.exe -L --fail --retry 3 --silent --show-error $Url | Out-String
    if ($LASTEXITCODE -ne 0) { throw "download failed: $Url" }
    $js = New-Object System.Web.Script.Serialization.JavaScriptSerializer
    $js.MaxJsonLength = [int]::MaxValue
    return $js.DeserializeObject($text)
}

# Unpacks a zip, optionally only the entries under $Prefix (stripped), with percent-encoded names decoded (VSIX).
function Expand-Zip([string]$Zip, [string]$To, [string]$Prefix = '', [switch]$Decode) {
    $z = [IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        $base = [IO.Path]::GetFullPath($To).TrimEnd('\') + '\'
        foreach ($e in $z.Entries) {
            $name = $e.FullName
            if ($Decode) { $name = [Uri]::UnescapeDataString($name) }
            if ($Prefix) { if (-not $name.StartsWith($Prefix, [StringComparison]::OrdinalIgnoreCase)) { continue }; $name = $name.Substring($Prefix.Length) }
            if (-not $name -or $name.EndsWith('/')) { continue }
            $target = [IO.Path]::GetFullPath((Join-Path $base $name))
            if (-not $target.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) { throw "$Zip`: entry outside the target ($name)" }
            New-Item -ItemType Directory -Force (Split-Path -Parent $target) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($e, $target, $true)
        }
    } finally { $z.Dispose() }
}

# ---- MSVC and the Windows SDK from Microsoft's Visual Studio 2022 package feed
function Get-Msvc {
    $channel = Read-Json 'https://aka.ms/vs/17/release/channel'
    $items = $channel['channelItems']
    $bt = $items | Where-Object { $_['id'] -eq 'Microsoft.VisualStudio.Product.BuildTools' } | Select-Object -First 1
    $license = ($bt['localizedResources'] | Where-Object { $_['language'] -eq 'en-us' } | Select-Object -First 1)['license']
    if (-not $AcceptMicrosoftLicense) {
        Write-Host ''
        Write-Host 'The MSVC compiler libraries and the Windows SDK are Microsoft software under the Visual Studio Build Tools licence:'
        Write-Host "  $license"
        Write-Host 'They are downloaded from Microsoft for your own use; this project does not redistribute them.'
        $answer = Read-Host 'Type yes to accept the licence and continue'
        if ($answer -ne 'yes') { throw 'Microsoft licence not accepted: MSVC and the Windows SDK were not downloaded (or rerun with -AcceptMicrosoftLicense).' }
    }
    $manItem = $items | Where-Object { $_['id'] -eq 'Microsoft.VisualStudio.Manifests.VisualStudio' } | Select-Object -First 1
    Say 'reading the Visual Studio package list'
    $manifest = Read-Json $manItem['payloads'][0]['url']
    $packages = @{}
    foreach ($p in $manifest['packages']) {
        $id = ([string]$p['id']).ToLowerInvariant()
        if (-not $packages.ContainsKey($id)) { $packages[$id] = New-Object System.Collections.ArrayList }
        [void]$packages[$id].Add($p)
    }
    function Pick([string]$id) {
        $list = $packages[$id.ToLowerInvariant()]
        if (-not $list) { return $null }
        $en = $list | Where-Object { -not $_['language'] -or $_['language'] -eq 'en-US' -or $_['language'] -eq 'neutral' } | Select-Object -First 1
        if ($en) { return $en } else { return $list[0] }
    }
    # the newest 14.44.x toolset in the feed (side-by-side components keep older toolsets listed)
    $vers = $packages.Keys | ForEach-Object { if ($_ -match "^microsoft\.vc\.($([regex]::Escape($MsvcVersion))\.[\d.]+)\.tools\.hostx64\.targetx64\.base$") { $Matches[1] } } |
        Sort-Object { [version]$_ } -Descending
    if (-not $vers) { throw "MSVC $MsvcVersion is not in Microsoft's Visual Studio 2022 feed any more: change `$MsvcVersion in this script" }
    $v = @($vers)[0]
    Say "MSVC $v"
    $msvcOut = Join-Path $Dir 'msvc'
    foreach ($id in "microsoft.vc.$v.tools.hostx64.targetx64.base", "microsoft.vc.$v.tools.hostx64.targetx64.res.base",
                    "microsoft.vc.$v.crt.headers.base", "microsoft.vc.$v.crt.x64.desktop.base", "microsoft.vc.$v.crt.x64.store.base",
                    "microsoft.vc.$v.asan.headers.base", "microsoft.vc.$v.asan.x64.base", "microsoft.vc.$v.pgo.x64.base") {
        $p = Pick $id
        if (-not $p) { Say "  (not in the feed: $id)"; continue }
        foreach ($pl in $p['payloads']) {
            $file = Get-Download $pl['url'] ("msvc\" + ([string]$pl['sha256']).Substring(0, 12) + '-' + [IO.Path]::GetFileName($pl['fileName'])) $pl['sha256']
            Expand-Zip $file $msvcOut 'Contents/' -Decode
        }
    }
    # Windows SDK: its MSIs (and the cabs each one names), unpacked with an administrative install (no admin rights needed)
    $sdkId = $packages.Keys | Where-Object { $_ -like "win11sdk_$SdkVersion*" } | Sort-Object | Select-Object -Last 1
    if (-not $sdkId) { throw "Windows SDK $SdkVersion is not in the feed" }
    # (Win11SDK_10.0.26100 carries its installers itself; older feeds pointed to them through its first dependency)
    $sdk = Pick $sdkId
    if (-not $sdk['payloads'] -and $sdk['dependencies']) { $sdk = Pick (@($sdk['dependencies'].Keys)[0]) }
    if (-not $sdk['payloads']) { throw "Windows SDK $SdkVersion`: no installers listed in the feed" }
    $payloads = @{}
    foreach ($pl in $sdk['payloads']) { $payloads[[IO.Path]::GetFileName($pl['fileName']).ToLowerInvariant()] = $pl }
    $msis = 'Windows SDK for Windows Store Apps Tools-x86_en-us.msi', 'Windows SDK for Windows Store Apps Headers-x86_en-us.msi',
        'Windows SDK for Windows Store Apps Headers OnecoreUap-x86_en-us.msi', 'Windows SDK for Windows Store Apps Libs-x86_en-us.msi',
        'Universal CRT Headers Libraries and Sources-x86_en-us.msi', 'Windows SDK Desktop Headers x86-x86_en-us.msi',
        'Windows SDK Desktop Headers x64-x86_en-us.msi', 'Windows SDK Desktop Libs x64-x86_en-us.msi', 'Windows SDK Desktop Tools x64-x86_en-us.msi'
    # (Not "Windows SDK Redistributables": its administrative install fails with 1603. The x64 d3dcompiler_47.dll,
    # dxcompiler.dll and dxil.dll it would add are in Desktop Tools' bin\<version>\x64 too: CMakeLists.txt takes them there.)
    $sdkOut = Join-Path $Dir 'sdk'
    New-Item -ItemType Directory -Force $sdkOut | Out-Null
    foreach ($m in $msis) {
        $pl = $payloads[$m.ToLowerInvariant()]
        if (-not $pl) { Say "  (not in the SDK: $m)"; continue }
        $msi = Get-Download $pl['url'] "sdk\$m" $pl['sha256']
        $bytes = [IO.File]::ReadAllBytes($msi)
        $cabs = [regex]::Matches([Text.Encoding]::ASCII.GetString($bytes), '[0-9a-f]{32}\.cab') | ForEach-Object { $_.Value } | Sort-Object -Unique
        foreach ($c in $cabs) {
            $cp = $payloads[$c.ToLowerInvariant()]
            if ($cp) { [void](Get-Download $cp['url'] "sdk\$c" $cp['sha256']) }
        }
        Say "  unpacking $m"
        $p = Start-Process msiexec.exe -ArgumentList '/a', "`"$msi`"", '/quiet', '/qn', "TARGETDIR=`"$sdkOut`"" -Wait -PassThru
        if ($p.ExitCode -ne 0) { throw "msiexec /a failed for $m (exit $($p.ExitCode))" }
    }
    Get-ChildItem $sdkOut -Filter *.msi | Remove-Item -Force # (the administrative install leaves a copy of each MSI)
}

function Get-Llvm {
    $f = Get-Download $Llvm.Url 'clang+llvm-23.1.5-x86_64-pc-windows-msvc.tar.xz' $Llvm.Sha256
    $out = Join-Path $Dir 'llvm'
    if (Test-Path $out) { Remove-Item -Recurse -Force $out }
    New-Item -ItemType Directory -Force $out | Out-Null
    Say 'unpacking LLVM'
    & "$env:SystemRoot\System32\tar.exe" -x -f $f -C $out --strip-components 1
    if ($LASTEXITCODE -ne 0) { throw 'tar failed on the LLVM archive' }
}

function Get-ZipTool($spec, [string]$name, [string]$sub) {
    $f = Get-Download $spec.Url ([IO.Path]::GetFileName($spec.Url)) $spec.Sha256
    $out = Join-Path $Dir $name
    if (Test-Path $out) { Remove-Item -Recurse -Force $out }
    Expand-Zip $f $out $sub
}

function Get-Python {
    Get-ZipTool $Python 'python' ''
    # "py -3 <args>" (the build's and the tools' calls) -> this python.exe <args>
    @('@echo off', 'setlocal', 'set "ARGS=%*"', 'if "%~1"=="-3" set "ARGS=%ARGS:~3%"', '"%~dp0python.exe" %ARGS%') |
        Set-Content -Encoding ascii (Join-Path $Dir 'python\py.cmd')
}

function Get-FFmpeg {
    $spec = $FFmpeg
    try { $f = Get-Download $spec.Url ([IO.Path]::GetFileName($spec.Url)) $spec.Sha256 }
    catch {
        Say "the pinned FFmpeg build is gone ($($_.Exception.Message)); taking the rolling n8.1 build"
        $rel = Read-Json 'https://api.github.com/repos/BtbN/FFmpeg-Builds/releases/tags/latest'
        $a = $rel['assets'] | Where-Object { $_['name'] -eq 'ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip' } | Select-Object -First 1
        if (-not $a -or -not ([string]$a['digest']).StartsWith('sha256:')) { throw 'no FFmpeg n8.1 LGPL shared build with a SHA-256 on BtbN/FFmpeg-Builds' }
        $f = Get-Download $a['browser_download_url'] $a['name'] ([string]$a['digest']).Substring(7)
    }
    $out = Join-Path $ThirdParty 'ffmpeg'
    if (Test-Path $out) { Remove-Item -Recurse -Force $out }
    $tmp = Join-Path $Dir 'ffmpeg-unpack'
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
    Expand-Zip $f $tmp
    Move-Item (Get-ChildItem $tmp -Directory | Select-Object -First 1).FullName $out
    Remove-Item -Recurse -Force $tmp
}

function Get-Dxc {
    Say "DirectX Shader Compiler (SPIR-V) $($Dxc.Size)"
    $f = Get-Download $Dxc.Url ([IO.Path]::GetFileName($Dxc.Url)) $Dxc.Sha256
    $out = Join-Path $ThirdParty 'dxc'
    if (Test-Path $out) { Remove-Item -Recurse -Force $out }
    Expand-Zip $f $out
    if (-not (Test-Path (Join-Path $out 'bin\x64\dxcompiler.dll'))) { throw 'the DXC release has no bin\x64\dxcompiler.dll' }
}

function Get-FidelityFx {
    $out = Join-Path $ThirdParty 'fidelityfx'
    $item = Get-Item $out -ErrorAction SilentlyContinue
    if ($item -and $item.Attributes -band [IO.FileAttributes]::ReparsePoint) { Say 'thirdparty\fidelityfx is a link to your own copy: left as it is'; return }
    if (Test-Path $out) { Remove-Item -Recurse -Force $out }
    Say "FidelityFX SDK $($FidelityFx.Tag) (Kits\FidelityFX only)"
    # (git reports progress on stderr, which Windows PowerShell turns into errors under 'Stop': judged by the exit code)
    $ErrorActionPreference = 'Continue'
    & git -c advice.detachedHead=false clone --quiet --depth 1 --filter=blob:none --sparse --branch $FidelityFx.Tag $FidelityFx.Repo $out 2>&1 | Out-Host
    $clone = $LASTEXITCODE
    if ($clone -eq 0) { & git -C $out sparse-checkout set Kits/FidelityFX 2>&1 | Out-Host }
    $sparse = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($clone -ne 0) { throw 'git clone of the FidelityFX SDK failed' }
    if ($sparse -ne 0) { throw 'git sparse-checkout of the FidelityFX SDK failed' }
    foreach ($k in $FidelityFx.Dlls.Keys) {
        $h = (Get-FileHash (Join-Path $out "Kits\FidelityFX\signedbin\$k")).Hash
        if ($h -ne $FidelityFx.Dlls[$k]) { throw "FidelityFX $k`: SHA-256 mismatch ($h)" }
    }
}

function Get-DotNet {
    $meta = Read-Json 'https://dotnetcli.blob.core.windows.net/dotnet/release-metadata/9.0/releases.json'
    $sdk = $meta['releases'][0]['sdk']
    $file = $sdk['files'] | Where-Object { $_['rid'] -eq 'win-x64' -and $_['name'] -like '*.zip' } | Select-Object -First 1
    Say ".NET SDK $($sdk['version'])"
    $f = Get-Download $file['url'] ([IO.Path]::GetFileName($file['url'])) $file['hash'] 'SHA512'
    $out = Join-Path $Dir 'dotnet'
    if (Test-Path $out) { Remove-Item -Recurse -Force $out }
    Expand-Zip $f $out
}

# toolchain\env.cmd: what vcvars64.bat would set, for these folders (tools\vcenv.cmd calls it)
function Write-Env {
    $msvcRoot = Get-ChildItem (Join-Path $Dir 'msvc\VC\Tools\MSVC') -Directory -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -Last 1
    $kits = Join-Path $Dir 'sdk\Windows Kits\10'
    $sdkVer = Get-ChildItem (Join-Path $kits 'Include') -Directory -ErrorAction SilentlyContinue | Where-Object Name -like "$SdkVersion*" | Sort-Object Name | Select-Object -Last 1
    if (-not $msvcRoot -or -not $sdkVer) { Say 'MSVC or the SDK is missing: env.cmd not written'; return }
    $m = $msvcRoot.FullName; $s = $sdkVer.Name
    $lines = @(
        '@echo off',
        'rem Written by tools\get-toolchain.ps1: the portable MSVC, Windows SDK, LLVM, CMake and Ninja in front of PATH.',
        'set "TC=%~dp0"', 'set "TC=%TC:~0,-1%"',
        "set `"VCToolsInstallDir=%TC%\msvc\VC\Tools\MSVC\$($msvcRoot.Name)\`"",
        "set `"VCToolsVersion=$($msvcRoot.Name)`"",
        'set "VCINSTALLDIR=%TC%\msvc\VC\"',
        'set "WindowsSdkDir=%TC%\sdk\Windows Kits\10\"',
        "set `"WindowsSDKVersion=$s\`"",
        "set `"WindowsSDKLibVersion=$s\`"",
        "set `"UniversalCRTSdkDir=%TC%\sdk\Windows Kits\10\`"",
        "set `"UCRTVersion=$s`"",
        'set "INCLUDE=%VCToolsInstallDir%include;%WindowsSdkDir%Include\%UCRTVersion%\ucrt;%WindowsSdkDir%Include\%UCRTVersion%\shared;%WindowsSdkDir%Include\%UCRTVersion%\um;%WindowsSdkDir%Include\%UCRTVersion%\winrt;%WindowsSdkDir%Include\%UCRTVersion%\cppwinrt"',
        'set "LIB=%VCToolsInstallDir%lib\x64;%WindowsSdkDir%Lib\%UCRTVersion%\ucrt\x64;%WindowsSdkDir%Lib\%UCRTVersion%\um\x64"',
        'set "LIBPATH=%VCToolsInstallDir%lib\x64"',
        'set "PATH=%TC%\llvm\bin;%TC%\cmake\bin;%TC%\ninja;%TC%\python;%VCToolsInstallDir%bin\Hostx64\x64;%WindowsSdkDir%bin\%UCRTVersion%\x64;%PATH%"',
        'if exist "%TC%\dotnet\dotnet.exe" (',
        '    set "PATH=%TC%\dotnet;%PATH%"',
        '    set "DOTNET_ROOT=%TC%\dotnet"',
        ')',
        'set "VSCMD_ARG_TGT_ARCH=x64"'
    )
    $lines | Set-Content -Encoding ascii (Join-Path $Dir 'env.cmd')
    Say "wrote $(Join-Path $Dir 'env.cmd') (MSVC $($msvcRoot.Name), SDK $s)"
}

foreach ($p in $Parts) {
    switch ($p) {
        'msvc' { Get-Msvc }
        'llvm' { Get-Llvm }
        'cmake' { Get-ZipTool $CMake 'cmake' 'cmake-3.31.10-windows-x86_64/' }
        'ninja' { Get-ZipTool $Ninja 'ninja' '' }
        'python' { Get-Python }
        'fidelityfx' { Get-FidelityFx }
        'ffmpeg' { Get-FFmpeg }
        'dxc' { Get-Dxc }
        default { throw "unknown part: $p" }
    }
}
if ($Launcher) { Get-DotNet }
Write-Env
Say 'done. tools\vcenv.cmd now uses this toolchain; build as in README.md ("Build").'
