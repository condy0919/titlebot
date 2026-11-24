add_definitions(-g)
add_definitions(-ggdb)

include_directories(../src)

add_executable(httpie httpclient.cpp ../src/http/connection.cpp ../src/http/decoder.cpp ../src/http/fetcher.cpp ../src/http/global.cpp ../src/http/ssl_ctx.cpp ../src/html/title_parser.cpp ../src/utils/entities.cpp ../src/utils/format.cpp ../src/utils/http.cpp ../src/utils/string.cpp)

target_link_libraries(httpie ssl crypto boost_system pthread z uchardet)
