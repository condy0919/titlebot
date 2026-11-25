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

add_library(titlebot_session OBJECT
  src/app/fetch_scheduler.cpp
  src/app/server.cpp
  src/irc/irc.cpp
)

add_library(titlebot
  src/http/fetcher.cpp
  src/http/decoder.cpp
  src/html/title_parser.cpp
  src/common/utils.cpp
)
target_include_directories(titlebot_session PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

### Compile options
# Enable C++23 (Required)
target_compile_features(titlebot_session
  PUBLIC
    cxx_std_23
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

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.17.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(spdlog)
target_link_libraries(titlebot PRIVATE spdlog::spdlog)

find_package(Boost 1.81 CONFIG REQUIRED COMPONENTS url)
# No old APIs compatible.
target_compile_definitions(titlebot_session PUBLIC
  SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG
  BOOST_ASIO_NO_DEPRECATED
  BOOST_ASIO_NO_TS_EXECUTORS
  BOOST_ASIO_NO_DYNAMIC_BUFFER_V1
)
target_link_libraries(titlebot_session PUBLIC Boost::headers OpenSSL::SSL
  PRIVATE Boost::url spdlog::spdlog Threads::Threads)
target_link_libraries(titlebot PUBLIC titlebot_session PRIVATE Boost::url)
add_executable(titlebot_cli src/app/main.cpp)
set_target_properties(titlebot_cli PROPERTIES OUTPUT_NAME titlebot)
target_link_libraries(titlebot_cli PRIVATE titlebot spdlog::spdlog)

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
    src/http/fetcher_test.cpp
    src/http/decoder_test.cpp
    src/common/deadline_test.cpp
    src/common/log_test.cpp
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
      spdlog::spdlog
      GTest::gtest_main
  )

  # Link the IRC tests with their mock FetchTitle implementation.
  add_executable(titlebot_server_tests src/app/server_test.cpp)
  target_link_libraries(titlebot_server_tests PRIVATE titlebot_session Boost::url
    spdlog::spdlog Threads::Threads GTest::gmock_main)

  include(GoogleTest)
  gtest_discover_tests(titlebot_server_tests)
  gtest_discover_tests(titlebot_unittests)
endif()

foreach(target IN ITEMS titlebot titlebot_session titlebot_cli titlebot_unittests titlebot_server_tests)
  if(TARGET ${target} AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -Wall -Wextra)
  endif()
endforeach()
