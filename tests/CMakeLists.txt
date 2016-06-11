find_package(catch)

if(CATCH_FOUND)
    add_executable(test_chunk_parser chunk_parser.cpp ../src/utils/http.cpp ../src/utils/string.cpp)

    add_executable(test_decode decode.cpp ../src/utils/http.cpp ../src/title_parser.cpp ../src/decoder.cpp ../src/utils/entities.cpp ../src/utils/string.cpp)
    target_link_libraries(test_decode z uchardet)

    add_executable(test_privmsg_parser privmsg_parser.cpp)
    target_link_libraries(test_privmsg_parser boost_system)

    add_executable(test_html_unescape unescape.cpp ../src/utils/entities.cpp)

    add_executable(test_iconv iconv.cpp)

    add_executable(test_title_parser title_parser.cpp ../src/title_parser.cpp ../src/utils/entities.cpp)
    target_link_libraries(test_title_parser uchardet)

    add_executable(test_numfmt format.cpp ../src/utils/format.cpp)

    add_executable(test_charset chardet.cpp)
    target_link_libraries(test_charset uchardet)

    add_executable(test_cache cache.cpp)
    target_link_libraries(test_cache boost_system pthread)

    set(test_list
        test_chunk_parser
        test_decode
        test_privmsg_parser
        test_html_unescape
        test_iconv
        test_title_parser
        test_numfmt
        test_charset
        test_cache
        )

    foreach(it ${test_list})
        add_test(NAME ${it} COMMAND "${PROJECT_BINARY_DIR}/tests/${it}")
    endforeach()
endif(CATCH_FOUND)
