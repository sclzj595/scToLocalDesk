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

TEST_CASE("name_safe：目录传输相对路径（Doc19 §5）")
{
    // '/' 分隔相对路径：每级按单文件名红线校验，安全段放行
    CHECK(FileService::name_safe("docs/report.docx"));
    CHECK(FileService::name_safe("root/子目录/a.bin"));
    // 任意一级危险即整体拒绝（防 ../ 穿越红线，覆盖每级）
    CHECK_FALSE(FileService::name_safe("dir/../evil.exe"));     // 中间级父引用
    CHECK_FALSE(FileService::name_safe("dir/sub/.hidden"));     // 中间级隐藏名
    CHECK_FALSE(FileService::name_safe("dir//a.bin"));          // 空段（连续分隔符）
    CHECK_FALSE(FileService::name_safe("/abs/a.bin"));          // 首段空（绝对化）
    CHECK_FALSE(FileService::name_safe("dir/"));                // 尾段空
    CHECK_FALSE(FileService::name_safe("a/b\\c.bin"));          // 任意级反斜杠
    CHECK_FALSE(FileService::name_safe("ok/CON"));              // 中间级保留设备名
    CHECK_FALSE(FileService::name_safe("a/../..//../b"));       // 多级混合穿越
}

TEST_CASE("name_safe：路径穿越与分隔符拒绝")
{
    CHECK_FALSE(FileService::name_safe("../evil.exe"));
    CHECK_FALSE(FileService::name_safe("a..b.txt"));          // 任意位置的父目录引用
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

TEST_CASE("blocked_extension：可执行后缀黑名单（Doc19 §5）")
{
    // 全量黑名单（大小写不敏感）
    CHECK(FileService::blocked_extension("setup.exe"));
    CHECK(FileService::blocked_extension("VIRUS.EXE"));
    CHECK(FileService::blocked_extension("run.bat"));
    CHECK(FileService::blocked_extension("a.cmd"));
    CHECK(FileService::blocked_extension("x.ps1"));
    CHECK(FileService::blocked_extension("inst.msi"));
    CHECK(FileService::blocked_extension("scr.scr"));
    CHECK(FileService::blocked_extension("old.com"));
    CHECK(FileService::blocked_extension("bad.vbs"));
    CHECK(FileService::blocked_extension("page.js"));
    // 目录传输相对路径：取末段后缀判定（Doc19 §5）
    CHECK(FileService::blocked_extension("dir/sub/setup.exe"));
    // 放行
    CHECK_FALSE(FileService::blocked_extension("doc.pdf"));
    CHECK_FALSE(FileService::blocked_extension("archive.tar.gz"));
    CHECK_FALSE(FileService::blocked_extension("noext"));
    CHECK_FALSE(FileService::blocked_extension("exet"));
    CHECK_FALSE(FileService::blocked_extension("dir/ok.txt"));
    // 目录名中的点不参与后缀判定
    CHECK_FALSE(FileService::blocked_extension("v1.2/notes.txt"));
}
