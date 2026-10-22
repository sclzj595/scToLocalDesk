// ============================================================
// scToLocalDesk | src/core/interface/iauthenticator.hpp
// 鉴权策略抽象契约：手动确认（默认）/ 无人值守密码（V1.1）（Doc06 §7 / Doc09 / Doc16）
// ============================================================
#pragma once

#include <string_view>

#include "core/interface/types.hpp"

namespace sc::localdesk {

class IAuthenticator {
public:
    virtual ~IAuthenticator() = default;

    /// 连接请求鉴权路由决策（PendingApprove 入口）
    [[nodiscard]] virtual AuthResult decide(const AuthRequest& req) = 0;

    /// 无人值守挑战应答校验：proof = HMAC-SHA256(challenge, K)（Doc05 §6）
    /// 实现必须使用常数时间比较，防时序侧信道（Doc16 §2.8）
    [[nodiscard]] virtual bool verify_proof(std::string_view challenge,
                                            std::string_view proof) = 0;
};

}  // namespace sc::localdesk
