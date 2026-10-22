// ============================================================
// scToLocalDesk | src/main_headless.cpp
// core-only 冒烟入口：无 Qt、无界面，验证 DI 组装与核心链路
// 产物：scToLocalDeskHeadless.exe（CI / 开发自检用）
// ============================================================
#include <cstdio>
#include <memory>
#include <string>

#include "foundation/config_parser.hpp"
#include "foundation/event_bus.hpp"
#include "foundation/logger/console_logger.hpp"
#include "generated/project_config.h"

using namespace sc::localdesk;
using sc::localdesk::foundation::ConsoleLogger;
using sc::localdesk::foundation::EventBus;
using sc::localdesk::foundation::IniConfig;
using sc::localdesk::foundation::LogLevel;

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--version") {
        std::printf("scToLocalDesk core %d.%d.%d\n",
                    SCTOLOCALDESK_VERSION_MAJOR, SCTOLOCALDESK_VERSION_MINOR,
                    SCTOLOCALDESK_VERSION_PATCH);
        return 0;
    }

    auto logger = std::make_shared<ConsoleLogger>();
    auto bus = std::make_shared<EventBus>();
    logger->log(LogLevel::Info, "main",
                "scToLocalDesk headless 启动 v" + std::to_string(SCTOLOCALDESK_VERSION_MAJOR)
                    + "." + std::to_string(SCTOLOCALDESK_VERSION_MINOR) + "."
                    + std::to_string(SCTOLOCALDESK_VERSION_PATCH));

    // 配置加载 → 默认值兜底（Doc17 §5）
    IniConfig config("config.ini");
    if (!config.load()) {
        logger->log(LogLevel::Info, "main", "config.ini 不存在，使用默认值");
        const int port = config.get_int("network", "ws_port", 8899);
        (void)port;
        // TODO(V1.0): 全量默认值初始化（Doc17 §3），首次运行回写
    }

    // TODO(V1.0): 按需接入 transport / session / auth（与 main.cpp 的 DI 相同，无 UI 层）
    logger->log(LogLevel::Info, "main", "冒烟自检完成，退出");
    return 0;
}
