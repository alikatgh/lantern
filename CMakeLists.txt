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
    src/audio.cpp)
target_include_directories(lantern_core
    PUBLIC include
    PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_directories(lantern_core PUBLIC ${SDL2_LIBRARY_DIRS})
target_link_libraries(lantern_core PUBLIC ${SDL2_LIBRARIES})
target_compile_options(lantern_core PRIVATE -Wall -Wextra)

# The bundled Lua host: `lantern <game_dir>` runs <game_dir>/main.lua.
add_executable(lantern src/main.cpp)
target_include_directories(lantern PRIVATE ${LUA_INCLUDE_DIRS})
target_link_directories(lantern PRIVATE ${LUA_LIBRARY_DIRS})
target_link_libraries(lantern lantern_core ${LUA_LINK})
target_compile_options(lantern PRIVATE -Wall -Wextra)

# Pure-C sample game — proves the ABI compiles and links from C.
add_executable(lantern_hello_c games/hello_c/main.c)
set_target_properties(lantern_hello_c PROPERTIES C_STANDARD 99)
target_link_libraries(lantern_hello_c lantern_core)
target_compile_options(lantern_hello_c PRIVATE -Wall -Wextra)
