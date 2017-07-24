cmake_minimum_required(VERSION 3.0)

project(interception-tools)

add_executable(udevmon udevmon.cpp)
target_include_directories(udevmon PRIVATE "/usr/include/libevdev-1.0")
target_compile_options(udevmon PRIVATE -Wall -Wextra -pedantic -std=c++11)
target_link_libraries(udevmon evdev udev yaml-cpp)

add_executable(intercept intercept.c)
target_include_directories(intercept PRIVATE "/usr/include/libevdev-1.0")
target_compile_options(intercept PRIVATE -Wall -Wextra)
target_link_libraries(intercept evdev)

add_executable(uinput uinput.cpp)
target_include_directories(uinput PRIVATE "/usr/include/libevdev-1.0")
target_compile_options(uinput PRIVATE -Wall -Wextra -pedantic -std=c++11)
target_link_libraries(uinput evdev udev yaml-cpp)

install(TARGETS udevmon RUNTIME DESTINATION bin)
install(TARGETS intercept RUNTIME DESTINATION bin)
install(TARGETS uinput RUNTIME DESTINATION bin)
