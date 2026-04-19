add_executable(websocklib_tests
    test_tcpclient.cpp
    test_tcpserver.cpp
)

target_link_libraries(websocklib_tests
    PRIVATE
        websocklib
        GTest::gtest
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(websocklib_tests)
