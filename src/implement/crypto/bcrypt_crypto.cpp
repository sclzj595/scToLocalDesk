// ============================================================
// scToLocalDesk | src/implement/crypto/bcrypt_crypto.cpp
// ICrypto 实现：Windows CNG（bcrypt.lib）——AES-256-GCM / PBKDF2 /
// HMAC / SHA256 / ECDH P-256。OS 内建零外部依赖（Windows 10+）。
// 密钥协商约定与 openssl_crypto 对齐：共享秘密 32B 大端 x 坐标
// （CNG RAW_SECRET 输出小端序，此处翻转）
// ============================================================
#include <vector>

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

#include <mutex>

#include "implement/crypto/crypto_factory.hpp"

#pragma comment(lib, "bcrypt.lib")

// 19041 SDK 未定义 ECDH 专属 BLOB 名，统一用 ECCPUBLIC（blob 头 magic 自识别曲线）
#ifndef BCRYPT_ECDH_PUBLIC_P256_BLOB
#define BCRYPT_ECDH_PUBLIC_P256_BLOB BCRYPT_ECCPUBLIC_BLOB
#endif

#ifndef NT_SUCCESS
#define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

namespace sc::localdesk {

namespace {

/// const 缓冲 → CNG 可写指针（API 参数均为 PUCHAR，实际只读）
inline PUCHAR buf(const void* p) {
    return const_cast<PUCHAR>(reinterpret_cast<const unsigned char*>(p));
}

class BcryptCrypto final : public ICrypto {
public:
    BcryptCrypto() = default;
    ~BcryptCrypto() override {
        std::lock_guard<std::mutex> lk(mtx_);
        clear_aes_key_locked();
        clear_dh_key_locked();
        for (BCRYPT_ALG_HANDLE& a : algs_) {
            if (a != nullptr) {
                BCryptCloseAlgorithmProvider(a, 0);
                a = nullptr;
            }
        }
    }

    BcryptCrypto(const BcryptCrypto&) = delete;
    BcryptCrypto& operator=(const BcryptCrypto&) = delete;

    bool encrypt(const std::vector<uint8_t>& plain,
                 std::vector<uint8_t>& cipher) override {
        std::lock_guard<std::mutex> lk(mtx_);
        if (aes_key_ == nullptr) {
            return false;  // 密钥未注入（协商未完成）——调用方不得明文外发
        }
        std::vector<uint8_t> iv(12);
        if (!NT_SUCCESS(BCryptGenRandom(nullptr, iv.data(),
                                        static_cast<ULONG>(iv.size()),
                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
            return false;
        }
        std::vector<uint8_t> tag(16);
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
        BCRYPT_INIT_AUTH_MODE_INFO(info);
        info.pbNonce = iv.data();
        info.cbNonce = static_cast<ULONG>(iv.size());
        info.pbTag = tag.data();
        info.cbTag = static_cast<ULONG>(tag.size());

        std::vector<uint8_t> ct(plain.size());
        ULONG out_len = 0;
        if (!NT_SUCCESS(BCryptEncrypt(aes_key_, buf(plain.data()),
                                      static_cast<ULONG>(plain.size()), &info,
                                      nullptr, 0, ct.data(),
                                      static_cast<ULONG>(ct.size()), &out_len,
                                      0))) {
            return false;
        }
        ct.resize(out_len);
        cipher = iv;  // 格式：iv(12) || tag(16) || ct（与 openssl 实现一致）
        cipher.insert(cipher.end(), tag.begin(), tag.end());
        cipher.insert(cipher.end(), ct.begin(), ct.end());
        return true;
    }

    bool decrypt(const std::vector<uint8_t>& cipher,
                 std::vector<uint8_t>& plain) override {
        std::lock_guard<std::mutex> lk(mtx_);
        if (aes_key_ == nullptr || cipher.size() < 12 + 16) {
            return false;
        }
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
        BCRYPT_INIT_AUTH_MODE_INFO(info);
        info.pbNonce = buf(cipher.data());
        info.cbNonce = 12;
        info.pbTag = buf(cipher.data() + 12);
        info.cbTag = 16;
        const uint8_t* ct = cipher.data() + 28;
        const ULONG ct_len = static_cast<ULONG>(cipher.size() - 28);

        std::vector<uint8_t> out(ct_len);
        ULONG out_len = 0;
        // tag 不匹配返回非零状态 → 非法报文
        if (!NT_SUCCESS(BCryptDecrypt(aes_key_, buf(ct), ct_len, &info, nullptr,
                                      0, out.data(),
                                      static_cast<ULONG>(out.size()), &out_len,
                                      0))) {
            return false;  // 数据被篡改 / 密钥不一致
        }
        plain.assign(out.begin(), out.begin() + out_len);
        return true;
    }

    bool derive_key(const std::string& password, const std::string& salt,
                    std::vector<uint8_t>& key) override {
        std::lock_guard<std::mutex> lk(mtx_);
        key.resize(32);
        // PBKDF2 为 HMAC 构造，算法句柄必须带 HANDLE_HMAC_FLAG 打开
        BCRYPT_ALG_HANDLE alg = ensure_alg(AlgIdx::ShaHmac);
        if (alg == nullptr) {
            return false;
        }
        // 迭代 100001 次：PBKDF2 高迭代成本，防离线字典攻击（Doc16 §8）
        return NT_SUCCESS(BCryptDeriveKeyPBKDF2(
            alg, buf(password.data()), static_cast<ULONG>(password.size()),
            buf(salt.data()), static_cast<ULONG>(salt.size()), 100001,
            key.data(), static_cast<ULONG>(key.size()), 0));
    }

    bool hmac_sha256(const std::string& key, const std::string& data,
                     std::vector<uint8_t>& mac) override {
        std::lock_guard<std::mutex> lk(mtx_);
        mac.resize(32);
        BCRYPT_ALG_HANDLE alg = ensure_alg(AlgIdx::ShaHmac);
        if (alg == nullptr) {
            return false;
        }
        BCRYPT_HASH_HANDLE hh = nullptr;
        if (!NT_SUCCESS(BCryptCreateHash(alg, &hh, nullptr, 0, buf(key.data()),
                                         static_cast<ULONG>(key.size()), 0))) {
            return false;
        }
        const bool ok =
            NT_SUCCESS(BCryptHashData(hh, buf(data.data()),
                                      static_cast<ULONG>(data.size()), 0))
            && NT_SUCCESS(BCryptFinishHash(hh, mac.data(),
                                           static_cast<ULONG>(mac.size()), 0));
        BCryptDestroyHash(hh);
        return ok;
    }

    bool sha256(const std::string& data,
                std::vector<uint8_t>& digest) override {
        std::lock_guard<std::mutex> lk(mtx_);
        digest.resize(32);
        BCRYPT_ALG_HANDLE alg = ensure_alg(AlgIdx::Sha);
        if (alg == nullptr) {
            return false;
        }
        BCRYPT_HASH_HANDLE hh = nullptr;
        if (!NT_SUCCESS(BCryptCreateHash(alg, &hh, nullptr, 0, nullptr, 0, 0))) {
            return false;
        }
        const bool ok =
            NT_SUCCESS(BCryptHashData(hh, buf(data.data()),
                                      static_cast<ULONG>(data.size()), 0))
            && NT_SUCCESS(BCryptFinishHash(hh, digest.data(),
                                           static_cast<ULONG>(digest.size()),
                                           0));
        BCryptDestroyHash(hh);
        return ok;
    }

    void set_key(const std::vector<uint8_t>& key) override {
        std::lock_guard<std::mutex> lk(mtx_);
        clear_aes_key_locked();
        if (key.size() != 32) {
            return;  // 空表/非法长度 = 清除
        }
        BCRYPT_ALG_HANDLE alg = ensure_alg(AlgIdx::Aes);
        if (alg == nullptr) {
            return;
        }
        BCryptGenerateSymmetricKey(alg, &aes_key_, nullptr, 0, buf(key.data()),
                                   static_cast<ULONG>(key.size()), 0);
    }

    bool ecdh_keygen(std::vector<uint8_t>& pub) override {
        std::lock_guard<std::mutex> lk(mtx_);
        BCRYPT_ALG_HANDLE alg = ensure_alg(AlgIdx::Ecdh);
        if (alg == nullptr) {
            return false;
        }
        clear_dh_key_locked();
        pub.clear();
        if (!NT_SUCCESS(BCryptGenerateKeyPair(alg, &ecdh_key_, 256, 0))
            || !NT_SUCCESS(BCryptFinalizeKeyPair(ecdh_key_, 0))) {
            clear_dh_key_locked();
            return false;
        }
        // BLOB = 8B 头(magic 4B + cbKey 4B) + 32B x + 32B y = 72B
        std::vector<uint8_t> blob(72);
        ULONG cb = 0;
        const bool ok =
            NT_SUCCESS(BCryptExportKey(ecdh_key_, nullptr,
                                       BCRYPT_ECDH_PUBLIC_P256_BLOB, blob.data(),
                                       static_cast<ULONG>(blob.size()), &cb, 0))
            && cb == blob.size();
        if (!ok) {
            clear_dh_key_locked();
            return false;
        }
        pub.assign(blob.begin() + 8, blob.end());  // 取尾部 64B 裸坐标
        return true;
    }

    bool ecdh_shared(const std::vector<uint8_t>& peer_pub,
                     std::vector<uint8_t>& secret) override {
        std::lock_guard<std::mutex> lk(mtx_);
        if (peer_pub.size() != 64 || ecdh_key_ == nullptr) {
            return false;  // 非法公钥 / 本端密钥对未生成
        }
        BCRYPT_ALG_HANDLE alg = ensure_alg(AlgIdx::Ecdh);
        if (alg == nullptr) {
            return false;
        }
        // 重建 8B BLOB 头：magic(0x314B4345 小端=ECK1) + cbKey(32) + 64B 裸坐标
        std::vector<uint8_t> blob(8 + 64);
        blob[0] = 0x45; blob[1] = 0x43; blob[2] = 0x4B; blob[3] = 0x31;
        blob[4] = 32;  // cbKey（小端 32）
        std::copy(peer_pub.begin(), peer_pub.end(), blob.begin() + 8);
        BCRYPT_KEY_HANDLE peer = nullptr;
        if (!NT_SUCCESS(BCryptImportKeyPair(alg, nullptr,
                                            BCRYPT_ECDH_PUBLIC_P256_BLOB,
                                            &peer, blob.data(),
                                            static_cast<ULONG>(blob.size()),
                                            0))) {
            return false;
        }
        BCRYPT_SECRET_HANDLE sec = nullptr;
        std::vector<uint8_t> raw(32);
        ULONG cb = 0;
        // RAW_SECRET 输出 32B 小端序 x → 翻转为大端（与 openssl 后端一致）
        const bool ok = NT_SUCCESS(BCryptSecretAgreement(ecdh_key_, peer, &sec, 0))
                     && NT_SUCCESS(BCryptDeriveKey(sec, BCRYPT_KDF_RAW_SECRET,
                                                   nullptr, raw.data(),
                                                   static_cast<ULONG>(raw.size()),
                                                   &cb, 0))
                     && cb == 32;
        BCryptDestroySecret(sec);
        BCryptDestroyKey(peer);
        if (!ok) {
            return false;
        }
        secret.assign(raw.rbegin(), raw.rend());
        return true;
    }

private:
    enum class AlgIdx { Aes = 0, Sha = 1, ShaHmac = 2, Ecdh = 3 };

    BCRYPT_ALG_HANDLE ensure_alg(AlgIdx idx) {
        BCRYPT_ALG_HANDLE& slot = algs_[static_cast<int>(idx)];
        if (slot != nullptr) {
            return slot;
        }
        LPCWSTR id = nullptr;
        ULONG flags = 0;
        switch (idx) {
        case AlgIdx::Aes: id = BCRYPT_AES_ALGORITHM; break;
        case AlgIdx::Sha: id = BCRYPT_SHA256_ALGORITHM; break;
        case AlgIdx::ShaHmac:
            id = BCRYPT_SHA256_ALGORITHM;
            flags = BCRYPT_ALG_HANDLE_HMAC_FLAG;
            break;
        case AlgIdx::Ecdh: id = BCRYPT_ECDH_P256_ALGORITHM; break;
        }
        BCRYPT_ALG_HANDLE h = nullptr;
        if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&h, id, nullptr, flags))) {
            return nullptr;
        }
        if (idx == AlgIdx::Aes) {
            BCryptSetProperty(h, BCRYPT_CHAINING_MODE, buf(BCRYPT_CHAIN_MODE_GCM),
                              sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
        }
        slot = h;
        return h;
    }

    void clear_aes_key_locked() {
        if (aes_key_ != nullptr) {
            BCryptDestroyKey(aes_key_);
            aes_key_ = nullptr;
        }
    }

    void clear_dh_key_locked() {
        if (ecdh_key_ != nullptr) {
            BCryptDestroyKey(ecdh_key_);
            ecdh_key_ = nullptr;
        }
    }

    BCRYPT_ALG_HANDLE algs_[4]{nullptr, nullptr, nullptr, nullptr};
    BCRYPT_KEY_HANDLE aes_key_{nullptr};   // set_key 注入的对称密钥
    BCRYPT_KEY_HANDLE ecdh_key_{nullptr};  // ecdh_keygen 生成的临时密钥对
    std::mutex mtx_;  // encrypt 与 set_key/ecdh 并发（IO 线程 × 会话线程）
};

}  // namespace

std::shared_ptr<ICrypto> make_bcrypt_crypto() {
    return std::make_shared<BcryptCrypto>();
}

}  // namespace sc::localdesk

#endif  // _WIN32
