# ============================================================
# scToLocalDesk | tests/CMakeLists.txt
# 单元测试（Doc18 §6.1）：doctest header-only，链接核心静态库
# 用例覆盖：protocol / bounded_queue / config / trust_store / id_service
# ============================================================

add_executable(sc_localdesk_tests
    main.cpp
    test_protocol.cpp
    test_bounded_queue.cpp
    test_config.cpp
    test_trust_store.cpp
    test_id_service.cpp
    test_file_service.cpp
)

target_include_directories(sc_localdesk_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/doctest/doctest
)

target_link_libraries(sc_localdesk_tests PRIVATE sc_localdesk_core)

if(MSVC)
    # 不开 /WX：vendored doctest.h 在 /W4 下有告警，测试代码保持 /W3 自净
    target_compile_options(sc_localdesk_tests PRIVATE /W3 /utf-8)
    target_compile_definitions(sc_localdesk_tests PRIVATE
        _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(sc_localdesk_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME sc_localdesk_tests COMMAND sc_localdesk_tests)
