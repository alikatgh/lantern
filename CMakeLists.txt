cmake_minimum_required(VERSION 3.20)
project(lantern CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(LUA REQUIRED lua)

# Distribution builds link Lua statically (one fewer dylib to bundle).
option(LANTERN_STATIC_LUA "Link Lua as a static library" OFF)
if(LANTERN_STATIC_LUA)
    find_library(LUA_STATIC liblua.a PATHS ${LUA_LIBRARY_DIRS} REQUIRED)
    set(LUA_LINK ${LUA_STATIC})
else()
    set(LUA_LINK ${LUA_LIBRARIES})
endif()

# The engine proper — what C/C++ games link against (include/lantern.h).
add_library(lantern_core STATIC
    src/engine.cpp
    src/gfx.cpp
    src/obj.cpp
    src/audio.cpp
    src/platform_sdl.cpp)
target_include_directories(lantern_core
    PUBLIC include
    PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_directories(lantern_core PUBLIC ${SDL2_LIBRARY_DIRS})
target_link_libraries(lantern_core PUBLIC ${SDL2_LIBRARIES})
target_compile_options(lantern_core PRIVATE -Wall -Wextra)

# wick — lantern's own scripting language (compiler + VM, no dependencies).
add_library(wick STATIC
    wick/wick_front.cpp
    wick/wick_vm.cpp)
target_include_directories(wick PUBLIC wick)
target_compile_options(wick PRIVATE -Wall -Wextra)

# The bundled host: `lantern <game_dir | game.lant>` runs main.wick if
# present, else main.lua; .lant packages extract to a temp dir first.
add_executable(lantern src/main.cpp src/wick_host.cpp src/package.cpp)
target_include_directories(lantern PRIVATE ${LUA_INCLUDE_DIRS})
target_link_directories(lantern PRIVATE ${LUA_LIBRARY_DIRS})
target_link_libraries(lantern lantern_core wick ${LUA_LINK})
target_compile_options(lantern PRIVATE -Wall -Wextra)

# Pure-C sample game — proves the ABI compiles and links from C.
add_executable(lantern_hello_c games/hello_c/main.c)
set_target_properties(lantern_hello_c PROPERTIES C_STANDARD 99)
target_link_libraries(lantern_hello_c lantern_core)
target_compile_options(lantern_hello_c PRIVATE -Wall -Wextra)

# Touch demo — C game shared with the iOS app (its host drives the same
# touchdemo_* functions from a display-link loop).
add_executable(lantern_touchdemo games/touchdemo/main.c)
set_target_properties(lantern_touchdemo PROPERTIES C_STANDARD 99)
target_link_libraries(lantern_touchdemo lantern_core)
target_compile_options(lantern_touchdemo PRIVATE -Wall -Wextra)

# ABI-level touch test: injects SDL events, asserts mapping + edges.
enable_testing()
add_executable(lantern_touch_test tests/touch_test.c)
set_target_properties(lantern_touch_test PROPERTIES C_STANDARD 99)
target_include_directories(lantern_touch_test PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(lantern_touch_test lantern_core)
add_test(NAME touch COMMAND lantern_touch_test)

# wick language suite: semantics assertions + must-fail-to-compile cases.
add_test(NAME wick COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/wick_test.sh
                                $<TARGET_FILE:lantern>)

# The store packager: `lantern_pack <game_dir> <out.lant>`.
add_executable(lantern_pack tools/pack_main.cpp src/package.cpp)
target_compile_options(lantern_pack PRIVATE -Wall -Wextra)

# .lant round trip: packed game renders byte-identical to its folder, and
# a corrupted package is refused.
add_test(NAME package
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_test.sh
                      $<TARGET_FILE:lantern> $<TARGET_FILE:lantern_pack>
                      ${CMAKE_CURRENT_SOURCE_DIR})
