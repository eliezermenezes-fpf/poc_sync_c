cmake_minimum_required(VERSION 3.30)

project(play VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(ENABLE_WARNINGS "Enable all warnings" ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

if(ENABLE_WARNINGS)
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()
if(ENABLE_DEBUG)
    add_compile_options(-g -O0)
else()
    add_compile_options(-O2)
endif()


find_package(PkgConfig REQUIRED)

pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0>=1.14)
pkg_check_modules(GSTREAMER_BASE REQUIRED gstreamer-base-1.0>=1.14)
pkg_check_modules(GSTREAMER_APP REQUIRED gstreamer-app-1.0>=1.14)
pkg_check_modules(GSTREAMER_NET REQUIRED gstreamer-net-1.0>=1.14)

set(application play)

add_executable(${application} sync/src/play.c)

target_include_directories(${application} PRIVATE ${GSTREAMER_INCLUDE_DIRS} ${GSTREAMER_BASE_DIRS} ${GSTREAMER_NET_DIRS})

target_link_libraries(${application} PRIVATE ${GSTREAMER_LIBRARIES} ${GSTREAMER_BASE_LIBRARIES} ${GSTREAMER_NET_LIBRARIES})

set(client_app client)

add_executable(${client_app} sync/src/client.c)

target_include_directories(${client_app} PRIVATE ${GSTREAMER_INCLUDE_DIRS} ${GSTREAMER_BASE_DIRS} ${GSTREAMER_NET_DIRS})

target_link_libraries(${client_app} PRIVATE ${GSTREAMER_LIBRARIES} ${GSTREAMER_BASE_LIBRARIES} ${GSTREAMER_NET_LIBRARIES})
