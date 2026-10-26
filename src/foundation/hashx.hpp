// ============================================================
// scToLocalDesk | src/foundation/hashx.hpp
// xxHash 封装（third_party/xxhash，XXH_INLINE_ALL 纯头启用）
// 用途：文件分片校验（V1.0 FILE_CHUNK checksum 接线点）、去重指纹
// 选型：MD5/SHA 走 OpenSSL（crypto_factory）；对非安全场景的
//   高吞吐校验，xxH3 吞吐比 SHA1 高一个量级且零依赖
// ============================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#define XXH_INLINE_ALL
#include "xxhash.h"

namespace sc::localdesk::foundation {

/// xxH3-64：缓冲区指纹/校验（seed 隔离不同用途域防碰撞误判）
inline uint64_t xxh3_64(const void* data, size_t len,
                                      uint64_t seed = 0) noexcept {
    return XXH3_64bits_withSeed(data, len, seed);
}

/// 字符串便捷重载（字面量/std::string/string_view 通吃，免手动传长度）
inline uint64_t xxh3_64(std::string_view data,
                                      uint64_t seed = 0) noexcept {
    return xxh3_64(data.data(), data.size(), seed);
}

}  // namespace sc::localdesk::foundation
