// ============================================================
// scToLocalDesk | src/main_headless.cpp
// core-only 冒烟入口：无 Qt、无界面，验证 DI 组装与核心链路
// 产物：scToLocalDeskHeadless.exe（CI / 开发自检用）
// 检查项（每项一行 PASS/FAIL，任一 FAIL 退出码 1）：
//   filemeta_flags     FileMeta is_dir flags build→parse 往返
//   conflict_rename    同名冲突 rename 策略 _1/_2 递增（真实接收路径，
//                      同时覆盖 .scpart → 目标名原子转正）
//   blocked_extension  可执行后缀黑名单判定
//   clipboard_sync     剪贴板 build/parse 往返 + 超限拒
//   manual_peers       [discovery] manual_peers 配置解析（alias|ip[:port]）
// ============================================================
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "core/interface/types.hpp"
#include "core/protocol/tlv.hpp"
#include "foundation/config_parser.hpp"
#include "foundation/event_bus.hpp"
#include "foundation/hashx.hpp"
#include "foundation/logger/console_logger.hpp"
#include "generated/project_config.h"
#include "service/alias_service.hpp"
#include "service/file_service.hpp"

using namespace sc::localdesk;
using sc::localdesk::foundation::ConsoleLogger;
using sc::localdesk::foundation::EventBus;
using sc::localdesk::foundation::IniConfig;
using sc::localdesk::foundation::LogLevel;

namespace {

int g_failed = 0;

/// 每项一行 PASS/FAIL（冒烟口径：任一 FAIL 退出码非零）
void check(bool ok, const char* name) {
    std::printf("[smoke] %s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) {
        ++g_failed;
    }
}

// 分片校验种子域（Doc18 §5："SCLD"，与 file_service kChunkSeed 同源）
constexpr uint64_t kChunkSeed = 0x53434C4400000000ULL;

std::string hex_to_bytes(const std::string& hex) {
    std::string raw;
    raw.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        raw.push_back(static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return raw;
}

/// build_* 返回完整帧（kHeaderSize 头 + body）；parse_* 吃裸 body——剥头
std::vector<uint8_t> body_of(const std::vector<uint8_t>& frame) {
    return std::vector<uint8_t>(frame.begin() + protocol::kHeaderSize, frame.end());
}

void append_be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 24));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

void append_be64(std::vector<uint8_t>& v, uint64_t x) {
    for (int s = 56; s >= 0; s -= 8) {
        v.push_back(static_cast<uint8_t>((x >> s) & 0xFF));
    }
}

/// 冒烟 1：FileMeta is_dir flags build→parse 往返（Doc19 §5 flags 兼容扩展）
void smoke_filemeta_flags() {
    bool ok = true;
    // 目录条目：flags bit0 = 1
    const auto dir_body = body_of(protocol::build_file_meta(
        "0123456789abcdef0123456789abcdef", "docs", 0, 0, kFileFlagIsDir));
    protocol::FileMetaBody m;
    ok = ok && protocol::parse_file_meta(dir_body.data(), dir_body.size(), m);
    ok = ok && m.name == "docs" && m.size == 0 && m.total_chunks == 0
         && (m.flags & kFileFlagIsDir) != 0;
    // 文件条目：flags=0（老格式字节面不变）
    const auto file_body = body_of(protocol::build_file_meta(
        "0123456789abcdef0123456789abcdef", "a.txt", 123, 2, 0));
    ok = ok && protocol::parse_file_meta(file_body.data(), file_body.size(), m);
    ok = ok && m.name == "a.txt" && m.size == 123 && m.total_chunks == 2
         && m.flags == 0;
    check(ok, "filemeta_flags  FileMeta is_dir flags build->parse 往返");
}

/// 冒烟 2：同名冲突 rename 策略（_1/_2 递增）+ .scpart 原子转正。
/// unique_target_name 为 file_service.cpp 内部函数（不可独立调用），故走
/// 真实接收路径：accept_file（policy=rename 就地定盘名）→ 单分片 → 转正
void smoke_conflict_rename(const std::shared_ptr<EventBus>& bus,
                           const std::shared_ptr<ConsoleLogger>& logger) {
    namespace fs = std::filesystem;
    bool ok = true;
    const auto dir = fs::temp_directory_path() / "scld_smoke_rename";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    // 预置同名冲突源文件 a.txt（内容占位，不应被覆盖）
    { std::ofstream f(dir / "a.txt", std::ios::binary); f << "orig"; }

    FileService files(nullptr, bus, logger);  // 接收路径不触 transport，空指针安全
    files.set_confirm_enabled(false);         // 跳过前置确认直进接收
    files.set_overwrite_policy("rename");     // 冲突自动重命名（Doc19 §5）
    files.set_safe_path(true);

    const std::string payload = "hello";
    for (int round = 1; round <= 2 && ok; ++round) {
        // 32 位 hex file_id（round=1/2 区分两轮传输）
        const std::string file_id =
            "000000000000000" + std::to_string(round) + "000000000000000f";
        FileMeta meta;
        meta.file_id = file_id;
        meta.name = "a.txt";
        meta.size = payload.size();
        meta.total_chunks = 1;
        ok = ok && files.accept_file(meta, dir.u8string());
        // FILE_CHUNK：file_id(16)+idx(4)+total(4)+last(1)+len(4)+checksum(8)+data
        std::vector<uint8_t> chunk;
        const std::string raw_id = hex_to_bytes(file_id);
        chunk.insert(chunk.end(), raw_id.begin(), raw_id.end());
        append_be32(chunk, 0);
        append_be32(chunk, 1);
        chunk.push_back(1);  // last
        append_be32(chunk, static_cast<uint32_t>(payload.size()));
        append_be64(chunk, foundation::xxh3_64(payload.data(), payload.size(),
                                               kChunkSeed));
        chunk.insert(chunk.end(), payload.begin(), payload.end());
        ok = ok && files.on_file_chunk(chunk.data(), chunk.size());
        // rename 策略：第 1 轮落 a_1.txt，第 2 轮落 a_2.txt（递增找空位）
        const std::string expect =
            "a_" + std::to_string(round) + ".txt";
        ok = ok && fs::exists(dir / expect, ec);
        ok = ok && fs::file_size(dir / expect, ec) == payload.size();
    }
    // .scpart 已全部原子转正 + 源文件未被覆盖
    ok = ok && !fs::exists(dir / "a.txt.scpart", ec)
         && !fs::exists(dir / "a_1.txt.scpart", ec);
    {
        std::ifstream f(dir / "a.txt", std::ios::binary);
        std::string content;
        f >> content;
        ok = ok && content == "orig";
    }
    fs::remove_all(dir, ec);
    check(ok, "conflict_rename  同名冲突 _1/_2 递增 + .scpart 原子转正");
}

/// 冒烟 3：可执行后缀黑名单判定（Doc19 §5：命中 .exe/.ps1，放行 .txt）
void smoke_blocked_extension() {
    bool ok = true;
    ok = ok && FileService::blocked_extension("setup.exe")
         && FileService::blocked_extension("run.ps1")
         && FileService::blocked_extension("dir/sub/EVIL.EXE");
    ok = ok && !FileService::blocked_extension("notes.txt")
         && !FileService::blocked_extension("noext")
         && !FileService::blocked_extension("dir/ok.txt");
    check(ok, "blocked_extension  可执行后缀黑名单命中/放行");
}

/// 冒烟 4：剪贴板 build/parse 往返（PRD P9）+ 超限拒构建
void smoke_clipboard_sync() {
    bool ok = true;
    const std::string text = "你好 scToLocalDesk clipboard";
    const auto body = body_of(protocol::build_clipboard_sync(text));
    ok = ok && !body.empty();
    std::string out;
    ok = ok && protocol::parse_clipboard_sync(body.data(), body.size(), out)
         && out == text;
    // 超限（1MB 上限）构建即拒（空帧），空帧 parse 拒
    const std::string big(protocol::kClipboardMaxBytes + 1, 'x');
    const auto oversized = protocol::build_clipboard_sync(big);
    ok = ok && oversized.empty();
    ok = ok && !protocol::parse_clipboard_sync(oversized.data(), oversized.size(), out);
    check(ok, "clipboard_sync    build/parse 往返 + 超限拒");
}

/// 冒烟 5：manual_peers 配置解析（Doc19 §6.3）：alias|ip[:port] 拆分、
/// 缺省端口、畸形条目跳过、同名去重
void smoke_manual_peers() {
    bool ok = true;
    const auto list = parse_manual_peers(
        "A|192.168.1.10;B|10.0.0.2:9000;bad;C|999.1.1.1;D|1.2.3.4:0;"
        "E|1.2.3.4:abc;A|192.168.1.11",
        8899);
    ok = ok && list.size() == 2;  // 畸形 5 条跳过，A 去重保留后者
    if (list.size() == 2) {
        ok = ok && list[0].alias == "A" && list[0].ip == "192.168.1.11"
             && list[0].port == 8899 && list[0].manual;   // 缺省端口=默认 WS 端口
        ok = ok && list[1].alias == "B" && list[1].ip == "10.0.0.2"
             && list[1].port == 9000 && list[1].manual;   // 显式端口生效
    }
    // 空配置安全
    ok = ok && parse_manual_peers("", 8899).empty();
    check(ok, "manual_peers      alias|ip[:port] 解析/缺省端口/去重/畸形跳过");
}

}  // namespace

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

    // ---------------- core 冒烟（Doc19 §6.3 / §5，失败退出码 1） ----------------
    smoke_filemeta_flags();
    smoke_conflict_rename(bus, logger);
    smoke_blocked_extension();
    smoke_clipboard_sync();
    smoke_manual_peers();

    std::printf("[smoke] %s（失败 %d 项）\n", g_failed == 0 ? "冒烟自检完成" : "冒烟存在失败",
                g_failed);
    logger->log(g_failed == 0 ? LogLevel::Info : LogLevel::Error, "main",
                g_failed == 0 ? "冒烟自检完成，退出" : "冒烟存在失败项，退出");
    return g_failed == 0 ? 0 : 1;
}
