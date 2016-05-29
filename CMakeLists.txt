cmake_minimum_required(VERSION 2.6)

project(titlebot)

add_definitions(-std=c++17)
add_definitions(-O2)
add_definitions(-Wall)
add_definitions(-Wextra)
add_definitions(-g)
add_definitions(-ggdb)

file(GLOB SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/*.cpp)

add_library(titlebot STATIC ${SRC})

target_link_libraries(titlebot
    /usr/lib/libboost_system.so
    /usr/lib/libpthread.so)
