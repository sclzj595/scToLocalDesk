// ============================================================
// scToLocalDesk | tests/test_alias_service.cpp
// 静态映射解析与 replace 语义（Doc07 §7 / Doc17 §3.3 热生效）
// ============================================================
#include "doctest.h"

#include <string>

#include "core/events.hpp"
#include "foundation/event_bus.hpp"
#include "foundation/logger/ilogger.hpp"
#include "service/alias_service.hpp"

using sc::localdesk::DeviceInfo;
using sc::localdesk::parse_static_map;

namespace {

/// 静默日志桩（解析器不依赖日志输出，服务用例防空指针）
class SilentLogger : public sc::localdesk::foundation::ILogger {
public:
    void log(sc::localdesk::foundation::LogLevel, const std::string&,
             const std::string&) override {}
    void set_level(sc::localdesk::foundation::LogLevel) override {}
    sc::localdesk::foundation::LogLevel level() const override {
        return sc::localdesk::foundation::LogLevel::Off;
    }
};

}  // namespace

TEST_CASE("静态映射解析：合法/畸形/溢出/空")
{
    SUBCASE("合法单条与多条")
    {
        const auto v = parse_static_map("IT-PC=192.168.1.10:8899");
        REQUIRE(v.size() == 1);
        CHECK(v[0].alias == "IT-PC");
        CHECK(v[0].ip == "192.168.1.10");
        CHECK(v[0].port == 8899);

        const auto multi =
            parse_static_map("A=10.0.0.1:8899;B=10.0.0.2:12345;");
        REQUIRE(multi.size() == 2);
        CHECK(multi[1].alias == "B");
        CHECK(multi[1].port == 12345);
    }
    SUBCASE("畸形条目跳过不致命")
    {
        // 缺端口 / 缺等号 / 空别名 / 端口非数字 / 冒号位置非法
        const auto v =
            parse_static_map("=1.1.1.1:8899;NOEQ;A=ip:abc;B=1.1.1.1:;C=1.1.1.1");
        CHECK(v.empty());
    }
    SUBCASE("端口溢出丢弃（无 throw）")
    {
        CHECK(parse_static_map("A=1.1.1.1:99999").empty());
        CHECK(parse_static_map("A=1.1.1.1:65535").size() == 1);
    }
    SUBCASE("空文本返回空表")
    {
        CHECK(parse_static_map("").empty());
    }
}

TEST_CASE("set_static_map replace 语义：热更新删条目生效")
{
    auto bus = std::make_shared<sc::localdesk::foundation::EventBus>();
    auto log = std::make_shared<SilentLogger>();
    sc::localdesk::AliasService svc(bus, log);

    DeviceInfo a;
    a.alias = "A";
    a.ip = "10.0.0.1";
    a.port = 8899;
    DeviceInfo b = a;
    b.alias = "B";
    b.ip = "10.0.0.2";

    svc.set_static_map({a, b});
    CHECK(svc.online_devices().size() == 2);
    DeviceInfo out;
    CHECK(svc.resolve("A", out));
    CHECK(svc.resolve("B", out));

    // 热更新仅保留 A：B 必须消失（merge 旧语义下 B 会永久残留）
    svc.set_static_map({a});
    CHECK(svc.online_devices().size() == 1);
    CHECK_FALSE(svc.resolve("B", out));
    CHECK(svc.resolve("A", out));

    // 清空：全部移除
    svc.set_static_map({});
    CHECK(svc.online_devices().empty());
}
