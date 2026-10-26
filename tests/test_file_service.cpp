// ============================================================
// scToLocalDesk | tests/test_file_service.cpp
// 文件服务纯逻辑用例（Doc18 §6.1）：对端文件名安全校验（PRD P10）
// ============================================================
#include "doctest.h"

#include "service/file_service.hpp"

using sc::localdesk::FileService;

TEST_CASE("name_safe：常规名放行")
{
    CHECK(FileService::name_safe("report.docx"));
    CHECK(FileService::name_safe("数据表 2026.xlsx"));
    CHECK(FileService::name_safe("v1.2.3_backup.tar.gz"));
    CHECK(FileService::name_safe("a"));  // 单字符
}

TEST_CASE("name_safe：路径穿越与分隔符拒绝")
{
    CHECK_FALSE(FileService::name_safe("../evil.exe"));
    CHECK_FALSE(FileService::name_safe("a..b.txt"));          // 任意位置的父目录引用
    CHECK_FALSE(FileService::name_safe("dir/evil.exe"));      // 正斜杠
    CHECK_FALSE(FileService::name_safe("dir\\evil.exe"));     // 反斜杠
    CHECK_FALSE(FileService::name_safe("C:evil.exe"));        // 盘符
}

TEST_CASE("name_safe：元字符/控制符/隐藏名拒绝")
{
    CHECK_FALSE(FileService::name_safe("a*b"));
    CHECK_FALSE(FileService::name_safe("a?b"));
    CHECK_FALSE(FileService::name_safe("a<b>c|d"));
    CHECK_FALSE(FileService::name_safe("a\"b"));
    CHECK_FALSE(FileService::name_safe("a\nb"));    // 控制字符
    CHECK_FALSE(FileService::name_safe("a\x7f"));   // DEL
    CHECK_FALSE(FileService::name_safe(".hidden"));
    CHECK_FALSE(FileService::name_safe(""));        // 空
}

TEST_CASE("name_safe：超长与 Windows 保留设备名")
{
    CHECK_FALSE(FileService::name_safe(std::string(256, 'x')));
    CHECK(FileService::name_safe(std::string(255, 'x')));
    CHECK_FALSE(FileService::name_safe("CON"));
    CHECK_FALSE(FileService::name_safe("con"));     // 大小写不敏感
    CHECK_FALSE(FileService::name_safe("CON.txt")); // 8.3 形态同拦
    CHECK_FALSE(FileService::name_safe("Lpt9.dll"));
    CHECK(FileService::name_safe("console.log"));   // 前缀命中不拦
}
