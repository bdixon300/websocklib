add_executable(websocklib_tests
    test_tcpclient.cpp
    test_tcpserver.cpp
    test_websocketclient.cpp
)

target_link_libraries(websocklib_tests
    PRIVATE
        websocklib
        GTest::gtest
        GTest::gtest_main
        OpenSSL::Crypto
)

include(GoogleTest)
gtest_discover_tests(websocklib_tests)
