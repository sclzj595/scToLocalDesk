// ============================================================
// scToLocalDesk | src/foundation/jsonx.hpp
// nlohmann/json 封装（third_party/nlohmann 单头）
// 用途：协议信令扩展（V1.0）、结构化配置迁移预留
// 封装约定：业务层禁止直接用 nlohmann 命名空间——统一经本层，
//   异常在边界吃掉（noexcept 包装），错误返回空可选
// ============================================================
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace sc::localdesk::foundation {

using json = nlohmann::json;

/// 解析（noexcept 包装）：非法输入返回 nullopt，不抛异常打断业务流
inline std::optional<json> json_parse(std::string_view text) noexcept {
    try {
        return json::parse(text);
    }
    catch (...) {
        return std::nullopt;
    }
}

/// 紧凑序列化：中文保持 UTF-8 明文（ensure_ascii=false，协议可读），
/// 非法 UTF-8 替换而非抛异常（error_handler=replace）
inline std::string json_dump(const json& j) {
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

}  // namespace sc::localdesk::foundation
