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
#include "foundation/fs_portable.hpp"
#include "foundation/hashx.hpp"
#include "foundation/logger/ilogger.hpp"
#include "foundation/thread_pool.hpp"

namespace sc::localdesk {

namespace {

using sc::localdesk::foundation::LogLevel;

// 分片布局：file_id(16)+idx(u32BE)+total(u32BE)+last(1)+len(u32BE)+checksum(u64BE)+data
constexpr size_t kChunkHeaderLen = 16 + 4 + 4 + 1 + 4 + 8;

/// 片校验种子域隔离（"SCLD"），防与其它 xxh3 用途碰撞误判
constexpr uint64_t kChunkSeed = 0x53434C4400000000ULL;

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

void append_be64(std::vector<uint8_t>& v, uint64_t x) {
    for (int s = 56; s >= 0; s -= 8) {
        v.push_back(static_cast<uint8_t>((x >> s) & 0xFF));
    }
}

uint64_t read_be64(const uint8_t* p) {
    uint64_t x = 0;
    for (int i = 0; i < 8; ++i) {
        x = (x << 8) | p[i];
    }
    return x;
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
        // meta 已预建接收（confirm 窗口防丢片）：仅转正，勿重复打开 .part
        const auto t = transfers_.find(file_id);
        if (t != transfers_.end() && t->second.awaiting_confirm) {
            t->second.awaiting_confirm = false;
            return true;
        }
    }
    return accept_file(meta, dir);
}

bool FileService::reject_pending(const std::string& file_id) {
    std::lock_guard<std::mutex> lk(mtx_);
    const bool had = pending_.erase(file_id) > 0;
    // 预建接收清理：关句柄 + 删 .part + 回收条目（用户拒绝，窗口内已写数据不转正）
    const auto it = transfers_.find(file_id);
    if (it != transfers_.end() && it->second.awaiting_confirm) {
        finish_receive(it->second, false);
    }
    return had;
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
    FILE* fp = foundation::fs_open(std::filesystem::path(abs_path), "rb");
    if (fp == nullptr) {
        SCLD_LOG(log_, LogLevel::Error, "file", "[file_id=" + meta.file_id + "] 打开失败");
        return;
    }
    std::vector<uint8_t> buf(chunk_size_);
    std::vector<uint8_t> body;
    body.reserve(kChunkHeaderLen + chunk_size_);
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
        body.clear();
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
        // 片校验（Doc18 §5：xxH3 首个业务用途；接收端不符即断传）
        append_be64(body, foundation::xxh3_64(buf.data(), n, kChunkSeed));
        body.insert(body.end(), buf.begin(), buf.begin() + static_cast<long>(n));
        transport_->send_binary(protocol::build_frame(protocol::MsgType::FileChunk, body));

        transferred += n;
        publish_progress(meta.file_id, meta.name, transferred, meta.size, FileDir::Send);
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
        e.name = meta.name;
        e.dir = FileDir::Send;
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
        // 前置确认：挂 pending → UI 弹窗（Doc11 §4.3 / Doc13）。
        // 同时预建接收（打开 .part）：confirm 走 UI Queued 异步，窗口内到达的片
        // 若无 Transfer 即丢，1GB 压测实测首片丢致跳号失败——预建后片照收，拒绝即清理
        std::string alias;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            pending_[meta.file_id] = meta;
            alias = peer_alias_;
        }
        if (!accept_file(meta, dir)) {
            SCLD_LOG(log_, LogLevel::Warn, "file",
                     "[file_id=" + m.file_id + "] 预建接收失败");
        }
        else {
            std::lock_guard<std::mutex> lk(mtx_);
            const auto it = transfers_.find(meta.file_id);
            if (it != transfers_.end()) {
                it->second.awaiting_confirm = true;
            }
        }
        if (bus_ != nullptr) {
            Evt_FileMetaReceived e;
            e.device_alias = alias;
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
    // 保存目录自建（用户任意配置的 save_path 不保证存在，Doc11 §4.4）
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(save_dir), ec);
    const std::string part_path = save_dir + "/" + meta.file_id + ".part";
    t.out = foundation::fs_open(std::filesystem::path(part_path), "wb");
    if (t.out == nullptr) {
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
    if (pos + 8 + data_len > len) {
        SCLD_LOG(log_, LogLevel::Warn, "file",
                 "[file_id=" + file_id + "] 分片截断 idx=" + std::to_string(idx));
        return false;  // 分片截断，丢弃
    }
    const uint64_t checksum = read_be64(body + pos);
    pos += 8;

    std::lock_guard<std::mutex> lk(mtx_);
    const auto it = transfers_.find(file_id);
    if (it == transfers_.end() || it->second.aborted) {
        SCLD_LOG(log_, LogLevel::Warn, "file",
                 "[file_id=" + file_id + "] 片无归属 idx=" + std::to_string(idx));
        return false;
    }
    auto& t = it->second;
    // 片校验（Doc18 §5）：不符 = 传输损坏，断传清理（Integrity）
    if (foundation::xxh3_64(body + pos, data_len, kChunkSeed) != checksum) {
        SCLD_LOG(log_, LogLevel::Error, "file",
                 "[file_id=" + file_id + "] 片校验失败 idx=" + std::to_string(idx));
        finish_receive(t, false, FileFailReason::Integrity);
        return false;
    }
    if (fwrite(body + pos, 1, data_len, t.out) != data_len) {
        SCLD_LOG(log_, LogLevel::Error, "file",
                 "[file_id=" + file_id + "] 片写入失败 idx=" + std::to_string(idx)
                     + " errno=" + std::to_string(errno));
        finish_receive(t, false);  // 磁盘满 / IO 错误（Doc11 §4.5）
        return false;
    }
    if (idx != t.received_chunks) {
        // 顺序校验：WS 保序，跳号即发送侧丢片（不可续传，按完整性失败终止，Doc11 §4.5）
        SCLD_LOG(log_, LogLevel::Error, "file",
                 "[file_id=" + file_id + "] 片序跳号 idx=" + std::to_string(idx)
                     + " 期望=" + std::to_string(t.received_chunks));
        finish_receive(t, false, FileFailReason::Integrity);
        return false;
    }
    t.received_chunks = idx + 1;
    publish_progress_nolock(file_id, t.meta.name,
                            t.received_chunks * t.meta.size / t.meta.total_chunks,
                            t.meta.size, FileDir::Recv);

    if (is_last || t.received_chunks >= total) {
        finish_receive(t, t.received_chunks >= total);
    }
    return true;
}

void FileService::finish_receive(Transfer& t, bool ok, FileFailReason reason) {
    if (t.out != nullptr) {
        fclose(t.out);
        t.out = nullptr;
    }
    const std::string part_path = t.save_dir + "/" + t.meta.file_id + ".part";
    if (ok) {
        // 完整性校验：落盘字节数对账（跳号/丢片/截断兜底，Doc11 §3；SHA256 全量为 V1.0 任务）
        std::error_code ec;
        const auto part_size = static_cast<uint64_t>(
            std::filesystem::file_size(part_path, ec));
        if (!ec && part_size != t.meta.size) {
            SCLD_LOG(log_, LogLevel::Error, "file",
                     "[file_id=" + t.meta.file_id + "] 落盘不完整 part="
                         + std::to_string(part_size) + " 期望="
                         + std::to_string(t.meta.size));
            ok = false;
        }
    }
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
            e.name = t.meta.name;
            e.dir = FileDir::Recv;
            e.timestamp_us = now_us();
            bus_->publish(e);
        }
        else {
            Evt_FileFailed e;
            e.file_id = t.meta.file_id;
            e.name = t.meta.name;
            e.reason = reason;
            e.dir = FileDir::Recv;
            e.timestamp_us = now_us();
            bus_->publish(e);
        }
    }
    static constexpr const char* kReasonStr[] = {"None",  "Cancelled", "DiskFull",
                                                 "Io",    "Integrity", "Unauthed",
                                                 "Peer"};
    const auto ri = static_cast<int>(reason);
    SCLD_LOG(log_, ok ? LogLevel::Info : LogLevel::Error, "file",
             "[file_id=" + t.meta.file_id + "] 接收"
                 + (ok ? std::string("完成")
                       : "失败 reason="
                             + std::string(kReasonStr[ri >= 0 && ri < 7 ? ri : 3])));
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

void FileService::publish_progress(const std::string& file_id, const std::string& name,
                                   uint64_t transferred, uint64_t total, FileDir dir) {
    std::lock_guard<std::mutex> lk(mtx_);
    publish_progress_nolock(file_id, name, transferred, total, dir);
}

void FileService::publish_progress_nolock(const std::string& file_id, const std::string& name,
                                          uint64_t transferred, uint64_t total, FileDir dir) {
    // 调用方已持有 mtx_：本函数全程不得再加锁（nolock 语义）
    if (bus_ != nullptr) {
        Evt_FileProgress e;
        e.file_id = file_id;
        e.name = name;
        e.transferred = transferred;
        e.total = total;
        e.dir = dir;
        e.timestamp_us = now_us();
        bus_->publish(e);
    }
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
