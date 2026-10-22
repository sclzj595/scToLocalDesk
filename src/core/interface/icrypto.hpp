// ============================================================
// scToLocalDesk | src/core/interface/icrypto.hpp
// 加密抽象契约：AES-GCM 载荷加密 + 口令派生 + HMAC（Doc06 §4 / Doc16）
// V1.0 默认实现：OpenSSL（implement/crypto/openssl_crypto）
// ============================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sc::localdesk {

class ICrypto {
public:
    virtual ~ICrypto() = default;

    /// 业务 Payload AES-256-GCM 加密（内含随机 IV，密文格式 iv||tag||ct）
    [[nodiscard]] virtual bool encrypt(const std::vector<uint8_t>& plain,
                                       std::vector<uint8_t>& cipher) = 0;

    [[nodiscard]] virtual bool decrypt(const std::vector<uint8_t>& cipher,
                                       std::vector<uint8_t>& plain) = 0;

    /// PBKDF2-HMAC-SHA256 口令派生，用于无人值守密码（Doc16 §2），迭代 >= 10 万
    [[nodiscard]] virtual bool derive_key(const std::string& password,
                                          std::string_view salt,
                                          std::vector<uint8_t>& key) = 0;

    /// HMAC-SHA256：无人值守挑战应答 proof 计算（Doc05 §6）
    [[nodiscard]] virtual bool hmac_sha256(std::string_view key,
                                           std::string_view data,
                                           std::vector<uint8_t>& mac) = 0;

    /// SHA-256 摘要（口令校验值 / 文件完整性）
    [[nodiscard]] virtual bool sha256(std::string_view data,
                                      std::vector<uint8_t>& digest) = 0;
};

}  // namespace sc::localdesk
