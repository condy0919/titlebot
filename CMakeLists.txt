cmake_minimum_required(VERSION 3.20)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(titlebot VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(FetchContent)
option(TITLEBOT_ENABLE_TESTS "Enable tests" ON)
option(TITLEBOT_BUILD_APP "Build the titlebot library and executable" ON)
option(IRCBOT_BUILD_EXAMPLES "Build the standalone IRC bot example" OFF)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost 1.81 CONFIG REQUIRED)

add_library(ircbot
  src/ircbot/client.cpp
  src/ircbot/features.cpp
  src/ircbot/message.cpp
  src/ircbot/registration.cpp
)
add_library(ircbot::ircbot ALIAS ircbot)
include(GNUInstallDirs)
target_include_directories(ircbot PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_include_directories(ircbot PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(ircbot PUBLIC cxx_std_23)
target_compile_definitions(ircbot PUBLIC
  BOOST_ASIO_NO_DEPRECATED
  BOOST_ASIO_NO_TS_EXECUTORS
  BOOST_ASIO_NO_DYNAMIC_BUFFER_V1
)
target_link_libraries(ircbot PUBLIC Boost::headers Threads::Threads PRIVATE OpenSSL::SSL)
install(TARGETS ircbot)
install(FILES include/ircbot/client.hpp include/ircbot/features.hpp include/ircbot/message.hpp
  include/ircbot/registration.hpp include/ircbot/transport.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ircbot)

if(IRCBOT_BUILD_EXAMPLES)
  add_executable(ircbot_echo examples/echo_bot.cpp)
  target_link_libraries(ircbot_echo PRIVATE ircbot::ircbot)
endif()

if(TITLEBOT_BUILD_APP)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(uchardet REQUIRED IMPORTED_TARGET uchardet)
  find_package(ZLIB REQUIRED)
  find_package(Boost 1.81 CONFIG REQUIRED COMPONENTS url)
  FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.17.0
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(spdlog)

  add_library(titlebot
    src/app/title_cache.cpp
    src/app/fetch_scheduler.cpp
    src/app/server.cpp
    src/irc/policy.cpp
    src/irc/irc.cpp
    src/http/fetcher.cpp
    src/http/decoder.cpp
    src/html/title_parser.cpp
    src/common/utils.cpp
  )
  target_include_directories(titlebot PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_definitions(titlebot PUBLIC SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)
  target_link_libraries(titlebot PUBLIC ircbot::ircbot
    PRIVATE OpenSSL::SSL PkgConfig::uchardet ZLIB::ZLIB Boost::url spdlog::spdlog)
  add_executable(titlebot_cli src/app/main.cpp)
  set_target_properties(titlebot_cli PROPERTIES OUTPUT_NAME titlebot)
  target_link_libraries(titlebot_cli PRIVATE titlebot spdlog::spdlog)
endif()

if(TITLEBOT_ENABLE_TESTS)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(googletest)
  include(GoogleTest)

  add_executable(ircbot_tests
    src/ircbot/client_test.cpp
    src/ircbot/features_test.cpp
    src/ircbot/message_test.cpp
    src/ircbot/registration_test.cpp
  )
  target_compile_definitions(ircbot_tests PRIVATE IRCBOT_TEST_ROOT="${PROJECT_SOURCE_DIR}")
  target_link_libraries(ircbot_tests PRIVATE ircbot::ircbot OpenSSL::SSL GTest::gtest_main)
  gtest_discover_tests(ircbot_tests)

  if(TITLEBOT_BUILD_APP)
    add_executable(titlebot_unittests
      src/app/title_cache_test.cpp
      src/app/fetch_scheduler_test.cpp
      src/http/fetcher_test.cpp
      src/http/decoder_test.cpp
      src/common/deadline_test.cpp
      src/common/log_test.cpp
      src/common/numfmt_test.cpp
      src/common/utils_test.cpp
      src/irc/policy_test.cpp
      src/irc/irc_test.cpp
      src/html/title_parser_test.cpp
    )
    target_compile_definitions(titlebot_unittests PRIVATE TITLEBOT_PROJ_ROOT="${PROJECT_SOURCE_DIR}")
    target_link_libraries(titlebot_unittests PRIVATE titlebot OpenSSL::SSL ZLIB::ZLIB
      spdlog::spdlog GTest::gtest_main)
    add_executable(titlebot_server_tests src/app/server_test.cpp)
    target_link_libraries(titlebot_server_tests PRIVATE titlebot Boost::url
      spdlog::spdlog GTest::gmock_main)
    gtest_discover_tests(titlebot_server_tests)
    gtest_discover_tests(titlebot_unittests)
  endif()
endif()

foreach(target IN ITEMS ircbot ircbot_echo ircbot_tests titlebot titlebot_cli
    titlebot_unittests titlebot_server_tests)
  if(TARGET ${target} AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -Wall -Wextra)
  endif()
endforeach()
