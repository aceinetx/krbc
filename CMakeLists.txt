cmake_minimum_required(VERSION 3.20)
project(krbc)

set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED YES)

if(LINUX)
	add_compile_options(-Wall -Wpedantic -Wextra)
endif()

file(GLOB SOURCES CONFIGURE_DEPENDS "src/*.c")

include_directories(include)

add_executable(krbc ${SOURCES})
target_precompile_headers(krbc PUBLIC include/krbc.h)
