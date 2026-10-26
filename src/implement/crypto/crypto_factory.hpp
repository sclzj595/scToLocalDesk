// ============================================================
// scToLocalDesk | src/implement/crypto/crypto_factory.hpp
// 加密实现工厂：Windows 走 OS 内建 CNG（bcrypt.lib，零外部依赖），
// 其它平台走 OpenSSL（头存在即启用），都不可用返回显式失败桩
// （会话密钥协商失败时上层降级明文并 Warn 审计，不静默）
// ============================================================
#pragma once

#include <memory>

#include "core/interface/icrypto.hpp"

namespace sc::localdesk {

/// 平台默认后端（业务层唯一入口）
std::shared_ptr<ICrypto> make_default_crypto();

/// 平台内部实现入口（crypto_factory.cpp 分发用）
std::shared_ptr<ICrypto> make_bcrypt_crypto();     ///< Windows CNG
std::shared_ptr<ICrypto> make_openssl_crypto();    ///< OpenSSL / 无头桩

}  // namespace sc::localdesk
