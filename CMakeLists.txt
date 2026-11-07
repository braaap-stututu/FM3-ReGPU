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

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/XenonRecomp/XenonUtils/ppc_context.h"
   OR NOT EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/ReGPU/include/regpu/regpu.h")
    message(FATAL_ERROR "submodules missing: run tools/setup.ps1 (or git submodule update --init --recursive)")
endif()
add_subdirectory(thirdparty/XenonRecomp EXCLUDE_FROM_ALL)

# Recompiled game code: out/ppc, produced by XenonRecomp from your own default.xex
# (config/fm3.toml, Phase 2). Never committed.
set(FM3_PPC_DIR "${CMAKE_SOURCE_DIR}/out/ppc")
file(GLOB FM3_PPC_SOURCES CONFIGURE_DEPENDS "${FM3_PPC_DIR}/*.cpp")
if(NOT FM3_PPC_SOURCES)
    message(FATAL_ERROR "out/ppc is empty: recompile default.xex first (README.md, Phase 2). "
                        "The host tools build on their own: tools/hosttools.")
endif()
add_library(fm3_ppc STATIC ${FM3_PPC_SOURCES})
target_include_directories(fm3_ppc PUBLIC "${FM3_PPC_DIR}" thirdparty/XenonRecomp/thirdparty/simde)
target_compile_options(fm3_ppc PRIVATE -Wno-everything -gline-tables-only "/FI${CMAKE_SOURCE_DIR}/src/ppc_hooks.h")
target_compile_options(fm3_ppc PUBLIC -msse4.1 -fno-strict-aliasing)

# mftb reads the host's raw timestamp counter; give it the console's 49.875 MHz timebase
# (tools/patch_mftb.py, GuestTimebase in src/kernel/runtime.cpp). Idempotent.
execute_process(COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_SOURCE_DIR}/tools/patch_mftb.py" "${FM3_PPC_DIR}"
                OUTPUT_VARIABLE FM3_MFTB_RESULT RESULT_VARIABLE FM3_MFTB_CODE OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT FM3_MFTB_CODE EQUAL 0)
    message(FATAL_ERROR "tools/patch_mftb.py failed on ${FM3_PPC_DIR}")
endif()
message(STATUS "mftb: ${FM3_MFTB_RESULT}")

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
set(REGPU_XENONRECOMP "${CMAKE_SOURCE_DIR}/thirdparty/XenonRecomp")
set(REGPU_PPC_DIR "${FM3_PPC_DIR}")
set(REGPU_FFX "${FM3_FFX}")
if(EXISTS "${FM3_FFX}/signedbin/amd_fidelityfx_upscaler_dx12.dll")
    set(REGPU_FSR3 ON)
endif()
add_subdirectory(thirdparty/ReGPU)

add_executable(fm3 ${FM3_SOURCES})
target_include_directories(fm3 PRIVATE src "${FM3_PPC_DIR}" thirdparty/XenonRecomp/thirdparty/simde)
target_link_libraries(fm3 PRIVATE regpu fm3_ppc XenonUtils synchronization winmm onecore dbghelp xaudio2 bcrypt)
target_compile_definitions(fm3 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(fm3 PRIVATE -gline-tables-only)
target_link_options(fm3 PRIVATE /STACK:0x1000000 /DEBUG)
if(REGPU_FSR3)
    add_custom_command(TARGET fm3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FM3_FFX}/signedbin/amd_fidelityfx_loader_dx12.dll" "${FM3_FFX}/signedbin/amd_fidelityfx_upscaler_dx12.dll" $<TARGET_FILE_DIR:fm3>)
endif()
# ReGPU compiles the translated shaders with the redistributable d3dcompiler_47.dll beside the exe.
if(EXISTS "${REGPU_D3DCOMPILER}")
    add_custom_command(TARGET fm3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${REGPU_D3DCOMPILER}" $<TARGET_FILE_DIR:fm3>)
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
