find_package(catch)

if(CATCH_FOUND)
    add_executable(test_chunk_parser chunk_parser.cpp ../utils/http.cpp)
    add_executable(test_decode decode.cpp ../utils/http.cpp ../title_parser.cpp ../decoder.cpp)
    target_link_libraries(test_decode z)

    set(test_list
        test_chunk_parser
        test_decode
        )

    foreach(it ${test_list})
        add_test(NAME ${it} COMMAND "${PROJECT_BINARY_DIR}/tests/${it}")
    endforeach()
endif(CATCH_FOUND)
