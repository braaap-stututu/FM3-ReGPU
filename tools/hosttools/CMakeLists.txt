# Host-side tools: the patched XenonRecomp / XenonAnalyse, plus small helpers that read the XEX.
# Built on their own (they must exist before out/ppc does):
#   tools\vcenv.cmd cmake -S tools/hosttools -B build/tools -G Ninja -DCMAKE_BUILD_TYPE=Release ^
#       -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
#   tools\vcenv.cmd ninja -C build/tools
cmake_minimum_required(VERSION 3.20)
project(fm3tools CXX C)
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 20)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/XenonRecomp/XenonRecomp/recompiler.cpp")
    message(FATAL_ERROR "thirdparty/XenonRecomp is missing: git submodule update --init --recursive")
endif()
add_subdirectory(../../thirdparty/XenonRecomp xenonrecomp)

# Decrypted, decompressed image of a XEX plus its section list (Phase 2 input for the config scans).
add_executable(xexdump xexdump.cpp)
target_link_libraries(xexdump PRIVATE XenonUtils fmt::fmt)

# Build everything by default (XenonRecomp's own targets are not EXCLUDE_FROM_ALL here).
