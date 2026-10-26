# Doc15 部署、打包、运维、风险与排障手册

> 版本：V2.0（2026-10-21 优化：端口表、config.ini 交付、无人值守排障项）

## 1. 打包方案

1. 编译 Release 版本 scToLocalDesk.exe
2. windeployqt 部署 Qt 依赖；优先动态部署，后续评估静态 Qt 编译
3. 绿色免安装，直接拷贝到内网共享盘分发
4. 交付物附带 `config.ini.example`（契约见 [Doc17](./Doc17_Settings_Page.md)），首次运行自动生成 `config.ini`

```
scToLocalDesk/
├── scToLocalDesk.exe
├── *.dll（Qt 运行时，windeployqt 产出）
├── config.ini.example
├── config.ini（首次运行生成）
└── logs/（运行日志，滚动）
```

打包脚本（P5.1 交付，2026-10-26）：

```
powershell -File scripts\package.ps1 -Version v1.0.0
```

- 前置：`cmake --build build-msvc --config Release` 已完成
- 自动完成：双 exe + FFmpeg 4 dll 收集 → windeployqt（QML qmldir 指向 src/ui/qml）→ config.ini.example 生成 → zip
- 产物：`build-msvc\dist\scToLocalDesk-<Version>-win64.zip`（当前 ~82 MB）

## 2. 端口与防火墙放行

| 协议 | 端口 | 用途 |
| --- | --- | --- |
| UDP 组播 | 5388（`discovery.multicast_addr` 配套） | 设备发现心跳 |
| TCP 监听 | 8899（`network.ws_port`，可改） | WebSocket 会话 |

- 放行方式：组策略 / 防火墙入站规则（部署文档示例命令），或首次运行允许 Windows 弹窗
- 仅内网互通，不配置任何出站公网规则

## 3. 部署须知

1. 权限：普通用户运行；屏幕捕获需 Windows 隐私设置开启屏幕录制权限
2. 开机自启：设置页勾选后写入注册表 CurrentUser Run 键，不需要管理员权限
3. 单实例锁：重复启动自动唤起已有窗口
4. 无人值守部署（IT 批量启用，V1.1）：设置页安全页签开启并设置访问密码；密码哈希仅存本机 config.ini，不做集中下发（V1.2 评估批量配置分发）

## 4. 运维排障

| 现象 | 排查步骤 |
| --- | --- |
| 无法找到设备 | ① 查防火墙 UDP 组播放行 ② 确认同网段（跨 VLAN 用静态映射模式，Doc07 §7） ③ 确认对方软件已启动且日志有心跳 |
| 连接请求弹窗看不到 | 弹窗置顶失败、被全屏程序遮挡；检查提示音与托盘闪动 |
| 画面卡顿 | 设置页调低帧率 / 画质（Doc17 会话页签），或切换 H264 编码 |
| 连接秒断 | 查 logs/ 中 REJECT 原因码（Doc05 §3：0x01 密码错、0x02 会话占用、0x03 人工拒绝/开关关闭） |
| 无人值守连不上 | ① 对方安全页签开关是否开启 ② 密码是否重设过 ③ 对方是否正在会话中（单会话限制） ④ 日志检索 `unattended` 关键字 |
| 日志查看 | exe 同目录 logs/，spdlog 滚动日志；问题反馈附带最新 2 份 |

## 5. 风险预案

- 组播跨 VLAN 不通：切换静态别名映射模式，关闭 UDP 发现
- 发送队列堆积：水位丢帧 + 内存红线自动断开（Doc08 §4）
- 屏幕捕获 API 版本差异：采集器自动降级 DXGI → WGC（Doc10 §1）
- 无人值守密码遗忘：本机在位人员进入设置页重设；无密码找回后门（安全红线，Doc16 §2）
