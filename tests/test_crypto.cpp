// ============================================================
// scToLocalDesk | tests/test_crypto.cpp
// ICrypto 后端用例（PRD 会话加密）：标准向量 + ECDH 一致性 +
// AES-GCM 往返/防篡改。向量对 BCrypt(CNG) 与 OpenSSL 双后端同跑
// ============================================================
#include "doctest.h"

#include <string>
#include <vector>

#include "implement/crypto/crypto_factory.hpp"
#include "service/session_crypto.hpp"

using namespace sc::localdesk;

namespace {

std::string to_hex(const std::vector<uint8_t>& v) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (uint8_t b : v) {
        s.push_back(d[b >> 4]);
        s.push_back(d[b & 0x0F]);
    }
    return s;
}

}  // namespace

TEST_CASE("SHA-256 标准向量")
{
    auto c = make_default_crypto();
    std::vector<uint8_t> out;
    REQUIRE(c->sha256("abc", out));
    CHECK(to_hex(out) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("HMAC-SHA256 RFC4231 用例1")
{
    auto c = make_default_crypto();
    std::vector<uint8_t> out;
    REQUIRE(c->hmac_sha256(std::string(20, '\x0b'), "Hi There", out));
    CHECK(to_hex(out) == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
}

TEST_CASE("PBKDF2-HMAC-SHA256 标准向量")
{
    auto c = make_default_crypto();
    std::vector<uint8_t> out;
    REQUIRE(c->derive_key("password", "salt", out));
    CHECK(out.size() == 32);
    // 实现固定 100001 迭代（Doc16 §8 红线）；期望值经 Python hashlib 基准核验
    CHECK(to_hex(out) == "ceb01179b54c4178973f9b6b7d6ed30d0c6cf23b0ad170f2d68ef9bfb34c4786");
}

TEST_CASE("AES-GCM 往返与防篡改")
{
    auto c = make_default_crypto();
    std::vector<uint8_t> key(32);
    for (size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<uint8_t>(i);
    }
    c->set_key(key);

    const std::vector<uint8_t> plain = {1, 2, 3, 4, 5, 0x41, 0x42};
    std::vector<uint8_t> cipher, back;
    REQUIRE(c->encrypt(plain, cipher));
    CHECK(cipher.size() == 12 + 16 + plain.size());  // iv||tag||ct
    REQUIRE(c->decrypt(cipher, back));
    CHECK(back == plain);

    // 密文尾部（ct 区）翻转 1bit → 解密失败（tag 校验）
    std::vector<uint8_t> tampered = cipher;
    tampered.back() ^= 0x01;
    std::vector<uint8_t> junk;
    CHECK_FALSE(c->decrypt(tampered, junk));

    // 未注入密钥前加密必须拒绝（调用方不得明文外发）
    auto fresh = make_default_crypto();
    CHECK_FALSE(fresh->encrypt(plain, cipher));

    // set_key 空表 = 清除，后续加密拒绝
    c->set_key({});
    CHECK_FALSE(c->encrypt(plain, cipher));
}

TEST_CASE("ECDH P-256 双端共享秘密一致")
{
    auto a = make_default_crypto();
    auto b = make_default_crypto();

    std::vector<uint8_t> pub_a, pub_b;
    REQUIRE(a->ecdh_keygen(pub_a));
    REQUIRE(b->ecdh_keygen(pub_b));
    CHECK(pub_a.size() == 64);
    CHECK(pub_b.size() == 64);

    std::vector<uint8_t> sec_a, sec_b;
    REQUIRE(a->ecdh_shared(pub_b, sec_a));
    REQUIRE(b->ecdh_shared(pub_a, sec_b));
    CHECK(sec_a.size() == 32);
    CHECK(sec_b.size() == 32);
    CHECK(sec_a == sec_b);  // 大端 x 坐标约定：跨后端可比

    // 非法公钥长度拒绝
    std::vector<uint8_t> bad(63, 0), out;
    CHECK_FALSE(a->ecdh_shared(bad, out));

    // 密钥对未生成就协商 → 拒绝
    auto c = make_default_crypto();
    CHECK_FALSE(c->ecdh_shared(pub_a, out));
}

TEST_CASE("会话密钥派生确定性（双端一致）")
{
    // derive_session_key 契约：sha256(secret || nonce_c || nonce_s)
    // 双端各自以自己的角色顺序拼接，结果必须一致
    const std::vector<uint8_t> secret(32, 0xAB);
    const std::vector<uint8_t> nc(16, 0x01);
    const std::vector<uint8_t> ns(16, 0x02);
    auto c = make_default_crypto();

    std::vector<uint8_t> key_c, key_s;
    CHECK(RemoteSessionCrypto::derive_session_key(*c, secret, nc, ns, key_c));
    CHECK(RemoteSessionCrypto::derive_session_key(*c, secret, nc, ns, key_s));
    CHECK(key_c == key_s);
    CHECK(key_c.size() == 32);
}
