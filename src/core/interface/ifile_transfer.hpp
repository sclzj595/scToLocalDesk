// ============================================================
// scToLocalDesk | src/core/interface/ifile_transfer.hpp
// 双向文件传输抽象契约：分片协议 / 进度 / 取消（Doc06 §5 / Doc11）
// ============================================================
#pragma once

#include <functional>
#include <string>

#include "core/interface/types.hpp"

namespace sc::localdesk {

enum class FileEventKind {
    Queued,    ///< 出站排队登记（Doc19 §5 P2）：入队未启动，取消即出队零残留
    Progress,
    Done,
    Failed,
};

struct FileEvent {
    std::string file_id;
    FileEventKind kind{FileEventKind::Progress};
    uint64_t transferred{0};   ///< 已传字节
    uint64_t total{0};
    uint32_t reason{0};        ///< Failed 原因码（Doc11 §2）
};

class IFileTransfer {
public:
    virtual ~IFileTransfer() = default;

    using FileEventCb = std::function<void(const FileEvent&)>;

    /// 发起发送（读取本地 abs_path，按 chunk_size_kb 分片入队）
    virtual bool send_file(const std::string& abs_path) = 0;

    /// 接收端：确认接收并指定保存目录（前置确认开关由 service 层裁决）
    virtual bool accept_file(const FileMeta& meta, const std::string& save_dir) = 0;

    virtual bool reject_file(const std::string& file_id) = 0;

    /// 取消进行中的传输，清理临时文件（Doc11 §4）
    virtual void cancel(const std::string& file_id) = 0;

    virtual void set_event_callback(FileEventCb cb) = 0;
};

}  // namespace sc::localdesk
