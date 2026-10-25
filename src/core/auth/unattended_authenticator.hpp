// ============================================================
// scToLocalDesk | src/core/auth/unattended_authenticator.hpp
// 无人值守密码鉴权策略（V1.1，Doc16）：
//   proof = HMAC-SHA256(challenge, K)，K = PBKDF2-HMAC-SHA256(password, salt)
//   本地存储 K（等价凭据）+ SHA256(K)（设置页密码校验）
//   challenge 一次性、10s 有效，防重放；verify 用常数时间比较防时序侧信道
// 线程安全
// ============================================================
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/auth/manual_confirm_authenticator.hpp"
#include "core/interface/icrypto.hpp"

namespace sc::localdesk {

/// 无人值守凭据（config.ini [security] 持久化内容，hex 编码，Doc16 §6）
struct UnattendedSecret {
    std::string salt_hex;         ///< 16B 随机盐
    std::string key_hex;          ///< K = PBKDF2(pwd, salt)，32B —— 等价凭据
    std::string verify_hash_hex;  ///< SHA256(K)，32B —— 设置页密码校验用
};

class UnattendedPasswordAuthenticator final : public IAuthenticator {
public:
    static constexpr size_t kChallengeLen = 16;
    static constexpr size_t kProofLen = 32;     ///< HMAC-SHA256 输出
    static constexpr int64_t kChallengeTtlMs = 10'000;  ///< 挑战有效期（Doc05 §6）

    UnattendedPasswordAuthenticator(std::shared_ptr<ICrypto> crypto,
                                    UnattendedSecret secret,
                                    bool enabled);

    // ---------------- IAuthenticator ----------------

    /// 开关关闭 + unattended 请求 → Rejected（协议层补 REJECT 0x03）
    /// 开关开启 + unattended 请求 → StartChallenge
    /// 非 unattended 请求 → NeedManualConfirm（回退人工模式）
    AuthResult decide(const AuthRequest& req) override;

    /// 校验 AUTH_PROOF：challenge 有效性 + HMAC 常数时间比较
    bool verify_proof(std::string_view challenge,
                                    std::string_view proof) override;

    // ---------------- 服务层专用 ----------------

    /// 生成一次性挑战并登记（hex 字符串；内部记录过期时间）
    std::string issue_challenge();

    /// 用口令校验 hash 校验设置页输入（sha256(PBKDF2(pwd,salt)) == verify_hash）
    bool check_password(const std::string& password) const;

    /// 设置新密码：生成盐 → 派生 K → 计算校验 hash → 返回新凭据（调用方落盘）
    bool set_password(const std::string& password, UnattendedSecret& out_secret);

    void update_secret(UnattendedSecret secret, bool enabled);

    bool enabled() const;
    const UnattendedSecret& secret() const;

    /// 清理过期挑战（会话服务周期调用；返回清理条数）
    size_t purge_expired();

private:
    /// 常数时间比较，防时序侧信道（Doc16 §2.8）
    static bool constant_time_equal(std::string_view a, std::string_view b);

    bool compute_proof(std::string_view challenge_hex,
                                     std::vector<uint8_t>& proof) const;

    mutable std::mutex mtx_;
    std::shared_ptr<ICrypto> crypto_;
    UnattendedSecret secret_;
    bool enabled_{false};

    /// 已签发挑战：challenge_hex -> expire_ms（steady_clock 毫秒）
    std::unordered_map<std::string, int64_t> pending_challenges_;
};

}  // namespace sc::localdesk
