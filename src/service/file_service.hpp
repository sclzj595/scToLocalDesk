// ============================================================
// scToLocalDesk | src/service/file_service.hpp
// 文件传输服务：分片收发完整实现（Doc11 / Doc19 §5）
// 发送：线程池读文件分片 → FILE_META + FILE_CHUNK* N（经 ITransport）；
//       目录递归展开逐项入队（is_dir 条目 + '/' 分隔相对路径）
// 接收：临时文件 <target>.scpart → 全部校验通过后原子改名为目标名；
//       同名冲突三选项（覆盖/重命名/跳过，会话级记住）
// ============================================================
#pragma once

#include <cstdint>
#include <cstdio>
#include <deque>
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

    /// 发起发送：文件/目录（目录递归展开逐项入队，Doc19 §5）
    bool send_file(const std::string& abs_path) override;
    /// 接收登记（冲突裁决就地执行；confirm 预建延后裁决走 accept_file_impl）
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
    /// 同名覆盖策略（file.overwrite，Doc11 §4.2）：ask（默认，冲突时发
    /// Evt_FileConflict 等 UI 三选项）/ overwrite（覆盖）/ rename（自动 name_N.ext）
    void set_overwrite_policy(std::string policy) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (policy == "ask" || policy == "overwrite" || policy == "rename") {
            overwrite_policy_ = std::move(policy);
        }
    }
    /// 可执行后缀拦截开关（file.block_executable，Doc19 §5；默认关=全部放行）：
    /// 双端同判——接收 on_file_meta 命中黑名单直接拒收，发送 send_file 早拒
    void set_block_executable(bool enabled) {
        std::lock_guard<std::mutex> lk(mtx_);
        block_executable_ = enabled;
    }
    /// 危险路径校验开关（file.safe_path，PRD P10；默认开，调试用）
    void set_safe_path(bool enabled) {
        std::lock_guard<std::mutex> lk(mtx_);
        safe_path_ = enabled;
    }
    /// 出站发送并行上限（file.max_parallel，Doc19 §5 P2；范围 1~8，默认 3）：
    /// 超出的 send_file 请求串行排队，活跃任务收尾/失败/取消后自动补位
    void set_max_parallel(int n) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (n >= 1 && n <= 8) {
            max_parallel_ = static_cast<size_t>(n);
        }
    }

    /// 对端文件名安全校验（PRD P10 危险路径防线）：'/' 分隔相对路径逐级校验
    /// （目录传输 Doc19 §5），每段禁父目录/盘符/通配符/控制符/隐藏名/保留设备名；
    /// 反斜杠一律拒绝；on_file_meta 唯一拦截入口
    static bool name_safe(const std::string& name);
    /// 可执行后缀黑名单判定（Doc19 §5）：exe/bat/cmd/ps1/msi/scr/com/vbs/js，
    /// 取末段 '.' 后缀小写比较（收发两侧共用）
    static bool blocked_extension(const std::string& name);
    /// UI 确认后落盘接收（pending 中查找）
    bool accept_pending(const std::string& file_id);
    /// UI 拒绝
    bool reject_pending(const std::string& file_id);
    /// 同名冲突三选项裁决（Doc19 §5）：accept 时目标已存在且策略为 ask，
    /// 收到 Evt_FileConflict 后由 UI 调用；policy ∈ {"overwrite","rename","skip"}，
    /// remember=勾选「记住本次选择」→ 会话级记忆（不落盘 config，断开 reset）
    bool resolve_conflict(const std::string& file_id, const std::string& policy,
                          bool remember = false);
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
        std::string part_name;          ///< .scpart 临时名（开盘即定版，冲突改名不影响）
        FILE* out{nullptr};             ///< 临时文件句柄（接收端）
        uint32_t received_chunks{0};
        bool aborted{false};
        bool awaiting_confirm{false};   ///< 前置确认预建：confirm 窗口内片照收，拒绝即清理
        bool conflict_pending{false};   ///< 同名冲突待决（Doc19 §5）：等 resolve_conflict 裁决
        bool conflict_overwrite{false}; ///< 冲突裁决=覆盖：转正时显式 remove 旧文件
    };

    /// 出站排队条目（Doc19 §5 P2）：meta 为入队时快照（file_id 作取消索引，
    /// 分片数定版）；出队时直接启动，无需重扫磁盘
    struct QueuedSend
    {
        std::string abs_path;
        FileMeta meta;
    };

    void send_loop(const std::string& abs_path, const FileMeta& meta);
    /// 单文件入队（目录传输复用）：rel_name 为协议展示名（目录传输时 '/' 分隔）
    bool send_one_file(const std::string& abs_path, const std::string& rel_name);
    /// 目录递归入队（Doc19 §5）：root 名作路径前缀，逐项走既有并行/排队机制
    bool send_directory(const std::string& abs_path);
    /// 入队公共段（Doc19 §5 P2）：上限内立即启动，超出排队 + UI 登记
    bool enqueue_send(const std::string& abs_path, const FileMeta& meta);
    /// 出队启动：发 FILE_META → 登记条目 → 线程池分片发送（原 send_file 启动段
    /// 抽出，供 send_file 与队列补位复用）；META 失败回退计数并自动补位
    bool begin_send(const std::string& abs_path, const FileMeta& meta);
    /// 队列补位（Doc19 §5 P2）：活跃 send 收尾/失败后回退计数，上限内弹队首
    /// 启动；未持锁（内部加锁）。启动再失败经 begin_send 递归补位快速排空
    void backfill_send_slot();
    /// 出站统一口：send_fn_（加密链路）优先，回落 transport_ 裸发（可空）
    bool out_frame(const std::vector<uint8_t>& frame);
    /// 调用方已持锁：ok 且冲突待决时仅挂起（保留 .scpart 等 resolve_conflict），不转正
    void finish_receive(Transfer& t, bool ok,
                        FileFailReason reason = FileFailReason::IoError);
    /// 接收登记实现：defer_conflict=true（confirm 预建）时冲突裁决延后到
    /// accept_pending，防未同意接收就弹冲突窗
    bool accept_file_impl(const FileMeta& meta, const std::string& save_dir,
                          bool defer_conflict);
    /// 调用方已持锁：目标已存在时的策略裁决（Doc19 §5 三选项）。会话级记住
    /// 策略优先，其次 config overwrite 策略；ask → 置 conflict_pending
    void apply_conflict_policy_locked(Transfer& t);
    /// 调用方已持锁：同名冲突事件（UI 弹 覆盖/重命名/跳过 三选项）
    void publish_conflict_nolock(const Transfer& t);
    void publish_progress(const std::string& file_id, const std::string& name,
                          uint64_t transferred, uint64_t total, FileDir dir);
    void publish_progress_nolock(const std::string& file_id, const std::string& name,
                                 uint64_t transferred, uint64_t total,
                                 FileDir dir);  ///< 调用方已持锁
    /// 排队登记事件（send_file 入队后调用，内部加锁）：UI 插「排队中」行
    void publish_queued(const std::string& file_id, const std::string& name,
                        uint64_t size);
    /// 调用方已持锁：发送侧失败事件（排队取消/启动失败；发送路径此前无失败事件）
    void publish_send_failed_nolock(const std::string& file_id, const std::string& name,
                                    FileFailReason reason);

    std::shared_ptr<ITransport> transport_;
    std::function<bool(const std::vector<uint8_t>&)> send_fn_;  ///< 加密出站链路（可空）
    std::shared_ptr<foundation::EventBus> bus_;
    std::shared_ptr<foundation::ILogger> log_;
    std::shared_ptr<foundation::ThreadPool> pool_;

    mutable std::mutex mtx_;
    std::unordered_map<std::string, Transfer> transfers_;  ///< file_id -> 状态
    std::unordered_map<std::string, FileMeta> pending_;    ///< 待确认（confirm 流程）
    std::deque<QueuedSend> send_queue_;  ///< 出站发送队列（Doc19 §5 P2，会话级不持久化）
    size_t active_sends_{0};             ///< 已启动未收尾的 send_loop 数（含待调度）
    size_t max_parallel_{3};             ///< 并行上限（file.max_parallel，默认 3）
    bool confirm_enabled_{true};
    bool safe_path_{true};
    bool block_executable_{false};         ///< file.block_executable（Doc19 §5，默认关）
    std::string overwrite_policy_{"ask"};  ///< file.overwrite（Doc11 §4.2）
    std::string session_conflict_policy_;  ///< 冲突「记住本次选择」（会话级，断开 reset）
    FileEventCb event_cb_;
    uint32_t chunk_size_{256 * 1024};  ///< 默认 256KB（Doc05 §5）
    std::string save_dir_{"."};
    std::string peer_alias_;  ///< 当前会话对端连接名（会话层注入）
};

}  // namespace sc::localdesk
