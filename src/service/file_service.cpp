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

/// UTF-8 窄串 → filesystem path：MSVC path(std::string) 按 ACP 解释，
/// Qt 层传来的中文目录/文件名（UTF-8）会建出乱码目录——统一走 u8path。
/// 约定：本服务全部字符串路径参数为 UTF-8（含 meta.name 协议字段）
std::filesystem::path fs_path(std::string_view utf8) {
    return std::filesystem::u8path(utf8);
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
    bool prebuilt = false;
    bool conflict = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        const auto it = pending_.find(file_id);
        if (it == pending_.end()) {
            return false;
        }
        meta = it->second;
        dir = save_dir_;
        pending_.erase(it);
        // meta 已预建接收（confirm 窗口防丢片）：转正 + 冲突裁决（Doc19 §5）——
        // 预建时延后裁决，此处目标已存在且策略为 ask → 置冲突待决发事件
        const auto t = transfers_.find(file_id);
        if (t != transfers_.end() && t->second.awaiting_confirm) {
            t->second.awaiting_confirm = false;
            prebuilt = true;
            apply_conflict_policy_locked(t->second);
            conflict = t->second.conflict_pending;
        }
    }
    if (prebuilt) {
        if (conflict) {
            std::lock_guard<std::mutex> lk(mtx_);
            publish_conflict_nolock(Transfer{meta, dir, meta.name});
        }
        return true;  // .scpart 预建已打开，片照收；转正名由冲突策略/resolve 定
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
    const auto p = fs_path(abs_path);
    if (std::filesystem::is_regular_file(p, ec)) {
        return send_one_file(abs_path, p.filename().u8string());
    }
    // 目录入口（Doc19 §5）：递归展开逐项入队，沿用既有并行/排队机制
    if (std::filesystem::is_directory(p, ec)) {
        return send_directory(abs_path);
    }
    SCLD_LOG(log_, LogLevel::Warn, "file", "发送路径非法：" + abs_path);
    return false;
}

// 单文件入队（目录传输复用）：rel_name 为协议展示名（目录传输时 '/' 分隔相对路径）
bool FileService::send_one_file(const std::string& abs_path, const std::string& rel_name) {
    std::error_code ec;
    const auto size =
        static_cast<uint64_t>(std::filesystem::file_size(fs_path(abs_path), ec));
    if (ec) {
        return false;
    }
    // 可执行后缀早拒（Doc19 §5）：发送侧与接收侧同判，UI 失败态显示拦截文案
    bool block_exec = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        block_exec = block_executable_;
    }
    if (block_exec && blocked_extension(rel_name)) {
        SCLD_LOG(log_, LogLevel::Warn, "file", "已拦截可执行文件发送 name=" + rel_name);
        std::lock_guard<std::mutex> lk(mtx_);
        publish_send_failed_nolock(make_file_id(), rel_name, FileFailReason::Blocked);
        return false;
    }
    FileMeta meta;
    meta.file_id = make_file_id();
    meta.name = rel_name;  // 协议字段 UTF-8
    meta.size = size;
    meta.total_chunks = static_cast<uint32_t>((size + chunk_size_ - 1) / chunk_size_);
    return enqueue_send(abs_path, meta);
}

// 入队公共段（Doc19 §5 P2）：活跃数 < 上限立即启动，超出串行排队
bool FileService::enqueue_send(const std::string& abs_path, const FileMeta& meta) {
    bool immediate = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (active_sends_ < max_parallel_) {
            ++active_sends_;
            immediate = true;
        }
        else {
            send_queue_.push_back(QueuedSend{abs_path, meta});
        }
    }
    if (immediate) {
        // META 失败时计数回退与补位已在 begin_send 内完成
        return begin_send(abs_path, meta);
    }

    // 排队登记：仅发 UI 事件（不发 FILE_META 不占传输表，取消即出队零残留）
    publish_queued(meta.file_id, meta.name, meta.size);
    SCLD_LOG(log_, LogLevel::Info, "file",
             "[file_id=" + meta.file_id + "] 发送排队 name=" + meta.name
                 + " size=" + std::to_string(meta.size));
    return true;
}

// 目录递归入队（Doc19 §5）：根目录名作协议路径前缀，is_dir 条目与文件按
// 遍历顺序入队；隐藏/特殊文件提前跳过（接收端 name_safe 拒收 '.' 开头名）
bool FileService::send_directory(const std::string& abs_path) {
    std::error_code ec;
    const auto root = fs_path(abs_path).lexically_normal();
    if (!std::filesystem::is_directory(root, ec)) {
        SCLD_LOG(log_, LogLevel::Warn, "file", "发送目录非法：" + abs_path);
        return false;
    }
    std::string prefix = root.filename().u8string();
    if (prefix.empty()) {
        prefix = root.parent_path().filename().u8string();  // 尾带分隔符兜底
    }
    if (prefix.empty()) {
        SCLD_LOG(log_, LogLevel::Warn, "file", "目录名解析失败：" + abs_path);
        return false;
    }
    bool block_exec = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        block_exec = block_executable_;
    }
    size_t n_dirs = 1;
    size_t n_files = 0;
    size_t n_skipped = 0;
    // 根目录 meta（is_dir=1，size=0 无分片）：空目录也同步，接收侧 mkdir 即完成
    FileMeta root_meta;
    root_meta.file_id = make_file_id();
    root_meta.name = prefix;
    root_meta.flags = kFileFlagIsDir;
    if (!enqueue_send(abs_path, root_meta)) {
        return false;
    }
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    while (!ec && it != std::filesystem::recursive_directory_iterator()) {
        std::error_code e2;
        const auto& entry = *it;
        const auto rel = entry.path().lexically_relative(root).generic_u8string();
        if (entry.is_directory(e2) && !rel.empty()) {
            FileMeta m;
            m.file_id = make_file_id();
            m.name = prefix + "/" + rel;
            m.flags = kFileFlagIsDir;
            (void)enqueue_send(entry.path().u8string(), m);
            ++n_dirs;
            it.increment(ec);
            continue;
        }
        const std::string leaf = rel.substr(rel.find_last_of('/') + 1);
        if (!e2 && entry.is_regular_file(e2) && !rel.empty() && !leaf.empty()
            && leaf.front() != '.') {
            if (block_exec && blocked_extension(leaf)) {
                SCLD_LOG(log_, LogLevel::Warn, "file",
                         "目录传输拦截可执行文件 name=" + prefix + "/" + rel);
                ++n_skipped;
                it.increment(ec);
                continue;
            }
            const auto fsize = static_cast<uint64_t>(entry.file_size(e2));
            if (!e2) {
                FileMeta m;
                m.file_id = make_file_id();
                m.name = prefix + "/" + rel;
                m.size = fsize;
                m.total_chunks =
                    static_cast<uint32_t>((fsize + chunk_size_ - 1) / chunk_size_);
                (void)enqueue_send(entry.path().u8string(), m);
                ++n_files;
                it.increment(ec);
                continue;
            }
        }
        ++n_skipped;  // 隐藏/特殊/不可访问条目：跳过继续
        it.increment(ec);
    }
    if (ec) {
        SCLD_LOG(log_, LogLevel::Warn, "file",
                 "目录遍历中断 root=" + prefix + " ec=" + std::to_string(ec.value()));
    }
    SCLD_LOG(log_, LogLevel::Info, "file",
             "目录传输入队 root=" + prefix + " dirs=" + std::to_string(n_dirs)
                 + " files=" + std::to_string(n_files)
                 + " skipped=" + std::to_string(n_skipped));
    return true;
}

// 出队启动（Doc19 §5 P2）：发 FILE_META → 登记 → 线程池分片发送。
// META 失败 = 出站链路失效：发布发送侧失败事件后回退计数并补位下一个
bool FileService::begin_send(const std::string& abs_path, const FileMeta& meta) {
    if (!out_frame(protocol::build_file_meta(meta.file_id, meta.name, meta.size,
                                             meta.total_chunks, meta.flags))) {
        SCLD_LOG(log_, LogLevel::Error, "file",
                 "[file_id=" + meta.file_id + "] META 发送失败（无出站链路）");
        {
            std::lock_guard<std::mutex> lk(mtx_);
            publish_send_failed_nolock(meta.file_id, meta.name, FileFailReason::IoError);
        }
        backfill_send_slot();  // 失败自动补位：下一个排队任务顶上
        return false;
    }
    SCLD_LOG(log_, LogLevel::Info, "file",
             "[file_id=" + meta.file_id + "] 发送登记 name=" + meta.name
                 + " size=" + std::to_string(meta.size));

    // 发送条目注册：cancel/stop_all 置 aborted 后 send_loop 下片即退出（Doc11 §4.4）
    {
        std::lock_guard<std::mutex> lk(mtx_);
        Transfer t;
        t.meta = meta;
        transfers_.emplace(meta.file_id, std::move(t));
    }
    if ((meta.flags & kFileFlagIsDir) != 0) {
        // 目录条目无分片（Doc19 §5）：META 即完成，立即回收并补位下一个
        {
            std::lock_guard<std::mutex> lk(mtx_);
            transfers_.erase(meta.file_id);
        }
        if (bus_ != nullptr) {
            Evt_FileDone e;
            e.session_id.clear();
            e.device_alias.clear();
            e.file_id = meta.file_id;
            e.name = meta.name;
            e.dir = FileDir::Send;
            e.timestamp_us = now_us();
            bus_->publish(e);
        }
        SCLD_LOG(log_, LogLevel::Info, "file",
                 "[file_id=" + meta.file_id + "] 目录登记完成 name=" + meta.name);
        backfill_send_slot();
        return true;
    }

    if (pool_ != nullptr) {
        (void)pool_->submit([this, abs_path, meta] { send_loop(abs_path, meta); });
    }
    else {
        send_loop(abs_path, meta);  // 无池：同步发送（headless 自检）
    }
    return true;
}

// 补位（Doc19 §5 P2）：活跃 send 回退计数后，上限内弹队首启动。
// 启动再失败经 begin_send → 本函数递归（每层出队一条，快速排空整队）
void FileService::backfill_send_slot() {
    std::string abs_path;
    FileMeta meta;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (active_sends_ > 0) {
            --active_sends_;
        }
        if (active_sends_ >= max_parallel_ || send_queue_.empty()) {
            return;
        }
        QueuedSend q = std::move(send_queue_.front());
        send_queue_.pop_front();
        abs_path = std::move(q.abs_path);
        meta = std::move(q.meta);
        ++active_sends_;
    }
    SCLD_LOG(log_, LogLevel::Info, "file",
             "[file_id=" + meta.file_id + "] 出队启动 name=" + meta.name);
    (void)begin_send(abs_path, meta);
}

bool FileService::out_frame(const std::vector<uint8_t>& frame) {
    // 出站统一口：注入的加密链路优先（会话加密 PRD §6.2），未注入回落裸发
    if (send_fn_) {
        return send_fn_(frame);
    }
    if (transport_ != nullptr) {
        transport_->send_binary(frame);
        return true;
    }
    return false;
}

void FileService::send_loop(const std::string& abs_path, const FileMeta& meta) {
    if (transport_ == nullptr && !send_fn_) {
        backfill_send_slot();  // 出站链路缺失：判负收尾并补位（Doc19 §5）
        return;
    }
    FILE* fp = foundation::fs_open(fs_path(abs_path), "rb");
    if (fp == nullptr) {
        SCLD_LOG(log_, LogLevel::Error, "file", "[file_id=" + meta.file_id + "] 打开失败");
        backfill_send_slot();  // 打开失败：收尾补位（Doc19 §5）
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
        if (!out_frame(protocol::build_frame(protocol::MsgType::FileChunk, body))) {
            SCLD_LOG(log_, LogLevel::Error, "file",
                     "[file_id=" + meta.file_id + "] 分片发送失败 idx="
                         + std::to_string(idx));
            break;
        }

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
    backfill_send_slot();  // 队列补位（Doc19 §5 P2）：成功/失败/取消均弹下一个
}

// ---------------- 接收 ----------------

bool FileService::name_safe(const std::string& name) {
    if (name.empty() || name.size() > 1024) {
        return false;
    }
    if (name.find('\\') != std::string::npos) {
        return false;  // 反斜杠一律拒绝（协议相对路径仅用 '/' 分隔）
    }
    // 相对路径逐级校验（Doc19 §5 目录传输）：每段按单文件名红线规则，
    // 覆盖每级防 ../ 段内穿越；单段名（绝大多数传输）走同一逻辑
    size_t start = 0;
    while (true) {
        const size_t slash = name.find('/', start);
        const std::string seg =
            name.substr(start, slash == std::string::npos ? std::string::npos
                                                          : slash - start);
        if (seg.empty() || seg.size() > 255) {
            return false;  // 空段（含首尾 '/'、连续 '//'）与超长段拒绝
        }
        if (seg.front() == '.') {
            return false;  // 隐藏文件 / 相对引用开头
        }
        for (const char c : seg) {
            const auto uc = static_cast<unsigned char>(c);
            if (uc < 0x20 || uc == 0x7F) {
                return false;  // 控制字符
            }
            if (c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<'
                || c == '>' || c == '|') {
                return false;  // 盘符/通配符元字符（'/' 已作为分隔符消费）
            }
        }
        if (seg.find("..") != std::string::npos) {
            return false;  // 父目录引用（含 a..b 形态）
        }
        // Windows 保留设备名（CON.asp 等 8.3 形态同拦；小写表配 low 比较）
        static const char* const kReserved[] = {
            "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6",
            "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6",
            "lpt7", "lpt8", "lpt9"};
        const size_t dot = seg.find(".");
        const std::string base =
            dot == std::string::npos ? seg : seg.substr(0, dot);
        std::string low = base;
        for (char& c : low) {
            if (c >= 'A' && c <= 'Z') {
                c += 32;
            }
        }
        for (const char* r : kReserved) {
            if (low == r) {
                return false;
            }
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    return true;
}

// 可执行后缀黑名单（Doc19 §5）：取末段 '.' 后缀小写比较；后缀仅认叶子名内
// 的 '.'（父目录名中的点不参与），双端共用同一张表
bool FileService::blocked_extension(const std::string& name) {
    static constexpr const char* kExt[] = {".exe", ".bat", ".cmd", ".ps1", ".msi",
                                           ".scr", ".com", ".vbs", ".js"};
    const size_t slash = name.find_last_of('/');
    const std::string leaf =
        slash == std::string::npos ? name : name.substr(slash + 1);
    const size_t dot = leaf.find_last_of('.');
    if (dot == std::string::npos) {
        return false;
    }
    std::string ext = leaf.substr(dot);
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') {
            c += 32;
        }
    }
    for (const char* e : kExt) {
        if (ext == e) {
            return true;
        }
    }
    return false;
}

/// 同名冲突自动重命名（Doc19 §5 rename 策略）：name_1.ext / name_2.ext 递增
/// 找空位；目录传输相对路径保留 '/' 前缀（docs/a.bin → docs/a_1.bin），
/// 返回首个不存在的候选名（无扩展名文件同样适用，ext 为空串）
std::string unique_target_name(const std::filesystem::path& dir,
                               const std::string& name) {
    const size_t slash = name.find_last_of('/');
    const std::string parent =
        slash == std::string::npos ? std::string() : name.substr(0, slash + 1);
    const std::string leaf = slash == std::string::npos ? name : name.substr(slash + 1);
    const size_t dot = leaf.find_last_of('.');
    const std::string stem =
        (dot == std::string::npos || dot == 0) ? leaf : leaf.substr(0, dot);
    const std::string ext =
        (dot == std::string::npos || dot == 0) ? std::string() : leaf.substr(dot);
    for (int i = 1; i <= 9999; ++i) {
        const std::string cand = parent + stem + "_" + std::to_string(i) + ext;
        std::error_code ec;
        if (!std::filesystem::exists(dir / fs_path(cand), ec)) {
            return cand;
        }
    }
    return name + ".new";  // 兜底（万级同名几乎不可达）
}

void FileService::on_file_meta(const uint8_t* body, size_t len) {
    protocol::FileMetaBody m;
    if (!protocol::parse_file_meta(body, len, m)) {
        SCLD_LOG(log_, LogLevel::Warn, "file", "FILE_META 解析失败");
        return;
    }
    FileMeta meta;
    meta.file_id = m.file_id;
    meta.name = m.name;
    meta.size = m.size;
    meta.total_chunks = m.total_chunks;
    meta.flags = m.flags;
    std::string dir;
    bool need_confirm = false;
    bool safe = true;
    bool block_exec = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        need_confirm = confirm_enabled_;
        dir = save_dir_;
        safe = safe_path_;
        block_exec = block_executable_;
    }
    // 危险路径防线（PRD P10）：对端 meta.name 是不可信输入，'/' 分隔相对路径
    // 逐级校验（目录传输 Doc19 §5），路径穿越/保留名一律拒收（唯一入口）
    if (safe && !name_safe(m.name)) {
        SCLD_LOG(log_, LogLevel::Warn, "file",
                 "[file_id=" + m.file_id + "] 对端文件名不安全，拒收 name=" + m.name);
        return;
    }
    if ((meta.flags & kFileFlagIsDir) != 0) {
        // 目录条目（Doc19 §5）：逐级 mkdir 后即完成（空目录也同步，size=0 无分片）；
        // 无内容写盘风险，不走确认/冲突/磁盘预检流程
        std::error_code ec;
        std::filesystem::create_directories(fs_path(dir) / fs_path(meta.name), ec);
        if (ec) {
            SCLD_LOG(log_, LogLevel::Warn, "file",
                     "[file_id=" + meta.file_id + "] 目录创建失败 name=" + meta.name);
            if (bus_ != nullptr) {
                Evt_FileFailed e;
                e.file_id = meta.file_id;
                e.name = meta.name;
                e.reason = FileFailReason::IoError;
                e.dir = FileDir::Recv;
                e.timestamp_us = now_us();
                bus_->publish(e);
            }
            return;
        }
        SCLD_LOG(log_, LogLevel::Info, "file",
                 "[file_id=" + meta.file_id + "] 目录已创建 name=" + meta.name);
        if (bus_ != nullptr) {
            Evt_FileDone e;
            e.file_id = meta.file_id;
            e.name = meta.name;
            e.dir = FileDir::Recv;
            e.timestamp_us = now_us();
            bus_->publish(e);
        }
        return;
    }
    // 可执行后缀拦截（Doc19 §5）：命中黑名单直接拒收（不进确认弹窗）；
    // audit 通道在会话层（RemoteSessionService），此处主日志 Warn
    if (block_exec && blocked_extension(meta.name)) {
        SCLD_LOG(log_, LogLevel::Warn, "file",
                 "[file_id=" + meta.file_id + "] 已拦截可执行文件 name=" + meta.name);
        if (bus_ != nullptr) {
            Evt_FileFailed e;
            e.file_id = meta.file_id;
            e.name = meta.name;
            e.reason = FileFailReason::Blocked;
            e.dir = FileDir::Recv;
            e.timestamp_us = now_us();
            bus_->publish(e);
        }
        return;
    }
    // 磁盘空间预检（Doc11 §4.5）：剩余 < 文件大小 → 直接 Failed 并提示；
    // save_dir 未建时 space() 失败跳过（accept_file 自建目录后写失败兜底）
    {
        std::error_code ec;
        const auto sp = std::filesystem::space(fs_path(dir), ec);
        if (!ec && sp.available < meta.size) {
            SCLD_LOG(log_, LogLevel::Warn, "file",
                     "[file_id=" + meta.file_id + "] 磁盘空间不足，拒收 需要="
                         + std::to_string(meta.size) + " 可用="
                         + std::to_string(sp.available));
            if (bus_ != nullptr) {
                Evt_FileFailed e;
                e.file_id = meta.file_id;
                e.name = meta.name;
                e.reason = FileFailReason::DiskFull;
                e.dir = FileDir::Recv;
                e.timestamp_us = now_us();
                bus_->publish(e);
            }
            return;
        }
    }
    if (need_confirm) {
        // 前置确认：挂 pending → UI 弹窗（Doc11 §4.3 / Doc13）。
        // 同时预建接收（打开 .scpart）：confirm 走 UI Queued 异步，窗口内到达的片
        // 若无 Transfer 即丢，1GB 压测实测首片丢致跳号失败——预建后片照收，拒绝即清理；
        // 同名冲突裁决延后到 accept_pending（防未同意接收就弹冲突窗，Doc19 §5）
        std::string alias;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            pending_[meta.file_id] = meta;
            alias = peer_alias_;
        }
        if (!accept_file_impl(meta, dir, true)) {
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
    return accept_file_impl(meta, save_dir, false);
}

bool FileService::accept_file_impl(const FileMeta& meta, const std::string& save_dir,
                                   bool defer_conflict) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (meta.file_id.empty()
        || ((meta.flags & kFileFlagIsDir) == 0 && meta.total_chunks == 0)) {
        return false;
    }
    Transfer t;
    t.meta = meta;
    t.save_dir = save_dir;
    t.final_name = meta.name;
    // 保存目录自建（用户任意配置的 save_path 不保证存在；目录传输目标含
    // '/' 相对路径，逐级一并创建）
    std::error_code ec;
    std::filesystem::create_directories(
        fs_path(save_dir) / fs_path(meta.name).parent_path(), ec);
    if (!defer_conflict) {
        // 同名冲突裁决（Doc19 §5 三选项）：ask 且无会话级记住策略 → 置冲突
        // 待决发 Evt_FileConflict 等 UI 决策；overwrite/rename 就地定盘名
        apply_conflict_policy_locked(t);
    }
    // .scpart 临时文件（Doc19 §5）：写盘全程走临时名，全部校验通过后原子改名；
    // 临时名开盘即定版（冲突重命名只动 final_name，不影响 .scpart）
    t.part_name = t.final_name + ".scpart";
    const auto part_path = fs_path(save_dir) / fs_path(t.part_name);
    t.out = foundation::fs_open(part_path, "wb");
    if (t.out == nullptr) {
        SCLD_LOG(log_, LogLevel::Error, "file",
                 "[file_id=" + meta.file_id + "] 临时文件创建失败："
                     + part_path.u8string());
        return false;
    }
    transfers_.emplace(meta.file_id, std::move(t));
    if (transfers_[meta.file_id].conflict_pending) {
        publish_conflict_nolock(transfers_[meta.file_id]);
    }
    SCLD_LOG(log_, LogLevel::Info, "file",
             "[file_id=" + meta.file_id + "] 接收登记 name=" + meta.name
                 + " chunks=" + std::to_string(meta.total_chunks));
    return true;
}

/// 调用方已持锁：目标已存在时的冲突裁决（Doc19 §5）。会话级「记住本次选择」
/// 策略优先（不落盘 config——覆盖风险不永久记住），其次 config overwrite 策略；
/// ask → conflict_pending，等 UI resolve_conflict
void FileService::apply_conflict_policy_locked(Transfer& t) {
    std::error_code ec;
    const auto final_path = fs_path(t.save_dir) / fs_path(t.final_name);
    if (!std::filesystem::exists(final_path, ec)) {
        return;
    }
    std::string pol = session_conflict_policy_;
    if (pol.empty()) {
        pol = overwrite_policy_;
    }
    if (pol == "overwrite") {
        return;  // 保持原名，转正时显式 remove + rename 覆盖
    }
    if (pol == "rename") {
        const auto alt = unique_target_name(fs_path(t.save_dir), t.final_name);
        SCLD_LOG(log_, LogLevel::Info, "file",
                 "[file_id=" + t.meta.file_id + "] 同名冲突自动重命名 " + t.final_name
                     + " → " + alt);
        t.final_name = alt;
        return;
    }
    t.conflict_pending = true;  // ask：冲突待决
}

/// 调用方已持锁：同名冲突事件（Doc19 §5）：UI 弹 覆盖/重命名/跳过 三选项
void FileService::publish_conflict_nolock(const Transfer& t) {
    if (bus_ != nullptr) {
        Evt_FileConflict e;
        e.device_alias = peer_alias_;
        e.file_id = t.meta.file_id;
        e.name = t.meta.name;
        e.size = t.meta.size;
        e.timestamp_us = now_us();
        bus_->publish(e);
    }
}

/// 同名冲突三选项裁决（Doc19 §5）：skip=删 .scpart 判取消；rename=自动
/// name_N.ext；overwrite=转正时显式 remove 旧文件。remember=会话级记住
bool FileService::resolve_conflict(const std::string& file_id,
                                   const std::string& policy, bool remember) {
    if (policy != "overwrite" && policy != "rename" && policy != "skip") {
        return false;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    const auto it = transfers_.find(file_id);
    if (it == transfers_.end() || !it->second.conflict_pending) {
        return false;
    }
    auto& t = it->second;
    if (remember) {
        session_conflict_policy_ = policy;
        SCLD_LOG(log_, LogLevel::Info, "file",
                 "冲突策略会话级记住 policy=" + policy);
    }
    t.conflict_pending = false;
    if (policy == "skip") {
        finish_receive(t, false, FileFailReason::Cancelled);
        return true;
    }
    if (policy == "rename") {
        t.final_name = unique_target_name(fs_path(t.save_dir), t.final_name);
        SCLD_LOG(log_, LogLevel::Info, "file",
                 "[file_id=" + t.meta.file_id + "] 冲突裁决重命名 → " + t.final_name);
    }
    else {
        t.conflict_overwrite = true;  // 转正时显式 remove 旧文件
    }
    if (t.out == nullptr) {
        // 数据已收全（收完即挂起等裁决）：立即校验转正；
        // 仍在传输则最后一片到达时按已清标志走正常收尾
        finish_receive(t, t.received_chunks >= t.meta.total_chunks);
    }
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
    if (ok && t.conflict_pending) {
        // 冲突待决（Doc19 §5）：数据已收全，保留条目与 .scpart 等 resolve_conflict
        // 裁决（skip/overwrite/rename），此处不转正不回收不发布事件
        SCLD_LOG(log_, LogLevel::Info, "file",
                 "[file_id=" + t.meta.file_id + "] 接收数据完整，等待冲突裁决 name="
                     + t.meta.name);
        return;
    }
    const auto part_path = fs_path(t.save_dir) / fs_path(t.part_name);
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
        // .scpart → target 原子转正（Doc11 §4.2/§4.4 / Doc19 §5）：覆盖语义先
        // 显式移除旧文件再 rename（MSVC rename 不保证替换语义，显式 remove 保证
        // 跨平台一致，blacklist_store 同款）；rename 失败回退自动重命名，仍失败 → Failed
        std::error_code ec;
        const auto final_path = fs_path(t.save_dir) / fs_path(t.final_name);
        if (t.conflict_overwrite
            || (overwrite_policy_ == "overwrite" && t.final_name == t.meta.name)) {
            std::filesystem::remove(final_path, ec);
        }
        std::filesystem::rename(part_path, final_path, ec);
        if (ec) {
            const auto alt =
                unique_target_name(fs_path(t.save_dir), t.final_name);
            std::error_code ec2;
            std::filesystem::rename(part_path, fs_path(t.save_dir) / fs_path(alt), ec2);
            if (ec2) {
                SCLD_LOG(log_, LogLevel::Error, "file",
                         "[file_id=" + t.meta.file_id + "] rename 失败：" + t.save_dir
                             + "/" + t.final_name);
                ok = false;
            }
            else {
                SCLD_LOG(log_, LogLevel::Warn, "file",
                         "[file_id=" + t.meta.file_id + "] rename 失败已回退重命名 "
                             + t.final_name + " → " + alt);
                t.final_name = alt;
            }
        }
    }
    if (!ok) {
        std::error_code ec;
        std::filesystem::remove(part_path, ec);  // 失败/取消清理 .scpart（Doc11 §4.4）
    }
    if (bus_ != nullptr) {
        if (ok) {
            Evt_FileDone e;
            e.file_id = t.meta.file_id;
            e.name = t.final_name;  // 实际落盘名（冲突重命名后 UI 展示真名）
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
                                                 "Blocked"};
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
    // 排队任务取消（Doc19 §5 P2）：出队即清理——未发 FILE_META 未登记，零残留
    for (auto it = send_queue_.begin(); it != send_queue_.end(); ++it) {
        if (it->meta.file_id == file_id) {
            const std::string name = it->meta.name;
            send_queue_.erase(it);
            publish_send_failed_nolock(file_id, name, FileFailReason::Cancelled);
            SCLD_LOG(log_, LogLevel::Warn, "file",
                     "[file_id=" + file_id + "] 排队任务已取消 name=" + name);
            return;
        }
    }
    const auto it = transfers_.find(file_id);
    if (it == transfers_.end()) {
        return;
    }
    auto& t = it->second;
    t.aborted = true;  // 发送循环下片感知（Doc11 §4.4）
    if (t.out != nullptr || t.conflict_pending) {
        finish_receive(t, false);  // 接收中取消/冲突待决取消：立即回收句柄与临时文件
    }
}

void FileService::stop_all() {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<std::string> receive_ids;  // 接收条目：立即回收
    for (auto& kv : transfers_) {
        kv.second.aborted = true;  // 发送条目：send_loop 下片感知后自行退出并 erase
        if (kv.second.out != nullptr || kv.second.conflict_pending) {
            receive_ids.push_back(kv.first);  // 含冲突待决挂起条目（.scpart 回收）
        }
    }
    for (const auto& id : receive_ids) {
        const auto it = transfers_.find(id);
        if (it != transfers_.end()) {
            finish_receive(it->second, false);  // 关句柄 + 清临时文件 + erase
        }
    }
    // 出站排队清理（Doc19 §5 P2，会话级不持久化）：逐条发取消事件后清空
    while (!send_queue_.empty()) {
        QueuedSend q = std::move(send_queue_.front());
        send_queue_.pop_front();
        publish_send_failed_nolock(q.meta.file_id, q.meta.name, FileFailReason::Cancelled);
    }
    pending_.clear();
    // 冲突「记住本次选择」随会话复位（Doc19 §5：不落盘 config，断开即忘）
    session_conflict_policy_.clear();
    SCLD_LOG(log_, LogLevel::Info, "file", "会话回收：全部传输已中止");
}

void FileService::publish_progress(const std::string& file_id, const std::string& name,
                                   uint64_t transferred, uint64_t total, FileDir dir) {
    std::lock_guard<std::mutex> lk(mtx_);
    publish_progress_nolock(file_id, name, transferred, total, dir);
}

void FileService::publish_queued(const std::string& file_id, const std::string& name,
                                 uint64_t size) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (bus_ != nullptr) {
        Evt_FileQueued e;
        e.file_id = file_id;
        e.name = name;
        e.size = size;
        e.dir = FileDir::Send;
        e.timestamp_us = now_us();
        bus_->publish(e);
    }
    if (event_cb_) {
        FileEvent fe;
        fe.file_id = file_id;
        fe.kind = FileEventKind::Queued;
        fe.total = size;
        event_cb_(fe);
    }
}

void FileService::publish_send_failed_nolock(const std::string& file_id,
                                             const std::string& name,
                                             FileFailReason reason) {
    // 调用方已持有 mtx_：本函数全程不得再加锁（nolock 语义）
    if (bus_ != nullptr) {
        Evt_FileFailed e;
        e.file_id = file_id;
        e.name = name;
        e.reason = reason;
        e.dir = FileDir::Send;
        e.timestamp_us = now_us();
        bus_->publish(e);
    }
    if (event_cb_) {
        FileEvent fe;
        fe.file_id = file_id;
        fe.kind = FileEventKind::Failed;
        fe.reason = static_cast<uint32_t>(reason);
        event_cb_(fe);
    }
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
