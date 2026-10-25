// ============================================================
// scToLocalDesk | tests/test_trust_store.cpp
// 信任缓存用例（Doc18 §3.2）：信任/解除/清空/落盘重载/码归一化
// ============================================================
#include "doctest.h"

#include <filesystem>
#include <fstream>

#include "service/trust_store.hpp"

using sc::localdesk::TrustStore;

namespace {

/// 每用例独立 json 文件（退场清理）
class TempJson {
public:
    explicit TempJson(const char* tag) {
        path_ = std::filesystem::temp_directory_path()
                / ("scld_test_" + std::string(tag) + ".json");
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    ~TempJson() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    TempJson(const TempJson&) = delete;
    TempJson& operator=(const TempJson&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace

TEST_CASE("信任 / 查询 / 解除 / 清空")
{
    TempJson t("basic");
    TrustStore ts(t.path());

    CHECK_FALSE(ts.is_trusted("66847531"));
    ts.trust("66847531", "办公室-PC");
    CHECK(ts.is_trusted("66847531"));
    CHECK(ts.count() == 1);

    // 重复 trust：刷新而非追加
    ts.trust("66847531", "办公室-PC2");
    CHECK(ts.count() == 1);

    // 解除不存在的码：无操作不落盘
    ts.untrust("000000000");
    CHECK(ts.count() == 1);

    ts.untrust("66847531");
    CHECK_FALSE(ts.is_trusted("66847531"));
    CHECK(ts.count() == 0);

    ts.trust("111111111", "a");
    ts.trust("222222222", "b");
    ts.clear();
    CHECK(ts.count() == 0);
}

TEST_CASE("落盘重载：变更即持久化")
{
    TempJson t("persist");
    {
        TrustStore ts(t.path());
        ts.trust("66847531", "办公室-PC");
        ts.trust("123456789", "会议室-主板");
    }
    // 新实例构造即加载
    TrustStore ts2(t.path());
    CHECK(ts2.count() == 2);
    CHECK(ts2.is_trusted("66847531"));
    CHECK(ts2.is_trusted("123456789"));

    const auto entries = ts2.list();
    REQUIRE(entries.size() == 2);
    bool has_cn_name = false;
    for (const auto& e : entries) {
        if (e.name == "办公室-PC") {
            has_cn_name = true;  // 中文经 ensure_ascii=false 保持 UTF-8 明文
        }
    }
    CHECK(has_cn_name);
}

TEST_CASE("损坏文件视为空表")
{
    TempJson t("corrupt");
    {
        std::ofstream f(t.path(), std::ios::binary);
        f << "{{{not json";
    }
    TrustStore ts(t.path());
    CHECK(ts.count() == 0);
    // 损坏后仍可正常写入重建
    ts.trust("111111111", "a");
    CHECK(ts.is_trusted("111111111"));
}

TEST_CASE("码归一化：含空格照常识别")
{
    TempJson t("normalize");
    TrustStore ts(t.path());
    ts.trust(" 668 475 31 ", "sc");
    // 存储层归一去空格（连接码三段展示格式直接入参）
    CHECK(ts.is_trusted("66847531"));
}

TEST_CASE("list 快照与迭代安全")
{
    TempJson t("list");
    TrustStore ts(t.path());
    ts.trust("111111111", "a");
    const auto entries = ts.list();
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].code == "111111111");
    CHECK(entries[0].name == "a");
    CHECK_FALSE(entries[0].trusted_at.empty());
}
