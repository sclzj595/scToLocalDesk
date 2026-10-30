// ============================================================
// scToLocalDesk | src/service/app_config.hpp
// 全量应用配置（契约唯一基准：docs/Doc17_Settings_Page.md §3）
// 加载 = 缺失/非法 key 兜底默认值（Doc12 §5）；保存 = 全量回写
// ============================================================
#pragma once

#include <string>

namespace sc::localdesk::foundation {
class IniConfig;
}

namespace sc::localdesk {

struct AppConfig {
    // [general]
    std::string alias{"SC-PC"};
    std::string run_mode{"controlled"};  // controlled | controller
    std::string view_mode{"employee"};   // employee | it
    // [network]
    int ws_port{8899};
    int heartbeat_interval_s{3};
    int offline_timeout_s{15};
    int send_queue_max_mb{8};
    // [discovery]
    bool multicast_enabled{true};
    std::string multicast_addr{"239.255.88.99"};
    std::string static_map;  // alias=ip:port[:code];...（code 可选=对端 9 位连接码）
    // [session]
    std::string encoder{"mjpeg"};
    std::string capture{"auto"};
    int fps{15};
    std::string quality{"medium"};
    int jpeg_quality{80};
    int session_request_timeout_s{30};
    /// 会话内剪贴板文本同步总开关（Doc19 §5 P2）：同一键两端读——
    /// 关=不发不收（隐私红线：被控端由本机设置控制），默认开
    bool clipboard_sync{true};
    // [file]
    std::string save_path;  // 空 = 桌面（main 解析 %USERPROFILE%）
    bool confirm_before_transfer{true};
    /// 对端文件名安全校验开关（PRD P10）：默认开启，关闭仅在受控环境调试
    bool file_safe_path{true};
    int chunk_size_kb{256};
    std::string overwrite{"ask"};
    /// 出站发送并行上限（Doc19 §5 P2）：1~8，默认 3；超出的 send_file 串行排队
    int max_parallel{3};
    // [security]（Doc16 §6）
    bool unattended_enabled{false};
    std::string unattended_salt;          // 16B hex
    std::string unattended_key;           // K hex（等价凭据）
    std::string unattended_verify_hash;   // SHA256(K) hex
    int unattended_fail_count{0};
    // [log]
    std::string log_level{"info"};
    int log_max_size_mb{10};
    int log_max_files{7};
    // [app]
    bool auto_start{false};
    bool minimize_to_tray{true};
    // [appearance]（Doc18 §3.3：主题选择持久化）
    std::string appearance_theme{"light"};  // light | dark
    bool appearance_reduce_transparency{false};

    /// 从 ini 读取（缺失/非法回退成员当前值 = 默认值）
    void load(const foundation::IniConfig& ini);
    /// 全量写回 ini（调用方负责 ini.save() 原子落盘）
    void save(foundation::IniConfig& ini) const;
};

}  // namespace sc::localdesk
