cmake_minimum_required(VERSION 2.6)

project(titlebot)

enable_testing()
set(CMAKE_ALLOW_LOOSE_LOOP_CONSTRUCTS ON)
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")

add_definitions(-std=c++14)
add_definitions(-O3)
add_definitions(-march=native)
add_definitions(-Wall)
add_definitions(-Wextra)
#add_definitions(-g)
#add_definitions(-ggdb)
#add_definitions(-fno-omit-frame-pointer)
add_definitions(-fuse-ld=gold)


file(GLOB SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/*.cpp)


set(TITLE_LINK_LIBS)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME ON)
find_package(Boost REQUIRED COMPONENTS system)
if(Boost_FOUND)
    list(APPEND TITLE_LINK_LIBS ${Boost_LIBRARIES})
endif()

find_package(Threads REQUIRED)
if(Threads_FOUND)
    list(APPEND TITLE_LINK_LIBS pthread)
endif()

find_package(ZLIB REQUIRED)
string(REGEX REPLACE .so .a ZLIB_STATIC_LIBRARIES ${ZLIB_LIBRARIES})
if(EXISTS ${ZLIB_STATIC_LIBRARIES})
    set(ZLIB_LIBRARIES ${ZLIB_STATIC_LIBRARIES})
endif()
if(ZLIB_FOUND)
    list(APPEND TITLE_LINK_LIBS ${ZLIB_LIBRARIES})
endif()

find_package(jemalloc)
if(JEMALLOC_FOUND)
    list(APPEND TITLE_LINK_LIBS jemalloc)
endif()

## SSL Support
find_package(OpenSSL REQUIRED)
if(OPENSSL_FOUND)
    list(APPEND TITLE_LINK_LIBS ${OPENSSL_LIBRARIES})
endif()

find_package(uchardet REQUIRED)
if(UCHARDET_FOUND)
    list(APPEND TITLE_LINK_LIBS ${UCHARDET_LIBRARIES})
endif()


## Custom
set(ADDITIONAL_LIBS
    #tcmalloc
    #profiler # for gperftools profiling
)


add_executable(titlebot ${SRC})
target_link_libraries(titlebot ${TITLE_LINK_LIBS} ${ADDITIONAL_LIBS})

add_subdirectory(tests)
