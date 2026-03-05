cmake_minimum_required(VERSION 3.20)
project(felwood VERSION 0.1.0 LANGUAGES CXX)

# ─────────────────────────────────────────────────────────────────────────────
# Felwood – single-node columnar OLAP execution engine
#
# Build:
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#
# In a real engine we would add:
#   - Link-time optimisation (-flto / /GL + /LTCG)
#   - Profile-guided optimisation (PGO)
#   - Sanitiser builds (asan, ubsan) for CI
#   - A separate benchmark/ and test/ CMake target
# ─────────────────────────────────────────────────────────────────────────────

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)          # -std=c++20 not -std=gnu++20

# Default to Release when the user does not specify a build type
# (single-config generators such as Makefiles / Ninja).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
                 Debug Release RelWithDebInfo MinSizeRel)
endif()

# ── Sources ──────────────────────────────────────────────────────────────────
add_executable(felwood src/main.cpp)

if(WIN32)
    target_link_libraries(felwood PRIVATE ws2_32)
endif()

# ── Include paths ─────────────────────────────────────────────────────────────
# Every translation unit can include as "common/types.hpp", "storage/table.hpp", …
target_include_directories(felwood PRIVATE ${CMAKE_SOURCE_DIR}/src)

# ── Compiler flags ────────────────────────────────────────────────────────────
if(MSVC)
    # MSVC: /O2 = speed, /arch:AVX2 = SIMD, /W4 = warnings
    target_compile_options(felwood PRIVATE
        $<$<CONFIG:Release>:/O2 /arch:AVX2>
        $<$<CONFIG:Debug>:/Od /RTC1 /W4>
    )
else()
    # GCC / Clang: -O3, auto-vectorisation, native SIMD
    target_compile_options(felwood PRIVATE
        $<$<CONFIG:Release>:-O3 -march=native>
        $<$<CONFIG:Debug>:-O0 -g>
        -Wall -Wextra -Wpedantic
    )
endif()
