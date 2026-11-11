# Offline GPU-code statistics of translated pixel shaders with AMD's Radeon GPU Analyzer (session 24, PERF-PLAN L6).
#
# Needs: RGA (rga.exe, GPUOpen-Tools/radeon_gpu_analyzer, unzipped outside the repo; -Rga), fxc from the Windows SDK,
# ReGPU's root signature (boot once with REGPU_DUMP_ROOTSIG=<file>) and the shaders' HLSL (Fm3 "hlsl <hash> out/hlsl/x.hlsl",
# a folder that exists under out\run). RGA compiles for the GPU given (-Asic, default gfx1100 = RX 7000) through the
# installed driver and writes VGPRs/SGPRs/ISA size per shader. RGA splits paths with spaces, so the work happens in -Work.
#
#   tools\rga_shaders.ps1 -RootSig out\rga\rootsig.bin -Hlsl out\rga\ps_*.hlsl
param(
    [Parameter(Mandatory)] [string]$RootSig,
    [Parameter(Mandatory)] [string[]]$Hlsl,
    [string]$Rga = "$PSScriptRoot\..\..\tools\rga-2.15\rga.exe",
    [string]$Asic = 'gfx1100',
    [string]$Work = (Join-Path $env:TEMP 'fm3-rga')
)
$ErrorActionPreference = 'Stop'
$fxc = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\fxc.exe" | Sort-Object FullName | Select-Object -Last 1
if (-not $fxc) { throw 'fxc.exe not found (Windows SDK)' }
if (-not (Test-Path $Rga)) { throw "rga.exe not found: $Rga" }
New-Item -ItemType Directory -Force $Work | Out-Null
Copy-Item $RootSig "$Work\rootsig.bin"
# ReGPU fetches vertices itself: no input layout. A stub vertex shader feeds the pixel shaders' VSOut.
Set-Content -Encoding ascii "$Work\vs.hlsl" @'
struct VSOut { float4 pos : SV_Position; float4 o[16] : TEXCOORD0; float2 pcoord : TEXCOORD16; };
VSOut main(uint id : SV_VertexID) { float4 p = float4(id & 1, id >> 1, 0, 1); VSOut r; r.pos = p; [unroll] for (int i = 0; i < 16; i++) r.o[i] = p * (i + 1); r.pcoord = p.xy; return r; }
'@
Set-Content -Encoding ascii "$Work\fm3.gpso" @'
# schemaVersion
1.0

# InputLayoutNumElements
0

# InputLayout

#PrimitiveTopologyType
D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE

# NumRenderTargets
1

# RTVFormats
{ DXGI_FORMAT_R16G16B16A16_FLOAT }
'@
$ErrorActionPreference = 'Continue' # fxc prints warnings on stderr
& $fxc.FullName /nologo /T vs_5_1 /E main /Fo "$Work\vs.dxbc" "$Work\vs.hlsl" 2>&1 | Out-Null
foreach ($pattern in $Hlsl) {
    foreach ($f in Get-ChildItem $pattern) {
        $n = $f.BaseName
        Copy-Item $f.FullName "$Work\$n.hlsl"
        # The flags ReGPU compiles with (renderer.cpp: skip optimisation, unbounded descriptor tables).
        & $fxc.FullName /nologo /T ps_5_1 /E main /Od /enable_unbounded_descriptor_tables /Fo "$Work\$n.dxbc" "$Work\$n.hlsl" 2>&1 | Out-Null
        & $Rga -s dx12 -c $Asic --vs-blob "$Work\vs.dxbc" --ps-blob "$Work\$n.dxbc" --rs-bin "$Work\rootsig.bin" --gpso "$Work\fm3.gpso" `
            --isa "$Work\isa_$n.txt" -a "$Work\stats_$n.txt" 2>&1 | Out-Null
        $stats = "$Work\${Asic}_stats_${n}_pixel.txt"
        if (-not (Test-Path $stats)) { "$n : RGA failed"; continue }
        $v = (Get-Content $stats)[1].Split(',')
        $isa = Get-Content "$Work\${Asic}_isa_${n}_pixel.txt"
        [pscustomobject]@{ Shader = $n; VGPRs = [int]$v[10]; SGPRs = [int]$v[7]; IsaBytes = [int]$v[15]
            Samples = ($isa | Select-String 'image_sample|image_load|image_gather').Count
            Branches = ($isa | Select-String 's_cbranch|s_branch').Count }
    }
}
