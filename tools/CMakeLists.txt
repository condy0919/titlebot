add_definitions(-g)
add_definitions(-ggdb)

add_executable(httpie httpclient.cpp ../src/connection.cpp ../src/decoder.cpp ../src/fetcher.cpp ../src/global.cpp ../src/ssl_ctx.cpp ../src/title_parser.cpp ../src/utils/entities.cpp ../src/utils/format.cpp ../src/utils/http.cpp ../src/utils/string.cpp)

target_link_libraries(httpie ssl crypto boost_system pthread z uchardet)
