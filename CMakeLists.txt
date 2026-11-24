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

add_library(titlebot
  src/app/fetch_scheduler.cpp
  src/app/server.cpp
  src/http/fetcher.cpp
  src/irc/irc.cpp
  src/http/decoder.cpp
  src/html/title_parser.cpp
  src/common/utils.cpp
)
target_include_directories(titlebot PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

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
pkg_check_modules(uchardet REQUIRED IMPORTED_TARGET uchardet)
target_link_libraries(titlebot PRIVATE PkgConfig::uchardet)

# zlib
find_package(ZLIB REQUIRED)
target_link_libraries(titlebot
  PRIVATE
    ZLIB::ZLIB
)

# openssl
find_package(OpenSSL REQUIRED)
target_link_libraries(titlebot
  PUBLIC
    OpenSSL::SSL
)

find_package(Boost 1.81 CONFIG REQUIRED COMPONENTS url)
target_link_libraries(titlebot PUBLIC Boost::headers PRIVATE Boost::url)
add_executable(titlebot_cli src/app/main.cpp)
set_target_properties(titlebot_cli PROPERTIES OUTPUT_NAME titlebot)
target_link_libraries(titlebot_cli PRIVATE titlebot)

# Test
if(TITLEBOT_ENABLE_TESTS)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(googletest)

  add_executable(titlebot_unittests
    src/app/fetch_scheduler_test.cpp
    src/app/server_test.cpp
    src/http/fetcher_test.cpp
    src/http/decoder_test.cpp
    src/common/numfmt_test.cpp
    src/common/utils_test.cpp
    src/irc/irc_test.cpp
    src/html/title_parser_test.cpp
  )

  target_compile_definitions(titlebot_unittests
    PUBLIC
      TITLEBOT_PROJ_ROOT="${PROJECT_SOURCE_DIR}"
  )

  target_link_libraries(titlebot_unittests
    PRIVATE
      titlebot
      ZLIB::ZLIB
      GTest::gtest_main
  )

  include(GoogleTest)
  gtest_discover_tests(titlebot_unittests)
endif()
