cmake_minimum_required(VERSION 3.0)

project(interception-tools)

find_package(Boost REQUIRED)
find_package(Threads REQUIRED)

find_package(PkgConfig)
pkg_check_modules(LIBEVDEV REQUIRED libevdev)

add_executable(udevmon udevmon.cpp)
target_include_directories(udevmon PRIVATE ${LIBEVDEV_INCLUDE_DIRS})
target_compile_options(udevmon PRIVATE -Wall -Wextra -pedantic -std=c++11)
target_link_libraries(udevmon evdev udev yaml-cpp)

add_executable(intercept intercept.c)
target_include_directories(intercept PRIVATE ${LIBEVDEV_INCLUDE_DIRS})
target_compile_options(intercept PRIVATE -Wall -Wextra)
target_link_libraries(intercept evdev)

add_executable(uinput uinput.cpp)
target_include_directories(uinput PRIVATE ${LIBEVDEV_INCLUDE_DIRS})
target_compile_options(uinput PRIVATE -Wall -Wextra -pedantic -std=c++11)
target_link_libraries(uinput evdev udev yaml-cpp)

add_executable(mux mux.cpp)
target_include_directories(mux PRIVATE ${Boost_INCLUDE_DIRS})
target_compile_options(mux PRIVATE -Wall -Wextra -pedantic -std=c++11 -DBOOST_DATE_TIME_NO_LIB)
target_link_libraries(mux Threads::Threads rt)

install(TARGETS udevmon RUNTIME DESTINATION bin)
install(TARGETS intercept RUNTIME DESTINATION bin)
install(TARGETS uinput RUNTIME DESTINATION bin)
install(TARGETS mux RUNTIME DESTINATION bin)
