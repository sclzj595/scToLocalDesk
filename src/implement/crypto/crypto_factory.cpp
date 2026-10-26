// ============================================================
// scToLocalDesk | src/implement/crypto/crypto_factory.cpp
// 后端分发：Windows = CNG（OS 内建），其它 = OpenSSL（头探测），
// 均不可用 = openssl_crypto 内的显式失败桩
// ============================================================
#include "implement/crypto/crypto_factory.hpp"

#if defined(_WIN32)
    #define SCLD_CRYPTO_BCRYPT 1
#else
    #if __has_include(<openssl/evp.h>)
        #define SCLD_CRYPTO_OPENSSL 1
    #endif
#endif

namespace sc::localdesk {

std::shared_ptr<ICrypto> make_default_crypto() {
#if defined(SCLD_CRYPTO_BCRYPT)
    return make_bcrypt_crypto();
#elif defined(SCLD_CRYPTO_OPENSSL)
    return make_openssl_crypto();
#else
    return make_openssl_crypto();  // 无头：openssl_crypto 内编译为桩
#endif
}

}  // namespace sc::localdesk
