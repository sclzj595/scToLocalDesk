// ============================================================
// scToLocalDesk | src/service/file_service.cpp
// 文件传输服务实现：FILE_META/FILE_CHUNK 收发（Doc05 §5 / Doc11）
// ============================================================
#include "service/file_service.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <random>
#include <vector>

#include "core/events.hpp"
#include "core/protocol/tlv.hpp"
#include "foundation/event_bus.hpp"
#include "foundation/logger/ilogger.hpp"
#include "foundation/thread_pool.hpp"

namespace sc::localdesk {

namespace {

using sc::localdesk::foundation::LogLevel;

constexpr size_t kChunkHeaderLen = 16 + 4 + 4 + 1 + 4;  // file_id+idx+total+last+len

int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string make_file_id() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::string hex;
    hex.reserve(32);
    static constexpr char kDigits[] = "0123456789abcdef";
    for (int i = 0; i < 2; ++i) {
        const uint64_t r = rng();
        for (int b = 60; b >= 0; b -= 4) {
            hex.push_back(kDigits[(r >> b) & 0xF]);
        }
    }
    return hex;
}

void append_be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 24));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

}  // namespace

FileService::FileService(std::shared_ptr<ITransport> transport,
                         std::shared_ptr<foundation::EventBus> bus,
                         std::shared_ptr<foundation::ILogger> logger)
    : transport_(std::move(transport)),
      bus_(std::move(bus)),
      log_(std::move(logger)) {
}

void FileService::set_chunk_size(uint32_t chunk_size_kb) {
    if (chunk_size_kb >= 64 && chunk_size_kb <= 1024) {
        chunk_size_ = chunk_size_kb * 1024;
    }
}

void FileService::set_save_dir(std::string dir) {
    std::lock_guard<std::mutex> lk(mtx_);
    save_dir_ = std::move(dir);
}

void FileService::set_thread_pool(std::shared_ptr<foundation::ThreadPool> pool) {
    pool_ = std::move(pool);
}

void FileService::set_confirm_enabled(bool enabled) {
    std::lock_guard<std::mutex> lk(mtx_);
    confirm_enabled_ = enabled;
}

bool FileService::accept_pending(const std::string& file_id) {
    std::string dir;
    FileMeta meta;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        const auto it = pending_.find(file_id);
        if (it == pending_.end()) {
            return false;
        }
        meta = it->second;
        dir = save_dir_;
        pending_.erase(it);
    }
    return accept_file(meta, dir);
}

bool FileService::reject_pending(const std::string& file_id) {
    std::lock_guard<std::mutex> lk(mtx_);
    return pending_.erase(file_id) > 0;
}

void FileService::set_event_callback(FileEventCb cb) {
    std::lock_guard<std::mutex> lk(mtx_);
    event_cb_ = std::move(cb);
}

// ---------------- 发送 ----------------

bool FileService::send_file(const std::string& abs_path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(abs_path, ec)) {
        SCLD_LOG(log_, LogLevel::Warn, "file", "发送路径非法：" + abs_path);
        return false;
    }
    const auto size = static_cast<uint64_t>(std::filesystem::file_size(abs_path, ec));
    if (ec) {
        return false;
    }

    FileMeta meta;
    meta.file_id = make_file_id();
    meta.name = std::filesystem::path(abs_path).filename().string();
    meta.size = size;
    meta.total_chunks = static_cast<uint32_t>((size + chunk_size_ - 1) / chunk_size_);

    if (transport_ != nullptr) {
        transport_->send_binary(
            protocol::build_file_meta(meta.file_id, meta.name, meta.size, meta.total_chunks));
    }
    SCLD_LOG(log_, LogLevel::Info, "file",
             "[file_id=" + meta.file_id + "] 发送登记 name=" + meta.name
                 + " size=" + std::to_string(size));

    // 发送条目注册：cancel/stop_all 置 aborted 后 send_loop 下片即退出（Doc11 §4.4）
    {
        std::lock_guard<std::mutex> lk(mtx_);
        Transfer t;
        t.meta = meta;
        transfers_.emplace(meta.file_id, std::move(t));
    }

    if (pool_ != nullptr) {
        (void)pool_->submit([this, abs_path, meta] { send_loop(abs_path, meta); });
    }
    else {
        send_loop(abs_path, meta);  // 无池：同步发送（headless 自检）
    }
    return true;
}

void FileService::send_loop(const std::string& abs_path, const FileMeta& meta) {
    if (transport_ == nullptr) {
        return;
    }
    FILE* fp = nullptr;
    if (fopen_s(&fp, abs_path.c_str(), "rb") != 0 || fp == nullptr) {
        SCLD_LOG(log_, LogLevel::Error, "file", "[file_id=" + meta.file_id + "] 打开失败");
        return;
    }
    std::vector<uint8_t> buf(chunk_size_);
    uint64_t transferred = 0;
    bool aborted = false;
    for (uint32_t idx = 0; idx < meta.total_chunks; ++idx) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            const auto it = transfers_.find(meta.file_id);
            aborted = (it == transfers_.end() || it->second.aborted);
        }
        if (aborted) {
            break;
        }
        const size_t n = fread(buf.data(), 1, chunk_size_, fp);
        if (n == 0) {
            break;
        }
        std::vector<uint8_t> body;
        body.reserve(kChunkHeaderLen + n);
        // file_id hex → 16B
        for (size_t i = 0; i < 16; ++i) {
            const auto nib = [](char c) {
                return c <= '9' ? static_cast<uint8_t>(c - '0')
                                : static_cast<uint8_t>(c - 'a' + 10);
            };
            body.push_back(static_cast<uint8_t>((nib(meta.file_id[i * 2]) << 4)
                                                | nib(meta.file_id[i * 2 + 1])));
        }
        append_be32(body, idx);
        append_be32(body, meta.total_chunks);
        body.push_back(idx == meta.total_chunks - 1 ? 1 : 0);
        append_be32(body, static_cast<uint32_t>(n));
        body.insert(body.end(), buf.begin(), buf.begin() + static_cast<long>(n));
        transport_->send_binary(protocol::build_frame(protocol::MsgType::FileChunk, body));

        transferred += n;
        publish_progress(meta.file_id, transferred, meta.size);
    }
    fclose(fp);

    // 发送条目自清（aborted 或正常结束均回收）
    {
        std::lock_guard<std::mutex> lk(mtx_);
        transfers_.erase(meta.file_id);
    }

    if (bus_ != nullptr && !aborted) {
        Evt_FileDone e;
        e.session_id.clear();
        e.device_alias.clear();
        e.file_id = meta.file_id;
        e.timestamp_us = now_us();
        bus_->publish(e);
    }
    SCLD_LOG(log_, aborted ? LogLevel::Warn : LogLevel::Info, "file",
             "[file_id=" + meta.file_id + "] 发送" + (aborted ? "中止" : "完成")
                 + " transferred=" + std::to_string(transferred));
}

// ---------------- 接收 ----------------

void FileService::on_file_meta(const uint8_t* body, size_t len) {
    protocol::FileMetaBody m;
    if (!protocol::parse_file_meta(body, len, m)) {
        SCLD_LOG(log_, LogLevel::Warn, "file", "FILE_META 解析失败");
        return;
    }
    // 前置确认（file.confirm_before_transfer，Doc11 §4.3）：开启时挂 pending 等accept/reject
    FileMeta meta;
    meta.file_id = m.file_id;
    meta.name = m.name;
    meta.size = m.size;
    meta.total_chunks = m.total_chunks;
    std::string dir;
    bool need_confirm = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        need_confirm = confirm_enabled_;
        dir = save_dir_;
    }
    if (need_confirm) {
        // 前置确认：挂 pending → UI 弹窗（Doc11 §4.3 / Doc13）
        std::lock_guard<std::mutex> lk(mtx_);
        pending_[meta.file_id] = meta;
        if (bus_ != nullptr) {
            Evt_FileMetaReceived e;
            e.device_alias.clear();
            e.file_id = meta.file_id;
            e.name = meta.name;
            e.size = meta.size;
            e.timestamp_us = now_us();
            bus_->publish(e);
        }
        SCLD_LOG(log_, LogLevel::Info, "file",
                 "[file_id=" + meta.file_id + "] 等待接收确认 name=" + meta.name);
        return;
    }
    if (!accept_file(meta, dir)) {
        SCLD_LOG(log_, LogLevel::Warn, "file", "[file_id=" + m.file_id + "] 接收登记失败");
    }
}

bool FileService::accept_file(const FileMeta& meta, const std::string& save_dir) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (meta.file_id.empty() || meta.total_chunks == 0) {
        return false;
    }
    Transfer t;
    t.meta = meta;
    t.save_dir = save_dir;
    const std::string part_path = save_dir + "/" + meta.file_id + ".part";
    if (fopen_s(&t.out, part_path.c_str(), "wb") != 0 || t.out == nullptr) {
        SCLD_LOG(log_, LogLevel::Error, "file",
                 "[file_id=" + meta.file_id + "] 临时文件创建失败：" + part_path);
        return false;
    }
    transfers_.emplace(meta.file_id, std::move(t));
    SCLD_LOG(log_, LogLevel::Info, "file",
             "[file_id=" + meta.file_id + "] 接收登记 name=" + meta.name
                 + " chunks=" + std::to_string(meta.total_chunks));
    return true;
}

bool FileService::on_file_chunk(const uint8_t* body, size_t len) {
    if (body == nullptr || len < kChunkHeaderLen) {
        return false;
    }
    static constexpr char kHex[] = "0123456789abcdef";
    std::string file_id;
    file_id.reserve(32);
    for (size_t i = 0; i < 16; ++i) {
        file_id.push_back(kHex[body[i] >> 4]);
        file_id.push_back(kHex[body[i] & 0x0F]);
    }
    size_t pos = 16;
    const uint32_t idx = (static_cast<uint32_t>(body[pos]) << 24)
                       | (static_cast<uint32_t>(body[pos + 1]) << 16)
                       | (static_cast<uint32_t>(body[pos + 2]) << 8)
                       | static_cast<uint32_t>(body[pos + 3]);
    pos += 4;
    const uint32_t total = (static_cast<uint32_t>(body[pos]) << 24)
                         | (static_cast<uint32_t>(body[pos + 1]) << 16)
                         | (static_cast<uint32_t>(body[pos + 2]) << 8)
                         | static_cast<uint32_t>(body[pos + 3]);
    pos += 4;
    const bool is_last = body[pos++] != 0;
    const uint32_t data_len = (static_cast<uint32_t>(body[pos]) << 24)
                            | (static_cast<uint32_t>(body[pos + 1]) << 16)
                            | (static_cast<uint32_t>(body[pos + 2]) << 8)
                            | static_cast<uint32_t>(body[pos + 3]);
    pos += 4;
    if (pos + data_len > len) {
        return false;  // 分片截断，丢弃
    }

    std::lock_guard<std::mutex> lk(mtx_);
    const auto it = transfers_.find(file_id);
    if (it == transfers_.end() || it->second.aborted) {
        return false;
    }
    auto& t = it->second;
    if (fwrite(body + pos, 1, data_len, t.out) != data_len) {
        finish_receive(t, false);  // 磁盘满 / IO 错误（Doc11 §4.5）
        return false;
    }
    t.received_chunks = idx + 1;
    publish_progress_nolock(file_id, t.received_chunks * t.meta.size / t.meta.total_chunks,
                            t.meta.size);

    if (is_last || t.received_chunks >= total) {
        finish_receive(t, t.received_chunks >= total);
    }
    return true;
}

void FileService::finish_receive(Transfer& t, bool ok) {
    if (t.out != nullptr) {
        fclose(t.out);
        t.out = nullptr;
    }
    const std::string part_path = t.save_dir + "/" + t.meta.file_id + ".part";
    if (ok) {
        // 完整性校验：分片计数对账（Doc11 §3；CRC32/SHA256 为 V1.0 任务）
        std::error_code ec;
        const std::string final_path = t.save_dir + "/" + t.meta.name;
        std::filesystem::rename(part_path, final_path, ec);
        if (ec) {
            SCLD_LOG(log_, LogLevel::Error, "file",
                     "[file_id=" + t.meta.file_id + "] rename 失败：" + final_path);
            ok = false;
        }
    }
    if (!ok) {
        std::error_code ec;
        std::filesystem::remove(part_path, ec);  // 失败清理临时文件（Doc11 §4.4）
    }
    if (bus_ != nullptr) {
        if (ok) {
            Evt_FileDone e;
            e.file_id = t.meta.file_id;
            e.timestamp_us = now_us();
            bus_->publish(e);
        }
        else {
            Evt_FileFailed e;
            e.file_id = t.meta.file_id;
            e.reason = FileFailReason::IoError;
            e.timestamp_us = now_us();
            bus_->publish(e);
        }
    }
    SCLD_LOG(log_, ok ? LogLevel::Info : LogLevel::Error, "file",
             "[file_id=" + t.meta.file_id + "] 接收" + (ok ? "完成" : "失败"));
    transfers_.erase(t.meta.file_id);
}

bool FileService::reject_file(const std::string& file_id) {
    std::lock_guard<std::mutex> lk(mtx_);
    const auto it = transfers_.find(file_id);
    if (it == transfers_.end()) {
        return false;
    }
    finish_receive(it->second, false);  // 关句柄 + 清临时文件 + 回收条目
    return true;
}

void FileService::cancel(const std::string& file_id) {
    std::lock_guard<std::mutex> lk(mtx_);
    const auto it = transfers_.find(file_id);
    if (it == transfers_.end()) {
        return;
    }
    auto& t = it->second;
    t.aborted = true;  // 发送循环下片感知（Doc11 §4.4）
    if (t.out != nullptr) {
        finish_receive(t, false);  // 接收中取消：立即回收句柄与临时文件
    }
}

void FileService::stop_all() {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<std::string> receive_ids;  // 接收条目：立即回收
    for (auto& [id, t] : transfers_) {
        t.aborted = true;  // 发送条目：send_loop 下片感知后自行退出并 erase
        if (t.out != nullptr) {
            receive_ids.push_back(id);
        }
    }
    for (const auto& id : receive_ids) {
        const auto it = transfers_.find(id);
        if (it != transfers_.end()) {
            finish_receive(it->second, false);  // 关句柄 + 清临时文件 + erase
        }
    }
    pending_.clear();
    SCLD_LOG(log_, LogLevel::Info, "file", "会话回收：全部传输已中止");
}

void FileService::publish_progress(const std::string& file_id, uint64_t transferred,
                                   uint64_t total) {
    std::lock_guard<std::mutex> lk(mtx_);
    publish_progress_nolock(file_id, transferred, total);
}

void FileService::publish_progress_nolock(const std::string& file_id, uint64_t transferred,
                                          uint64_t total) {
    // 调用方已持有 mtx_（send_loop 无锁路径也走 publish_progress 的外层加锁）
    if (bus_ != nullptr) {
        Evt_FileProgress e;
        e.file_id = file_id;
        e.transferred = transferred;
        e.total = total;
        e.timestamp_us = now_us();
        bus_->publish(e);
    }
    std::lock_guard<std::mutex> lk(mtx_);
    if (event_cb_) {
        FileEvent fe;
        fe.file_id = file_id;
        fe.kind = FileEventKind::Progress;
        fe.transferred = transferred;
        fe.total = total;
        event_cb_(fe);
    }
}

}  // namespace sc::localdesk
