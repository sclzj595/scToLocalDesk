// ============================================================
// scToLocalDesk | tests/test_id_service.cpp
// 机器身份用例（Doc18 §3.1）：连接码派生 / 展示格式 / 连接名组装
// ============================================================
#include "doctest.h"

#include "service/id_service.hpp"

using sc::localdesk::IdService;

TEST_CASE("derive_code 确定性与值域")
{
    const std::string fp = "PC-01\nAA:BB:CC:DD:EE:FF\nC:\\Apps\\scToLocalDesk";
    const auto a = IdService::derive_code(fp);
    const auto b = IdService::derive_code(fp);
    CHECK(a == b);                 // 同指纹同码
    CHECK(a < 1000000000u);        // 9 位以内
}

TEST_CASE("指纹变化码随之变化")
{
    const auto a = IdService::derive_code("h1\nmac1\ndir1");
    const auto b = IdService::derive_code("h2\nmac1\ndir1");
    CHECK(a != b);
}

TEST_CASE("format_code 三段分组与左补零")
{
    CHECK(IdService::format_code(0) == "000 000 000");
    CHECK(IdService::format_code(7) == "000 000 007");
    CHECK(IdService::format_code(123456789) == "123 456 789");
    CHECK(IdService::format_code(999999999) == "999 999 999");
}

TEST_CASE("self_name 组装与退化")
{
    SUBCASE("用户名在场")
    {
        IdService id("PC-01", "zhang", "AA:BB", "C:\\app");
        CHECK(id.self_name() == "zhang@PC-01");
    }
    SUBCASE("用户名缺失退化为 hostname")
    {
        IdService id("PC-01", "", "AA:BB", "C:\\app");
        CHECK(id.self_name() == "PC-01");
    }
}

TEST_CASE("指纹串换行分隔防拼接歧义")
{
    IdService id("h1", "u", "mac", "dir");
    CHECK(id.fingerprint() == "h1\nmac\ndir");
    CHECK(id.code_number() == IdService::derive_code(id.fingerprint()));
    CHECK(id.self_code() == IdService::format_code(id.code_number()));
}

TEST_CASE("实例码与展示码一致")
{
    IdService id("SC-PC", "11", "AA:BB:CC:DD:EE:FF", "G:/app");
    // code_number ∈ [0, 1e9)，self_code 为其三段展示
    CHECK(id.code_number() < 1000000000u);
    CHECK(id.self_code().size() == 11);  // 9 数字 + 2 空格
}
