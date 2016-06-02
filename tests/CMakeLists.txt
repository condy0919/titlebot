find_package(catch)

if(CATCH_FOUND)
    add_executable(test_chunk_parser chunk_parser.cpp ../utils/http.cpp)

    add_executable(test_decode decode.cpp ../utils/http.cpp ../title_parser.cpp ../decoder.cpp)
    target_link_libraries(test_decode z)

    add_executable(test_privmsg_parser privmsg_parser.cpp)
    target_link_libraries(test_privmsg_parser boost_system)

    add_executable(test_html_unescape unescape.cpp ../utils/entities.cpp)

    set(test_list
        test_chunk_parser
        test_decode
        test_privmsg_parser
        test_html_unescape
        )

    foreach(it ${test_list})
        add_test(NAME ${it} COMMAND "${PROJECT_BINARY_DIR}/tests/${it}")
    endforeach()
endif(CATCH_FOUND)
