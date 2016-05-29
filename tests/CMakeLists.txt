find_package(catch)

if(CATCH_FOUND)
    add_executable(chunk_parser chunk_parser.cpp ../utils/http.cpp)

    set(test_list
        chunk_parser
        )

    foreach(it ${test_list})
        add_test(NAME ${it} COMMAND "${PROJECT_BINARY_DIR}/tests/${it}")
    endforeach()
endif(CATCH_FOUND)
