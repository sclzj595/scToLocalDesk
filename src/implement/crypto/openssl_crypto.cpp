// ============================================================
// scToLocalDesk | src/implement/crypto/openssl_crypto.cpp
// ICrypto 实现：OpenSSL EVP（AES-256-GCM / PBKDF2 / HMAC / SHA256 / ECDH）
// 无 OpenSSL 头时编译为桩（__has_include 探测），运行期显式失败
// 工厂分发见 crypto_factory.cpp（Windows 走 CNG，不走本文件真实现）
// ============================================================
#include <memory>
#include <mutex>
#include <vector>

#if __has_include(<openssl/evp.h>)
    #define SCLD_HAS_OPENSSL 1
    #include <openssl/evp.h>
    #include <openssl/hmac.h>
    #include <openssl/rand.h>
    #include <openssl/ec.h>
    #include <openssl/ecdh.h>
#endif

#include "core/interface/icrypto.hpp"

namespace sc::localdesk {

namespace {

#ifdef SCLD_HAS_OPENSSL

class OpensslCrypto final : public ICrypto {
public:
    ~OpensslCrypto() override {
        if (dh_key_ != nullptr) {
            EVP_PKEY_free(dh_key_);
        }
    }

    bool encrypt(const std::vector<uint8_t>& plain,
                 std::vector<uint8_t>& cipher) override {
        std::lock_guard<std::mutex> lk(mtx_);
        std::vector<uint8_t> iv(12);
        if (RAND_bytes(iv.data(), static_cast<int>(iv.size())) != 1) {
            return false;
        }
        std::vector<uint8_t> tag(16);
        std::vector<uint8_t> ct(plain.size());

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        bool ok = ctx != nullptr
               && EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key_.data(),
                                     iv.data()) == 1
               && EVP_CIPHER_CTX_set_key_length(ctx, key_.size()) == 1;
        int out_len = 0;
        ok = ok && (plain.empty()
                    || EVP_EncryptUpdate(ctx, ct.data(), &out_len, plain.data(),
                                         static_cast<int>(plain.size())) == 1);
        ok = ok && EVP_EncryptFinal_ex(ctx, ct.data() + out_len, &out_len) == 1;
        ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag.data()) == 1;
        EVP_CIPHER_CTX_free(ctx);
        if (!ok) {
            return false;
        }
        cipher = iv;          // 格式：iv(12) || tag(16) || ct
        cipher.insert(cipher.end(), tag.begin(), tag.end());
        cipher.insert(cipher.end(), ct.begin(), ct.end());
        return true;
    }

    bool decrypt(const std::vector<uint8_t>& cipher,
                 std::vector<uint8_t>& plain) override {
        std::lock_guard<std::mutex> lk(mtx_);
        if (cipher.size() < 12 + 16) {
            return false;
        }
        const uint8_t* iv = cipher.data();
        const uint8_t* tag = cipher.data() + 12;
        const uint8_t* ct = cipher.data() + 28;
        const int ct_len = static_cast<int>(cipher.size() - 28);

        std::vector<uint8_t> out(static_cast<size_t>(ct_len) + 16);
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        bool ok = ctx != nullptr
               && EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key_.data(), iv) == 1;
        int out_len = 0;
        ok = ok && (ct_len == 0
                    || EVP_DecryptUpdate(ctx, out.data(), &out_len, ct, ct_len) == 1);
        ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16,
                                       const_cast<uint8_t*>(tag)) == 1;
        ok = ok && EVP_DecryptFinal_ex(ctx, out.data() + out_len, &out_len) == 1;
        EVP_CIPHER_CTX_free(ctx);
        if (!ok) {
            return false;  // tag 校验失败 = 数据被篡改
        }
        plain.assign(out.begin(), out.begin() + out_len);
        return true;
    }

    bool derive_key(const std::string& password, const std::string& salt,
                    std::vector<uint8_t>& key) override {
        std::lock_guard<std::mutex> lk(mtx_);
        key.resize(32);
        // 迭代 100001 次：PBKDF2 高迭代成本，防离线字典攻击（Doc16 §8）
        return PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                                 reinterpret_cast<const unsigned char*>(salt.data()),
                                 static_cast<int>(salt.size()), 100001, EVP_sha256(),
                                 static_cast<int>(key.size()), key.data()) == 1;
    }

    bool hmac_sha256(const std::string& key, const std::string& data,
                     std::vector<uint8_t>& mac) override {
        std::lock_guard<std::mutex> lk(mtx_);
        mac.resize(32);
        unsigned int mac_len = 0;
        return HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
                    reinterpret_cast<const unsigned char*>(data.data()), data.size(),
                    mac.data(), &mac_len) != nullptr
            && mac_len == 32;
    }

    bool sha256(const std::string& data,
                std::vector<uint8_t>& digest) override {
        std::lock_guard<std::mutex> lk(mtx_);
        digest.resize(32);
        unsigned int len = 0;
        return EVP_Digest(data.data(), data.size(), digest.data(), &len, EVP_sha256(),
                          nullptr) == 1
            && len == 32;
    }

    void set_key(const std::vector<uint8_t>& key) override {
        std::lock_guard<std::mutex> lk(mtx_);
        key_ = key;  // 空表 = 清除密钥
    }

    bool ecdh_keygen(std::vector<uint8_t>& pub) override {
        std::lock_guard<std::mutex> lk(mtx_);
        pub.clear();
        if (dh_key_ != nullptr) {
            EVP_PKEY_free(dh_key_);
            dh_key_ = nullptr;
        }
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
        bool ok = ctx != nullptr
               && EVP_PKEY_keygen_init(ctx) == 1
               && EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx,
                                                         NID_X9_62_prime256v1) == 1;
        EVP_PKEY* pkey = nullptr;
        ok = ok && EVP_PKEY_keygen(ctx, &pkey) == 1;
        EVP_PKEY_CTX_free(ctx);
        if (!ok || pkey == nullptr) {
            EVP_PKEY_free(pkey);
            return false;
        }
        // 裸公钥 = 65B 非压缩点 0x04||x||y → 去 0x04 前缀出 64B
        const EC_KEY* ec = EVP_PKEY_get0_EC_KEY(pkey);
        const EC_POINT* pt = EC_KEY_get0_public_key(ec);
        const EC_GROUP* gr = EC_KEY_get0_group(ec);
        uint8_t buf[65] = {};
        const size_t len =
            EC_POINT_point2oct(gr, pt, POINT_CONVERSION_UNCOMPRESSED, buf,
                               sizeof(buf), nullptr);
        if (len != sizeof(buf)) {
            EVP_PKEY_free(pkey);
            return false;
        }
        pub.assign(buf + 1, buf + sizeof(buf));
        dh_key_ = pkey;
        return true;
    }

    bool ecdh_shared(const std::vector<uint8_t>& peer_pub,
                     std::vector<uint8_t>& secret) override {
        std::lock_guard<std::mutex> lk(mtx_);
        if (peer_pub.size() != 64 || dh_key_ == nullptr) {
            return false;
        }
        const EC_KEY* my = EVP_PKEY_get0_EC_KEY(dh_key_);
        const EC_GROUP* gr = EC_KEY_get0_group(my);
        // 对端 64B 裸公钥 → 0x04||x||y 非压缩点导入
        std::vector<uint8_t> buf;
        buf.push_back(0x04);
        buf.insert(buf.end(), peer_pub.begin(), peer_pub.end());
        EC_POINT* pt = EC_POINT_new(gr);
        bool ok = EC_POINT_oct2point(gr, pt, buf.data(), buf.size(), nullptr) == 1;
        EC_KEY* peer_ec = ok ? EC_KEY_new_by_curve_name(NID_X9_62_prime256v1)
                             : nullptr;
        ok = ok && EC_KEY_set_public_key(peer_ec, pt) == 1;
        EVP_PKEY* peer_pkey = nullptr;
        if (ok) {
            peer_pkey = EVP_PKEY_new();
            EVP_PKEY_assign_EC_KEY(peer_pkey, peer_ec);  // peer_ec 所有权移入
            peer_ec = nullptr;
        }
        EC_POINT_free(pt);

        std::vector<uint8_t> out;
        size_t len = 0;
        EVP_PKEY_CTX* dctx = nullptr;
        ok = ok && (dctx = EVP_PKEY_CTX_new(dh_key_, nullptr)) != nullptr
          && EVP_PKEY_derive_init(dctx) == 1
          && EVP_PKEY_derive_set_peer(dctx, peer_pkey) == 1
          && EVP_PKEY_derive(dctx, nullptr, &len) == 1;
        if (ok) {
            out.resize(len);
            ok = EVP_PKEY_derive(dctx, out.data(), &len) == 1 && len == 32;
        }
        EVP_PKEY_CTX_free(dctx);
        EVP_PKEY_free(peer_pkey);
        if (!ok) {
            return false;
        }
        secret = std::move(out);  // OpenSSL 输出天然大端 x，无需翻转
        return true;
    }

private:
    std::vector<uint8_t> key_{32, 0};  // set_key 注入会话密钥，协商完成前全零
    EVP_PKEY* dh_key_{nullptr};        // ecdh_keygen 保留的本端密钥对
    std::mutex mtx_;  // encrypt 与 set_key/ecdh 并发（IO 线程 × 会话线程）
};

#endif  // SCLD_HAS_OPENSSL

class CryptoUnavailableStub final : public ICrypto {
public:
    bool encrypt(const std::vector<uint8_t>&, std::vector<uint8_t>&) override { return false; }
    bool decrypt(const std::vector<uint8_t>&, std::vector<uint8_t>&) override { return false; }
    bool derive_key(const std::string&, const std::string&, std::vector<uint8_t>&) override {
        return false;
    }
    bool hmac_sha256(const std::string&, const std::string&, std::vector<uint8_t>&) override {
        return false;
    }
    bool sha256(const std::string&, std::vector<uint8_t>&) override { return false; }
    void set_key(const std::vector<uint8_t>&) override {}
    bool ecdh_keygen(std::vector<uint8_t>&) override { return false; }
    bool ecdh_shared(const std::vector<uint8_t>&, std::vector<uint8_t>&) override {
        return false;
    }
};

}  // namespace

std::shared_ptr<ICrypto> make_openssl_crypto() {
#ifdef SCLD_HAS_OPENSSL
    return std::make_shared<OpensslCrypto>();
#else
    return std::make_shared<CryptoUnavailableStub>();  // 构建时明确关闭加密
#endif
}

}  // namespace sc::localdesk
