// ============================================================
// scToLocalDesk | src/service/file_service.hpp
// 文件传输服务：分片收发完整实现（Doc11）
// 发送：线程池读文件分片 → FILE_META + FILE_CHUNK* N（经 ITransport）
// 接收：临时文件 <save_dir>/<file_id>.part → 校验完成后 rename 为目标名
// ============================================================
#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "core/events.hpp"
#include "core/interface/ifile_transfer.hpp"
#include "core/interface/itransport.hpp"
#include "core/interface/types.hpp"

namespace sc::localdesk::foundation {
class EventBus;
class ILogger;
class ThreadPool;
}

namespace sc::localdesk {

class FileService final : public IFileTransfer {
public:
    FileService(std::shared_ptr<ITransport> transport,
                std::shared_ptr<foundation::EventBus> bus,
                std::shared_ptr<foundation::ILogger> logger);

    // ---------------- IFileTransfer ----------------

    /// 发起发送：读文件 → 建 Transfer → 线程池分片发送
    bool send_file(const std::string& abs_path) override;
    bool accept_file(const FileMeta& meta, const std::string& save_dir) override;
    bool reject_file(const std::string& file_id) override;
    void cancel(const std::string& file_id) override;
    void set_event_callback(FileEventCb cb) override;

    /// 会话服务 Running 态分派入口（session → file）
    void on_file_meta(const uint8_t* body, size_t len);
    bool on_file_chunk(const uint8_t* body, size_t len);

    /// 设置分片大小（config.ini file.chunk_size_kb，Doc17 §3.5，64~1024）
    void set_chunk_size(uint32_t chunk_size_kb);
    /// 设置接收保存目录（file.save_path）
    void set_save_dir(std::string dir);
    /// 注入线程池（发送分片 IO 用；未注入则同步发送）
    void set_thread_pool(std::shared_ptr<foundation::ThreadPool> pool);
    /// 接收前置确认（file.confirm_before_transfer，Doc11 §4.3）；
    /// 开启时对端文件先挂 pending 并发 Evt_FileMetaReceived，等 UI accept/reject
    void set_confirm_enabled(bool enabled);
    /// 同名覆盖策略（file.overwrite，Doc11 §4.2）：ask（默认，冲突时强制弹窗
    /// 询问）/ overwrite（覆盖）/ rename（自动加 (N)）；ask 同意后按 rename 落盘
    void set_overwrite_policy(std::string policy) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (policy == "ask" || policy == "overwrite" || policy == "rename") {
            overwrite_policy_ = std::move(policy);
        }
    }
    /// 危险路径校验开关（file.safe_path，PRD P10；默认开，调试用）
    void set_safe_path(bool enabled) {
        std::lock_guard<std::mutex> lk(mtx_);
        safe_path_ = enabled;
    }

    /// 对端文件名安全校验（PRD P10 危险路径防线）：禁路径分隔/盘符/通配符/
    /// 父目录/控制符/隐藏名/Windows 保留设备名；on_file_meta 唯一拦截入口
    static bool name_safe(const std::string& name);
    /// UI 确认后落盘接收（pending 中查找）
    bool accept_pending(const std::string& file_id);
    /// UI 拒绝
    bool reject_pending(const std::string& file_id);
    /// 会话结束资源回收：中止全部收发 + 清空待确认（Doc09 §5）
    void stop_all();
    /// 对端连接名注入（RemoteSessionService 会话建立时调用）：
    /// Evt_FileMetaReceived.device_alias 展示来源；会话关闭传空
    void set_peer_alias(std::string alias) {
        std::lock_guard<std::mutex> lk(mtx_);
        peer_alias_ = std::move(alias);
    }
    /// 出站发送器注入（PRD §6.2 全载荷加密）：会话加密激活后 FILE_META/
    /// FILE_CHUNK 必须走 send_business_frame 加密链路——裸发 transport 的
    /// 明文帧会被对端 fail-closed 解密层整帧丢弃（加密会话中文件传输失效）
    /// 未注入回落裸发（headless 自检/无加密会话）
    void set_sender(std::function<bool(const std::vector<uint8_t>&)> fn) {
        std::lock_guard<std::mutex> lk(mtx_);
        send_fn_ = std::move(fn);
    }

private:
    struct Transfer
    {
        FileMeta meta;
        std::string save_dir;
        std::string final_name;         ///< 同名冲突解析后的落盘名（接收端）
        FILE* out{nullptr};             ///< 临时文件句柄（接收端）
        uint32_t received_chunks{0};
        bool aborted{false};
        bool awaiting_confirm{false};   ///< 前置确认预建：confirm 窗口内片照收，拒绝即清理
    };

    void send_loop(const std::string& abs_path, const FileMeta& meta);
    /// 出站统一口：send_fn_（加密链路）优先，回落 transport_ 裸发（可空）
    bool out_frame(const std::vector<uint8_t>& frame);
    /// 调用方已持锁：关句柄 + 清临时文件 + 发布失败事件 + 回收条目；
    /// reason 默认 IoError，片校验失败传 Integrity
    void finish_receive(Transfer& t, bool ok,
                        FileFailReason reason = FileFailReason::IoError);
    void publish_progress(const std::string& file_id, const std::string& name,
                          uint64_t transferred, uint64_t total, FileDir dir);
    void publish_progress_nolock(const std::string& file_id, const std::string& name,
                                 uint64_t transferred, uint64_t total,
                                 FileDir dir);  ///< 调用方已持锁

    std::shared_ptr<ITransport> transport_;
    std::function<bool(const std::vector<uint8_t>&)> send_fn_;  ///< 加密出站链路（可空）
    std::shared_ptr<foundation::EventBus> bus_;
    std::shared_ptr<foundation::ILogger> log_;
    std::shared_ptr<foundation::ThreadPool> pool_;

    mutable std::mutex mtx_;
    std::unordered_map<std::string, Transfer> transfers_;  ///< file_id -> 状态
    std::unordered_map<std::string, FileMeta> pending_;    ///< 待确认（confirm 流程）
    bool confirm_enabled_{true};
    bool safe_path_{true};
    std::string overwrite_policy_{"ask"};  ///< file.overwrite（Doc11 §4.2）
    FileEventCb event_cb_;
    uint32_t chunk_size_{256 * 1024};  ///< 默认 256KB（Doc05 §5）
    std::string save_dir_{"."};
    std::string peer_alias_;  ///< 当前会话对端连接名（会话层注入）
};

}  // namespace sc::localdesk
