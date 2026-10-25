// ============================================================
// scToLocalDesk | src/service/id_service.hpp
// 机器身份服务：9 位连接码派生 + 连接名组装（Doc18 §3.1）
// 连接码 = xxh3_64(指纹串, kIdSalt) 折叠 3×3 位十进制；
// 指纹串 = hostname + '\n' + MAC + '\n' + 安装目录（'\n' 防拼接歧义）
// 纯逻辑无平台依赖：原料由 platform 采集后注入（同 InputService 模式）
// ============================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace sc::localdesk {

class IdService final {
public:
    /// salt 内置固定：与分片校验/自检等其它 xxH3 用途域隔离，防碰撞误判
    static constexpr uint64_t kIdSalt = 0x53434C44'53434C44ull;  // "SCLD"x2

    IdService(std::string hostname, std::string username,
              std::string mac, std::string install_dir);

    /// 9 位连接码展示格式 "xxx xxx xxx"
    const std::string& self_code() const;

    /// 纯数字码 0~999999999（心跳扩展/落盘用，P1.4 接线点）
    uint32_t code_number() const;

    /// 连接名 username@hostname（用户名缺失时退化为 hostname）
    const std::string& self_name() const;

    /// 指纹串（诊断日志用）
    const std::string& fingerprint() const;

    /// 指纹派生（测试挂点）：xxH3 → 折叠 3×3 位
    static uint32_t derive_code(std::string_view fingerprint);

    /// 数字码 → "xxx xxx xxx"（不足 9 位左补零）
    static std::string format_code(uint32_t code);

private:
    std::string fingerprint_;
    std::string code_;
    std::string name_;
    uint32_t code_number_ = 0;
};

}  // namespace sc::localdesk
