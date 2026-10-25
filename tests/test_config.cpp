// ============================================================
// scToLocalDesk | tests/test_config.cpp
// INI 配置用例（Doc12 §5 / Doc17 §5）：往返 / 默认兜底 / 容错解析
// ============================================================
#include "doctest.h"

#include <filesystem>
#include <fstream>

#include "foundation/config_parser.hpp"

using sc::localdesk::foundation::IniConfig;

namespace {

/// 每用例独立临时文件（用例名作文件名，退场清理）
class TempIni {
public:
    explicit TempIni(const char* tag) {
        path_ = std::filesystem::temp_directory_path()
                / ("scld_test_" + std::string(tag) + ".ini");
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    ~TempIni() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    TempIni(const TempIni&) = delete;
    TempIni& operator=(const TempIni&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace

TEST_CASE("文件缺失 load 返回 false")
{
    TempIni t("missing");
    IniConfig cfg(t.path());
    CHECK_FALSE(cfg.load());
}

TEST_CASE("set → save → load 往返")
{
    TempIni t("roundtrip");
    {
        IniConfig cfg(t.path());
        cfg.set_str("App", "Alias", "SC-PC");
        cfg.set_int("Stream", "fps", 15);
        cfg.set_bool("Security", "Confirm", true);
        CHECK(cfg.save());
    }
    IniConfig cfg2(t.path());
    REQUIRE(cfg2.load());
    CHECK(cfg2.get_str("app", "alias", "") == "SC-PC");
    CHECK(cfg2.get_int("stream", "fps", 0) == 15);
    CHECK(cfg2.get_bool("security", "confirm", false));
}

TEST_CASE("section/key 大小写不敏感")
{
    TempIni t("case");
    IniConfig cfg(t.path());
    cfg.set_str("NETWORK", "MulticastAddr", "239.255.0.1");
    // 存取两侧均归一小写：任意大小写组合均命中
    CHECK(cfg.get_str("network", "multicastaddr", "") == "239.255.0.1");
    CHECK(cfg.get_str("Network", "MULTICASTADDR", "") == "239.255.0.1");
}

TEST_CASE("缺失键返回默认值")
{
    TempIni t("default");
    {
        IniConfig cfg(t.path());
        cfg.set_str("a", "b", "1");
        CHECK(cfg.save());
    }
    IniConfig cfg(t.path());
    REQUIRE(cfg.load());
    CHECK(cfg.get_str("a", "missing", "fallback") == "fallback");
    CHECK(cfg.get_int("a", "b", 42) == 1);
    CHECK(cfg.get_int("a", "missing", 42) == 42);
    CHECK(cfg.get_bool("a", "b", true));  // "1" 视为 true
    CHECK(cfg.get_bool("a", "missing", true));
}

TEST_CASE("非法值回退默认")
{
    TempIni t("invalid");
    IniConfig cfg(t.path());
    cfg.set_str("s", "k", "not_a_number");
    CHECK(cfg.get_int("s", "k", 7) == 7);
}

TEST_CASE("引号值去包裹")
{
    TempIni t("quote");
    {
        std::ofstream f(t.path(), std::ios::binary);
        f << "[app]\nalias = \"SC PC\"\n";
    }
    IniConfig cfg(t.path());
    REQUIRE(cfg.load());
    CHECK(cfg.get_str("app", "alias", "") == "SC PC");
}

TEST_CASE("注释与畸形行忽略")
{
    TempIni t("junk");
    {
        std::ofstream f(t.path(), std::ios::binary);
        f << "; comment\n# comment2\nbroken line\n\n[app]\nalias=SC\n;; trailing\n";
    }
    IniConfig cfg(t.path());
    REQUIRE(cfg.load());
    CHECK(cfg.get_str("app", "alias", "") == "SC");
    CHECK(cfg.get_str("", "broken", "dft") == "dft");
}

TEST_CASE("原子落盘无 tmp 残留")
{
    TempIni t("atomic");
    IniConfig cfg(t.path());
    cfg.set_str("a", "b", "c");
    REQUIRE(cfg.save());
    std::error_code ec;
    CHECK_FALSE(std::filesystem::exists(t.path().wstring() + L".tmp", ec));
    // 二次 save 覆盖已存在目标
    cfg.set_str("a", "b", "c2");
    CHECK(cfg.save());
    IniConfig cfg2(t.path());
    REQUIRE(cfg2.load());
    CHECK(cfg2.get_str("a", "b", "") == "c2");
}
