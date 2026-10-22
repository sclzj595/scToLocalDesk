// ============================================================
// scToLocalDesk | src/implement/crypto/crypto_factory.hpp
// 加密实现工厂：默认 OpenSSL；构建未启用时返回显式失败桩
// ============================================================
#pragma once

#include <memory>

#include "core/interface/icrypto.hpp"

namespace sc::localdesk {

/// V1.0：make_default_crypto() 定义于 openssl_crypto.cpp
[[nodiscard]] std::shared_ptr<ICrypto> make_default_crypto();

}  // namespace sc::localdesk
