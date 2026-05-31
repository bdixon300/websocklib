add_executable(websocklib_tests
    test_tcpclient.cpp
    test_tcpserver.cpp
    test_websocketclient.cpp
    test_webframeserializer.cpp
    # TCPServer is a test-only helper; it is not part of libwebsocklib
    ../src/tcpserver.cpp
)

target_link_libraries(websocklib_tests
    PRIVATE
        websocklib
        GTest::gtest
        GTest::gtest_main
        OpenSSL::SSL
        OpenSSL::Crypto
)

include(GoogleTest)
gtest_discover_tests(websocklib_tests)
