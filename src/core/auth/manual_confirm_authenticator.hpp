// ============================================================
// scToLocalDesk | src/core/auth/manual_confirm_authenticator.hpp
// 手动确认鉴权策略（V1 默认）：一律抛 UI 弹窗（Doc09 §3）
// 无状态、线程安全
// ============================================================
#pragma once

#include "core/interface/iauthenticator.hpp"

namespace sc::localdesk {

class ManualConfirmAuthenticator final : public IAuthenticator {
public:
    ManualConfirmAuthenticator() = default;

    /// 人工模式：任何请求都交给 UI 确认（Doc09 §3 红线：握手成功 ≠ 会话可用）
    AuthResult decide(const AuthRequest& /*req*/) override {
        return AuthResult::NeedManualConfirm;
    }

    /// 人工模式无挑战应答：恒 false（调用方不应到达此处）
    bool verify_proof(const std::string& /*challenge*/,
                                    const std::string& /*proof*/) override {
        return false;
    }
};

}  // namespace sc::localdesk
