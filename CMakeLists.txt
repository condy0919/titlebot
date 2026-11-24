cmake_minimum_required(VERSION 3.20)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(titlebot
  VERSION 0.1.0
  LANGUAGES CXX
  DESCRIPTION ""
  HOMEPAGE_URL ""
)

include(CTest)
include(FetchContent)

option(TITLEBOT_ENABLE_TESTS "Enable tests" ON)
option(TITLEBOT_ENABLE_BENCHMARKS "Enable benchmarks" OFF)

# Sources for the library are specified at the end
add_library(titlebot "")

### Compile options
# Enable C++23 (Required)
target_compile_features(titlebot
  PUBLIC
    cxx_std_23
)

# Common GCC/Clang options
target_compile_options(titlebot
  PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

### Libraries
find_package(PkgConfig REQUIRED)

# Enable threading support
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(titlebot PRIVATE Threads::Threads)

# uchardet
pkg_search_module(uchardet REQUIRED uchardet)
target_include_directories(titlebot
  PUBLIC
    ${uchardet_INCLUDE_DIRS}
)
target_link_libraries(titlebot
  PUBLIC
    ${uchardet_LIBRARIES}
)

# zlib
find_package(ZLIB REQUIRED)
target_link_libraries(titlebot
  PUBLIC
    ZLIB::ZLIB
)

# openssl
find_package(OpenSSL REQUIRED)
target_link_libraries(titlebot
  PUBLIC
    OpenSSL::SSL
)

# Test
if(TITLEBOT_ENABLE_TESTS)
  # libdoctest.a and libdoctest_with_main.a will be built
  FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest
    GIT_TAG        v2.4.11
    GIT_SHALLOW    true
    GIT_PROGRESS   true
  )
  FetchContent_MakeAvailable(doctest)
endif()

### Definitions

### Includes

### Sources
target_sources(titlebot
  PRIVATE
    # ${CMAKE_CURRENT_SOURCE_DIR}/src/connection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/decoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/irc.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/title_parser.cpp
)

# Tests
if(TITLEBOT_ENABLE_TESTS)
  add_executable(titlebot_unittests
    ${CMAKE_CURRENT_SOURCE_DIR}/src/decoder_unittest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/numfmt_unittest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils_unittest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/irc_unittest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/title_parser_unittest.cpp
  )

  target_compile_definitions(titlebot_unittests
    PUBLIC
      TITLEBOT_PROJ_ROOT="${PROJECT_SOURCE_DIR}"
  )

  target_link_libraries(titlebot_unittests
    PRIVATE
      titlebot
      doctest::doctest_with_main
  )

  add_test(NAME titlebot_unittests COMMAND titlebot_unittests)
endif()
