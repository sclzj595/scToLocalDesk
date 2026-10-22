# Doc17 设置页配置规范（SettingPage 全量契约）

> 版本：V1.0（2026-10-21 新增。本文档是全部配置 key、类型、默认值、校验规则的**唯一权威基准**，其余文档引用不得冲突）

## 1. 设计原则

1. 一切可配置项收敛到设置页 + `config.ini`，代码中不允许散落硬编码行为开关
2. 小白友好：员工视图只露 3 项（别名、运行模式、文件保存路径）；高级项全部 IT 视图可见
3. 每项标注生效方式：**热生效**（立即）/ **重启生效**；默认尽量热生效
4. 非法输入就地校验拒绝并回显原因，不写脏值进 ini

## 2. 页面布局

```
SettingPage（QML，左侧页签导航）
├── 通用        别名 / 运行模式 / 视图模式（员工/IT）/ 自启 / 托盘
├── 网络与发现  端口 / 心跳 / 组播 / 静态映射 / 发现自检
├── 会话与画质  编码器 / 采集器 / 帧率 / 画质 / 请求等待超时
├── 文件传输    保存路径 / 前置确认 / 分片大小 / 覆盖策略
├── 安全        无人值守卡片（开关 + 密码）/ 失败计数显示
├── 日志        等级 / 目录打开按钮 / 保留策略
└── 关于        版本 / 协议版本 / 检查更新（灰置，V1 无公网）
```

## 3. 全量配置项表

### 3.1 [general]

| key | 类型 | 默认 | 可选 / 范围 | UI 控件 | 生效 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| `general.alias` | string | 主机名 | 2~20 字符，中文/字母/数字 | 输入框 + 冲突检测 | 热生效 | 设备别名（Doc07） |
| `general.run_mode` | enum | controlled | controlled / controller | 大按钮切换 | 热生效 | 被控 / 控制模式（可运行时切换） |
| `general.view_mode` | enum | employee | employee / it | 切换开关 | 热生效 | 员工视图隐藏高级项（§7） |

### 3.2 [network]

| key | 类型 | 默认 | 范围 | 生效 | 说明 |
| --- | --- | --- | --- | --- | --- |
| `network.ws_port` | int | 8899 | 1024~65535 | 重启 | 监听端口，保存前本机占用检测 |
| `network.heartbeat_interval_s` | int | 3 | 1~10 | 重启 | 心跳周期（Doc05 §2） |
| `network.offline_timeout_s` | int | 15 | 5~60 且 ≥3×心跳 | 重启 | 离线判定 |
| `network.send_queue_max_mb` | int | 8 | 4~32 | 重启 | 画面帧队列水位（Doc08 §4） |

### 3.3 [discovery]

| key | 类型 | 默认 | 可选 | 生效 | 说明 |
| --- | --- | --- | --- | --- | --- |
| `discovery.multicast_enabled` | bool | true | — | 重启 | 关闭进入静态映射模式（Doc07 §7） |
| `discovery.multicast_addr` | string | 239.255.88.99 | 合法组播段 | 重启 | 组播地址 |
| `discovery.static_map` | string | 空 | `alias=ip:port;...` | 热生效 | 跨 VLAN 静态映射，格式实时校验 |

### 3.4 [session]

| key | 类型 | 默认 | 可选 | 生效 | 说明 |
| --- | --- | --- | --- | --- | --- |
| `session.encoder` | enum | mjpeg | mjpeg / h264 | 重启 | h264 需 FFmpeg 编译（Doc10） |
| `session.capture` | enum | auto | auto / dxgi / wgc | 重启 | 采集器选择 |
| `session.fps` | enum | 15 | 10 / 15 / 24 / 30 | 热生效 | 帧率 |
| `session.quality` | enum | medium | low / medium / high / custom | 热生效 | 画质档位 |
| `session.jpeg_quality` | int | 80 | 1~100 | 热生效 | quality=custom 时生效 |
| `security.session_request_timeout_s` | int | 30 | 10~120 | 热生效 | 人工确认等待超时（Doc09 §3） |

### 3.5 [file]

| key | 类型 | 默认 | 可选 | 生效 | 说明 |
| --- | --- | --- | --- | --- | --- |
| `file.save_path` | string | %USERPROFILE%\Desktop | 合法可写目录 | 热生效 | 接收保存路径，目录选择器 |
| `file.confirm_before_transfer` | bool | true | — | 热生效 | 接收前置确认弹窗（Doc11 §4） |
| `file.chunk_size_kb` | int | 256 | 64~1024 | 重启 | 分片大小 |
| `file.overwrite` | enum | ask | ask / overwrite / rename | 热生效 | 同名覆盖策略 |

### 3.6 [security]（无人值守详见 Doc16 §6）

| key | 类型 | 默认 | 校验 | 生效 | 说明 |
| --- | --- | --- | --- | --- | --- |
| `security.unattended_enabled` | bool | false | 开启时必须已有密码 | 热生效（仅新连接） | 无人值守总开关 |
| `security.unattended_salt` | hex | 空 | 16 B 随机生成 | — | 密码盐，不暴露 UI |
| `security.unattended_password_hash` | hex | 空 | PBKDF2 派生 | — | 哈希，不暴露 UI；设置对话框只写不读 |
| `security.unattended_fail_count` | int | 0 | — | — | 失败计数，成功接入清零 |
| `security.session_request_timeout_s` | int | 30 | 10~120 | 热生效 | 同 §3.4（归安全语义，置于安全页签展示） |

### 3.7 [log]

| key | 类型 | 默认 | 范围 | 生效 | 说明 |
| --- | --- | --- | --- | --- | --- |
| `log.level` | enum | info | trace~critical | 热生效 | 日志等级（Doc12 §1） |
| `log.max_size_mb` | int | 10 | 1~100 | 重启 | 单文件上限 |
| `log.max_files` | int | 7 | 1~30 | 重启 | 保留份数 |

### 3.8 [app]

| key | 类型 | 默认 | 生效 | 说明 |
| --- | --- | --- | --- | --- |
| `app.auto_start` | bool | false | 热生效 | 写注册表 CurrentUser Run 键（Win32 适配器） |
| `app.minimize_to_tray` | bool | true | 热生效 | 最小化缩入托盘 |

## 4. config.ini 完整示例（config.ini.example）

```ini
[general]
alias=FrontDesk-XiaoWang
run_mode=controlled          ; controlled | controller
view_mode=employee           ; employee | it

[network]
ws_port=8899
heartbeat_interval_s=3
offline_timeout_s=15
send_queue_max_mb=8

[discovery]
multicast_enabled=true
multicast_addr=239.255.88.99
static_map=                  ; alias=ip:port;alias2=ip2:port2

[session]
encoder=mjpeg                ; mjpeg | h264
capture=auto                 ; auto | dxgi | wgc
fps=15                       ; 10 | 15 | 24 | 30
quality=medium               ; low | medium | high | custom
jpeg_quality=80              ; 1~100, quality=custom 时生效
session_request_timeout_s=30

[file]
save_path=%USERPROFILE%\Desktop
confirm_before_transfer=true
chunk_size_kb=256            ; 64~1024
overwrite=ask                ; ask | overwrite | rename

[security]
unattended_enabled=false     ; 无人值守总开关（Doc16）
unattended_salt=
unattended_password_hash=
unattended_fail_count=0

[log]
level=info                   ; trace | debug | info | warn | error | critical
max_size_mb=10
max_files=7

[app]
auto_start=false
minimize_to_tray=true
```

## 5. 配置读写流程

```
启动:  ConfigParser 加载 → 逐 key 校验 → 非法/缺失补默认 → 常驻 ConfigRepository
保存:  UI 就地校验 → 写内存快照 → 原子落盘（临时文件 + rename，防掉电截断）
通知:  热生效项保存后发布 Evt_ConfigChanged(key)，相关服务自行刷新；重启生效项保存后角标提示"重启后生效"
```

- 校验规则与 §3 表格一一对应，实现收敛在 ConfigParser + 各 service 的 set 接口
- 密码设置走专用通道：PBKDF2 在线程池执行（≥10 万迭代），完成后只落 hash + salt（Doc16 §5）

## 6. QML 数据绑定（Doc13 §4）

- SettingPage 只与 `qml_bridge` 暴露的 ConfigViewModel 交互：`getProperty(key) / setProperty(key, value)`，key 与 §3 完全一致
- bridge 内部转调 ConfigRepository 门面；QML 不 import core 头、不碰 ini 文本
- 密码对话框：`setUnattendedPassword(pwd)` 单向写入，无读取接口

## 7. 视图权限（员工 / IT）

| 页签 | 员工视图 | IT 视图 |
| --- | --- | --- |
| 通用 | 别名、运行模式可见可改；自启/托盘可见 | 全量 |
| 网络与发现 | 隐藏 | 全量 |
| 会话与画质 | 隐藏 | 全量 |
| 文件传输 | 保存路径可见 | 全量 |
| 安全 | 无人值守开关 + 密码设置可见（员工也可为自己开启） | 全量 + 失败计数 |
| 日志 | 仅"打开日志目录"按钮 | 全量 |
| 关于 | 可见 | 可见 |

- 切换 view_mode=it 时无密码门槛（V1 内网可信假设；V1.2 评估 IT 视图独立口令）

## 8. 兼容与迁移

- 版本升级：未知 key 忽略、缺失 key 补默认并回写（Doc12 §5）
- 修改 alias / 端口后：发现服务与监听服务按"重启生效"约定在下次启动应用，避免运行态撕裂
- 卸载重装：config.ini 保留在 exe 目录，随目录删除而清除（含无人值守哈希，删除即失效）

## 9. 测试要点（准入，Doc14 §2.7）

1. 全量 key 三态遍历：默认加载 / 合法修改 / 非法拒绝
2. 热生效项保存后行为即时变化（如 log.level、fps）；重启生效项角标与延迟生效正确
3. 手工破坏 ini（删段、错类型、超范围）→ 启动兜底不崩溃
4. 员工视图隐藏项在 UI 与 bridge 双层不可达
5. 原子写：保存过程强杀进程，ini 不截断
