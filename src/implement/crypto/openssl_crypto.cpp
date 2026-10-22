// ============================================================
// scToLocalDesk | src/implement/crypto/openssl_crypto.cpp
// ICrypto 实现：OpenSSL EVP（AES-256-GCM / PBKDF2 / HMAC / SHA256）
// 无 OpenSSL 时编译为显式失败桩，编译期可知、运行期不静默
// ============================================================
#include <memory>
#include <vector>

#if __has_include(<openssl/evp.h>)
    #define SCLD_HAS_OPENSSL 1
    #include <openssl/evp.h>
    #include <openssl/hmac.h>
    #include <openssl/rand.h>
#endif

#include "core/interface/icrypto.hpp"

namespace sc::localdesk {

namespace {

#ifdef SCLD_HAS_OPENSSL

class OpensslCrypto final : public ICrypto {
public:
    bool encrypt(const std::vector<uint8_t>& plain,
                 std::vector<uint8_t>& cipher) override {
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

    bool derive_key(const std::string& password, std::string_view salt,
                    std::vector<uint8_t>& key) override {
        key.resize(32);
        // 迭代 100001 次：PBKDF2 高迭代成本，防离线字典攻击（Doc16 §8）
        return PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                                 reinterpret_cast<const unsigned char*>(salt.data()),
                                 static_cast<int>(salt.size()), 100001, EVP_sha256(),
                                 static_cast<int>(key.size()), key.data()) == 1;
    }

    bool hmac_sha256(std::string_view key, std::string_view data,
                     std::vector<uint8_t>& mac) override {
        mac.resize(32);
        unsigned int mac_len = 0;
        return HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
                    reinterpret_cast<const unsigned char*>(data.data()), data.size(),
                    mac.data(), &mac_len) != nullptr
            && mac_len == 32;
    }

    bool sha256(std::string_view data, std::vector<uint8_t>& digest) override {
        digest.resize(32);
        unsigned int len = 0;
        return EVP_Digest(data.data(), data.size(), digest.data(), &len, EVP_sha256(),
                          nullptr) == 1
            && len == 32;
    }

    void set_key(const std::vector<uint8_t>& key) { key_ = key; }

private:
    std::vector<uint8_t> key_{32, 0};  // TODO(V1.0): 会话密钥协商（CONFIG_SYNC）
};

#endif  // SCLD_HAS_OPENSSL

class CryptoUnavailableStub final : public ICrypto {
public:
    bool encrypt(const std::vector<uint8_t>&, std::vector<uint8_t>&) override { return false; }
    bool decrypt(const std::vector<uint8_t>&, std::vector<uint8_t>&) override { return false; }
    bool derive_key(const std::string&, std::string_view, std::vector<uint8_t>&) override {
        return false;
    }
    bool hmac_sha256(std::string_view, std::string_view, std::vector<uint8_t>&) override {
        return false;
    }
    bool sha256(std::string_view, std::vector<uint8_t>&) override { return false; }
};

}  // namespace

std::shared_ptr<ICrypto> make_default_crypto() {
#ifdef SCLD_HAS_OPENSSL
    return std::make_shared<OpensslCrypto>();
#else
    return std::make_shared<CryptoUnavailableStub>();  // 构建时明确关闭加密
#endif
}

}  // namespace sc::localdesk
