// ============================================================
// scToLocalDesk | src/core/auth/unattended_authenticator.cpp
// 无人值守密码鉴权实现（Doc16 §3/§5）
// ============================================================
#include "core/auth/unattended_authenticator.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <optional>
#include <random>

namespace sc::localdesk {

namespace {

using Clock = std::chrono::steady_clock;

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               Clock::now().time_since_epoch())
        .count();
}

// ---------------- hex 编解码 ----------------

std::string to_hex(const uint8_t* data, size_t len) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(kDigits[data[i] >> 4]);
        out.push_back(kDigits[data[i] & 0x0F]);
    }
    return out;
}

bool from_hex(const std::string& hex, std::vector<uint8_t>& out) {
    out.clear();
    if (hex.size() % 2 != 0) {
        return false;
    }
    out.reserve(hex.size() / 2);
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    };
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return true;
}

}  // namespace

UnattendedPasswordAuthenticator::UnattendedPasswordAuthenticator(
    std::shared_ptr<ICrypto> crypto, UnattendedSecret secret, bool enabled)
    : crypto_(std::move(crypto)),
      secret_(std::move(secret)),
      enabled_(enabled && crypto_ != nullptr) {
}

AuthResult UnattendedPasswordAuthenticator::decide(const AuthRequest& req) {
    if (!req.unattended) {
        return AuthResult::NeedManualConfirm;  // 回退人工弹窗（Doc16 §3）
    }
    if (!enabled()) {
        return AuthResult::Rejected;  // 开关关闭：上层回复 REJECT(0x03)
    }
    return AuthResult::StartChallenge;
}

std::string UnattendedPasswordAuthenticator::issue_challenge() {
    // std::random_device + mt19937 足够内网单机用途；密码学安全随机由
    // ICrypto 实现层承担（OpenSSL RAND），此处仅生成不可预测 nonce
    static thread_local std::mt19937_64 rng{std::random_device{}()};

    std::array<uint8_t, kChallengeLen> buf{};
    for (size_t i = 0; i < buf.size(); i += 8) {
        const uint64_t r = rng();
        std::memcpy(buf.data() + i, &r, std::min<size_t>(8, buf.size() - i));
    }

    const std::string hex = to_hex(buf.data(), buf.size());
    {
        std::lock_guard<std::mutex> lk(mtx_);
        pending_challenges_[hex] = now_ms() + kChallengeTtlMs;
    }
    return hex;
}

bool UnattendedPasswordAuthenticator::verify_proof(const std::string& challenge,
                                                   const std::string& proof) {
    const int64_t now = now_ms();
    {
        std::lock_guard<std::mutex> lk(mtx_);
        const auto it = pending_challenges_.find(std::string(challenge));
        if (it == pending_challenges_.end() || it->second < now) {
            return false;  // 未知挑战 / 已过期 → 防重放（Doc16 §2.4）
        }
        pending_challenges_.erase(it);  // 一次性：用完即焚
    }

    std::vector<uint8_t> expected;
    if (!compute_proof(challenge, expected)) {
        return false;
    }
    return constant_time_equal(proof,
                               std::string(reinterpret_cast<const char*>(expected.data()),
                                                expected.size()));
}

bool UnattendedPasswordAuthenticator::check_password(const std::string& password) const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (secret_.salt_hex.empty() || secret_.verify_hash_hex.empty() || crypto_ == nullptr) {
        return false;
    }
    std::vector<uint8_t> salt;
    if (!from_hex(secret_.salt_hex, salt)) {
        return false;
    }
    std::vector<uint8_t> key;
    if (!crypto_->derive_key(password, std::string(
                                            reinterpret_cast<const char*>(salt.data()),
                                            salt.size()),
                             key)) {
        return false;
    }
    std::vector<uint8_t> digest;
    if (!crypto_->sha256(std::string(reinterpret_cast<const char*>(key.data()),
                                          key.size()),
                         digest)) {
        return false;
    }
    return constant_time_equal(to_hex(digest.data(), digest.size()), secret_.verify_hash_hex);
}

bool UnattendedPasswordAuthenticator::set_password(const std::string& password,
                                                   UnattendedSecret& out_secret) {
    if (crypto_ == nullptr || password.size() < 8) {
        return false;  // 密码强度红线：>= 8（Doc16 §2.2，字符集校验在 UI 层）
    }

    std::array<uint8_t, 16> salt{};
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    for (size_t i = 0; i < salt.size(); i += 8) {
        const uint64_t r = rng();
        std::memcpy(salt.data() + i, &r, std::min<size_t>(8, salt.size() - i));
    }

    std::vector<uint8_t> key;
    if (!crypto_->derive_key(password, std::string(
                                        reinterpret_cast<const char*>(salt.data()),
                                        salt.size()),
                             key)) {
        return false;
    }
    std::vector<uint8_t> digest;
    if (!crypto_->sha256(std::string(reinterpret_cast<const char*>(key.data()),
                                          key.size()),
                         digest)) {
        return false;
    }

    out_secret.salt_hex = to_hex(salt.data(), salt.size());
    out_secret.key_hex = to_hex(key.data(), key.size());
    out_secret.verify_hash_hex = to_hex(digest.data(), digest.size());

    std::lock_guard<std::mutex> lk(mtx_);
    secret_ = out_secret;
    pending_challenges_.clear();  // 换密码即作废在途挑战
    return true;
}

void UnattendedPasswordAuthenticator::update_secret(UnattendedSecret secret, bool enabled) {
    std::lock_guard<std::mutex> lk(mtx_);
    secret_ = std::move(secret);
    enabled_ = enabled && crypto_ != nullptr && !secret_.key_hex.empty();
    pending_challenges_.clear();
}

bool UnattendedPasswordAuthenticator::enabled() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return enabled_;
}

const UnattendedSecret& UnattendedPasswordAuthenticator::secret() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return secret_;
}

size_t UnattendedPasswordAuthenticator::purge_expired() {
    const int64_t now = now_ms();
    std::lock_guard<std::mutex> lk(mtx_);
    const size_t before = pending_challenges_.size();
    for (auto it = pending_challenges_.begin(); it != pending_challenges_.end();) {
        if (it->second < now) {
            it = pending_challenges_.erase(it);
        }
        else {
            ++it;
        }
    }
    return before - pending_challenges_.size();
}

bool UnattendedPasswordAuthenticator::constant_time_equal(const std::string& a,
                                                          const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    volatile uint8_t diff = 0;  // volatile 阻止编译器短路优化
    for (size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    }
    return diff == 0;
}

bool UnattendedPasswordAuthenticator::compute_proof(const std::string& challenge_hex,
                                                    std::vector<uint8_t>& proof) const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<uint8_t> challenge;
    std::vector<uint8_t> key;
    if (!from_hex(challenge_hex, challenge) || !from_hex(secret_.key_hex, key)
        || crypto_ == nullptr) {
        return false;
    }
    return crypto_->hmac_sha256(
        std::string(reinterpret_cast<const char*>(key.data()), key.size()),
        std::string(reinterpret_cast<const char*>(challenge.data()),
                         challenge.size()),
        proof);
}

}  // namespace sc::localdesk
