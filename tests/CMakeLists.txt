find_package(catch)

if(CATCH_FOUND)
    add_executable(test_chunk_parser chunk_parser.cpp ../utils/http.cpp)

    add_executable(test_decode decode.cpp ../utils/http.cpp ../title_parser.cpp ../decoder.cpp ../utils/entities.cpp)
    target_link_libraries(test_decode z)

    add_executable(test_privmsg_parser privmsg_parser.cpp)
    target_link_libraries(test_privmsg_parser boost_system)

    add_executable(test_html_unescape unescape.cpp ../utils/entities.cpp)

    add_executable(test_iconv iconv.cpp)

    add_executable(test_title_parser title_parser.cpp ../title_parser.cpp ../utils/entities.cpp)

    add_executable(test_numfmt format.cpp ../utils/format.cpp)

    set(test_list
        test_chunk_parser
        test_decode
        test_privmsg_parser
        test_html_unescape
        test_iconv
        test_title_parser
        test_numfmt
        )

    foreach(it ${test_list})
        add_test(NAME ${it} COMMAND "${PROJECT_BINARY_DIR}/tests/${it}")
    endforeach()
endif(CATCH_FOUND)
