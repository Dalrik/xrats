cmake_minimum_required(VERSION 3.5.1)

project(xrats)

find_package(X11 REQUIRED)

add_executable(xrats xrats.cpp)
target_compile_features(xrats PUBLIC cxx_std_17)
target_link_libraries(xrats X11::X11)
