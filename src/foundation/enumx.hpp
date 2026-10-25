// ============================================================
// scToLocalDesk | src/foundation/enumx.hpp
// magic_enum 封装（third_party/magic_enum）：枚举名反射
// 用途：日志/协议调试中打印枚举名（避免手写 switch 映射表）
// 守卫：__has_include 探测，库未落位时退化 "enum" 占位——
//   third_party/magic_enum 补齐后无需改代码自动启用
// 封装约定：只暴露 enum_name/enum_cast 两个高频入口
// ============================================================
#pragma once

#include <optional>
#include <string_view>

#if __has_include(<magic_enum/magic_enum_all.hpp>)
#define SCLD_HAS_MAGIC_ENUM 1
#include <magic_enum/magic_enum_all.hpp>
#else
#define SCLD_HAS_MAGIC_ENUM 0
#endif

namespace sc::localdesk::foundation {

template <typename E>
constexpr std::string_view enum_name(E value) noexcept {
#if SCLD_HAS_MAGIC_ENUM
    return magic_enum::enum_name(value);
#else
    (void)value;
    return "enum";   // 库未落位：占位名（CMake 注册中心补齐即自动真名）
#endif
}

template <typename E>
constexpr std::optional<E> enum_cast(std::string_view name) noexcept {
#if SCLD_HAS_MAGIC_ENUM
    const auto v = magic_enum::enum_cast<E>(name, magic_enum::case_insensitive);
    if (v.has_value()) {
        return v.value();
    }
    return std::nullopt;
#else
    (void)name;
    return std::nullopt;
#endif
}

}  // namespace sc::localdesk::foundation
