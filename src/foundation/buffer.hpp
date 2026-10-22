// ============================================================
// scToLocalDesk | src/foundation/buffer.hpp
// 动态二进制缓冲区：几何扩容 + 读写游标，TLV 组包/解析专用（Doc12 §4）
// 纯 C++ 无 Qt。跨线程不共享所有权，移交即转移
// ============================================================
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace sc::localdesk::foundation {

class Buffer {
public:
    Buffer() = default;
    explicit Buffer(size_t reserve) { data_.reserve(reserve); }

    // ---------------- 写（追加语义，大端序对齐协议 Doc05） ----------------

    void append(const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        data_.insert(data_.end(), b, b + n);
    }

    void append_u8(uint8_t v) { data_.push_back(v); }

    void append_u16be(uint16_t v) {
        data_.push_back(static_cast<uint8_t>(v >> 8));
        data_.push_back(static_cast<uint8_t>(v));
    }

    void append_u32be(uint32_t v) {
        data_.push_back(static_cast<uint8_t>(v >> 24));
        data_.push_back(static_cast<uint8_t>(v >> 16));
        data_.push_back(static_cast<uint8_t>(v >> 8));
        data_.push_back(static_cast<uint8_t>(v));
    }

    /// u8 长度前缀字符串（心跳 alias、别名载体）
    void append_string8(std::string_view sv) {
        append_u8(static_cast<uint8_t>(sv.size()));
        append(sv.data(), sv.size());
    }

    /// u16 长度前缀字符串（TLV body 内字符串字段）
    void append_string16(std::string_view sv) {
        append_u16be(static_cast<uint16_t>(sv.size()));
        append(sv.data(), sv.size());
    }

    /// 回写已占位字段：如 header 的 body_len（offset 从 0 计）
    void overwrite_u32be(size_t offset, uint32_t v) {
        if (offset + 4 > data_.size()) {
            return;
        }
        data_[offset]     = static_cast<uint8_t>(v >> 24);
        data_[offset + 1] = static_cast<uint8_t>(v >> 16);
        data_[offset + 2] = static_cast<uint8_t>(v >> 8);
        data_[offset + 3] = static_cast<uint8_t>(v);
    }

    // ---------------- 读（游标语义，失败不移动游标） ----------------

    [[nodiscard]] bool read_u8(uint8_t& out) {
        if (read_pos_ + 1 > data_.size()) {
            return false;
        }
        out = data_[read_pos_++];
        return true;
    }

    [[nodiscard]] bool read_u16be(uint16_t& out) {
        if (read_pos_ + 2 > data_.size()) {
            return false;
        }
        out = static_cast<uint16_t>((data_[read_pos_] << 8) | data_[read_pos_ + 1]);
        read_pos_ += 2;
        return true;
    }

    [[nodiscard]] bool read_u32be(uint32_t& out) {
        if (read_pos_ + 4 > data_.size()) {
            return false;
        }
        out = (static_cast<uint32_t>(data_[read_pos_]) << 24)
            | (static_cast<uint32_t>(data_[read_pos_ + 1]) << 16)
            | (static_cast<uint32_t>(data_[read_pos_ + 2]) << 8)
            | static_cast<uint32_t>(data_[read_pos_ + 3]);
        read_pos_ += 4;
        return true;
    }

    /// 读取 n 字节拷贝到 out（越界返回 false）
    [[nodiscard]] bool read_bytes(size_t n, std::vector<uint8_t>& out) {
        if (read_pos_ + n > data_.size()) {
            return false;
        }
        out.assign(data_.begin() + static_cast<long>(read_pos_),
                   data_.begin() + static_cast<long>(read_pos_ + n));
        read_pos_ += n;
        return true;
    }

    /// 零拷贝视图读取（调用方不得持有引用越过下次读操作）
    [[nodiscard]] bool read_string_view(size_t n, std::string_view& out) {
        if (read_pos_ + n > data_.size()) {
            return false;
        }
        out = {reinterpret_cast<const char*>(data_.data()) + read_pos_, n};
        read_pos_ += n;
        return true;
    }

    [[nodiscard]] bool read_string8(std::string& out) {
        uint8_t len = 0;
        if (!read_u8(len)) {
            return false;
        }
        std::string_view sv;
        if (!read_string_view(len, sv)) {
            return false;
        }
        out.assign(sv);
        return true;
    }

    // ---------------- 状态 ----------------

    [[nodiscard]] const uint8_t* data() const noexcept { return data_.data(); }
    [[nodiscard]] size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] size_t readable() const noexcept { return data_.size() - read_pos_; }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }

    void clear() noexcept {
        data_.clear();
        read_pos_ = 0;
    }

    void reset_read() noexcept { read_pos_ = 0; }

    /// 释放底层内存，保留容量清零（内存复用，Doc12 §4）
    void reuse() noexcept {
        data_.assign(data_.capacity(), 0);
        data_.clear();
        read_pos_ = 0;
    }

    /// 取出全部字节（移交所有权）
    [[nodiscard]] std::vector<uint8_t> take() {
        read_pos_ = 0;
        std::vector<uint8_t> out;
        out.swap(data_);
        return out;
    }

private:
    std::vector<uint8_t> data_;
    size_t read_pos_{0};
};

}  // namespace sc::localdesk::foundation
