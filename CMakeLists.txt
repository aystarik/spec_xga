cmake_minimum_required(VERSION 3.5)

project(vgasdl LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(vgasdl main.cpp hqx.cpp colors.h colors.cpp)
#add_executable(colors colors.cc)
target_link_libraries(vgasdl SDL2)
