# 单元测试：仅依赖 sc_localdesk_core + gtest（无网络、无 Qt，Doc14 §1）
find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
    message(STATUS "未找到 gtest，跳过单元测试（vcpkg install gtest 后重试）")
    return()
endif()

add_executable(sc_localdesk_tests
    protocol_test/tlv_test.cpp
    foundation_test/buffer_test.cpp
    foundation_test/event_bus_test.cpp
    foundation_test/bounded_queue_test.cpp
)
target_link_libraries(sc_localdesk_tests PRIVATE
    sc_localdesk_core
    GTest::gtest GTest::gtest_main)

if(MSVC)
    target_compile_options(sc_localdesk_tests PRIVATE /W4)
endif()

include(GoogleTest)
gtest_discover_tests(sc_localdesk_tests)
