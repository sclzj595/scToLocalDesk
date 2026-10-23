// ============================================================
// scToLocalDesk | src/service/app_config.cpp
// Doc17 §3 全量 key 读写实现
// ============================================================
#include "service/app_config.hpp"

#include "foundation/config_parser.hpp"

namespace sc::localdesk {

void AppConfig::load(const foundation::IniConfig& ini) {
    alias = ini.get_str("general", "alias", alias);
    run_mode = ini.get_str("general", "run_mode", run_mode);
    view_mode = ini.get_str("general", "view_mode", view_mode);

    ws_port = ini.get_int("network", "ws_port", ws_port);
    if (ws_port < 1024 || ws_port > 65535) {
        ws_port = 8899;  // 范围校验兜底（Doc17 §3.2）
    }
    heartbeat_interval_s = ini.get_int("network", "heartbeat_interval_s", heartbeat_interval_s);
    offline_timeout_s = ini.get_int("network", "offline_timeout_s", offline_timeout_s);
    send_queue_max_mb = ini.get_int("network", "send_queue_max_mb", send_queue_max_mb);

    multicast_enabled = ini.get_bool("discovery", "multicast_enabled", multicast_enabled);
    multicast_addr = ini.get_str("discovery", "multicast_addr", multicast_addr);
    static_map = ini.get_str("discovery", "static_map", static_map);

    encoder = ini.get_str("session", "encoder", encoder);
    capture = ini.get_str("session", "capture", capture);
    fps = ini.get_int("session", "fps", fps);
    quality = ini.get_str("session", "quality", quality);
    jpeg_quality = ini.get_int("session", "jpeg_quality", jpeg_quality);
    session_request_timeout_s =
        ini.get_int("session", "session_request_timeout_s", session_request_timeout_s);

    save_path = ini.get_str("file", "save_path", save_path);
    confirm_before_transfer =
        ini.get_bool("file", "confirm_before_transfer", confirm_before_transfer);
    chunk_size_kb = ini.get_int("file", "chunk_size_kb", chunk_size_kb);
    overwrite = ini.get_str("file", "overwrite", overwrite);

    unattended_enabled = ini.get_bool("security", "unattended_enabled", unattended_enabled);
    unattended_salt = ini.get_str("security", "unattended_salt", unattended_salt);
    unattended_key = ini.get_str("security", "unattended_password_key", unattended_key);
    unattended_verify_hash =
        ini.get_str("security", "unattended_verify_hash", unattended_verify_hash);
    unattended_fail_count =
        ini.get_int("security", "unattended_fail_count", unattended_fail_count);

    log_level = ini.get_str("log", "level", log_level);
    log_max_size_mb = ini.get_int("log", "max_size_mb", log_max_size_mb);
    log_max_files = ini.get_int("log", "max_files", log_max_files);

    auto_start = ini.get_bool("app", "auto_start", auto_start);
    minimize_to_tray = ini.get_bool("app", "minimize_to_tray", minimize_to_tray);

    appearance_theme = ini.get_str("appearance", "theme", appearance_theme);
    appearance_reduce_transparency =
        ini.get_bool("appearance", "reduce_transparency", appearance_reduce_transparency);
}

void AppConfig::save(foundation::IniConfig& ini) const {
    ini.set_str("general", "alias", alias);
    ini.set_str("general", "run_mode", run_mode);
    ini.set_str("general", "view_mode", view_mode);

    ini.set_int("network", "ws_port", ws_port);
    ini.set_int("network", "heartbeat_interval_s", heartbeat_interval_s);
    ini.set_int("network", "offline_timeout_s", offline_timeout_s);
    ini.set_int("network", "send_queue_max_mb", send_queue_max_mb);

    ini.set_bool("discovery", "multicast_enabled", multicast_enabled);
    ini.set_str("discovery", "multicast_addr", multicast_addr);
    ini.set_str("discovery", "static_map", static_map);

    ini.set_str("session", "encoder", encoder);
    ini.set_str("session", "capture", capture);
    ini.set_int("session", "fps", fps);
    ini.set_str("session", "quality", quality);
    ini.set_int("session", "jpeg_quality", jpeg_quality);
    ini.set_int("session", "session_request_timeout_s", session_request_timeout_s);

    ini.set_str("file", "save_path", save_path);
    ini.set_bool("file", "confirm_before_transfer", confirm_before_transfer);
    ini.set_int("file", "chunk_size_kb", chunk_size_kb);
    ini.set_str("file", "overwrite", overwrite);

    ini.set_bool("security", "unattended_enabled", unattended_enabled);
    ini.set_str("security", "unattended_salt", unattended_salt);
    ini.set_str("security", "unattended_password_key", unattended_key);
    ini.set_str("security", "unattended_verify_hash", unattended_verify_hash);
    ini.set_int("security", "unattended_fail_count", unattended_fail_count);

    ini.set_str("log", "level", log_level);
    ini.set_int("log", "max_size_mb", log_max_size_mb);
    ini.set_int("log", "max_files", log_max_files);

    ini.set_bool("app", "auto_start", auto_start);
    ini.set_bool("app", "minimize_to_tray", minimize_to_tray);

    ini.set_str("appearance", "theme", appearance_theme);
    ini.set_bool("appearance", "reduce_transparency", appearance_reduce_transparency);
}

}  // namespace sc::localdesk
