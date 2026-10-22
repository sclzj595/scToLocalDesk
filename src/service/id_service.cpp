// ============================================================
// scToLocalDesk | src/service/id_service.cpp
// 派生实现：指纹拼装 → xxH3 → 3×3 位折叠格式化
// ============================================================
#include "service/id_service.hpp"

#include <cstdio>

#include "foundation/hashx.hpp"

namespace sc::localdesk {

namespace {

constexpr uint32_t kCodeDomain = 1'000'000'000u;  // 3×3 位十进制域

}  // namespace

IdService::IdService(std::string hostname, std::string username,
                     std::string mac, std::string install_dir) {
    fingerprint_.reserve(hostname.size() + mac.size() + install_dir.size() + 2);
    fingerprint_.append(hostname).push_back('\n');
    fingerprint_.append(mac).push_back('\n');
    fingerprint_.append(install_dir);
    code_number_ = derive_code(fingerprint_);
    code_ = format_code(code_number_);
    name_ = username.empty() ? hostname : username + "@" + hostname;
}

uint32_t IdService::derive_code(std::string_view fingerprint) {
    return static_cast<uint32_t>(foundation::xxh3_64(fingerprint, kIdSalt)
                                 % kCodeDomain);
}

std::string IdService::format_code(uint32_t code) {
    char buf[16] = {};
    std::snprintf(buf, sizeof(buf), "%03u %03u %03u",
                  code / 1'000'000u, code / 1'000u % 1'000u, code % 1'000u);
    return buf;
}

const std::string& IdService::self_code() const {
    return code_;
}

uint32_t IdService::code_number() const {
    return code_number_;
}

const std::string& IdService::self_name() const {
    return name_;
}

const std::string& IdService::fingerprint() const {
    return fingerprint_;
}

}  // namespace sc::localdesk
