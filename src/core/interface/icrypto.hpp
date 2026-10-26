// ============================================================
// scToLocalDesk | src/core/interface/icrypto.hpp
// 加密抽象契约：AES-GCM 载荷加密 + 口令派生 + HMAC + ECDH 密钥协商
// （Doc06 §4 / Doc16 / PRD 会话加密）。后端选择见 crypto_factory：
//   Windows  OS 内建 CNG（bcrypt.lib，零外部依赖）
//   Linux    OpenSSL（头存在即启用）
// ============================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sc::localdesk {

class ICrypto {
public:
    virtual ~ICrypto() = default;

    /// 业务 Payload AES-256-GCM 加密（内含随机 IV，密文格式 iv(12)||tag(16)||ct）
    virtual bool encrypt(const std::vector<uint8_t>& plain,
                                       std::vector<uint8_t>& cipher) = 0;

    virtual bool decrypt(const std::vector<uint8_t>& cipher,
                                       std::vector<uint8_t>& plain) = 0;

    /// PBKDF2-HMAC-SHA256 口令派生，用于无人值守密码（Doc16 §2），迭代 >= 10 万
    virtual bool derive_key(const std::string& password,
                                          const std::string& salt,
                                          std::vector<uint8_t>& key) = 0;

    /// HMAC-SHA256：无人值守挑战应答 proof 计算（Doc05 §6）
    virtual bool hmac_sha256(const std::string& key,
                                           const std::string& data,
                                           std::vector<uint8_t>& mac) = 0;

    /// SHA-256 摘要（口令校验值 / 文件完整性 / 会话密钥派生）
    virtual bool sha256(const std::string& data,
                                      std::vector<uint8_t>& digest) = 0;

    /// ---------------- 会话密钥协商（PRD：ECDH P-256 临时密钥交换） ----------------

    /// 注入会话密钥（KEY_SYNC 协商完成后调用；32B AES-256-GCM 密钥）。
    /// 空表 = 清除密钥（会话复位时调用）
    virtual void set_key(const std::vector<uint8_t>& key) = 0;

    /// 生成 ECDH P-256 临时密钥对。pub 出参 64B 裸 x||y（无 0x04 前缀，
    /// 传输前按 KEY_SYNC 帧格式拼 nonce）
    virtual bool ecdh_keygen(std::vector<uint8_t>& pub) = 0;

    /// 以对端 64B 裸公钥导出共享秘密。secret 出参 32B 大端 x 坐标
    /// （CNG RAW_SECRET 小端序由实现层翻转为大端，保证跨后端一致）
    virtual bool ecdh_shared(const std::vector<uint8_t>& peer_pub,
                                           std::vector<uint8_t>& secret) = 0;
};

}  // namespace sc::localdesk
