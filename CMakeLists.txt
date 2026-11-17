cmake_minimum_required(VERSION 3.25)
project(fm3 C CXX)

# Forza Motorsport 3 (Ultimate Collection, USA, default.xex v0.0.0.8) on XenonRecomp + ReGPU.
# Layout and build flags follow the FM2/FM4 ReGPU ports. Build steps: README.md.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "fm3 needs clang-cl (the recompiled code uses clang builtins)")
endif()
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)

# FM3_RECOMPILER: which recompiler made the game code. xenonrecomp (hedge-dev's, with our 5 patches): out/ppc.
# rexenon (zer0ducksgiven's fork, thirdparty/ReXenon, session 41): out/ppc-rexenon (tools/recomp.ps1 -Recompiler
# rexenon); its timebase comes from PPC_MFTB() in src/ppc_hooks.h instead of tools/patch_mftb.py.
set(FM3_RECOMPILER "rexenon" CACHE STRING "Recompiler of the game code: rexenon (default since session 41) or xenonrecomp")
set_property(CACHE FM3_RECOMPILER PROPERTY STRINGS xenonrecomp rexenon)
if(FM3_RECOMPILER STREQUAL "rexenon")
    set(FM3_XENON_DIR "thirdparty/ReXenon")
    set(FM3_PPC_DEFAULT "${CMAKE_SOURCE_DIR}/out/ppc-rexenon")
else()
    set(FM3_XENON_DIR "thirdparty/XenonRecomp")
    set(FM3_PPC_DEFAULT "${CMAKE_SOURCE_DIR}/out/ppc")
endif()
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${FM3_XENON_DIR}/XenonUtils/ppc_context.h"
   OR NOT EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/ReGPU/include/regpu/regpu.h")
    message(FATAL_ERROR "submodules missing: run tools/setup.ps1 (or git submodule update --init --recursive)")
endif()
add_subdirectory(${FM3_XENON_DIR} EXCLUDE_FROM_ALL)

# Recompiled game code: out/ppc (or out/ppc-rexenon), produced from your own default.xex
# (config/fm3.toml, Phase 2). Never committed.
set(FM3_PPC_DIR "${FM3_PPC_DEFAULT}")
file(GLOB FM3_PPC_SOURCES CONFIGURE_DEPENDS "${FM3_PPC_DIR}/*.cpp")
if(NOT FM3_PPC_SOURCES)
    message(FATAL_ERROR "out/ppc is empty: recompile default.xex first (README.md, Phase 2). "
                        "The host tools build on their own: tools/hosttools.")
endif()
add_library(fm3_ppc STATIC ${FM3_PPC_SOURCES})
target_include_directories(fm3_ppc PUBLIC "${FM3_PPC_DIR}" ${FM3_XENON_DIR}/thirdparty/simde)
if(FM3_RECOMPILER STREQUAL "rexenon")
    target_compile_definitions(fm3_ppc PRIVATE FM3_REXENON=1)
endif()
target_compile_options(fm3_ppc PRIVATE -Wno-everything -gline-tables-only "/FI${CMAKE_SOURCE_DIR}/src/ppc_hooks.h")
target_compile_options(fm3_ppc PUBLIC -msse4.1 -fno-strict-aliasing)

# mftb reads the host's raw timestamp counter; give it the console's 49.875 MHz timebase
# (tools/patch_mftb.py, GuestTimebase in src/kernel/runtime.cpp). Idempotent.
if(NOT FM3_RECOMPILER STREQUAL "rexenon")
    execute_process(COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_SOURCE_DIR}/tools/patch_mftb.py" "${FM3_PPC_DIR}"
                    OUTPUT_VARIABLE FM3_MFTB_RESULT RESULT_VARIABLE FM3_MFTB_CODE OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT FM3_MFTB_CODE EQUAL 0)
        message(FATAL_ERROR "tools/patch_mftb.py failed on ${FM3_PPC_DIR}")
    endif()
    message(STATUS "mftb: ${FM3_MFTB_RESULT}")
endif()

# Weak import stubs (tools/genstubs.py), regenerated from out/ppc so the list always matches.
set(FM3_GENERATED "${CMAKE_BINARY_DIR}/generated")
add_custom_command(OUTPUT "${FM3_GENERATED}/stubs.cpp"
    COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_SOURCE_DIR}/tools/genstubs.py" "${FM3_PPC_DIR}/ppc_recomp_shared.h" "${FM3_GENERATED}/stubs.cpp"
    DEPENDS "${FM3_PPC_DIR}/ppc_recomp_shared.h" "${CMAKE_SOURCE_DIR}/tools/genstubs.py"
    COMMENT "Generating import stubs")

# FM3_PPC_ONLY: build only the recompiled code (a compile check of out/ppc before the runtime
# exists; needs neither src/ nor the FidelityFX SDK).
option(FM3_PPC_ONLY "Build only fm3_ppc (the recompiled code)" OFF)
if(FM3_PPC_ONLY)
    return()
endif()

# Runtime sources. Everything under src/ is ours; address-bound hooks live in src/hooks and are
# addressed for this default.xex only (never FM2/FM4 sub_ addresses).
file(GLOB_RECURSE FM3_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(APPEND FM3_SOURCES "${FM3_GENERATED}/stubs.cpp")

# The GPU: ReGPU (thirdparty/ReGPU, MIT), a static library providing the Vd* imports. It builds
# against XenonRecomp and the game's ppc_config.h, and needs the AMD FidelityFX SDK (MIT) for FSR 1.
set(FM3_FFX "${CMAKE_SOURCE_DIR}/thirdparty/fidelityfx/Kits/FidelityFX")
if(NOT EXISTS "${FM3_FFX}/upscalers/fsr3/include/gpu/fsr1/ffx_fsr1.h")
    message(FATAL_ERROR "thirdparty/fidelityfx is missing: put FidelityFX-SDK v2.x there (README.md)")
endif()
set(REGPU_XENONRECOMP "${CMAKE_SOURCE_DIR}/${FM3_XENON_DIR}")
set(REGPU_PPC_DIR "${FM3_PPC_DIR}")
set(REGPU_FFX "${FM3_FFX}")
if(EXISTS "${FM3_FFX}/signedbin/amd_fidelityfx_upscaler_dx12.dll")
    set(REGPU_FSR3 ON)
endif()
# FM3_REGPU_DIR: which ReGPU to build. Default: our submodule (upstream). For A/B tests it can point
# at another copy, e.g. the FM4 port's vendored ReGPU (more EDRAM emulation); set FM3_REGPU_FM4=ON
# then, as that copy's API differs slightly (src/crash.cpp).
set(FM3_REGPU_DIR "${CMAKE_SOURCE_DIR}/thirdparty/ReGPU" CACHE PATH "ReGPU source folder")
option(FM3_REGPU_FM4 "FM3_REGPU_DIR is the FM4 port's vendored ReGPU" OFF)
# Anti-aliasing filters (session 36): the original FXAA 3.11, SMAA and CMAA 2 shaders in thirdparty/aa (their licences
# there; pinned sources in thirdparty/aa/SOURCES.txt), compiled into ReGPU.
set(REGPU_AA_DIR "${CMAKE_SOURCE_DIR}/thirdparty/aa" CACHE PATH "" FORCE)
# Session 37: VR (ReGPU src/render/xr.cpp, settings.ini vr=screen): the OpenXR SDK's headers and loader in
# thirdparty/openxr (Khronos release 1.1.65, Apache-2.0; not in git, see thirdparty/openxr/SOURCE.txt). Without it the
# game builds without VR.
set(FM3_OPENXR "${CMAKE_SOURCE_DIR}/thirdparty/openxr")
if(EXISTS "${FM3_OPENXR}/include/openxr/openxr.h")
    set(REGPU_OPENXR_DIR "${FM3_OPENXR}" CACHE PATH "" FORCE)
endif()
add_subdirectory("${FM3_REGPU_DIR}" regpu)

# Profile-guided optimisation of ReGPU (session 32, PERF-PLAN L23; FH1R measured +6%): FM3_PGO=gen builds ReGPU
# instrumented (record a profile: pgo/README.md), FM3_PGO=use builds it with FM3_PGO_PROFILE, off = as before.
# Only the regpu target changes, so switching rebuilds ReGPU alone.
set(FM3_PGO "off" CACHE STRING "ReGPU profile-guided optimisation: off, gen or use")
set_property(CACHE FM3_PGO PROPERTY STRINGS off gen use)
set(FM3_PGO_PROFILE "${CMAKE_SOURCE_DIR}/pgo/regpu.profdata" CACHE FILEPATH "ReGPU PGO profile (llvm-profdata merge output)")

add_executable(fm3 ${FM3_SOURCES} "${CMAKE_SOURCE_DIR}/src/fm3.rc")
target_include_directories(fm3 PRIVATE src "${FM3_PPC_DIR}" ${FM3_XENON_DIR}/thirdparty/simde)
target_link_libraries(fm3 PRIVATE regpu fm3_ppc XenonUtils synchronization winmm onecore dbghelp xaudio2 bcrypt)
# Racing wheels, pedals, shifters and handbrakes (src/input/wheel, DirectInput).
target_link_libraries(fm3 PRIVATE dinput8 dxguid)
target_compile_definitions(fm3 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS FM3_REGPU_FM4=$<BOOL:${FM3_REGPU_FM4}>)
target_compile_options(fm3 PRIVATE -gline-tables-only)
target_link_options(fm3 PRIVATE /STACK:0x1000000 /DEBUG)
if(FM3_PGO STREQUAL "gen")
    target_compile_options(regpu PRIVATE -fprofile-instr-generate)
    get_filename_component(FM3_LLVM_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    file(GLOB FM3_PROFILE_RT "${FM3_LLVM_BIN}/../lib/clang/*/lib/windows/clang_rt.profile-x86_64.lib")
    if(NOT FM3_PROFILE_RT)
        message(FATAL_ERROR "FM3_PGO=gen: clang_rt.profile-x86_64.lib not found next to ${CMAKE_CXX_COMPILER}")
    endif()
    list(GET FM3_PROFILE_RT 0 FM3_PROFILE_RT)
    target_link_libraries(fm3 PRIVATE "${FM3_PROFILE_RT}")
    target_compile_definitions(fm3 PRIVATE FM3_PGO_GEN=1)
elseif(FM3_PGO STREQUAL "use")
    if(NOT EXISTS "${FM3_PGO_PROFILE}")
        message(FATAL_ERROR "FM3_PGO=use: no profile at ${FM3_PGO_PROFILE}")
    endif()
    target_compile_options(regpu PRIVATE "-fprofile-instr-use=${FM3_PGO_PROFILE}" -Wno-profile-instr-out-of-date -Wno-profile-instr-unprofiled
        -Wno-backend-plugin)
endif()
if(REGPU_FSR3)
    add_custom_command(TARGET fm3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FM3_FFX}/signedbin/amd_fidelityfx_loader_dx12.dll" "${FM3_FFX}/signedbin/amd_fidelityfx_upscaler_dx12.dll" $<TARGET_FILE_DIR:fm3>)
endif()
# ReGPU compiles the translated shaders with the redistributable d3dcompiler_47.dll beside the exe.
if(EXISTS "${REGPU_D3DCOMPILER}")
    add_custom_command(TARGET fm3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${REGPU_D3DCOMPILER}" $<TARGET_FILE_DIR:fm3>)
endif()
# Session 34: DXC (REGPU_DXC=1, ReGPU src/gpu/dxc.cpp) loads dxcompiler.dll + dxil.dll beside the exe; the SDK's
# redistributables, next to d3dcompiler_47.dll.
get_filename_component(FM3_D3D_REDIST "${REGPU_D3DCOMPILER}" DIRECTORY)
if(EXISTS "${FM3_OPENXR}/bin/openxr_loader.dll")
    add_custom_command(TARGET fm3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FM3_OPENXR}/bin/openxr_loader.dll" $<TARGET_FILE_DIR:fm3>)
endif()
if(EXISTS "${FM3_D3D_REDIST}/dxcompiler.dll" AND EXISTS "${FM3_D3D_REDIST}/dxil.dll")
    add_custom_command(TARGET fm3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FM3_D3D_REDIST}/dxcompiler.dll" "${FM3_D3D_REDIST}/dxil.dll" $<TARGET_FILE_DIR:fm3>)
endif()

# XMA audio through FFmpeg's xma2 decoder (LGPL, linked dynamically): a win64 LGPL shared build
# (BtbN ffmpeg-n8.1-latest-win64-lgpl-shared) in thirdparty/ffmpeg with include/, lib/ and bin/.
set(FM3_FFMPEG "${CMAKE_SOURCE_DIR}/thirdparty/ffmpeg")
if(NOT EXISTS "${FM3_FFMPEG}/include/libavcodec/avcodec.h")
    message(FATAL_ERROR "thirdparty/ffmpeg is missing: extract an FFmpeg win64 LGPL shared build there (README.md)")
endif()
target_include_directories(fm3 PRIVATE "${FM3_FFMPEG}/include")
target_link_libraries(fm3 PRIVATE "${FM3_FFMPEG}/lib/avcodec.lib" "${FM3_FFMPEG}/lib/avutil.lib" winhttp)
file(GLOB FM3_FFMPEG_DLLS "${FM3_FFMPEG}/bin/avcodec-*.dll" "${FM3_FFMPEG}/bin/avutil-*.dll" "${FM3_FFMPEG}/bin/swresample-*.dll")
add_custom_command(TARGET fm3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FM3_FFMPEG_DLLS} $<TARGET_FILE_DIR:fm3>)
