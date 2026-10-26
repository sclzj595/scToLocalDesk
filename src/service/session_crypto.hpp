// ============================================================
// scToLocalDesk | src/service/session_crypto.hpp
// 会话密钥派生（KEY_SYNC，PRD 会话加密）：
//   session_key = SHA-256("SCLD-KEY-V1" || ecdh_secret(32B) || nonce_c || nonce_s)
// nonce_c/nonce_s 按 控制端→被控端 顺序拼接，双端按各自角色传参，
// 结果一致。域分隔串防跨协议重放。
// ============================================================
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/interface/icrypto.hpp"

namespace sc::localdesk {

struct RemoteSessionCrypto final {
    static bool derive_session_key(ICrypto& crypto,
                                   const std::vector<uint8_t>& secret,
                                   const std::vector<uint8_t>& nonce_c,
                                   const std::vector<uint8_t>& nonce_s,
                                   std::vector<uint8_t>& key) {
        if (secret.size() != 32 || nonce_c.size() != 16 || nonce_s.size() != 16) {
            return false;
        }
        std::string data = "SCLD-KEY-V1";
        data.append(reinterpret_cast<const char*>(secret.data()), secret.size());
        data.append(reinterpret_cast<const char*>(nonce_c.data()), nonce_c.size());
        data.append(reinterpret_cast<const char*>(nonce_s.data()), nonce_s.size());
        return crypto.sha256(data, key) && key.size() == 32;
    }
};

}  // namespace sc::localdesk
